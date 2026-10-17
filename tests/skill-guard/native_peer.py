"""Explicit MC authority substitute for the real ReHLDS/ZP consumer regression.

This does not run Minecraft. Real Epic event semantics have a separate FML test.
Credentials remain in memory; reports contain no pairing token or secret.
"""
import re
import socket
import struct
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
VERSION = int(re.search(r'protocol_version = (\d+)', (ROOT / 'native/include/goldcraft/wire.hpp').read_text())[1])
HEADER = struct.Struct('<IHHIIQQ')
CAPABILITIES = 16 | 64 | 1024 | 32768


class AuthorityPeer:
    def __init__(self, config, identity, capabilities=CAPABILITIES):
        self.identity = identity
        self.socket = socket.create_connection(('127.0.0.1', config['serverPort']), timeout=3)
        self.socket.settimeout(.01)
        self.lock = threading.RLock()
        self.closed = False
        self.error = None
        self.epoch = self.world = self.sequence = self.received = self.revision = self.pose_sequence = 0
        self.capabilities = 0
        self.actors = {}
        self.messages = []
        self.requests = []
        self.actor = None
        self.flags = None
        self.repeat = False
        self.last_gameplay = None
        self.damage_reply = 0.0
        self.send(1, bytes.fromhex(config['serverSession']) + bytes.fromhex(config['serverToken']) + struct.pack('<II', 4, capabilities))
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def send(self, kind, body):
        with self.lock:
            self.sequence += 1
            self.socket.sendall(HEADER.pack(0x31464347, VERSION, kind, len(body), 0, self.epoch, self.sequence) + body)

    def configure(self, actor, flags, *, reply=0.0, repeat=False):
        with self.lock:
            self.actor = dict(actor) if actor else None
            self.flags, self.damage_reply, self.repeat = flags, reply, repeat

    def publish(self):
        a = self.actor
        if not a:
            return
        self.revision += 1
        if self.flags is not None:
            if self.repeat:
                if self.last_gameplay:
                    self.send(85, self.last_gameplay)
            else:
                self.last_gameplay = struct.pack('<QQ4I', self.world, self.revision, a['slot'], a['serial'], a['spawn'], a['life']) + self.identity + struct.pack('<I', self.flags)
                self.send(85, self.last_gameplay)
        self.send(77, struct.pack('<4Q5I4f', self.world, self.revision, 0, 0,
                                 a['slot'], a['serial'], a['spawn'], a['life'], 0, 1, 1, 0, 0))
        # Actual host collision still validates this fixed, previously observed position.
        self.pose_sequence += 1
        x, y, z = a['origin']
        feet = x / 32, (z + a['mins'][2]) / 32 + 64, -y / 32
        pose = struct.pack('<QQIII', self.world, self.pose_sequence, a['slot'], a['serial'], a['life']) + self.identity
        pose += struct.pack('<I11f', 1, *feet, 0, 0, 0, 0, 0, 1.62, .6, 1.8)
        self.send(50, pose)

    def receive(self, kind, body, epoch):
        if kind == 2:
            self.epoch = epoch
            self.capabilities = struct.unpack_from('<I', body, 20)[0]
        elif epoch != self.epoch:
            raise ValueError('Wrong authority connection epoch')
        elif kind == 10:
            self.world = struct.unpack_from('<Q', body)[0]
        elif kind == 11:
            count = struct.unpack_from('<I', body, 24)[0]
            if len(body) != 28 + 116 * count:
                raise ValueError('Wrong actor snapshot size')
            actors = {}
            for n in range(count):
                offset = 28 + 116 * n
                slot, serial, userid, team, flags, life = struct.unpack_from('<6I', body, offset)
                actors[slot] = dict(slot=slot, serial=serial, userid=userid, team=team, flags=flags, life=life,
                    spawn=struct.unpack_from('<I', body, offset+104)[0],
                    origin=struct.unpack_from('<3f', body, offset+32), mins=struct.unpack_from('<3f', body, offset+64))
            self.actors = actors
        elif kind == 80:
            if len(body) != 64:
                raise ValueError('Wrong equipment request size')
            world, event, slot, serial, spawn, life, attacker, attacker_serial, attacker_spawn, damage, damage_kind = struct.unpack_from('<QQ7IfI', body)
            self.requests.append(dict(world=world, event=event, slot=slot, serial=serial, spawn=spawn, life=life,
                attacker=attacker, attackerSerial=attacker_serial, attackerSpawn=attacker_spawn, damage=damage, kind=damage_kind,
                response=self.damage_reply))
            if self.damage_reply is not None:
                self.send(81, body[:32] + struct.pack('<Iff', 0, min(damage, self.damage_reply), 1))
        elif kind == 14:
            self.messages.append(body)

    def run(self):
        buffer = b''
        heartbeat = published = 0
        try:
            while not self.closed:
                now = time.monotonic()
                with self.lock:
                    if self.epoch and now >= heartbeat:
                        self.send(3, b'')
                        heartbeat = now + .5
                    if self.world and now >= published:
                        self.publish()
                        published = now + .04
                try:
                    data = self.socket.recv(65536)
                except socket.timeout:
                    continue
                if not data:
                    raise EOFError('Native authority connection closed')
                buffer += data
                while len(buffer) >= HEADER.size:
                    magic, version, kind, size, flags, epoch, sequence = HEADER.unpack_from(buffer)
                    if magic != 0x31464347 or version != VERSION or size > 32*1024*1024 or flags or sequence != self.received+1:
                        raise ValueError('Invalid host frame')
                    if len(buffer) < HEADER.size + size:
                        break
                    body, buffer = buffer[HEADER.size:HEADER.size+size], buffer[HEADER.size+size:]
                    self.received = sequence
                    with self.lock:
                        self.receive(kind, body, epoch)
        except Exception as exc:
            if not self.closed:
                self.error = exc

    def close(self):
        self.closed = True
        self.thread.join(2)
        self.socket.close()
        if self.thread.is_alive():
            raise RuntimeError('Authority fixture thread did not stop')
