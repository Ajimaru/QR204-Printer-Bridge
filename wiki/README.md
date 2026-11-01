# Wiki navigation templates

These three files are **not** synced by the `wiki-sync` workflow. They are the wiki's landing page and
navigation, and they live only in the wiki itself.

Add them once, when the wiki is first created:

```sh
git clone https://github.com/Ajimaru/QR204-Printer-Bridge.wiki.git
cp wiki/Home.md wiki/_Sidebar.md wiki/_Footer.md QR204-Printer-Bridge.wiki/
cd QR204-Printer-Bridge.wiki && git add -A && git commit -m "docs: wiki navigation" && git push
```

A GitHub wiki must have at least one page before it can be cloned: create the Home page once in the browser
(Wiki tab → Create the first page), then the clone above works.

The guide pages themselves come from `docs/` and are published automatically. Editing them in the wiki is
pointless — the next sync overwrites them.
