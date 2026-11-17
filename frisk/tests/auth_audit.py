#!/usr/bin/env python3
"""Authorization and input-audit negative tests for every FRISK endpoint.

Runs against an isolated server/database on the generated tiny map.  Each
check names the endpoint and the role that must be refused; a failure prints
the unexpectedly successful response.
"""

import json
import os
import shutil
import sys
import tempfile

import requests

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import long_game as lg  # noqa: E402

FAILURES = 0


def post(session, base, path, fields=None, upload=None):
    body = {key: (None, str(value)) for key, value in (fields or {}).items()
            if value is not None and value != ""}
    if upload is not None:
        body["clip"] = upload
    try:
        response = session.post(base + path, files=body, timeout=20,
                                headers={"Connection": "close"})
        return response.status_code, response.json()
    except Exception as error:
        return 0, {"ok": 0, "err": "exception", "msg": repr(error)}


def get(session, base, path, params=None):
    try:
        response = session.get(base + path, params=params or {}, timeout=20,
                               headers={"Connection": "close"})
        return response.status_code, response.json()
    except Exception as error:
        return 0, {"ok": 0, "err": "exception", "msg": repr(error)}


def expect(name, response, err):
    global FAILURES
    status, body = response
    if body.get("ok") == 0 and body.get("err") == err:
        print(f"PASS: {name}")
        return True
    print(f"FAIL: {name} (status {status}, got {json.dumps(body)[:240]})",
          file=sys.stderr)
    FAILURES += 1
    return False


def expect_ok(name, response):
    global FAILURES
    status, body = response
    if status == 200 and (body.get("ok") == 1 or
                          ("format" in body and "players" in body)):
        print(f"PASS: {name}")
        return True
    print(f"FAIL: {name} (status {status}, got {json.dumps(body)[:240]})",
          file=sys.stderr)
    FAILURES += 1
    return False


