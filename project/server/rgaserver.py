import asyncio
import json

import websockets

HOST = "localhost"
PORT = 8765

rooms: dict[str, set] = {}
room_users: dict[str, dict] = {}
room_host: dict[str, int | None] = {}
project_files: dict[str, list] = {}
file_snapshots: dict[str, dict] = {}
file_history: dict[str, dict] = {}
cursor_state: dict[str, dict] = {}


def get_room(path: str) -> str:
    r = path.split("/")[-1]
    return r if r else "default"


async def send_json(ws, payload: dict):
    try:
        await ws.send(json.dumps(payload, ensure_ascii=False))
    except Exception:
        pass


async def broadcast(room: str, sender, payload: dict):
    for ws in list(rooms.get(room, set())):
        if ws != sender:
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
        })

    for snap in file_snapshots.get(room, {}).values():
        await send_json(ws, snap)

    for ops in file_history.get(room, {}).values():
        for op in ops:
            await send_json(ws, op)

    for other_ws, file_cursors in cursor_state.get(room, {}).items():
        if other_ws is not ws:
            for payload in file_cursors.values():
                await send_json(ws, payload)


async def handle_client(websocket):
    room = get_room(websocket.request.path)

    rooms.setdefault(room, set()).add(websocket)
    room_users.setdefault(room, {})[websocket] = None
    room_host.setdefault(room, None)
    project_files.setdefault(room, [])
    file_snapshots.setdefault(room, {})
    file_history.setdefault(room, {})
    cursor_state.setdefault(room, {})[websocket] = {}

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

                if role == "host":
                    room_host[room] = sid
                    files = payload.get("files", [])
                    project_files[room] = files
                    await broadcast(room, websocket, {
                        "type": "project_init",
                        "host": sid,
                        "files": files,
                    })

                await broadcast_user_list(room)
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

            if t in ("insert", "delete"):
                file_history[room].setdefault(file_key, []).append(payload)
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

    except websockets.ConnectionClosed:
        pass
    finally:
        sid = room_users.get(room, {}).get(websocket)
        rooms[room].discard(websocket)
        room_users[room].pop(websocket, None)
        cursor_state[room].pop(websocket, None)

        if not rooms[room]:
            del rooms[room]
            room_users.pop(room, None)
            room_host.pop(room, None)
            project_files.pop(room, None)
            file_snapshots.pop(room, None)
            file_history.pop(room, None)
            cursor_state.pop(room, None)
        else:
            await broadcast_user_list(room)
            if sid is not None:
                await broadcast(room, None, {"type": "cursor_leave", "siteId": sid})

        print(f"[LEAVE] room={room}")


async def main():
    print(f"[SERVER] ws://{HOST}:{PORT}")
    async with websockets.serve(handle_client, HOST, PORT):
        await asyncio.Future()


asyncio.run(main())
