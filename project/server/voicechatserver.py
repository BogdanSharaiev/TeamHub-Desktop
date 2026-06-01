import asyncio
import json
import websockets

rooms = {}
clients = {}


async def broadcast_room(room):
    if room not in rooms:
        return

    msg = json.dumps({
        "type": "peer_list",
        "peers": rooms[room]["peers"]
    })

    for ws, info in clients.items():
        if info["room"] == room:
            try:
                await ws.send(msg)
            except:
                pass


async def send_rooms_list(websocket):
    msg = json.dumps({
        "type": "rooms_list",
        "rooms": list(rooms.keys())
    })
    try:
        await websocket.send(msg)
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
                    "mode": data.get("mode", "hybrid")
                }

                clients[websocket] = {
                    "room": room,
                    "id": data["id"],
                    "ip": data["ip"],
                    "port": data["port"],
                    "mode": data.get("mode", "hybrid")
                }

                if room not in rooms:
                    rooms[room] = {"peers": []}

                rooms[room]["peers"] = [
                    p for p in rooms[room]["peers"]
                    if p["id"] != peer["id"]
                ]
                rooms[room]["peers"].append(peer)

                await broadcast_room(room)
                await send_rooms_list(websocket)
            elif msg_type == "mode":
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

                await broadcast_room(room)


async def main():
    print("Unified VoIP server ws://0.0.0.0:9000")
    async with websockets.serve(handle_client, "0.0.0.0", 9000):
        await asyncio.Future()

asyncio.run(main())