import asyncio
import json
import websockets

HOST = "localhost"
PORT = 8765

rooms:           dict[str, set]   = {}
history:         dict[str, list]  = {}
snapshots:       dict[str, dict]  = {}
room_users:      dict[str, dict]  = {}
cursor_positions: dict[str, dict] = {}


def get_room(path):
    r = path.split('/')[-1]
    return r if r else "default"


async def send_json(ws, payload):
    try:
        await ws.send(json.dumps(payload, ensure_ascii=False))
    except Exception:
        pass


async def broadcast(room, sender, payload):
    for ws in list(rooms.get(room, set())):
        if ws != sender:
            await send_json(ws, payload)


async def broadcast_user_list(room):
    ids = list(room_users.get(room, {}).values())
    payload = {"type": "user_list", "siteIds": ids}
    for ws in list(rooms.get(room, set())):
        await send_json(ws, payload)


async def handle_client(websocket):
    room = get_room(websocket.request.path)
    rooms.setdefault(room, set()).add(websocket)
    history.setdefault(room, [])
    snapshots.setdefault(room, {})
    room_users.setdefault(room, {})
    cursor_positions.setdefault(room, {})
    room_users[room][websocket] = None

    print(f"[JOIN] room={room} clients={len(rooms[room])}")

    if snapshots[room]:
        await send_json(websocket, snapshots[room])
    for op in history[room]:
        await send_json(websocket, op)

    for ws, payload in cursor_positions[room].items():
        if ws != websocket:
            await send_json(websocket, payload)

    try:
        async for message in websocket:
            try:
                payload = json.loads(message)
            except Exception:
                continue

            t = payload.get("type")

            if t == "register":
                sid = payload.get("siteId")
                room_users[room][websocket] = sid
                await broadcast_user_list(room)
                continue

            if t == "cursor":
                cursor_positions[room][websocket] = payload
                await broadcast(room, websocket, payload)
                continue

            if t == "snapshot":
                if not snapshots[room]:
                    snapshots[room] = payload
                    await broadcast(room, websocket, payload)
                continue

            if t in ("insert", "delete"):
                history[room].append(payload)
                await broadcast(room, websocket, payload)

    except websockets.ConnectionClosed:
        pass
    finally:
        sid = room_users.get(room, {}).get(websocket)
        rooms[room].discard(websocket)
        room_users[room].pop(websocket, None)
        cursor_positions[room].pop(websocket, None)
        if not rooms[room]:
            del rooms[room]
            history.pop(room, None)
            snapshots.pop(room, None)
            room_users.pop(room, None)
            cursor_positions.pop(room, None)
        else:
            await broadcast_user_list(room)
            if sid is not None:
                await broadcast(room, None,
                                {"type": "cursor_leave", "siteId": sid})
        print(f"[LEAVE] room={room}")


async def main():
    print(f"[SERVER] ws://{HOST}:{PORT}")
    async with websockets.serve(handle_client, HOST, PORT):
        await asyncio.Future()

asyncio.run(main())