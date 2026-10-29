"""Send an admin command to the loopback-only ReHLDS sandbox, without exposing credentials."""
import json
import re
import socket
import struct
import sys
import time
from pathlib import Path

ROOT=Path(__file__).resolve().parent.parent


class ReplyPackets:
    """Reassemble GoldSrc's nine-byte SPLITPACKET header before decoding text.

    The pinned ReHLDS NET_SendLong puts the fragment number in the high
    nibble and fragment count in the low nibble. UDP arrival order is not
    message order; an incomplete diagnostic must never look like success.
    """
    def __init__(self):
        self.messages = []
        self.groups = {}
        self.bytes = 0

    def feed(self, packet):
        self.bytes += len(packet)
        if self.bytes > 1024 * 1024:
            raise ValueError('RCON reply exceeds the diagnostic budget')
        if packet.startswith(b'\xfe\xff\xff\xff'):
            if len(packet) <= 9:
                raise ValueError('Truncated GoldSrc split packet')
            sequence, descriptor = struct.unpack_from('<IB', packet, 4)
            index, count = descriptor >> 4, descriptor & 15
            if not count or index >= count:
                raise ValueError('Invalid GoldSrc split fragment')
            if sequence not in self.groups:
                if len(self.groups) >= 128:
                    raise ValueError('Too many RCON split messages')
                self.groups[sequence] = [None] * count
                self.messages.append(sequence)
            fragments = self.groups[sequence]
            if len(fragments) != count:
                raise ValueError('Inconsistent GoldSrc split count')
            payload = packet[9:]
            if fragments[index] is not None and fragments[index] != payload:
                raise ValueError('Conflicting GoldSrc split fragment')
            fragments[index] = payload
        else:
            self.messages.append(packet)

    def finish(self):
        result = []
        for message in self.messages:
            if isinstance(message, int):
                fragments = self.groups[message]
                if any(fragment is None for fragment in fragments):
                    raise TimeoutError('Incomplete GoldSrc RCON split reply')
                message = b''.join(fragments)
            if not message.startswith(b'\xff\xff\xff\xffl'):
                raise ValueError('Unexpected GoldSrc RCON reply type')
            result.append(message[5:].rstrip(b'\0').decode('utf-8', errors='replace'))
        return ''.join(result)


def command(text, *, config_path=None):
    if not text or len(text)>512 or '\n' in text or '\r' in text:
        raise ValueError('One bounded console command is required')
    path=Path(config_path) if config_path is not None else ROOT/'sandbox/cluster.json'
    if not path.resolve().is_relative_to((ROOT/'sandbox').resolve()):
        raise ValueError('RCON configuration must belong to this workspace sandbox')
    config=json.loads(path.read_text(encoding='utf-8-sig'))
    token=config['csRconToken']
    with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as connection:
        connection.settimeout(3)
        connection.connect(('127.0.0.1',config['csPort']))
        connection.send(b'\xff\xff\xff\xffchallenge rcon\n')
        challenge=re.search(rb'challenge rcon (-?\d+)',connection.recv(65535))
        if not challenge:raise RuntimeError('ReHLDS did not return an RCON challenge')
        payload=f'rcon {challenge[1].decode()} "{token}" {text}\n'.encode('ascii')
        connection.send(b'\xff\xff\xff\xff'+payload)
        replies=ReplyPackets()
        deadline=time.monotonic()+5
        while True:
            try:
                packet=connection.recv(65535)
            except socket.timeout:break
            replies.feed(packet)
            remaining=deadline-time.monotonic()
            if remaining <= 0:raise TimeoutError('RCON reply exceeded the collection deadline')
            connection.settimeout(min(.25,remaining))
        if not replies.messages:raise TimeoutError('ReHLDS did not acknowledge the command')
        output=replies.finish().replace(token,'[redacted]')
        if 'Bad rcon_password' in output:raise RuntimeError('ReHLDS RCON authentication failed')
        return output


if __name__=='__main__':
    if len(sys.argv)!=2:raise SystemExit('Usage: GoldSrc-Command.py "console command"')
    if hasattr(sys.stdout,'reconfigure'):sys.stdout.reconfigure(encoding='utf-8',errors='replace')
    print(command(sys.argv[1]))
