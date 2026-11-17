#!/usr/bin/env python3
"""Drive a long, live game through the FRISK HTTP API.

The bot plays a two-player game on a generated eight-territory pack.  It
reinforces, trades sets, takes only safe attacks (never reducing an opponent
below two territories, so the game does not end), and ends turns.  After the
requested number of turns it checks that the serialized game stayed small and
prints the durable-log growth.
"""

import argparse
import json
import random
import os
import shutil
import subprocess
import sys
import tempfile
import time

import requests

HERE = os.path.dirname(os.path.abspath(__file__))
FRISK_DIR = os.path.dirname(HERE)
AMMAR_ROOT = os.path.abspath(os.path.join(FRISK_DIR, "..", "..", ".."))
DEFAULT_SERVER = os.path.join(FRISK_DIR, "frisk")


def start_server(port, tmp):
    pack_root = os.path.join(tmp, "res")
    os.makedirs(os.path.join(pack_root, "uploads", "clips"), exist_ok=True)
    subprocess.check_call(
        [sys.executable, "-I", os.path.join(HERE, "tiny_pack.py"),
         os.path.join(pack_root, "maps", "tiny")])
    env = os.environ.copy()
    env.update({
        "FRISK_RES_ROOT": pack_root + "/",
        "FRISK_DATA_DIR": os.path.join(tmp, "data"),
        "FRISK_USERS_DB": os.path.join(tmp, "users.db"),
        "FRISK_CLIPS_DIR": os.path.join(pack_root, "uploads", "clips"),
        "FRISK_PORT": str(port),
    })
    log = open(os.path.join(tmp, "server.log"), "w")
    server = subprocess.Popen(
        [os.environ.get("FRISK_SERVER_BIN", DEFAULT_SERVER)],
        cwd=AMMAR_ROOT, env=env, stdout=log, stderr=subprocess.STDOUT)
    base = f"http://127.0.0.1:{port}"
    for _ in range(100):
        try:
            if requests.get(base + "/api/me", timeout=.5).json().get("ok") == 1:
                return server, base
        except Exception:
            pass
        time.sleep(.15)
    raise RuntimeError("server did not start")


def form(session, path, fields):
    body = {key: (None, str(value)) for key, value in fields.items()
            if value is not None and value != ""}
    return session.post(path, files=body, timeout=20).json()


def get(session, path, params=None):
    return session.get(path, params=params or {}, timeout=20).json()


def register(base, name, invite=None):
    session = requests.Session()
    fields = {"name": name, "password": "password123"}
    if invite:
        fields["invite"] = invite
    result = form(session, base + "/api/register", fields)
    if result.get("ok") != 1:
        raise RuntimeError(f"register {name}: {result}")
    result = get(session, base + "/api/me")
    return session, result.get("csrf", "")


def make_invite(base, session, csrf):
    result = form(session, base + "/api/invite/new", {"csrf": csrf})
    if result.get("ok") != 1:
        raise RuntimeError(f"invite: {result}")
    return result["code"]


def find_set(hand):
    wilds = [card for card in hand if card.get("wild")]
    plain = [card for card in hand if not card.get("wild")]
    if len(wilds) >= 3:
        return [card["card"] for card in wilds[:3]]
    if len(wilds) >= 2 and plain:
        return [wilds[0]["card"], wilds[1]["card"], plain[0]["card"]]
    if len(wilds) == 1 and len(plain) >= 2:
        return [wilds[0]["card"], plain[0]["card"], plain[1]["card"]]
    groups = {"A": [], "B": [], "C": []}
    for card in plain:
        if card.get("symbol") in groups:
            groups[card["symbol"]].append(card)
    for symbol in ("A", "B", "C"):
        if len(groups[symbol]) >= 3:
            return [card["card"] for card in groups[symbol][:3]]
    if groups["A"] and groups["B"] and groups["C"]:
        return [groups["A"][0]["card"], groups["B"][0]["card"],
                groups["C"][0]["card"]]
    return None


