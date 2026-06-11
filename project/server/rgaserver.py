import asyncio
import datetime
import json
import os
import time
import urllib.error
import urllib.request

import websockets

HOST = "localhost"
PORT = 8765

_env: dict[str, str] = {}
_env_path = os.path.join(os.path.dirname(__file__), ".env")
if os.path.exists(_env_path):
    with open(_env_path, encoding="utf-8") as _f:
        for _line in _f:
            _line = _line.strip()
            if _line and not _line.startswith("#") and "=" in _line:
                _k, _, _v = _line.partition("=")
                _env[_k.strip()] = _v.strip().strip('"').strip("'")

GEMINI_API_KEY: str = _env.get("GEMINI_API_KEY", "")
AI_ENABLED: bool = _env.get("AI_ENABLED", "true").lower() == "true"

rooms: dict[str, set] = {}
room_users: dict[str, dict] = {}
room_host: dict[str, int | None] = {}
room_mode: dict[str, str] = {}
project_files: dict[str, list] = {}
file_snapshots: dict[str, dict] = {}
file_history: dict[str, dict] = {}
cursor_state: dict[str, dict] = {}
user_file_state: dict[str, dict] = {}
room_start_time: dict[str, float] = {}
room_user_joins: dict[str, dict] = {}


def get_room(path: str) -> str:
    r = path.split("/")[-1]
    return r if r else "default"


def fmt_time(ts: float) -> str:
    return datetime.datetime.fromtimestamp(ts).strftime("%Y-%m-%dT%H:%M:%S")


async def send_json(ws, payload: dict):
    try:
        await ws.send(json.dumps(payload, ensure_ascii=False))
    except Exception:
        pass


async def broadcast(room: str, sender, payload: dict):
    for ws in list(rooms.get(room, set())):
        if ws != sender:
            await send_json(ws, payload)


async def broadcast_all(room: str, payload: dict):
    for ws in list(rooms.get(room, set())):
        await send_json(ws, payload)


async def broadcast_user_list(room: str):
    ids = [v for v in room_users.get(room, {}).values() if v is not None]
    msg = {"type": "user_list", "siteIds": ids}
    for ws in list(rooms.get(room, set())):
        await send_json(ws, msg)


async def send_room_state(ws, room: str):
    if project_files.get(room):
        await send_json(ws, {
            "type": "project_init",
            "host": room_host.get(room),
            "files": project_files[room],
            "mode": room_mode.get(room, "readwrite"),
        })

    for snap in file_snapshots.get(room, {}).values():
        await send_json(ws, snap)

    for ops in file_history.get(room, {}).values():
        for op in ops:
            clean = {k: v for k, v in op.items() if not k.startswith("_")}
            await send_json(ws, clean)

    for other_ws, file_cursors in cursor_state.get(room, {}).items():
        if other_ws is not ws:
            for payload in file_cursors.values():
                await send_json(ws, payload)

    for other_ws, fp in user_file_state.get(room, {}).items():
        if other_ws is not ws and fp is not None:
            await send_json(ws, fp)


def compute_op_stats(room: str) -> tuple[dict[int, int], dict[int, int], dict[int, set]]:
    ins: dict[int, int] = {}
    dels: dict[int, int] = {}
    files: dict[int, set] = {}

    for fname, ops in file_history.get(room, {}).items():
        for op in ops:
            if op["type"] == "insert":
                sid = op["node"]["id"]["siteId"]
                ins[sid] = ins.get(sid, 0) + 1
                files.setdefault(sid, set()).add(fname)
            elif op["type"] == "delete":
                actor = op.get("_actor")
                if actor is not None:
                    dels[actor] = dels.get(actor, 0) + 1

    return ins, dels, files


def build_report(room: str) -> dict:
    now = time.time()
    start = room_start_time.get(room, now)
    duration = max(0, int(now - start))

    ins, dels, user_files = compute_op_stats(room)

    file_editors: dict[str, set[int]] = {}
    for fname, ops in file_history.get(room, {}).items():
        for op in ops:
            if op["type"] == "insert":
                sid = op["node"]["id"]["siteId"]
                file_editors.setdefault(fname, set()).add(sid)

    host_sid = room_host.get(room)
    join_times = room_user_joins.get(room, {})

    all_sids: set[int] = set(join_times.keys())
    for sid in list(ins.keys()) + list(dels.keys()):
        all_sids.add(sid)
    for ws, sid in room_users.get(room, {}).items():
        if sid is not None:
            all_sids.add(sid)

    participants = []
    for sid in sorted(all_sids):
        join_ts = join_times.get(sid, start)
        active_s = max(0, int(now - join_ts))
        files_touched = sorted(user_files.get(sid, set()))
        participants.append({
            "site_id": sid,
            "is_host": (sid == host_sid),
            "active_sec": active_s,
            "total_inserts": ins.get(sid, 0),
            "total_deletes": dels.get(sid, 0),
            "files_touched": files_touched,
        })

    files = [
        {"name": fname, "editors": sorted(editors)}
        for fname, editors in sorted(file_editors.items())
    ]

    return {
        "room": room,
        "start_time": fmt_time(start),
        "end_time": fmt_time(now),
        "duration_sec": duration,
        "participants": participants,
        "files": files,
    }


