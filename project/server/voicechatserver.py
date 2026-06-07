import asyncio
import json
import websockets

rooms = {}
clients = {}
room_host: dict[str, int] = {}


async def broadcast_room(room):
    if room not in rooms:
        return

    msg = json.dumps({
        "type": "peer_list",
        "peers": rooms[room]["peers"],
        "host_id": room_host.get(room, -1),
    })

    for ws, info in clients.items():
        if info["room"] == room:
            try:
                await ws.send(msg)
            except:
                pass


def _build_rooms_payload():
    room_info = {}
    for room_name, room_data in rooms.items():
        room_info[room_name] = [str(p["id"]) for p in room_data.get("peers", [])]
    return json.dumps({"type": "rooms_list", "rooms": room_info})


async def send_rooms_list(websocket):
    try:
        await websocket.send(_build_rooms_payload())
    except:
        pass


async def broadcast_rooms_list_all():
    msg = _build_rooms_payload()
    for ws in list(clients.keys()):
        try:
            await ws.send(msg)
        except:
            pass


async def relay_audio(room, sender_ws, data):
    for ws, info in clients.items():
        if info["room"] == room and ws != sender_ws:
            if info["mode"] in ["relay", "hybrid"]:
                try:
                    await ws.send(data)
                except:
                    pass


async def handle_client(websocket):
    try:
        async for message in websocket:

            if isinstance(message, bytes):
                info = clients.get(websocket)
                if info:
                    await relay_audio(info["room"], websocket, message)
                continue

            data = json.loads(message)
            msg_type = data.get("type")

            if msg_type == "get_rooms":
                await send_rooms_list(websocket)
                continue

            if msg_type in ["register", "join"]:
                room = data.get("room", "default")

                peer = {
                    "ip": data["ip"],
                    "port": data["port"],
                    "id": data["id"],
                    "mode": data.get("mode", "hybrid"),
                }

                clients[websocket] = {
                    "room": room,
                    "id": data["id"],
                    "ip": data["ip"],
                    "port": data["port"],
                    "mode": data.get("mode", "hybrid"),
                }

                if room not in rooms:
                    rooms[room] = {"peers": []}
                    room_host[room] = peer["id"]

                rooms[room]["peers"] = [
                    p for p in rooms[room]["peers"]
                    if p["id"] != peer["id"]
                ]
                rooms[room]["peers"].append(peer)

                await broadcast_room(room)
                await broadcast_rooms_list_all()
                continue

            if msg_type == "voip_kick":
                sender_id = clients.get(websocket, {}).get("id")
                sender_room = clients.get(websocket, {}).get("room")
                if room_host.get(sender_room) != sender_id:
                    continue
                target_id = data.get("target")
                for ws, info in list(clients.items()):
                    if info["id"] == target_id and info["room"] == sender_room:
                        try:
                            await ws.send(json.dumps({"type": "voip_kicked"}))
                            await ws.close(1000, "kicked")
                        except:
                            pass
                        break
                continue

            if msg_type == "mode":
                if websocket in clients:
                    clients[websocket]["mode"] = data["mode"]

    except:
        pass

    finally:
        if websocket in clients:
            info = clients.pop(websocket)
            room = info["room"]
            pid = info["id"]

            if room in rooms:
                rooms[room]["peers"] = [
                    p for p in rooms[room]["peers"]
                    if p["id"] != pid
                ]

                if room_host.get(room) == pid:
                    remaining = rooms[room].get("peers", [])
                    if remaining:
                        room_host[room] = remaining[0]["id"]
                    else:
                        room_host.pop(room, None)

                await broadcast_room(room)
                await broadcast_rooms_list_all()


async def main():
    print("Unified VoIP server ws://0.0.0.0:9000")
    async with websockets.serve(handle_client, "0.0.0.0", 9000):
        await asyncio.Future()

asyncio.run(main())
