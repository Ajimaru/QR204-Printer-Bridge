# QR204 Printer Bridge Wiki

<p align="center">
	<b>Network bridge that turns a QR204 58 mm thermal printer into an ESC/POS network printer.</b><br>
	An ESP32-C3 serves raw TCP port 9100, so anything on the LAN can print: a Home Assistant action, a
	shell one-liner, or a sensor box sending its current screen.
</p>

## Documentation

Follow the documentation workflow from [Features](Features) to [Hardware & Printing](Hardware-and-Printing-Guide), [Assembly Guide](Assembly-Guide), [Setup Guide](Setup-Guide) and [User Guide](User-Guide).

```text
Sensor box (ESP32-S2)  --WiFi, TCP port 9100, ESC/POS-->  ESP32-C3 bridge  --UART (TTL)-->  QR204
airquality                                                thermal-printer
```
