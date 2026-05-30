import asyncio
import json
import websockets

HOST = "0.0.0.0"
PORT = 9000

peers: list[dict] = []
clients: dict = {}


async def broadcast_peer_list():
    peer_list = [
        {
            "ip": p["ip"],
            "port": p["port"],
            "id": p["id"]
        }
        for p in peers
    ]
    msg = json.dumps({"type": "peer_list", "peers": peer_list})
    for ws in list(clients.keys()):
        try:
            await ws.send(msg)
        except websockets.ConnectionClosed:
            pass


async def handle_client(websocket):
    print(f"[JOIN] {websocket.remote_address}")
    try:
        async for message in websocket:
            try:
                data = json.loads(message)
            except json.JSONDecodeError:
                continue

            if data.get("type") == "register":
                ip   = data.get("ip", "")
                port = data.get("port", 0)
                id = data.get("id", 0);
                clients[websocket] = {"ip": ip, "port": port, "id": id}
                peers.append({"ip": ip, "port": port, "id": id})
                print(f"[REG] {ip}:{port}:{id}")
                await broadcast_peer_list()

    except websockets.ConnectionClosed:
        pass
    finally:
        if websocket in clients:
            peer = clients.pop(websocket)
            peers[:] = [p for p in peers
                        if p["id"] != peer["id"]]
            print(f"[LEAVE] {peer['ip']}:{peer['port']}")
            await broadcast_peer_list()


async def main():
    print(f"[VoIP Server] ws://{HOST}:{PORT}")
    async with websockets.serve(handle_client, HOST, PORT):
        await asyncio.Future()

asyncio.run(main())