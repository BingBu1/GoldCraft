"""Send a command only to the private loopback Minecraft sandbox configured by this project."""
import argparse
import json
from pathlib import Path
import socket
import struct


def receive(sock, count):
    result = bytearray()
    while len(result) < count:
        part = sock.recv(count - len(result))
        if not part:
            raise ConnectionError("Minecraft RCON closed the connection")
        result.extend(part)
    return result


def exchange(sock, request_id, kind, text):
    body = struct.pack("<ii", request_id, kind) + text.encode("utf-8") + b"\0\0"
    sock.sendall(struct.pack("<i", len(body)) + body)
    size, = struct.unpack("<i", receive(sock, 4))
    if size < 10 or size > 4 * 1024 * 1024:
        raise ValueError("Invalid RCON response length")
    reply = receive(sock, size)
    reply_id, _ = struct.unpack_from("<ii", reply)
    if reply_id != request_id:
        raise PermissionError("Sandbox RCON authentication or request failed")
    return reply[8:-2].decode("utf-8", errors="replace")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    config = json.loads((root / "sandbox/cluster.json").read_text(encoding="utf-8-sig"))
    with socket.create_connection(("127.0.0.1", config["minecraftRconPort"]), timeout=10) as connection:
        exchange(connection, 1, 3, config["rconToken"])
        print(exchange(connection, 2, 2, args.command))
