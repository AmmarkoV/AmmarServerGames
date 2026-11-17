#!/usr/bin/env python3
"""Tests for tools/frisk_edit.py (resource download and the map/card editor).

1. `download` from a local HTTP server: unpacks, refuses unsafe paths, keeps
   existing files unless --force.
2. `edit` save API on a copy of the LOTR pack: an unchanged save rewrites
   map.json and board.svg byte for byte; count changes, one-sided borders and
   a disconnected territory (mapcheck) are refused and leave the files alone.
3. The editor page in headless Chrome with trusted mouse input: drag a
   corner, Shift+click a new corner, add a border and a sea-line, edit a
   card, save, and check map.json.  Skipped without Chrome or `websockets`.

Skips entirely without res/maps/lotr (not in git).
"""

import asyncio
import functools
import json
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import urllib.request
import zipfile
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from browser_input import RESULTS, Abort, Page, check, find_chrome, require  # noqa: E402

FRISK = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOL = os.path.join(FRISK, "tools", "frisk_edit.py")
PACK = os.path.join(FRISK, "res", "maps", "lotr")


def run_tool(*args):
    return subprocess.run([sys.executable, "-I", TOOL, *args], capture_output=True, text=True)


class QuietHandler(SimpleHTTPRequestHandler):
    def log_message(self, fmt, *args):
        pass


def test_download(tmp):
    www = os.path.join(tmp, "www")
    os.makedirs(www)
    with zipfile.ZipFile(os.path.join(www, "res.zip"), "w") as z:
        z.writestr("res/a.txt", "archive a")
        z.writestr("res/maps/x/map.json", "archive map")
        z.writestr("../evil.txt", "outside")
    server = ThreadingHTTPServer(("127.0.0.1", 0),
                                 functools.partial(QuietHandler, directory=www))
    threading.Thread(target=server.serve_forever, daemon=True).start()
    url = f"http://127.0.0.1:{server.server_address[1]}/res.zip"
    out = os.path.join(tmp, "out")
    try:
        result = run_tool("download", "--url", url, "--zip", os.path.join(tmp, "res.zip"), "--dest", out)
        check(result.returncode == 0 and open(os.path.join(out, "res", "a.txt")).read() == "archive a",
              "download fetches and unpacks the archive")
        check(not os.path.exists(os.path.join(tmp, "evil.txt")), "a path outside the destination is refused")
        with open(os.path.join(out, "res", "maps", "x", "map.json"), "w") as f:
            f.write("edited")
        run_tool("download", "--zip", os.path.join(tmp, "res.zip"), "--dest", out)
        check(open(os.path.join(out, "res", "maps", "x", "map.json")).read() == "edited",
              "a second download keeps local edits")
        run_tool("download", "--zip", os.path.join(tmp, "res.zip"), "--dest", out, "--force")
        check(open(os.path.join(out, "res", "maps", "x", "map.json")).read() == "archive map",
              "--force overwrites local files")
    finally:
        server.shutdown()


def start_editor(pack, backups):
    process = subprocess.Popen([sys.executable, "-I", TOOL, "edit", "--pack", pack, "--port", "0",
                                "--backup-dir", backups, "--no-browser"],
                               stdout=subprocess.PIPE, text=True)
    for _ in range(2):
        line = process.stdout.readline()
        if line.startswith("open "):
            return process, line.split()[1].rstrip("/")
    process.kill()
    raise RuntimeError("the editor did not start")


def post(base, data):
    request = urllib.request.Request(base + "/api/save", data=json.dumps(data).encode("utf-8"), method="POST")
    with urllib.request.urlopen(request, timeout=30) as response:
        return json.load(response)


def files(pack):
    return [open(os.path.join(pack, name), "rb").read() for name in ("map.json", "board.svg")]