def act(base, session, csrf, game_id, kind, **fields):
    body = {"csrf": csrf, "g": game_id, "type": kind}
    body.update(fields)
    result = form(session, base + "/api/game/act", body)
    if result.get("ok") != 1:
        raise RuntimeError(f"{kind} {fields}: {result}")
    return result


def choose_action(state, rng=None):
    pick = (lambda values: rng.choice(values)) if rng else (lambda values: values[0])
    legal = state.get("legal") or {}
    phase = state.get("phase")
    flags = state.get("flags") or {}
    if flags.get("mustDiscardAdventure") and len(state.get("adventureHand") or []) > 4:
        return ("discard_adventure", {"card": 0})
    if flags.get("mustTrade") or (phase == "reinforce" and legal.get("canTrade")):
        cards = find_set(state.get("hand") or [])
        if cards:
            if rng:
                rng.shuffle(cards)
            return ("trade_cards",
                    {"cardA": cards[0], "cardB": cards[1], "cardC": cards[2]})
    if phase == "setup_claim":
        choices = legal.get("claimable") or []
        if choices:
            return ("claim", {"t": pick(choices)})
        return None
    if phase == "setup_place":
        choices = legal.get("placeable") or []
        remaining = int(legal.get("batchRemaining") or 0)
        if choices and remaining > 0:
            return ("place", {"t": pick(choices), "count": min(remaining, 5)})
        return None
    if phase == "setup_leaders":
        choices = legal.get("leaderPlaceable") or []
        if choices and int(legal.get("leadersRemaining") or 0) > 0:
            return ("place_leader", {"t": pick(choices)})
        return None
    if phase == "reinforce":
        choices = legal.get("placeable") or []
        pending = int(legal.get("reinforcementsPending") or 0)
        if choices and pending > 0:
            return ("place", {"t": pick(choices), "count": min(pending, 10)})
        if legal.get("canDoneReinforce"):
            return ("done_reinforce", {})
        return None
    if phase == "attack":
        territories = state.get("territories") or []
        counts = {}
        for territory in territories:
            owner = territory.get("owner", -1)
            if owner >= 0 and not territory.get("neutral"):
                counts[owner] = counts.get(owner, 0) + 1
        candidates = []
        for attack in legal.get("attacks") or []:
            source = territories[attack["from"]]
            target = territories[attack["to"]]
            owner = target.get("owner", -1)
            if owner >= 0 and counts.get(owner, 0) <= 2:
                continue
            if source.get("armies", 0) < target.get("armies", 0) + 2:
                continue
            candidates.append(attack)
        if candidates:
            attack = pick(candidates)
            dice = max(1, min(3, int(attack.get("maxDice") or 1)))
            if rng:
                dice = rng.randint(1, dice)
            return ("attack", {"from": attack["from"], "to": attack["to"],
                               "dice": dice})
        if legal.get("canDoneAttack"):
            return ("done_attack", {})
        return None
    if phase == "conquest_move":
        minimum = int(legal.get("moveInMin") or 1)
        maximum = int(legal.get("moveInMax") or minimum)
        count = rng.randint(minimum, maximum) if rng else maximum
        return ("move_in", {"from": state.get("pending", {}).get("from"),
                            "to": state.get("pending", {}).get("to"),
                            "count": count, "leader": 0})
    if phase == "fortify":
        return ("end_turn", {})
    return None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--turns", type=int, default=2000)
    parser.add_argument("--port", type=int, default=18100)
    parser.add_argument("--timeout", type=int, default=1800)
    parser.add_argument("--seed", type=int, default=1337)
    args = parser.parse_args()
    rng = random.Random(args.seed)

    tmp = tempfile.mkdtemp(prefix="frisk-long-")
    server = None
    started = time.time()
    try:
        server, base = start_server(args.port, tmp)
        admin, admin_csrf = register(base, "admin")
        invite = make_invite(base, admin, admin_csrf)
        bob, bob_csrf = register(base, "bob", invite)
        created = form(admin, base + "/api/game/create", {
            "csrf": admin_csrf, "name": "Long Game", "map": "tiny",
            "seats": 2, "mode": "deal", "batch": 5})
        if created.get("ok") != 1:
            raise RuntimeError(f"game create: {created}")
        game_id = created["id"]
        joined = form(bob, base + "/api/game/join",
                      {"csrf": bob_csrf, "g": game_id})
        if joined.get("ok") != 1:
            raise RuntimeError(f"game join: {joined}")
        started_game = form(admin, base + "/api/game/start",
                            {"csrf": admin_csrf, "g": game_id})
        if started_game.get("ok") != 1:
            raise RuntimeError(f"game start: {started_game}")

        sessions = {"admin": (admin, admin_csrf),
                    "bob": (bob, bob_csrf)}
        last_turn = 0
        actions = 0
        errors = 0
        while time.time() - started < args.timeout:
            state = get(admin, base + "/api/game/state", {"g": game_id})
            if state.get("status") == "finished":
                raise RuntimeError(f"game ended at turn {state.get('turn')}")
            if int(state.get("turn") or 0) > args.turns:
                last_turn = int(state["turn"])
                break
            current = state.get("players", [])[state.get("current", -1)]["user"]
            session, csrf = sessions[current]
            own = get(session, base + "/api/game/state", {"g": game_id})
            if not own.get("legal"):
                time.sleep(.05)
                continue
            chosen = choose_action(own, rng)
            if chosen is None:
                errors += 1
                if errors < 10:
                    print("no action in", own.get("phase"), own.get("legal"),
                          file=sys.stderr)
                if errors >= 20:
                    raise RuntimeError("bot is stuck")
                # Fall back to advancing the phase where the API allows it.
                legal = own.get("legal") or {}
                fallback = None
                if own.get("phase") == "attack" and legal.get("canDoneAttack"):
                    fallback = ("done_attack", {})
                elif own.get("phase") == "reinforce" and legal.get("canDoneReinforce"):
                    fallback = ("done_reinforce", {})
                elif own.get("phase") == "fortify":
                    fallback = ("end_turn", {})
                if fallback is None:
                    time.sleep(.1)
                    continue
                chosen = fallback
            kind, fields = chosen
            act(base, session, csrf, game_id, kind, **fields)
            actions += 1
            last_turn = max(last_turn, int(own.get("turn") or 0))
            if actions % 100 == 0:
                print(f"turn {last_turn} action {actions}", flush=True)
        if last_turn <= args.turns:
            raise RuntimeError(f"only reached turn {last_turn}/{args.turns} "
                               f"in {time.time() - started:.1f}s")

        final = get(admin, base + "/api/game/state", {"g": game_id})
        game_file = os.path.join(tmp, "data", "games", f"game_{game_id}.json")
        log_file = os.path.join(tmp, "data", "games", f"game_{game_id}.log")
        game_size = os.path.getsize(game_file)
        log_size = os.path.getsize(log_file)
        log_text = open(log_file, "rb").read()
        lines = log_text.count(b"\n")
        print(json.dumps({
            "ok": 1,
            "turns": final.get("turn"),
            "actions": actions,
            "game_json_bytes": game_size,
            "log_bytes": log_size,
            "log_lines": lines,
            "deck": final.get("deck"),
            "discard": final.get("discard"),
            "trade_events": log_text.count(b'"kind":"trade_cards"'),
            "draw_events": log_text.count(b'"kind":"draw_card"'),
            "seconds": round(time.time() - started, 1),
            "seed": args.seed,
        }))
        if game_size > 256 * 1024:
            raise RuntimeError(f"game file grew to {game_size} bytes")
        if log_size > 16 * 1024 * 1024:
            raise RuntimeError(f"durable log grew to {log_size} bytes")
        if final.get("status") != "running":
            raise RuntimeError("game did not stay running")
        if final.get("turn", 0) <= args.turns:
            raise RuntimeError("turn target not reached")
        if log_text.count(b'"kind":"draw_card"') <= 8:
            raise RuntimeError("territory deck was never reshuffled")
    finally:
        if server is not None:
            server.terminate()
            try:
                server.wait(timeout=10)
            except Exception:
                server.kill()
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    main()
