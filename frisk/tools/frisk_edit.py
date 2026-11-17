#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""FRISK resource downloader and map/card editor.

    python3 tools/frisk_edit.py download [--url URL] [--zip FILE] [--dest DIR] [--force]
    python3 tools/frisk_edit.py edit [--pack DIR] [--port N] [--backup-dir DIR] [--no-browser]

`download` fetches the resource archive (board scans, card art, music,
taunts and the LOTR pack) from ammar.gr and unpacks it into the frisk
directory.  Files that already exist are kept unless --force is given, so
local map edits survive a re-download.

`edit` starts a local editor (127.0.0.1 only) and opens it in the browser.
It edits a pack's map.json in place: territory polygons, label anchors and
properties, borders, sea-lines, bridges, mountain ranges, territory-card
symbols and the adventure cards.  Saving validates the result with
build/mapcheck, keeps a backup in frisk/map_backups/ and rewrites board.svg.
map.json is the source of truth; tools/mkmap.py only bootstraps it.

Territories and adventure cards are edited in place, never added, removed or
reordered: saved games refer to both by position.  Restart the frisk server
to load a saved pack.  Standard library only.
"""

import argparse
import datetime
import html
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import urllib.parse
import urllib.request
import webbrowser
import zipfile
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

FRISK = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_URL = "http://ammar.gr/frisk_resources.zip"
DEFAULT_PACK = os.path.join(FRISK, "res", "maps", "lotr")
EDITOR_HTML = os.path.join(FRISK, "tools", "map_editor.html")
MAPCHECK = os.path.join(FRISK, "build", "mapcheck")
BACKUPS = os.path.join(FRISK, "map_backups")
PATH_RE = re.compile(r"^\s*M\s*(-?[\d.]+)\s*,\s*(-?[\d.]+)((?:\s*L\s*-?[\d.]+\s*,\s*-?[\d.]+)*)\s*Z\s*$")


# ---------------------------------------------------------------------------
# download
# ---------------------------------------------------------------------------

def fetch(url, target):
    """Download url to target (via target.part); reuse a complete copy."""
    request = urllib.request.Request(url, headers={"User-Agent": "frisk-edit"})
    with urllib.request.urlopen(request, timeout=60) as response:
        size = int(response.headers.get("Content-Length") or 0)
        if size and os.path.exists(target) and os.path.getsize(target) == size:
            print(f"{target} is already complete ({size} bytes)")
            return
        part = target + ".part"
        done = 0
        with open(part, "wb") as out:
            while True:
                chunk = response.read(1 << 20)
                if not chunk:
                    break
                out.write(chunk)
                done += len(chunk)
                if size:
                    print(f"\r{done >> 20} / {size >> 20} MB", end="", flush=True)
        print()
    if size and done != size:
        raise SystemExit(f"download incomplete: {done} of {size} bytes")
    os.replace(part, target)


def extract(zip_path, destination, force):
    added = replaced = kept = 0
    root = os.path.realpath(destination)
    with zipfile.ZipFile(zip_path) as archive:
        for member in archive.infolist():
            if member.is_dir():
                continue
            target = os.path.realpath(os.path.join(root, member.filename))
            if not target.startswith(root + os.sep):
                print(f"skipped unsafe path {member.filename}")
                continue
            exists = os.path.exists(target)
            if exists and not force:
                kept += 1
                continue
            os.makedirs(os.path.dirname(target), exist_ok=True)
            with archive.open(member) as source, open(target, "wb") as out:
                shutil.copyfileobj(source, out)
            replaced += exists
            added += not exists
    print(f"{added} files added, {replaced} replaced, {kept} existing files kept"
          + ("" if force or not kept else " (--force overwrites them)"))


def cmd_download(args):
    zip_path = args.zip or os.path.join(FRISK, "frisk_resources.zip")
    if not args.zip or not os.path.exists(zip_path):
        print(f"downloading {args.url}")
        fetch(args.url, zip_path)
    extract(zip_path, args.dest, args.force)


# ---------------------------------------------------------------------------
# validation and saving
# ---------------------------------------------------------------------------

def effect_ids(current):
    ids = []
    try:
        with open(os.path.join(FRISK, "engine", "cards.c"), encoding="utf-8") as f:
            ids = re.findall(r'^\s*"([A-Z_]+)",', f.read(), re.M)
    except OSError:
        pass
    for card in current.get("adventureCards", []):
        if card.get("effect") not in ids:
            ids.append(card.get("effect"))
    return ids


def structure_errors(new, old):
    """Changes that would break saved games, plus basic shape checks."""
    errors = []
    for key in ("territories", "adventureCards", "territoryCards", "regions"):
        if len(new.get(key, [])) != len(old.get(key, [])):
            errors.append(f"{key}: the count changed ({len(old.get(key, []))} -> "
                          f"{len(new.get(key, []))}); saved games refer to them by position")
    for i, (a, b) in enumerate(zip(new.get("adventureCards", []), old.get("adventureCards", []))):
        if a.get("id") != b.get("id"):
            errors.append(f"adventure card {i}: id changed or cards were reordered")
    count = len(new.get("territories", []))
    for i, t in enumerate(new.get("territories", [])):
        if t.get("id") != i:
            errors.append(f"territory {i}: id must stay {i}")
        if not PATH_RE.match(t.get("path", "")):
            errors.append(f"{t.get('name')}: path must be a closed polygon (M x,y L x,y ... Z)")
        elif len(t["path"].split("L")) < 3:
            errors.append(f"{t.get('name')}: a polygon needs at least 3 points")
        for n in t.get("adjacent", []):
            if not (0 <= n < count) or i not in new["territories"][n].get("adjacent", []):
                errors.append(f"{t.get('name')}: border with {n} is not symmetric")
    return errors


def run_mapcheck(pack, data):
    """Run build/mapcheck on a temporary copy of the pack with the new map.json."""
    if not os.path.exists(MAPCHECK):
        return True, "mapcheck is not built (cmake --build build); skipped"
    with tempfile.TemporaryDirectory(prefix="frisk-edit-") as tmp:
        for entry in os.listdir(pack):
            if entry != "map.json":
                os.symlink(os.path.join(pack, entry), os.path.join(tmp, entry))
        with open(os.path.join(tmp, "map.json"), "w", encoding="utf-8") as f:
            f.write(map_text(data))
        result = subprocess.run([MAPCHECK, tmp], capture_output=True, text=True)
    return result.returncode == 0, (result.stdout + result.stderr).strip()


def map_text(data):
    return json.dumps(data, indent=2, ensure_ascii=False) + "\n"


def svg_text(data, old_svg):
    """board.svg as tools/mkmap.py writes it: the head (styles, board image)
    is kept from the current file, the links and territories are rebuilt."""
    width, height = data["image"]["width"], data["image"]["height"]
    head = old_svg.split('  <g id="links">')[0] if '  <g id="links">' in old_svg else None
    if head is None:
        head = ('<?xml version="1.0" encoding="UTF-8"?>\n'
                '<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" '
                f'width="{width}" height="{height}" viewBox="0 0 {width} {height}">\n')
    esc = lambda text: html.escape(str(text), quote=True)  # noqa: E731
    names = [t["name"] for t in data["territories"]]
    regions = {r["id"]: r["name"] for r in data["regions"]}
    lines = [head.rstrip("\n"), '  <g id="links">']
    for link in data.get("links", []):
        a, b = esc(names[link["a"]]), esc(names[link["b"]])
        lines.append(f'    <path id="l{link["a"]}-{link["b"]}" class="link" data-a="{a}" data-b="{b}" '
                     f'data-kind="{esc(link["kind"])}" d="{esc(link["path"])}"><title>{a} - {b}</title></path>')
    lines += ["  </g>", '  <g id="territories">']
    for t in data["territories"]:
        name = esc(t["name"])
        lines.append(f'    <path id="t{t["id"]}" class="territory" data-name="{name}" '
                     f'data-region="{esc(regions[t["region"]])}" '
                     f'data-label="{int(t["label"][0])},{int(t["label"][1])}" d="{esc(t["path"])}">'
                     f'<title>{name}</title></path>')
    lines += ["  </g>", "</svg>"]
    return "\n".join(lines) + "\n"


def save_pack(pack, data, backups=BACKUPS):
    with open(os.path.join(pack, "map.json"), encoding="utf-8") as f:
        old = json.load(f)
    errors = structure_errors(data, old)
    if errors:
        return {"ok": False, "errors": errors}
    ok, report = run_mapcheck(pack, data)
    if not ok:
        return {"ok": False, "errors": [line for line in report.splitlines() if line.strip()]}
    svg_path = os.path.join(pack, data["image"]["file"])
    old_svg = open(svg_path, encoding="utf-8").read() if os.path.exists(svg_path) else ""
    os.makedirs(backups, exist_ok=True)
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    name = os.path.basename(os.path.normpath(pack))
    shutil.copy(os.path.join(pack, "map.json"), os.path.join(backups, f"{name}-{stamp}-map.json"))
    if old_svg:
        shutil.copy(svg_path, os.path.join(backups, f"{name}-{stamp}-board.svg"))
    for path, text in ((os.path.join(pack, "map.json"), map_text(data)),
                       (svg_path, svg_text(data, old_svg))):
        with open(path + ".tmp", "w", encoding="utf-8") as f:
            f.write(text)
        os.replace(path + ".tmp", path)
    return {"ok": True, "report": report, "backup": os.path.join(backups, f"{name}-{stamp}-*")}


# ---------------------------------------------------------------------------
# editor server
# ---------------------------------------------------------------------------

def make_handler(pack, backups):
    lock = threading.Lock()

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, fmt, *args):
            pass

        def send(self, code, body, kind="application/json; charset=utf-8"):
            if isinstance(body, (dict, list)):
                body = json.dumps(body, ensure_ascii=False)
            if isinstance(body, str):
                body = body.encode("utf-8")
            self.send_response(code)
            self.send_header("Content-Type", kind)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            path = urllib.parse.unquote(self.path.split("?")[0])
            if path == "/":
                with open(EDITOR_HTML, encoding="utf-8") as f:
                    return self.send(200, f.read(), "text/html; charset=utf-8")
            if path == "/api/state":
                with open(os.path.join(pack, "map.json"), encoding="utf-8") as f:
                    data = json.load(f)
                cards = os.path.join(pack, "cards")
                images = sorted("cards/" + n for n in os.listdir(cards)) if os.path.isdir(cards) else []
                background = None
                svg_path = os.path.join(pack, data["image"]["file"])
                if os.path.exists(svg_path):
                    match = re.search(r'<image[^>]*\shref="([^"]+)"', open(svg_path, encoding="utf-8").read())
                    background = match.group(1) if match else None
                return self.send(200, {"map": data, "effects": effect_ids(data), "cardImages": images,
                                       "background": background, "pack": pack})
            if path.startswith("/pack/"):
                root = os.path.realpath(pack)
                target = os.path.realpath(os.path.join(root, path[len("/pack/"):]))
                if target.startswith(root + os.sep) and os.path.isfile(target):
                    kinds = {".jpg": "image/jpeg", ".jpeg": "image/jpeg", ".png": "image/png",
                             ".svg": "image/svg+xml", ".webp": "image/webp"}
                    kind = kinds.get(os.path.splitext(target)[1].lower(), "application/octet-stream")
                    with open(target, "rb") as f:
                        return self.send(200, f.read(), kind)
            self.send(404, {"error": "not found"})

        def do_POST(self):
            if self.path != "/api/save":
                return self.send(404, {"error": "not found"})
            try:
                data = json.loads(self.rfile.read(int(self.headers.get("Content-Length") or 0)))
            except ValueError:
                return self.send(400, {"ok": False, "errors": ["the request is not JSON"]})
            with lock:
                try:
                    result = save_pack(pack, data, backups)
                except (KeyError, TypeError, IndexError, ValueError) as error:
                    result = {"ok": False, "errors": [f"malformed map: {error!r}"]}
            self.send(200, result)

    return Handler


def cmd_edit(args):
    pack = os.path.abspath(args.pack)
    if not os.path.exists(os.path.join(pack, "map.json")):
        raise SystemExit(f"no map.json in {pack}; run `{sys.argv[0]} download` first")
    server = ThreadingHTTPServer(("127.0.0.1", args.port), make_handler(pack, os.path.abspath(args.backup_dir)))
    url = f"http://127.0.0.1:{server.server_address[1]}/"
    print(f"editing {pack}\nopen {url}  (Ctrl+C to stop)", flush=True)
    if not args.no_browser:
        threading.Timer(0.3, webbrowser.open, (url,)).start()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    server.server_close()


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = parser.add_subparsers(dest="command", required=True)
    down = sub.add_parser("download", help="download and unpack the resource archive")
    down.add_argument("--url", default=DEFAULT_URL)
    down.add_argument("--zip", help="use this archive (downloaded here if missing)")
    down.add_argument("--dest", default=FRISK, help="unpack here (default: the frisk directory)")
    down.add_argument("--force", action="store_true", help="overwrite existing files")
    down.set_defaults(run=cmd_download)
    edit = sub.add_parser("edit", help="edit a map pack in the browser")
    edit.add_argument("--pack", default=DEFAULT_PACK)
    edit.add_argument("--port", type=int, default=8765)
    edit.add_argument("--backup-dir", default=BACKUPS, help="where saves keep the previous files")
    edit.add_argument("--no-browser", action="store_true")
    edit.set_defaults(run=cmd_edit)
    args = parser.parse_args()
    args.run(args)


if __name__ == "__main__":
    main()
