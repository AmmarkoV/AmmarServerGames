#!/usr/bin/env python3
"""Concurrent polling/action load sanity check for FRISK (PLAN.md 10.3).

Starts an isolated server on a generated four-faction map, registers four
players, and runs the requested number of polling clients plus four acting
clients for the requested number of seconds.  It samples the server's file
descriptor count and RSS and fails if descriptors or memory grow without
bound, or if polling starts failing.
"""

import argparse
import json
import os
import random
import shutil
import statistics
import sys
import tempfile
import threading
import time

import requests

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import long_game as lg  # noqa: E402


def http_get(session, base, path, params=None):
    return session.get(base + path, params=params or {}, timeout=20,
                       headers={"Connection": "close"})


def http_post(session, base, path, fields):
    body = {key: (None, str(value)) for key, value in fields.items()
            if value is not None and value != ""}
    return session.post(base + path, files=body, timeout=20,
                        headers={"Connection": "close"})


def clone_session(source):
    session = requests.Session()
    session.cookies.update(source.cookies)
    return session


def choose_load_action(state):
    legal = state.get("legal") or {}
    phase = state.get("phase")
    flags = state.get("flags") or {}
    if flags.get("mustDiscardAdventure") and len(state.get("adventureHand") or []) > 4:
        return ("discard_adventure", {"card": 0})
    if flags.get("mustTrade") or (phase == "reinforce" and legal.get("canTrade")):
        cards = lg.find_set(state.get("hand") or [])
        if cards:
            return ("trade_cards",
                    {"cardA": cards[0], "cardB": cards[1], "cardC": cards[2]})
    if phase == "setup_claim":
        choices = legal.get("claimable") or []
        return ("claim", {"t": choices[0]}) if choices else None
    if phase == "setup_place":
        choices = legal.get("placeable") or []
        remaining = int(legal.get("batchRemaining") or 0)
        if choices and remaining > 0:
            return ("place", {"t": choices[0], "count": min(remaining, 5)})
        return None
    if phase == "setup_leaders":
        choices = legal.get("leaderPlaceable") or []
        if choices and int(legal.get("leadersRemaining") or 0) > 0:
            return ("place_leader", {"t": choices[0]})
        return None
    if phase == "reinforce":
        choices = legal.get("placeable") or []
        pending = int(legal.get("reinforcementsPending") or 0)
        if choices and pending > 0:
            return ("place", {"t": choices[0], "count": min(pending, 10)})
        if legal.get("canDoneReinforce"):
            return ("done_reinforce", {})
        return None
    if phase == "attack":
        if legal.get("canDoneAttack"):
            return ("done_attack", {})
        return None
    if phase == "conquest_move":
        pending = state.get("pending") or {}
        count = int(legal.get("moveInMax") or legal.get("moveInMin") or 1)
        return ("move_in", {"from": pending.get("from"),
                            "to": pending.get("to"), "count": count,
                            "leader": 0})
    if phase == "fortify":
        return ("end_turn", {})
    return None


