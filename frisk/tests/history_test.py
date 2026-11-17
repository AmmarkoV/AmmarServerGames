#!/usr/bin/env python3
"""Finished-game history, unloading and faction switching (PLAN.md 7.6, 8.4).

Starts an isolated server on the generated tiny pack, checks that a seated
player can switch to a free faction (and not to a taken one), plays a
two-player game until one side resigns, then verifies the profile history.
The server is restarted with FRISK_FINISHED_TTL_SECONDS=0 to check that the
finished game is unloaded from RAM but kept on disk and in the history.
"""

import os
import shutil
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from long_game import (act, choose_action, form, get, make_invite,  # noqa: E402
                       register, start_server)

PORT = int(os.environ.get("FRISK_HISTORY_PORT", "18140"))


def check(condition, message):
    if not condition:
        raise RuntimeError(message)
    print(f"PASS: {message}")


def play_until_reinforce(base, sessions, game_id):
    for _ in range(2000):
        any_state = get(sessions["admin"][0], base + "/api/game/state",
                        {"g": game_id})
        if any_state.get("phase") == "reinforce":
            return any_state
        current = any_state["players"][any_state["current"]]["user"]
        session, csrf = sessions[current]
        own = get(session, base + "/api/game/state", {"g": game_id})
        chosen = choose_action(own)
        if chosen is None:
            time.sleep(.05)
            continue
        kind, fields = chosen
        act(base, session, csrf, game_id, kind, **fields)
    raise RuntimeError("setup did not reach the reinforce phase")


def main():
    tmp = tempfile.mkdtemp(prefix="frisk-history-")
    server = None
    try:
        server, base = start_server(PORT, tmp)
        admin, admin_csrf = register(base, "admin")
        bob, bob_csrf = register(base, "bob", make_invite(base, admin,
                                                          admin_csrf))
        created = form(admin, base + "/api/game/create", {
            "csrf": admin_csrf, "name": 'History "quoted" game',
            "map": "tiny", "seats": 2, "mode": "deal", "batch": 50})
        game_id = created["id"]
        joined = form(bob, base + "/api/game/join",
                      {"csrf": bob_csrf, "g": game_id})
        check(joined.get("ok") == 1, "bob joins")

        state = get(bob, base + "/api/game/state", {"g": game_id})
        mine = state["players"][state["you"]]["faction"]
        taken = state["players"][0]["faction"]
        free = next(f for f in range(4) if f not in (mine, taken))
        result = form(bob, base + "/api/game/join",
                      {"csrf": bob_csrf, "g": game_id, "faction": taken})
        check(result.get("err") == "faction",
              "switching to a taken faction is refused")
        result = form(bob, base + "/api/game/join",
                      {"csrf": bob_csrf, "g": game_id, "faction": free})
        state = get(bob, base + "/api/game/state", {"g": game_id})
        check(result.get("ok") == 1 and
              state["players"][state["you"]]["faction"] == free,
              "a seated player switches to a free faction")
        check(state["playerCount"] == 2, "switching keeps the seat count")

        started = form(admin, base + "/api/game/start",
                       {"csrf": admin_csrf, "g": game_id})
        check(started.get("ok") == 1, "game starts")
        sessions = {"admin": (admin, admin_csrf), "bob": (bob, bob_csrf)}
        play_until_reinforce(base, sessions, game_id)
        act(base, bob, bob_csrf, game_id, "resign")
        state = get(admin, base + "/api/game/state", {"g": game_id})
        check(state.get("status") == "finished", "resign finishes the game")

        profile = get(admin, base + "/api/profile")
        history = profile.get("history") or []
        check(len(history) == 1 and history[0]["id"] == game_id and
              history[0]["winner"] == "admin" and
              history[0]["name"] == 'History "quoted" game',
              "winner profile lists the finished game")
        players = {p["user"]: p for p in history[0]["players"]}
        check(players["admin"]["won"] is True and
              players["bob"]["won"] is False,
              "history records each player's result")
        bob_history = get(bob, base + "/api/profile").get("history") or []
        check(len(bob_history) == 1, "loser profile lists the finished game")
        other = get(bob, base + "/api/profile", {"u": "admin"})
        check(len(other.get("history") or []) == 1,
              "history is visible on another player's profile")

        lobby = get(admin, base + "/api/lobby")
        check(any(g["id"] == game_id for g in lobby["yourGames"]),
              "a fresh finished game stays loaded")

        server.terminate()
        server.wait(timeout=10)
        os.environ["FRISK_FINISHED_TTL_SECONDS"] = "0"
        server, base = start_server(PORT, tmp)
        admin, admin_csrf = register_login(base)
        lobby = get(admin, base + "/api/lobby")
        check(not any(g["id"] == game_id for g in lobby["yourGames"]),
              "a stale finished game is not loaded after restart")
        check(os.path.exists(os.path.join(tmp, "data", "games",
                                          f"game_{game_id}.json")),
              "the unloaded game file stays on disk")
        history = get(admin, base + "/api/profile").get("history") or []
        check(len(history) == 1, "history survives unloading without "
              "duplicate backfill")
        created = form(admin, base + "/api/game/create", {
            "csrf": admin_csrf, "name": "After", "map": "tiny", "seats": 2})
        check(created.get("id", 0) > game_id,
              "game ids continue after an unloaded game")
        print("All history tests passed.")
        return 0
    finally:
        if server is not None:
            server.terminate()
            try:
                server.wait(timeout=10)
            except Exception:
                server.kill()
        shutil.rmtree(tmp, ignore_errors=True)


def register_login(base):
    import requests
    session = requests.Session()
    result = form(session, base + "/api/login",
                  {"name": "admin", "password": "password123"})
    if result.get("ok") != 1:
        raise RuntimeError(f"login: {result}")
    return session, get(session, base + "/api/me").get("csrf", "")


if __name__ == "__main__":
    sys.exit(main())
