"""Lädt die aktuelle Zendure-App von Uptodown (per Headless-Browser) -> app.bin"""
import re, sys
from playwright.sync_api import sync_playwright

with sync_playwright() as p:
    b = p.chromium.launch()
    ctx = b.new_context(user_agent="Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0 Safari/537.36", locale="de-DE")
    pg = ctx.new_page()
    pg.goto("https://zendure.uptodown.com/android/download", timeout=60000)
    pg.wait_for_timeout(2500)
    # Cookie-Banner wegklicken, falls vorhanden
    for sel in ("#didomi-notice-agree-button", "button:has-text('Zustimmen')", "button:has-text('Accept')"):
        try: pg.click(sel, timeout=2000); break
        except Exception: pass
    ver = ""
    try: ver = pg.inner_text("div.version", timeout=3000)
    except Exception: pass
    print("Version laut Seite:", ver or "?")
    btn = pg.wait_for_selector("#detail-download-button", timeout=30000)
    du = btn.get_attribute("data-url")
    print("data-url:", (du or "")[:12] + "…", "Länge", len(du or ""))
    url = "https://dw.uptodown.com/dwn/" + du
    r = ctx.request.get(url, timeout=300000, max_redirects=10)
    data = r.body()
    open("app.bin", "wb").write(data)
    print("Geladen:", len(data), "Bytes, Status", r.status)
    b.close()