def server_metrics(pid):
    try:
        fds = len(os.listdir(f"/proc/{pid}/fd"))
    except OSError:
        fds = -1
    rss_kb = 0
    try:
        with open(f"/proc/{pid}/status", encoding="utf-8") as handle:
            for line in handle:
                if line.startswith("VmRSS:"):
                    rss_kb = int(line.split()[1])
                    break
    except OSError:
        pass
    return fds, rss_kb


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--seconds", type=int, default=600)
    parser.add_argument("--pollers", type=int, default=20)
    parser.add_argument("--actors", type=int, default=4)
    parser.add_argument("--port", type=int, default=18102)
    args = parser.parse_args()

    if not (1 <= args.actors <= 4):
        raise SystemExit("--actors must be 1..4")
    if args.pollers < 1:
        raise SystemExit("--pollers must be positive")

    tmp = tempfile.mkdtemp(prefix="frisk-load-")
    server = None
    stop = threading.Event()
    counters = {"polls": 0, "poll_failures": 0, "actions": 0,
                "action_failures": 0}
    counters_lock = threading.Lock()
    action_lock = threading.Lock()
    metrics = []
    failure_samples = []

    try:
        server, base = lg.start_server(args.port, tmp)
        admin, admin_csrf = lg.register(base, "admin")
        players = []
        for index in range(1, args.actors):
            name = f"player{index}"
            invite = lg.make_invite(base, admin, admin_csrf)
            players.append(lg.register(base, name, invite))
        sessions = [("admin", admin, admin_csrf)] + [
            (f"player{i + 1}", session, csrf)
            for i, (session, csrf) in enumerate(players)]
        created = http_post(admin, base, "/api/game/create", {
            "csrf": admin_csrf, "name": "Load Game", "map": "tiny",
            "seats": args.actors, "mode": "deal", "batch": 5})
        created = created.json()
        if created.get("ok") != 1:
            raise RuntimeError(f"create: {created}")
        game_id = created["id"]
        for name, session, csrf in sessions[1:]:
            result = http_post(session, base, "/api/game/join",
                               {"csrf": csrf, "g": game_id}).json()
            if result.get("ok") != 1:
                raise RuntimeError(f"join {name}: {result}")
        result = http_post(admin, base, "/api/game/start",
                           {"csrf": admin_csrf, "g": game_id}).json()
        if result.get("ok") != 1:
            raise RuntimeError(f"start: {result}")

        deadline = time.time() + args.seconds

        def poller_loop(index):
            session = clone_session(sessions[index % len(sessions)][1])
            since = 0
            after = 0
            while not stop.is_set() and time.time() < deadline:
                try:
                    choice = random.randrange(3)
                    if choice == 0:
                        response = http_get(
                            session, base, "/api/game/state",
                            {"g": game_id, "since": since})
                    elif choice == 1:
                        response = http_get(session, base, "/api/lobby")
                    else:
                        response = http_get(
                            session, base, "/api/chat",
                            {"room": "lobby", "after": after})
                    data = response.json()
                    ok = response.status_code == 200 and (
                        data.get("ok") == 1 or "format" in data or
                        "same" in data)
                    if data.get("version") is not None:
                        since = max(since, int(data["version"]))
                    if data.get("last") is not None:
                        after = max(after, int(data["last"]))
                    if not ok:
                        with counters_lock:
                            counters["polls"] += 1
                            counters["poll_failures"] += 1
                            if len(failure_samples) < 12:
                                failure_samples.append(
                                    (response.status_code, response.text[:300]))
                    else:
                        with counters_lock:
                            counters["polls"] += 1
                except Exception as error:
                    with counters_lock:
                        counters["polls"] += 1
                        counters["poll_failures"] += 1
                        if len(failure_samples) < 12:
                            failure_samples.append(("exception", repr(error)))
                time.sleep(random.uniform(0.4, 1.4))

        def actor_loop(name, base_session):
            session = clone_session(base_session)
            while not stop.is_set() and time.time() < deadline:
                try:
                    with action_lock:
                        state = http_get(session, base, "/api/game/state",
                                         {"g": game_id}).json()
                        if state.get("status") == "finished":
                            time.sleep(0.5)
                            continue
                        current = state.get("current")
                        if current is None or int(current) < 0:
                            time.sleep(0.3)
                            continue
                        current_name = state.get("players", [])[
                            int(current)]["user"]
                        if current_name != name:
                            time.sleep(0.25)
                            continue
                        chosen = choose_load_action(state)
                        if chosen is None:
                            time.sleep(0.25)
                            continue
                        csrf = next(csrf for user, _, csrf in sessions
                                    if user == name)
                        kind, fields = chosen
                        body = {"csrf": csrf, "g": game_id, "type": kind}
                        body.update(fields)
                        result = http_post(session, base, "/api/game/act",
                                           body).json()
                        with counters_lock:
                            counters["actions"] += 1
                            if result.get("ok") != 1:
                                counters["action_failures"] += 1
                except Exception:
                    with counters_lock:
                        counters["action_failures"] += 1
                time.sleep(random.uniform(0.15, 0.45))

        threads = []
        for index in range(args.pollers):
            thread = threading.Thread(target=poller_loop, args=(index,),
                                      daemon=True)
            thread.start()
            threads.append(thread)
        for name, session, _ in sessions:
            thread = threading.Thread(target=actor_loop,
                                      args=(name, session), daemon=True)
            thread.start()
            threads.append(thread)

        monitor_start = time.time()
        baseline = None
        while time.time() < deadline and not stop.is_set():
            fds, rss = server_metrics(server.pid)
            if fds >= 0:
                metrics.append((round(time.time() - monitor_start, 1),
                                fds, rss))
                if baseline is None and time.time() - monitor_start >= 20:
                    baseline = (fds, rss)
            time.sleep(5)

        stop.set()
        for thread in threads:
            thread.join(timeout=10)
        fds, rss = server_metrics(server.pid)
        if baseline is None:
            baseline = (fds, rss)
        if fds >= 0:
            metrics.append((round(time.time() - monitor_start, 1), fds, rss))

        max_fds = max(item[1] for item in metrics) if metrics else -1
        max_rss = max(item[2] for item in metrics) if metrics else 0
        summary = {
            "ok": 1,
            "seconds": round(time.time() - monitor_start, 1),
            "pollers": args.pollers,
            "actors": args.actors,
            "polls": counters["polls"],
            "poll_failures": counters["poll_failures"],
            "actions": counters["actions"],
            "action_failures": counters["action_failures"],
            "baseline_fds": baseline[0],
            "final_fds": fds,
            "max_fds": max_fds,
            "baseline_rss_kb": baseline[1],
            "final_rss_kb": rss,
            "max_rss_kb": max_rss,
            "samples": metrics,
            "failure_samples": failure_samples,
        }
        print(json.dumps(summary))
        if counters["poll_failures"] > max(5, counters["polls"] // 100):
            raise RuntimeError("too many polling failures")
        if counters["action_failures"] > max(20, counters["actions"] // 20):
            raise RuntimeError("too many action failures")
        if baseline[0] >= 0 and fds > baseline[0] + 16:
            raise RuntimeError(f"file descriptors grew {baseline[0]} -> {fds}")
        if baseline[0] >= 0 and max_fds > baseline[0] + 32:
            raise RuntimeError(f"file descriptors peaked at {max_fds}")
        if baseline[1] and rss > baseline[1] + 128 * 1024:
            raise RuntimeError(f"RSS grew {baseline[1]} -> {rss} KB")
        if max_rss > baseline[1] + 192 * 1024 and baseline[1]:
            raise RuntimeError(f"RSS peaked at {max_rss} KB")
    finally:
        stop.set()
        if server is not None:
            server.terminate()
            try:
                server.wait(timeout=10)
            except Exception:
                server.kill()
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    main()