def test_save_api(base, pack, backups):
    with urllib.request.urlopen(base + "/api/state", timeout=10) as response:
        state = json.load(response)
    data = state["map"]
    check(state["background"] == "board.jpg" and "SITE_MISSION" in state["effects"] and state["cardImages"],
          "the editor serves the map, effect ids and card art")
    before = files(pack)
    result = post(base, data)
    check(result.get("ok") and files(pack) == before, "an unchanged save rewrites both files byte for byte")
    check(len(os.listdir(backups)) == 2, "the save keeps a backup of map.json and board.svg")

    broken = json.loads(json.dumps(data))
    broken["adventureCards"].pop()
    result = post(base, broken)
    check(not result["ok"] and any("count changed" in e for e in result["errors"]),
          "removing a card is refused (saved games index cards by position)")
    broken = json.loads(json.dumps(data))
    broken["territories"][0]["adjacent"].append(30)
    result = post(base, broken)
    check(not result["ok"] and any("not symmetric" in e for e in result["errors"]), "a one-sided border is refused")
    if os.path.exists(os.path.join(FRISK, "build", "mapcheck")):
        broken = json.loads(json.dumps(data))
        lonely = next(i for i, t in enumerate(broken["territories"])
                      if not any(i in (l["a"], l["b"]) for l in broken["links"]))
        for n in broken["territories"][lonely]["adjacent"]:
            broken["territories"][n]["adjacent"].remove(lonely)
        broken["territories"][lonely]["adjacent"] = []
        broken["bridges"] = [b for b in broken["bridges"] if lonely not in (b["a"], b["b"])]
        result = post(base, broken)
        check(not result["ok"] and result["errors"], f"mapcheck refuses a disconnected territory: {result['errors'][:1]}")
    check(files(pack) == before, "refused saves leave the files alone")
    return data


