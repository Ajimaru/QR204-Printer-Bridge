#pragma once
// Network bridge for the QR204 thermal printer: raw TCP port 9100 <-> UART.
// Documentation: the project wiki (Development Guide for the internals below).
//
// One FreeRTOS task runs the TCP server and owns the printer UART, so the ESPHome loop never waits for the
// printer: at 9600 baud a 23 KB screen print takes ~24 s on the wire. The UART driver gets a 64 KB TX ring
// buffer, so a whole screen print fits and the sender is never throttled. While the buffer is full the bridge
// task sleeps (see tx_fits()), which throttles the TCP sender.
// Bytes the printer sends back are forwarded to the connected client. While no client is connected, the task
// asks the printer for its status (DLE EOT 2) every 10 s.
//
// Jobs pass through unchanged. uart_lock keeps a TCP job, the print_text action and the test page from
// interleaving on the UART.
//
// This printer's firmware (QR701_T1US-N32-2008) deviates from its manual; tested 2026-09-30, see the wiki's
// Development Guide, "The unit used here": ESC v and DC2 T don't work as documented, ESC 7 takes one
// parameter instead of three (the next two are printed as text), and FS ., ESC t, ESC R and ESC 9 are
// ignored, so it always stays in GB18030 mode.
//
// The UART is driven through ESP-IDF directly instead of ESPHome's uart component, whose writes would block
// the loop at 9600 baud. Sockets use lwip_* calls because ESPHome builds without VFS select support.
//
// Used from the YAML: on_boot (begin), template entities (online, paper_out, status_text, jobs, bytes,
// client_connected), the print_text action and the test page button (print_text, print_test_page), the
// "Feed Lines" number (set_feed_lines), the status LED (printing, ready) and the "Read Printer Info" button
// (info_requested, info_ready, info_text).

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "esphome/core/log.h"

