import asyncio
import json
import websockets

HOST = "localhost"
PORT = 8765

rooms: dict[str, set] = {}
history: dict[str, list[dict]] = {}
snapshots: dict[str, dict] = {}


def get_room_from_path(path):
    room = path.split('/')[-1]
    if not room:
        return "default"

    return room


async def send_json(client, payload):
    await client.send(json.dumps(payload, ensure_ascii=False))


async def broadcast(room, sender, payload):
    if not sender:
        return
    clients = rooms.get(room, set())

    if not clients:
        return

    disconnected_clients = []

    for client in clients:
        if client == sender:
            continue

        try:
            await send_json(client, payload)
        except websockets.ConnectionClosed:
            disconnected_clients.append(client)

    for dc in disconnected_clients:
        clients.discard(dc)


def is_operation(payload):
    operation = payload.get("type")

    if operation == "insert":
        node = payload.get("node")
        return isinstance(node, dict)
    elif operation == "delete":
        id = payload.get("id")
        return isinstance(id, dict)

    return False


def is_snapshot(payload):
    return payload.get("type") == "snapshot"


async def handle_client(websocket):
    room = get_room_from_path(websocket.request.path)
    rooms.setdefault(room, set()).add(websocket)
    history.setdefault(room, [])
    snapshots.setdefault(room, {})

    print(f"[JOIN] room={room}, clients={len(rooms[room])}")
    if snapshots[room]:
        await send_json(websocket, snapshots[room])
    for operation in history[room]:
        await send_json(websocket, operation)
    try:
        async for message in websocket:
            try:
                payload = json.loads(message)
            except json.JSONDecodeError:
                await send_json(websocket, {
                    "type": "error",
                    "message": "Invalid JSON"
                })
                continue
            if not is_operation(payload):
                if is_snapshot(payload):
                    if not snapshots[room]:
                        snapshots[room] = payload
                        await broadcast(room, websocket, payload)
                        print(f"[SNAPSHOT] room={room} set")
                    continue
                await send_json(websocket, {
                    "type": "error",
                    "message": "Invalid RGA operation"
                })
                continue

            history[room].append(payload)

            await broadcast(room, websocket, payload)

            print(f"[OP] room={room}, type={payload.get('type')}")

    except websockets.ConnectionClosed:
        pass

    finally:
        rooms[room].discard(websocket)

        if not rooms[room]:
            del rooms[room]

        print(f"[LEAVE] room={room}")


async def main() -> None:
    print(f"[SERVER] CRDT WebSocket server running on ws://{HOST}:{PORT}")

    async with websockets.serve(handle_client, HOST, PORT):
        await asyncio.Future()


if __name__ == "__main__":
        asyncio.run(main())