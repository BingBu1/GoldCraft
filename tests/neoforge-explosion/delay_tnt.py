"""Loopback-only relay for delaying authentic TNT requests in isolated tests.

No authentication data is logged and no frame is synthesized or modified.
An arm.json {label, slot} selects one request. The harness must create
release-LABEL within eight seconds; all later outbound frames remain ordered.
"""
from pathlib import Path
import hashlib
import json
import socket
import struct
import threading
import time

HEADER = struct.Struct('<IHHIIQQ')


def save(path, value):
    temporary = path.with_suffix('.pending')
    temporary.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')
    temporary.replace(path)


class DamageRelay:
    def __init__(self, upstream_port, evidence):
        self.evidence = Path(evidence)
        self.listener = socket.socket()
        self.listener.bind(('127.0.0.1', 0))
        self.listener.listen(1)
        self.port = self.listener.getsockname()[1]
        self.upstream_port = upstream_port
        self.stop = threading.Event()
        self.lock = threading.Lock()
        self.events = {}
        self.errors = []
        self.sockets = []
        self.thread = threading.Thread(target=self._serve, daemon=True)
        self.thread.start()

    def _read(self, stream, length):
        data = bytearray()
        while len(data) < length and not self.stop.is_set():
            part = stream.recv(min(length - len(data), 65536))
            if not part:
                if data:
                    raise EOFError('Partial frame')
                return None
            data.extend(part)
        return bytes(data) if len(data) == length else None

    def _request(self, payload, frame):
        arm = self.evidence / 'arm.json'
        if len(payload) != 88 or struct.unpack_from('<I', payload, 36)[0] != 6 or not arm.exists():
            return
        selection = json.loads(arm.read_text(encoding='utf-8'))
        slot, serial, life = struct.unpack_from('<III', payload, 24)
        if slot != selection['slot']:
            return
        label = selection['label']
        if not label.isalnum():
            raise ValueError('Unsafe evidence label')
        event = struct.unpack_from('<Q', payload, 8)[0]
        with self.lock:
            if event in self.events:
                raise ValueError('Duplicate held event')
            self.events[event] = label
        arm.unlink()
        started = time.monotonic()
        digest = hashlib.sha256(frame).hexdigest()
        save(self.evidence / f'held-{label}.json', dict(event=event, slot=slot, serial=serial,
             life=life, amount=struct.unpack_from('<f', payload, 40)[0],
             targetTick=struct.unpack_from('<Q', payload, 68)[0], frameSha256=digest,
             heldMonotonic=started))
        while not (self.evidence / f'release-{label}').exists():
            if self.stop.wait(.005):
                raise RuntimeError('Relay stopped with a held request')
            if time.monotonic() - started > 8:
                raise TimeoutError('Held TNT request exceeded eight seconds')
        save(self.evidence / f'released-{label}.json', dict(event=event,
             delaySeconds=time.monotonic()-started, frameSha256=digest))

    def _result(self, payload):
        if len(payload) != 40:
            return
        world, event, slot, serial, life, status, health, armor = struct.unpack('<QQIIIIff', payload)
        with self.lock:
            label = self.events.get(event)
        if label:
            save(self.evidence / f'result-{label}.json', dict(world=world, event=event, slot=slot,
                 serial=serial, life=life, status=status, health=health, armor=armor))

    def _pump(self, source, destination, outbound):
        sequence = 0
        try:
            while not self.stop.is_set():
                header = self._read(source, HEADER.size)
                if header is None:
                    break
                magic, version, kind, size, flags, epoch, received = HEADER.unpack(header)
                if magic != 0x31464347 or version != 30 or flags or size > 32*1024*1024 or received != sequence+1:
                    raise ValueError('Invalid relay frame')
                payload = self._read(source, size)
                if payload is None:
                    raise EOFError('Missing payload')
                frame = header + payload
                if outbound and kind == 30:
                    self._request(payload, frame)
                if not outbound and kind == 31:
                    self._result(payload)
                destination.sendall(frame)
                sequence = received
        except Exception as error:
            if not self.stop.is_set():
                self.errors.append(repr(error))
        finally:
            self.stop.set()
            for stream in self.sockets:
                try:
                    stream.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass

    def _serve(self):
        try:
            client, address = self.listener.accept()
            assert address[0] == '127.0.0.1'
            upstream = socket.create_connection(('127.0.0.1', self.upstream_port), timeout=5)
            upstream.settimeout(None)
            self.sockets = [client, upstream]
            for stream in self.sockets:
                stream.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            reverse = threading.Thread(target=self._pump, args=(upstream, client, False), daemon=True)
            reverse.start()
            self._pump(client, upstream, True)
            reverse.join(2)
        except Exception as error:
            if not self.stop.is_set():
                self.errors.append(repr(error))
        finally:
            for stream in self.sockets:
                stream.close()
            self.listener.close()

    def close(self):
        self.stop.set()
        self.listener.close()
        for stream in self.sockets:
            try:
                stream.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
        self.thread.join(3)
        save(self.evidence / 'relay-closed.json', dict(errors=self.errors, heldEvents=len(self.events),
             threadStopped=not self.thread.is_alive()))
