"""Lädt die aktuelle Zendure-App (Headless-Browser, mehrere Quellen) -> app.bin"""
import sys, time
from playwright.sync_api import sync_playwright

UA = "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36"

def save(data, src):
    open("app.bin", "wb").write(data)
    print(f"GESPEICHERT von {src}: {len(data)} Bytes")
    return len(data) > 5_000_000

def diag(pg, label):
    print(f"-- {label}: url={pg.url} titel={pg.title()[:60]!r}")

def try_uptodown(ctx):
    pg = ctx.new_page()
    pg.goto("https://zendure.uptodown.com/android", timeout=60000)
    pg.wait_for_timeout(2000)
    for sel in ("#didomi-notice-agree-button", "button:has-text('Zustimmen')", "button:has-text('Accept')", "#onetrust-accept-btn-handler"):
        try: pg.click(sel, timeout=1500); print("Cookie-Banner:", sel); break
        except Exception: pass
    diag(pg, "uptodown Landing")
    try: print("Version:", pg.inner_text("div.version", timeout=2000))
    except Exception: print("Version: ?")
    pg.goto("https://zendure.uptodown.com/android/download", timeout=60000)
    pg.wait_for_timeout(3000)
    diag(pg, "uptodown Download")
    urls = pg.eval_on_selector_all("[data-url]", "els => els.map(e => [e.id || e.className, e.getAttribute('data-url')])")
    print("data-url-Elemente:", [(i, (u or '')[:16]) for i, u in urls][:8])
    tok = next((u for i, u in urls if u and len(u) > 30), None)
    if not tok:
        print("HTML-Auszug:", pg.content()[:800].replace("\n", " "))
        return False
    r = ctx.request.get("https://dw.uptodown.com/dwn/" + tok, timeout=300000)
    print("dw-Status:", r.status)
    return save(r.body(), "uptodown") if r.ok else False

def try_apkcombo(ctx):
    pg = ctx.new_page()
    pg.goto("https://apkcombo.com/de/zendure/com.zendure.app/download/apk", timeout=60000)
    pg.wait_for_timeout(4000)
    diag(pg, "apkcombo")
    links = pg.eval_on_selector_all("a", "els => els.map(e => e.href).filter(h => h.includes('download.apkcombo') || h.endsWith('.apk') || h.endsWith('.xapk'))")
    print("Links:", [l[:70] for l in links[:5]])
    if not links:
        print("HTML-Auszug:", pg.content()[:800].replace("\n", " "))
        return False
    r = ctx.request.get(links[0], timeout=300000)
    print("Status:", r.status)
    return save(r.body(), "apkcombo") if r.ok else False

with sync_playwright() as p:
    b = p.chromium.launch(args=["--disable-blink-features=AutomationControlled"])
    ctx = b.new_context(user_agent=UA, locale="de-DE", viewport={"width": 1280, "height": 900})
    ok = False
    for f in (try_uptodown, try_apkcombo):
        try: ok = f(ctx)
        except Exception as e: print("Fehler:", type(e).__name__, str(e)[:200])
        if ok: break
    b.close()
    sys.exit(0 if ok else 1)