def main():
    global FAILURES
    tmp = tempfile.mkdtemp(prefix="frisk-audit-")
    server = None
    try:
        server, base = lg.start_server(18106, tmp)
        admin, admin_csrf = lg.register(base, "admin")
        invite = lg.make_invite(base, admin, admin_csrf)
        bob, bob_csrf = lg.register(base, "bob", invite)
        invite = lg.make_invite(base, admin, admin_csrf)
        carol, carol_csrf = lg.register(base, "carol", invite)
        anon = requests.Session()

        # Identity/auth boundary.
        status, body = get(anon, base, "/api/me")
        if status == 200 and body.get("user") is None:
            print("PASS: /api/me anonymous read")
        else:
            print(f"FAIL: /api/me anonymous (status {status})", file=sys.stderr)
            FAILURES += 1
        expect("/api/login GET", get(anon, base, "/api/login"), "post")
        expect("/api/register GET", get(anon, base, "/api/register"), "post")
        expect("/api/logout anonymous",
               post(anon, base, "/api/logout", {"csrf": "x"}), "login")
        expect("/api/invite/new anonymous",
               post(anon, base, "/api/invite/new", {"csrf": "x"}), "login")
        expect("/api/invite/new missing csrf",
               post(admin, base, "/api/invite/new", {"dummy": "1"}), "csrf")
        expect("/api/invite/list anonymous",
               get(anon, base, "/api/invite/list"), "login")
        expect("/api/profile anonymous", get(anon, base, "/api/profile"),
               "login")
        expect("/api/profile/set anonymous",
               post(anon, base, "/api/profile/set", {"colour": "#ff0000"}),
               "login")
        expect("/api/profile/set missing csrf",
               post(admin, base, "/api/profile/set", {"colour": "#ff0000"}),
               "csrf")
        expect("/api/profile/set bad colour",
               post(admin, base, "/api/profile/set",
                    {"csrf": admin_csrf, "colour": "red"}), "colour")
        expect("/api/profile/set bad glyph",
               post(admin, base, "/api/profile/set",
                    {"csrf": admin_csrf, "glyph": "\n"}), "glyph")
        expect("/api/profile overlong user",
               get(admin, base, "/api/profile", {"u": "a" * 40}), "name")
        expect_ok("/api/profile own", get(admin, base, "/api/profile"))

        # Invite ownership.
        open_invite = lg.make_invite(base, admin, admin_csrf)
        expect("/api/invite/revoke non-owner",
               post(bob, base, "/api/invite/revoke",
                    {"csrf": bob_csrf, "code": open_invite}), "invite")
        expect_ok("/api/invite/revoke owner",
                  post(admin, base, "/api/invite/revoke",
                       {"csrf": admin_csrf, "code": open_invite}))

        # Lobby and numeric input.
        expect("/api/lobby anonymous", get(anon, base, "/api/lobby"), "login")
        expect("/api/lobby bad since",
               get(admin, base, "/api/lobby", {"since": "abc"}), "params")
        expect_ok("/api/lobby member", get(admin, base, "/api/lobby"))
        expect("/api/game/create anonymous",
               post(anon, base, "/api/game/create", {"g": 1}), "login")
        expect("/api/game/create missing csrf",
               post(admin, base, "/api/game/create",
                    {"name": "No", "map": "tiny", "seats": 2, "mode": "deal"}),
               "csrf")
        expect("/api/game/create bad seats",
               post(admin, base, "/api/game/create",
                    {"csrf": admin_csrf, "name": "No", "map": "tiny",
                     "seats": 9, "mode": "deal", "batch": 5}), "params")
        expect("/api/game/create bad batch",
               post(admin, base, "/api/game/create",
                    {"csrf": admin_csrf, "name": "No", "map": "tiny",
                     "seats": 2, "mode": "deal", "batch": 99999}), "params")
        expect("/api/game/create long name",
               post(admin, base, "/api/game/create",
                    {"csrf": admin_csrf, "name": "x" * 200, "map": "tiny",
                     "seats": 2, "mode": "deal", "batch": 5}), "params")
        expect("/api/game/create unknown map",
               post(admin, base, "/api/game/create",
                    {"csrf": admin_csrf, "name": "No", "map": "nope",
                     "seats": 2, "mode": "deal", "batch": 5}), "map")
        expect_ok("/api/admin/overview admin",
                  get(admin, base, "/api/admin/overview"))
        expect("/api/admin/overview anonymous",
               get(anon, base, "/api/admin/overview"), "login")
        expect("/api/admin/overview non-admin",
               get(bob, base, "/api/admin/overview"), "admin")

        created = post(admin, base, "/api/game/create",
                       {"csrf": admin_csrf, "name": "Audit Game",
                        "map": "tiny", "seats": 2, "mode": "deal",
                        "batch": 5})[1]
        if not expect_ok("/api/game/create valid",
                         (200, created)):
            return 1
        game_id = created["id"]
        leave_game = post(admin, base, "/api/game/create",
                          {"csrf": admin_csrf, "name": "Leave Game",
                           "map": "tiny", "seats": 2, "mode": "deal",
                           "batch": 5})[1]
        if not expect_ok("/api/game/create leave game", (200, leave_game)):
            return 1
        expect("/api/game/leave non-seat",
               post(carol, base, "/api/game/leave",
                    {"csrf": carol_csrf, "g": leave_game["id"]}), "seat")
        expect("/api/game/join anonymous",
               post(anon, base, "/api/game/join",
                    {"csrf": "x", "g": game_id}), "login")
        expect_ok("/api/game/join seated",
                  post(bob, base, "/api/game/join",
                       {"csrf": bob_csrf, "g": game_id}))
        expect("/api/game/start anonymous",
               post(anon, base, "/api/game/start",
                    {"csrf": "x", "g": game_id}), "login")
        expect("/api/game/start non-host",
               post(bob, base, "/api/game/start",
                    {"csrf": bob_csrf, "g": game_id}), "host")
        expect_ok("/api/game/start host",
                  post(admin, base, "/api/game/start",
                       {"csrf": admin_csrf, "g": game_id}))

        # Game reads and per-seat permissions.
        expect("/api/game/state anonymous",
               get(anon, base, "/api/game/state", {"g": game_id}), "login")
        expect_ok("/api/game/state spectator",
                  get(carol, base, "/api/game/state", {"g": game_id}))
        expect("/api/game/state bad since",
               get(admin, base, "/api/game/state",
                   {"g": game_id, "since": "x"}), "params")
        expect("/api/game/state missing g",
               get(admin, base, "/api/game/state", {}), "game")
        expect("/api/game/state unknown",
               get(admin, base, "/api/game/state", {"g": 999999}), "notfound")
        expect("/api/game/log anonymous",
               get(anon, base, "/api/game/log", {"g": game_id}), "login")
        expect_ok("/api/game/log spectator",
                  get(carol, base, "/api/game/log", {"g": game_id}))
        for field in ("after", "limit", "from"):
            expect(f"/api/game/log bad {field}",
                   get(admin, base, "/api/game/log",
                       {"g": game_id, field: "x"}), "params")
        expect("/api/game/act anonymous",
               post(anon, base, "/api/game/act",
                    {"csrf": "x", "g": game_id, "type": "end_turn"}), "login")
        expect("/api/game/act non-seat",
               post(carol, base, "/api/game/act",
                    {"csrf": carol_csrf, "g": game_id,
                     "type": "end_turn"}), "seat")
        expect("/api/game/act missing csrf",
               post(admin, base, "/api/game/act",
                    {"g": game_id, "type": "end_turn"}), "csrf")
        # If it is bob's turn this is an invalid phase, otherwise it is not
        # his turn; either way a seated non-actor must not be able to act.
        status, body = post(bob, base, "/api/game/act",
                            {"csrf": bob_csrf, "g": game_id,
                             "type": "end_turn"})
        if body.get("ok") == 0:
            print("PASS: /api/game/act out-of-turn/invalid phase")
        else:
            print("FAIL: /api/game/act out-of-turn/invalid phase",
                  file=sys.stderr)
            FAILURES += 1
        expect("/api/game/kick non-host",
               post(bob, base, "/api/game/kick",
                    {"csrf": bob_csrf, "g": game_id, "user": "admin"}),
               "host")
        expect("/api/game/delete non-host",
               post(bob, base, "/api/game/delete",
                    {"csrf": bob_csrf, "g": game_id}), "host")

        # Chat and clips.
        expect("/api/chat anonymous",
               get(anon, base, "/api/chat", {"room": "lobby"}), "login")
        expect("/api/chat invalid room",
               get(admin, base, "/api/chat", {"room": "g0"}), "room")
        expect("/api/chat unknown game",
               get(admin, base, "/api/chat", {"room": "g999999"}), "notfound")
        expect_ok("/api/chat spectator",
                  get(carol, base, "/api/chat", {"room": f"g{game_id}"}))
        expect("/api/chat bad after",
               get(admin, base, "/api/chat",
                   {"room": "lobby", "after": "x"}), "params")
        expect("/api/chat/say anonymous",
               post(anon, base, "/api/chat/say",
                    {"csrf": "x", "room": "lobby", "text": "hi"}), "login")
        expect("/api/chat/say non-seat",
               post(carol, base, "/api/chat/say",
                    {"csrf": carol_csrf, "room": f"g{game_id}",
                     "text": "hi"}), "seat")
        expect("/api/chat/say missing csrf",
               post(bob, base, "/api/chat/say",
                    {"room": f"g{game_id}", "text": "hi"}), "csrf")
        expect("/api/chat/say overlong",
               post(bob, base, "/api/chat/say",
                    {"csrf": bob_csrf, "room": f"g{game_id}",
                     "text": "x" * 501}), "text")
        expect_ok("/api/chat/say seated",
                  post(bob, base, "/api/chat/say",
                       {"csrf": bob_csrf, "room": f"g{game_id}",
                        "text": "audit good"}))
        expect("/api/chat/clip anonymous",
               post(anon, base, "/api/chat/clip",
                    {"csrf": "x", "room": "lobby"}), "login")
        expect("/api/chat/clip non-seat",
               post(carol, base, "/api/chat/clip",
                    {"csrf": carol_csrf, "room": f"g{game_id}"}), "seat")
        expect("/api/chat/clip missing csrf",
               post(admin, base, "/api/chat/clip",
                    {"room": "lobby"}), "csrf")
        expect("/api/chat/clip invalid data",
               post(admin, base, "/api/chat/clip",
                    {"csrf": admin_csrf, "room": "lobby"},
                    ("bad.mp3", b"<html>not audio</html>")), "clip")
        expect("/api/clip anonymous",
               get(anon, base, "/api/clip", {"f": "a.wav"}), "login")
        expect("/api/clip traversal",
               get(admin, base, "/api/clip", {"f": "../users.db"}), "clip")
        expect_ok("/api/map public", get(anon, base, "/api/map"))

        # Admin neutralise is admin-only and validates its seat.
        expect("/api/admin/neutralise anonymous",
               post(anon, base, "/api/admin/neutralise",
                    {"csrf": "x", "g": game_id, "seat": 0}), "login")
        expect("/api/admin/neutralise non-admin",
               post(bob, base, "/api/admin/neutralise",
                    {"csrf": bob_csrf, "g": game_id, "seat": 0}), "admin")
        expect("/api/admin/neutralise bad seat",
               post(admin, base, "/api/admin/neutralise",
                    {"csrf": admin_csrf, "g": game_id, "seat": 99}), "params")
        status, body = post(admin, base, "/api/admin/neutralise",
                            {"csrf": admin_csrf, "g": game_id, "seat": 0})
        if body.get("ok") == 0 and body.get("err") == "action":
            print("PASS: /api/admin/neutralise setup phase refused")
        else:
            print("FAIL: /api/admin/neutralise setup phase "
                  f"(status {status}, got {json.dumps(body)[:240]})",
                  file=sys.stderr)
            FAILURES += 1

        # Drive the setup phases to a normal turn, then verify the positive
        # path of the admin neutralise action.
        sessions = {"admin": (admin, admin_csrf), "bob": (bob, bob_csrf)}
        for _ in range(200):
            status, state = get(admin, base, "/api/game/state",
                                {"g": game_id})
            if state.get("phase") in ("reinforce", "attack", "fortify"):
                break
            current_seat = int(state.get("current", -1))
            if current_seat < 0 or current_seat >= len(state.get("players", [])):
                break
            current = state["players"][current_seat]["user"]
            session, csrf = sessions[current]
            own = get(session, base, "/api/game/state", {"g": game_id})[1]
            legal = own.get("legal") or {}
            phase = own.get("phase")
            if phase == "setup_claim" and legal.get("claimable"):
                post(session, base, "/api/game/act",
                     {"csrf": csrf, "g": game_id, "type": "claim",
                      "t": legal["claimable"][0]})
            elif phase == "setup_place" and legal.get("placeable"):
                post(session, base, "/api/game/act",
                     {"csrf": csrf, "g": game_id, "type": "place",
                      "t": legal["placeable"][0],
                      "count": min(5, int(legal.get("batchRemaining") or 1))})
            elif phase == "setup_leaders" and legal.get("leaderPlaceable"):
                post(session, base, "/api/game/act",
                     {"csrf": csrf, "g": game_id, "type": "place_leader",
                      "t": legal["leaderPlaceable"][0]})
            else:
                break
        if state.get("phase") in ("reinforce", "attack", "fortify"):
            current_name = state["players"][int(state["current"])]["user"]
            other_name = "bob" if current_name == "admin" else "admin"
            other_session, other_csrf = sessions[other_name]
            status, body = post(other_session, base, "/api/game/act",
                                {"csrf": other_csrf, "g": game_id,
                                 "type": "end_turn"})
            if body.get("ok") == 0 and body.get("err") == "action":
                print("PASS: /api/game/act not-your-turn")
            else:
                print("FAIL: /api/game/act not-your-turn "
                      f"(status {status}, got {json.dumps(body)[:240]})",
                      file=sys.stderr)
                FAILURES += 1
        status, body = post(admin, base, "/api/admin/neutralise",
                            {"csrf": admin_csrf, "g": game_id, "seat": 0})
        if body.get("ok") == 1:
            print("PASS: /api/admin/neutralise running seat")
        else:
            print("FAIL: /api/admin/neutralise running seat "
                  f"(status {status}, got {json.dumps(body)[:240]})",
                  file=sys.stderr)
            FAILURES += 1

        # Logout requires a session and then stops the session working.
        expect("/api/logout user missing csrf",
               post(bob, base, "/api/logout", {"dummy": "1"}), "csrf")
        expect_ok("/api/logout user",
                  post(bob, base, "/api/logout",
                       {"csrf": bob_csrf}))
        expect("/api/lobby after logout",
               get(bob, base, "/api/lobby"), "login")

        if FAILURES:
            print(f"auth_audit: {FAILURES} failure(s)", file=sys.stderr)
            return 1
        print("All authorization audit tests passed.")
        return 0
    finally:
        if server is not None:
            server.terminate()
            try:
                server.wait(timeout=10)
            except Exception:
                server.kill()
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