namespace printer_bridge {

static const char *const TAG = "printer_bridge";
static const uart_port_t UART_PORT = UART_NUM_1;
// Larger than the largest screen print (1.5x: 22.8 KB). With 16 KB the sender had to wait for the C3's TCP window
// to reopen for the last ~0.6 KB, and exactly that tail got lost between the two lwIP stacks (seen 2026-09-30:
// 22112 and 22208 of 22774 bytes arrived, so the printer waited for the rest of the image and never fed).
// The same job from a Mac arrived complete.
// 64 KB leaves room for larger jobs (full width, 2x, several pages). With it the heap still has ~120 KB free
// (2026-10-02: 153 KB free with 32 KB); keep at least ~50 KB free for WiFi and lwIP.
static const int TX_BUFFER = 65536;
static const int RX_BUFFER = 1024;
static const uint32_t STATUS_INTERVAL_MS = 10000;
// Consecutive unanswered status queries before the printer counts as offline (it answers late while busy).
static const int STATUS_MISSES_OFFLINE = 3;
// A connected client that sends nothing for this long is dropped, so a stuck client can't block the port.
static const uint32_t CLIENT_IDLE_MS = 60000;
// printing() stays true this long after the queued data is on the wire (the printer finishes the last lines).
static const uint32_t PRINT_TAIL_MS = 2000;
// Room for uart_write_bytes()'s own ring buffer items: a 12-byte descriptor, 8-byte item headers, alignment.
static const size_t TX_ITEM_OVERHEAD = 64;

inline uint16_t tcp_port = 9100;
inline int uart_baud = 9600;
inline std::atomic<bool> client_connected{false};
inline std::atomic<uint32_t> jobs{0};
inline std::atomic<uint32_t> bytes{0};
// Last answer to DLE EOT 2 (offline cause), -1 while the printer does not answer; DLE EOT 3 (error cause) is
// only asked while that answer reports an error.
inline std::atomic<int> offline_status{-1};
inline std::atomic<int> error_status{0};
inline SemaphoreHandle_t uart_lock = nullptr;
// End of the current print for printing(), set by mark_printing().
inline std::atomic<uint32_t> print_until{0};
// Lines fed (ESC d) after print_text and the test page, so the end clears the tear bar. Web UI "Feed Lines".
inline std::atomic<uint8_t> feed_lines{5};
// "Read Printer Info" button: the bridge task queries the printer while idle and fills info_text (one line per
// item), the loop logs it and clears info_ready. The bridge task doesn't log it itself: its log buffer (768 bytes)
// would drop lines.
inline std::atomic<bool> info_requested{false};
inline std::atomic<bool> info_ready{false};
inline std::string info_text;
inline void set_feed_lines(float lines) { feed_lines = (uint8_t) std::clamp((int) lines, 0, 255); }

inline uint32_t now_ms() { return (uint32_t) (esp_timer_get_time() / 1000); }

// Real-time status, standard ESC/POS DLE EOT (not in the manual). The manual's ESC v doesn't match this firmware
// (QR701_T1US-N32-2008): it never sets the "mechanism connected" bit and reports paper out as 0x08, which the
// manual calls "supply above 9.5 V". Measured 2026-09-30: DLE EOT 2 answers 0x12 when ready, 0x32 without paper
// (the lid has no sensor of its own; an open lid reads as paper out).
// DLE EOT 2: 0x04 cover open, 0x08 feed button pressed, 0x20 stopped by paper end, 0x40 error.
// DLE EOT 3: 0x08 auto-cutter error, 0x20 unrecoverable error, 0x40 auto-recoverable error (print head
// temperature, supply voltage).
inline bool online() { return offline_status >= 0; }
inline bool paper_out() {
  int s = offline_status;
  return s >= 0 && (s & 0x20);
}
inline const char *status_text() {
  int s = offline_status;
  if (s < 0)
    return "No answer";
  if (s & 0x20)
    return "Paper out";
  if (s & 0x04)
    return "Cover open";
  if (s & 0x40) {
    int e = error_status;
    if (e & 0x40)
      return "Error: print head too hot or supply voltage";
    if (e & 0x20)
      return "Error: unrecoverable, power-cycle the printer";
    return "Error";
  }
  return "Ready";
}
inline bool ready() {
  int s = offline_status;
  return s >= 0 && !(s & (0x04 | 0x20 | 0x40));
}

// Only call when tx_fits(len). ESP-IDF 5.5's uart_write_bytes() doesn't sleep while the TX ring buffer is full:
// it polls the free space in a loop (uart_tx_all). The calling task then takes the whole CPU, and once it starves
// the ESPHome loop for 5 s the task watchdog resets the C3 (seen 2026-09-30 with a 23 KB screen job).
inline void write(const void *data, size_t len) { uart_write_bytes(UART_PORT, data, len); }

// Payload the TX ring buffer takes right now (the driver's conservative estimate).
inline size_t tx_free() {
  size_t free_space = 0;
  uart_get_tx_buffer_free_size(UART_PORT, &free_space);
  return free_space;
}
inline bool tx_fits(size_t len) { return tx_free() >= len + TX_ITEM_OVERHEAD; }

// Call after queueing print data: printing() stays true until the bytes still in the UART TX buffer are sent
// (10 bits per byte), plus tail_ms. A TCP job ends while up to 64 KB (~68 s at 9600 baud) are still queued.
inline void mark_printing(uint32_t tail_ms = PRINT_TAIL_MS) {
  uint32_t queued = TX_BUFFER - std::min<size_t>(tx_free(), TX_BUFFER);
  print_until = now_ms() + queued * 10000u / uart_baud + tail_ms;
}
// A job is being received or printed (status LED). The status query doesn't count.
inline bool printing() { return client_connected || (int32_t) (print_until - now_ms()) > 0; }

// For writes from the ESPHome loop (action, button): never wait for a running TCP job.
inline bool try_lock(const char *what) {
  if (uart_lock != nullptr && xSemaphoreTake(uart_lock, 0) == pdTRUE)
    return true;
  ESP_LOGW(TAG, "%s skipped: a print job is running", what);
  return false;
}
inline void unlock() { xSemaphoreGive(uart_lock); }

// UTF-8 to what this printer can print. It stays in GB18030 mode and its font has no ä, ö, ü or ß: umlauts become
// ae/oe/ue/ss, ° and µ their GB2312 codes (a double-width glyph), ² and ³ plain digits, anything else '?'.
inline std::string to_printer(const std::string &s) {
  static const struct {
    uint32_t code;
    const char *text;
  } MAP[] = {{0xE4, "ae"}, {0xF6, "oe"}, {0xFC, "ue"}, {0xC4, "Ae"}, {0xD6, "Oe"}, {0xDC, "Ue"}, {0xDF, "ss"},
             {0xB0, "\xA1\xE3"}, {0xB5, "\xA6\xCC"}, {0x3BC, "\xA6\xCC"}, {0xB2, "2"}, {0xB3, "3"}};
  std::string out;
  for (size_t i = 0; i < s.size();) {
    uint8_t c = s[i];
    int n = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 1;
    uint32_t code = c < 0x80 ? c : n == 1 ? 0xFFFD : c & (0x7F >> n);  // 0xFFFD: invalid byte, printed as '?'
    for (int k = 1; k < n && i + k < s.size(); k++)
      code = (code << 6) | (s[i + k] & 0x3F);
    i += n;
    if (code < 0x80) {
      out += (char) code;
      continue;
    }
    const char *text = "?";
    for (const auto &m : MAP) {
      if (m.code == code)
        text = m.text;
    }
    out += text;
  }
  return out;
}

// Prints text (UTF-8, '\n' for new lines) and feeds past the tear bar.
inline void print_text(const std::string &text) {
  if (!try_lock("print_text"))
    return;
  std::string job = "\x1B@" + to_printer(text) + "\n";  // ESC @: initialize
  job += std::string{0x1B, 'd', (char) feed_lines.load()};
  if (!tx_fits(job.size())) {  // the loop must not wait for the UART
    ESP_LOGW(TAG, "print_text skipped: the previous print is still being sent to the printer");
    unlock();
    return;
  }
  ESP_LOGD(TAG, "print_text: %u bytes", (unsigned) job.size());
  write(job.data(), job.size());
  mark_printing();
  unlock();
}

// Own test print instead of the manual's DC2 T self-test, which this firmware ignores: status and settings, the
// character replacements of to_printer(), a 32-character ruler (full width in font A) and a full-width black bar,
// which heats all 384 dots per line and so shows the darkness and whether the supply holds.
inline void print_test_page() {
  if (!try_lock("Test page"))
    return;
  char info[160];
  snprintf(info, sizeof(info), "Status: %s\nBaud: %d, TCP port: %u\n", status_text(), uart_baud, tcp_port);
  std::string job = "\x1B@\x1B" "a\x01\x1B" "E\x01" "QR204 test page\n";  // ESC a 1 centered, ESC E 1 bold
  job += std::string{0x1B, 'E', 0, 0x1B, 'a', 0};             // bold off, left (NUL bytes, so not as a C string)
  job += to_printer(std::string(info) + "Umlauts: ä ö ü Ä Ö Ü ß\nUnits: 21.5 °C, 12 µg/m³\n");
  job += "01234567890123456789012345678901\n";
  const uint8_t bar[] = {0x1D, 'v', '0', 0, 48, 0, 24, 0};  // GS v 0: 48 bytes (384 dots) x 24 rows
  job.append((const char *) bar, sizeof(bar));
  job.append(48 * 24, '\xFF');
  job += std::string{'\n', 0x1B, 'd', (char) feed_lines.load()};
  if (!tx_fits(job.size())) {  // the loop must not wait for the UART
    ESP_LOGW(TAG, "Test page skipped: the previous print is still being sent to the printer");
    unlock();
    return;
  }
  ESP_LOGD(TAG, "Test page: %u bytes", (unsigned) job.size());
  write(job.data(), job.size());
  mark_printing();
  unlock();
}

// Sends DLE EOT n and returns the answer byte, -1 without an answer, -2 for anything else. The printer also sends
// text on its own ("err loop!", "no paper!" when the paper runs out); a DLE EOT answer is a single byte with bits
// 1 and 4 set and bits 0 and 7 clear, so extra bytes or another pattern mean the answer is mixed with that text.
inline int query_status(uint8_t n) {
  uart_flush_input(UART_PORT);
  const uint8_t cmd[] = {0x10, 0x04, n};
  write(cmd, sizeof(cmd));
  uint8_t buf[32];
  if (uart_read_bytes(UART_PORT, buf, 1, pdMS_TO_TICKS(500)) != 1)  // ~100 ms even through WiFi and the bridge
    return -1;
  int extra = uart_read_bytes(UART_PORT, buf + 1, sizeof(buf) - 1, pdMS_TO_TICKS(50));
  if (extra > 0 || (buf[0] & 0x93) != 0x12) {
    ESP_LOGD(TAG, "DLE EOT %u: answer mixed with printer output (%d bytes, first 0x%02X), ignored", (unsigned) n,
             extra + 1, buf[0]);
    return -2;
  }
  return buf[0];
}

// Sends cmd and returns the answer. The GS I answers have no terminator, so reading stops at the first 50 ms
// pause (at 9600 baud the bytes come ~1 ms apart; the first one after ~150 ms).
inline std::string query(const uint8_t *cmd, size_t len) {
  uart_flush_input(UART_PORT);
  write(cmd, len);
  std::string out;
  uint8_t c;
  if (uart_read_bytes(UART_PORT, &c, 1, pdMS_TO_TICKS(500)) != 1)
    return out;
  out += (char) c;
  while (out.size() < 64 && uart_read_bytes(UART_PORT, &c, 1, pdMS_TO_TICKS(50)) == 1)
    out += (char) c;
  return out;
}

// GS I n (transmit printer ID, not in the manual). Text answers from this firmware start with a stray '-' or '_'
// ("-GEZHI", "_17030800001"), which is dropped.
inline std::string printer_id_text(uint8_t n) {
  const uint8_t cmd[] = {0x1D, 'I', n};
  std::string answer = query(cmd, sizeof(cmd));
  size_t start = answer.find_first_not_of("-_");
  std::string text;
  for (size_t i = start == std::string::npos ? answer.size() : start; i < answer.size(); i++) {
    if (answer[i] >= 32 && answer[i] < 127)
      text += answer[i];
  }
  return text.empty() ? "no answer" : text;
}
inline std::string printer_id_byte(uint8_t n) {
  const uint8_t cmd[] = {0x1D, 'I', n};
  std::string answer = query(cmd, sizeof(cmd));
  if (answer.size() != 1)
    return "--";
  char hex[8];
  snprintf(hex, sizeof(hex), "0x%02X", (uint8_t) answer[0]);
  return hex;
}

// Runs in the bridge task while no job is being sent; see info_requested.
inline void read_info() {
  if (info_ready || xSemaphoreTake(uart_lock, 0) != pdTRUE)
    return;  // the loop hasn't logged the last result yet, or print_text is writing: next round
  if (uart_wait_tx_done(UART_PORT, 0) != ESP_OK) {
    unlock();
    return;  // still printing: next round
  }
  info_requested = false;
  std::string t;
  t += "Firmware: " + printer_id_text(0x41) + "\n";
  t += "Manufacturer: " + printer_id_text(0x42) + "\n";
  t += "Model: " + printer_id_text(0x43) + "\n";
  t += "Serial number: " + printer_id_text(0x44) + "\n";
  t += "Model ID " + printer_id_byte(1) + ", type ID " + printer_id_byte(2) + ", ROM version " + printer_id_byte(3) +
       "\n";
  char line[128];
  t += "Status bytes DLE EOT 1-4:";
  for (int n = 1; n <= 4; n++) {
    int value = query_status(n);
    if (value >= 0) {
      snprintf(line, sizeof(line), " 0x%02X", value);
      t += line;
    } else {
      t += " --";
    }
  }
  t += "\n";
  unlock();
  snprintf(line, sizeof(line), "Printer status: %s\n", status_text());
  t += line;
  snprintf(line, sizeof(line), "Bridge: UART %d baud 8N1, TCP port %u, %u jobs and %u bytes since boot\n", uart_baud,
           tcp_port, (unsigned) jobs, (unsigned) bytes);
  t += line;
  info_text = t;
  info_ready = true;
}

inline void poll_status() {
  static int misses = 0;
  if (xSemaphoreTake(uart_lock, 0) != pdTRUE)
    return;  // print_text or the test page is writing right now
  if (uart_wait_tx_done(UART_PORT, 0) != ESP_OK) {
    ESP_LOGD(TAG, "Status query skipped: UART still sending");
    unlock();
    return;
  }
  int s = query_status(2);
  int e = (s >= 0 && (s & 0x40)) ? query_status(3) : 0;
  unlock();
  if (s == -2)
    return;  // keep the previous state, the next query comes in 10 s
  if (s >= 0) {
    misses = 0;
    if (e >= 0)
      error_status = e;
    offline_status = s;
    ESP_LOGD(TAG, "Status 0x%02X: %s", s, status_text());
  } else {
    misses++;
    ESP_LOGD(TAG, "Status query unanswered (%d in a row)", misses);
    if (misses >= STATUS_MISSES_OFFLINE)
      offline_status = -1;
  }
}

inline void serve_client(int fd) {
  xSemaphoreTake(uart_lock, portMAX_DELAY);  // print_text/test page hold it only for one write
  client_connected = true;
  uint32_t job = ++jobs;
  uint32_t job_bytes = 0;
  uint32_t last_data = now_ms();
  uint8_t buf[512];
  ESP_LOGI(TAG, "Job %u: client connected", (unsigned) job);
  for (;;) {
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    timeval tv = {0, 50000};
    int ready = lwip_select(fd + 1, &rfds, nullptr, nullptr, &tv);
    if (ready < 0)
      break;
    if (ready > 0) {
      int n = lwip_recv(fd, buf, sizeof(buf), 0);
      if (n < 0) {
        ESP_LOGW(TAG, "Job %u: connection lost after %u bytes (errno %d), job incomplete", (unsigned) job,
                 (unsigned) job_bytes, errno);
        break;
      }
      if (n == 0)
        break;  // the client closed its side: end of job
      while (!tx_fits(n))
        vTaskDelay(pdMS_TO_TICKS(20));  // sleep instead of letting uart_write_bytes() spin, see write()
      write(buf, n);
      job_bytes += n;
      bytes += n;
      last_data = now_ms();
    } else if (now_ms() - last_data > CLIENT_IDLE_MS) {
      ESP_LOGW(TAG, "Job %u: client idle, closing", (unsigned) job);
      break;
    }
    size_t available = 0;
    uart_get_buffered_data_len(UART_PORT, &available);
    if (available > 0) {
      int n = uart_read_bytes(UART_PORT, buf, std::min(available, sizeof(buf)), 0);
      if (n > 0) {
        ESP_LOGD(TAG, "Job %u: %d bytes from the printer to the client", (unsigned) job, n);
        lwip_send(fd, buf, n, 0);
      }
    }
  }
  lwip_close(fd);
  mark_printing();  // before client_connected drops, so printing() has no gap
  client_connected = false;
  unlock();
  ESP_LOGI(TAG, "Job %u: %u bytes", (unsigned) job, (unsigned) job_bytes);
}

inline int open_server() {
  int fd = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
  if (fd < 0)
    return -1;
  int yes = 1;
  lwip_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(tcp_port);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  if (lwip_bind(fd, (sockaddr *) &addr, sizeof(addr)) != 0 || lwip_listen(fd, 1) != 0) {
    lwip_close(fd);
    return -1;
  }
  return fd;
}

inline void server_task(void *) {
  int server = -1;
  while ((server = open_server()) < 0) {
    ESP_LOGW(TAG, "Cannot open TCP port %u, retrying", tcp_port);
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
  ESP_LOGI(TAG, "Listening on TCP port %u", tcp_port);
  uint32_t last_poll = 0;
  for (;;) {
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(server, &rfds);
    timeval tv = {1, 0};
    if (lwip_select(server + 1, &rfds, nullptr, nullptr, &tv) > 0) {
      int client = lwip_accept(server, nullptr, nullptr);
      if (client >= 0) {
        serve_client(client);
        last_poll = now_ms();  // let the printer work through the job before asking again
      }
      continue;
    }
    if (info_requested)
      read_info();
    if (now_ms() - last_poll >= STATUS_INTERVAL_MS) {
      poll_status();
      last_poll = now_ms();
    }
  }
}

inline bool begin(int tx_pin, int rx_pin, int baud_rate, uint16_t port) {
  tcp_port = port;
  uart_baud = baud_rate;
  uart_config_t cfg = {};
  cfg.baud_rate = baud_rate;
  cfg.data_bits = UART_DATA_8_BITS;
  cfg.parity = UART_PARITY_DISABLE;
  cfg.stop_bits = UART_STOP_BITS_1;
  cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  cfg.source_clk = UART_SCLK_DEFAULT;
  esp_err_t err = uart_driver_install(UART_PORT, RX_BUFFER, TX_BUFFER, 0, nullptr, 0);
  if (err == ESP_OK)
    err = uart_param_config(UART_PORT, &cfg);
  if (err == ESP_OK)
    err = uart_set_pin(UART_PORT, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "UART setup failed: %s", esp_err_to_name(err));
    return false;
  }
  // Keeps RX idle-high when the printer's TX line is not connected.
  gpio_set_pull_mode((gpio_num_t) rx_pin, GPIO_PULLUP_ONLY);
  uart_lock = xSemaphoreCreateMutex();
  ESP_LOGI(TAG, "UART TX GPIO%d, RX GPIO%d, %d baud", tx_pin, rx_pin, baud_rate);
  // Same priority as the ESPHome loop task (1): should the bridge ever busy-wait, time slicing still lets the
  // loop run and feed the task watchdog.
  return xTaskCreate(server_task, "printer_bridge", 4096, nullptr, tskIDLE_PRIORITY + 1, nullptr) == pdPASS;
}

}  // namespace printer_bridge