async def browser_run(chrome_bin, websockets, base, pack, data, tmp):
    profile = os.path.join(tmp, "chrome")
    chrome = subprocess.Popen([chrome_bin, "--headless=new", "--remote-debugging-port=0",
                               f"--user-data-dir={profile}", "--no-first-run", "--no-default-browser-check",
                               "--window-size=1400,1000", "about:blank"],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        port_file = os.path.join(profile, "DevToolsActivePort")
        for _ in range(200):
            if os.path.exists(port_file) and os.path.getsize(port_file) > 0:
                break
            time.sleep(0.05)
        cdp = int(open(port_file).read().split()[0])
        pages = []
        for _ in range(100):
            pages = [t for t in json.load(urllib.request.urlopen(f"http://127.0.0.1:{cdp}/json/list"))
                     if t["type"] == "page"]
            if pages:
                break
            time.sleep(0.05)
        async with websockets.connect(pages[0]["webSocketDebuggerUrl"], max_size=2 ** 26) as ws:
            page = Page(ws)
            await page.call("Page.enable")
            await page.call("Page.navigate", url=base + "/")
            require(await page.wait("document.querySelectorAll('circle.anchor').length > 0", 15), "the editor loads")

            async def center(selector):
                return await page.js(f"""(() => {{ const e = document.querySelector({json.dumps(selector)});
                    if (!e) return null; const r = e.getBoundingClientRect();
                    return [r.left + r.width / 2, r.top + r.height / 2]; }})()""")

            async def click(selector, modifiers=0):
                x, y = await center(selector)
                await page.call("Input.dispatchMouseEvent", type="mouseMoved", x=x, y=y)
                await page.call("Input.dispatchMouseEvent", type="mousePressed", x=x, y=y, button="left",
                                buttons=1, clickCount=1, modifiers=modifiers)
                await page.call("Input.dispatchMouseEvent", type="mouseReleased", x=x, y=y, button="left",
                                clickCount=1, modifiers=modifiers)
                await asyncio.sleep(0.15)

            territories = data["territories"]
            ports = {i for i, t in enumerate(territories) if t.get("port")}
            pick = next(i for i, t in enumerate(territories) if i not in ports and len(t["adjacent"]) < 9)
            await click(f'circle.anchor[data-t="{pick}"]')
            check(await page.js("document.getElementById('t-name').value") == territories[pick]["name"],
                  "clicking a label anchor selects the territory")

            async def press(x, y, modifiers, moves=()):
                await page.call("Input.dispatchMouseEvent", type="mouseMoved", x=x, y=y, modifiers=modifiers)
                await page.call("Input.dispatchMouseEvent", type="mousePressed", x=x, y=y, button="left",
                                buttons=1, clickCount=1, modifiers=modifiers)
                for mx, my in moves:
                    await page.call("Input.dispatchMouseEvent", type="mouseMoved", x=mx, y=my, button="left",
                                    buttons=1, modifiers=modifiers)
                    x, y = mx, my
                await page.call("Input.dispatchMouseEvent", type="mouseReleased", x=x, y=y, button="left",
                                clickCount=1, modifiers=modifiers)
                await asyncio.sleep(0.2)

            ALT, SHIFT = 1, 8
            # drag the first corner 40 px right and 25 px down (Alt: no snapping)
            x, y = await center('circle.vertex[data-v="0"]')
            await press(x, y, ALT, [(x + 8 * k, y + 5 * k) for k in range(1, 6)])
            # Shift+click on the middle of the edge between corners 2 and 3
            a, b = await center('circle.vertex[data-v="2"]'), await center('circle.vertex[data-v="3"]')
            await press((a[0] + b[0]) / 2, (a[1] + b[1]) / 2, SHIFT)

            # Redraw another territory's outline as a square around its label
            third = next(i for i in range(len(territories)) if i != pick)
            await click(f'circle.anchor[data-t="{third}"]')
            await click("#t-redraw")
            cx, cy = await center(f'circle.anchor[data-t="{third}"]')
            for dx, dy in ((-15, -15), (15, -15), (15, 15), (-15, 15), (-15, -15)):
                await press(cx + dx, cy + dy, ALT)
            await click(f'circle.anchor[data-t="{pick}"]')

            # Connections: a border and a sea-line
            await click('nav button[data-tab="graph"]')
            other = next(i for i, t in enumerate(territories)
                         if i != pick and i not in territories[pick]["adjacent"] and len(t["adjacent"]) < 9
                         and i not in ports)
            await click(f'circle.anchor[data-t="{pick}"]')
            await click(f'circle.anchor[data-t="{other}"]')
            await click('input[name=mode][value=sea]')
            await click(f'circle.anchor[data-t="{pick}"]')
            await click(f'circle.anchor[data-t="{other}"]')

            # Cards: retitle the first card
            await click('nav button[data-tab="cards"]')
            await click('#c-list div')
            await page.js("""(() => { const i = document.getElementById('c-title'); i.value = 'Edited title';
                i.dispatchEvent(new Event('change')); })()""")
            await click("#save")
            saved = await page.wait("document.getElementById('status').textContent.startsWith('Saved')", 15)
            require(saved, "the editor saves" + ("" if saved else ": " + str(
                await page.js("document.getElementById('errors').textContent"))))
            return pick, other, third
    finally:
        chrome.terminate()


def main():
    if not os.path.exists(os.path.join(PACK, "map.json")):
        print("SKIP: map editor test (needs res/maps/lotr)")
        return 0
    tmp = tempfile.mkdtemp(prefix="frisk-editor-")
    editor = None
    try:
        test_download(tmp)
        pack = os.path.join(tmp, "lotr")
        backups = os.path.join(tmp, "backups")
        shutil.copytree(PACK, pack)
        editor, base = start_editor(pack, backups)
        data = test_save_api(base, pack, backups)
        chrome_bin = find_chrome()
        try:
            import websockets
        except ImportError:
            websockets = None
        if chrome_bin is None or websockets is None:
            print("SKIP: editor browser checks (needs Chrome and python websockets)")
        else:
            try:
                pick, other, third = asyncio.run(browser_run(chrome_bin, websockets, base, pack, data, tmp))
                saved = json.load(open(os.path.join(pack, "map.json"), encoding="utf-8"))
                old_pts = data["territories"][pick]["path"].count("L")
                new_path = saved["territories"][pick]["path"]
                check(new_path.split(" L ")[0] != data["territories"][pick]["path"].split(" L ")[0],
                      "dragging a corner moved it")
                check(new_path.count("L") == old_pts + 1, "Shift+click added a corner")
                check(other in saved["territories"][pick]["adjacent"] and
                      pick in saved["territories"][other]["adjacent"], "the new border is on both sides")
                check(any({l["a"], l["b"]} == {pick, other} for l in saved["links"]) and
                      saved["territories"][pick]["port"] and saved["territories"][other]["port"],
                      "the new sea-line made both ends ports")
                check(saved["adventureCards"][0]["title"] == "Edited title", "the card title was saved")
                check(saved["territories"][third]["path"].count("L") == 3,
                      "Redraw outline replaced an outline with 4 clicked corners")
                svg = open(os.path.join(pack, "board.svg"), encoding="utf-8").read()
                check(new_path in svg, "board.svg was rebuilt with the new outline")
            except Abort:
                pass
    except Exception as error:
        import traceback
        traceback.print_exc()
        check(False, f"map editor test aborted: {error!r}")
    finally:
        if editor is not None:
            editor.terminate()
        shutil.rmtree(tmp, ignore_errors=True)
    failed = [message for ok, message in RESULTS if not ok]
    if failed:
        print(f"{len(failed)} map editor check(s) failed.")
        return 1
    print("All map editor tests passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