def build_ai_prompt(report: dict) -> str:
    parts_lines = []
    for p in report["participants"]:
        line = (f"  Site {p['site_id']}{'(Host)' if p['is_host'] else ''}: "
                f"{p['total_inserts']} inserts, {p['total_deletes']} deletes, "
                f"active {p['active_sec'] // 60}min")
        if p["files_touched"]:
            line += f", files: {', '.join(p['files_touched'])}"
        parts_lines.append(line)

    files_str = ", ".join(f["name"] for f in report["files"]) or "none"
    duration_min = report["duration_sec"] // 60

    return (
            "Analyze this collaborative coding session.\n\n"
            f"Duration: {duration_min} min\n"
            f"Files: {files_str}\n"
            "Participants:\n" + "\n".join(parts_lines) + "\n\n"
                                                         "Respond ONLY with valid JSON (no markdown) in this structure:\n"
                                                         '{\n'
                                                         '  "summary": "2-3 sentences in Ukrainian summarizing the session",\n'
                                                         '  "participant_work": {\n'
                                                         '    "<site_id_as_string>": "1-2 sentences in Ukrainian about what this person did"\n'
                                                         '  }\n'
                                                         '}'
    )


def call_gemini(prompt: str, body: bytes) -> str | None:
    url = (
        "https://generativelanguage.googleapis.com/v1beta/models/"
        f"gemini-2.0-flash:generateContent?key={GEMINI_API_KEY}"
    )
    req = urllib.request.Request(
        url, data=body,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    with urllib.request.urlopen(req, timeout=20) as resp:
        data = json.loads(resp.read().decode("utf-8"))
    return data["candidates"][0]["content"]["parts"][0]["text"]


async def generate_ai_insights(report: dict) -> dict | None:
    if not AI_ENABLED or not GEMINI_API_KEY:
        return None

    prompt = build_ai_prompt(report)
    body = json.dumps({
        "contents": [{"parts": [{"text": prompt}]}],
        "generationConfig": {"responseMimeType": "application/json", "temperature": 0.3},
    }, ensure_ascii=False).encode("utf-8")

    loop = asyncio.get_event_loop()
    delays = [5, 15, 45]
    for attempt, delay in enumerate(delays + [None], start=1):
        try:
            raw = await asyncio.wait_for(
                loop.run_in_executor(None, call_gemini, prompt, body),
                timeout=25.0,
            )
            return json.loads(raw) if raw else None
        except urllib.error.HTTPError as e:
            if e.code == 429:
                try:
                    reason = json.loads(e.read().decode()).get("error", {}).get("message", "")
                except Exception:
                    reason = ""
                if "quota" in reason.lower() or "day" in reason.lower():
                    print("[AI] Gemini daily quota exceeded — AI unavailable until tomorrow")
                    return None
                if delay is not None:
                    print(f"[AI] Gemini 429 rate limit — retry {attempt}/{len(delays)} in {delay}s")
                    await asyncio.sleep(delay)
                else:
                    print("[AI] Gemini 429 — all retries exhausted")
                    return None
            else:
                print(f"[AI] Gemini error: HTTP {e.code}")
                return None
        except asyncio.TimeoutError:
            print("[AI] Gemini timeout (25s)")
            return None
        except Exception as e:
            print(f"[AI] Gemini error: {e}")
            return None
    return None


async def handle_session_report(room: str):
    report = build_report(room)
    await broadcast_all(room, {"type": "session_report", "data": report})

    async def send_ai():
        insights = await generate_ai_insights(report)
        if insights:
            await broadcast_all(room, {"type": "session_report_ai", "data": insights})
        else:
            print(f"[AI] No insights for room={room}")

    asyncio.create_task(send_ai())


async def handle_client(websocket):
    room = get_room(websocket.request.path)

    rooms.setdefault(room, set()).add(websocket)
    room_users.setdefault(room, {})[websocket] = None
    room_host.setdefault(room, None)
    room_mode.setdefault(room, "readwrite")
    project_files.setdefault(room, [])
    file_snapshots.setdefault(room, {})
    file_history.setdefault(room, {})
    cursor_state.setdefault(room, {})[websocket] = {}
    user_file_state.setdefault(room, {})[websocket] = None
    room_start_time.setdefault(room, time.time())
    room_user_joins.setdefault(room, {})

    print(f"[JOIN] room={room} clients={len(rooms[room])}")
    await send_room_state(websocket, room)

    try:
        async for message in websocket:
            try:
                payload = json.loads(message)
            except Exception:
                continue

            t = payload.get("type")
            file_key = payload.get("file", "")

            if t == "register":
                sid = payload.get("siteId")
                role = payload.get("role", "guest")
                room_users[room][websocket] = sid
                if sid is not None:
                    room_user_joins[room][sid] = time.time()
                if role == "host":
                    room_host[room] = sid
                    files = payload.get("files", [])
                    project_files[room] = files
                    room_mode[room] = payload.get("mode", "readwrite")
                    await broadcast(room, websocket, {
                        "type": "project_init", "host": sid,
                        "files": files, "mode": room_mode[room],
                    })
                await broadcast_user_list(room)
                continue

            if t == "kick":
                sender_sid = room_users[room].get(websocket)
                if sender_sid is not None and sender_sid == room_host.get(room):
                    target_sid = payload.get("siteId")
                    for ws, sid in list(room_users[room].items()):
                        if sid == target_sid and ws is not websocket:
                            await send_json(ws, {"type": "kicked"})
                            await ws.close(1000, "kicked")
                            break
                continue

            if t == "cursor":
                cursor_state[room][websocket][file_key] = payload
                await broadcast(room, websocket, payload)
                continue

            if t == "snapshot":
                fk = file_key or ""
                if fk not in file_snapshots[room]:
                    file_snapshots[room][fk] = payload
                    await broadcast(room, websocket, payload)
                continue

            if t in ("insert", "delete", "undelete"):
                stored = dict(payload)
                if t == "delete":
                    stored["_actor"] = room_users[room].get(websocket)
                file_history[room].setdefault(file_key, []).append(stored)
                await broadcast(room, websocket, payload)
                continue

            if t == "cursor_leave":
                if file_key and websocket in cursor_state.get(room, {}):
                    cursor_state[room][websocket].pop(file_key, None)
                await broadcast(room, websocket, payload)
                continue

            if t == "file_focus":
                user_file_state[room][websocket] = payload
                await broadcast(room, websocket, payload)
                continue

            if t == "run_output":
                await broadcast(room, websocket, payload)
                continue

            if t == "file_create":
                fp = payload.get("file", "")
                if fp and fp not in project_files[room]:
                    project_files[room].append(fp)
                    file_snapshots[room][fp] = {
                        "type": "snapshot", "file": fp,
                        "text": payload.get("text", ""), "sequence": [],
                    }
                await broadcast(room, websocket, payload)
                continue

            if t == "file_rename":
                old, new = payload.get("old", ""), payload.get("new", "")
                if old in project_files[room]:
                    idx = project_files[room].index(old)
                    project_files[room][idx] = new
                    if old in file_snapshots[room]:
                        snap = dict(file_snapshots[room].pop(old))
                        snap["file"] = new
                        file_snapshots[room][new] = snap
                    if old in file_history[room]:
                        file_history[room][new] = file_history[room].pop(old)
                await broadcast(room, websocket, payload)
                continue

            if t == "file_delete":
                fp = payload.get("file", "")
                if fp in project_files[room]:
                    project_files[room].remove(fp)
                file_snapshots[room].pop(fp, None)
                file_history[room].pop(fp, None)
                await broadcast(room, websocket, payload)
                continue

            if t in ("session_report_request", "end_session"):
                await handle_session_report(room)
                continue

    except websockets.ConnectionClosed:
        pass
    finally:
        sid = room_users.get(room, {}).get(websocket)
        rooms[room].discard(websocket)
        room_users[room].pop(websocket, None)
        cursor_state[room].pop(websocket, None)
        user_file_state[room].pop(websocket, None)

        if not rooms[room]:
            for d in (room_users, room_host, room_mode, project_files,
                      file_snapshots, file_history, cursor_state,
                      user_file_state, room_start_time, room_user_joins):
                d.pop(room, None)
            del rooms[room]
        else:
            await broadcast_user_list(room)
            if sid is not None:
                await broadcast(room, None, {"type": "cursor_leave", "siteId": sid})

        print(f"[LEAVE] room={room}")


async def main():
    print(f"[SERVER] ws://{HOST}:{PORT}")
    if GEMINI_API_KEY:
        print(f"[AI] Gemini enabled (key: {GEMINI_API_KEY[:8]}...)")
    else:
        print("[AI] Gemini disabled — set GEMINI_API_KEY in server/.env")
    async with websockets.serve(handle_client, HOST, PORT):
        await asyncio.Future()


asyncio.run(main())
