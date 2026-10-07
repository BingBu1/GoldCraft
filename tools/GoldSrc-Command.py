"""Send an admin command to the loopback-only ReHLDS sandbox, without exposing credentials."""
import json
import re
import socket
import sys
from pathlib import Path

ROOT=Path(__file__).resolve().parent.parent


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
        result=[]
        while True:
            try:
                packet=connection.recv(65535)
                if packet.startswith(b'\xff\xff\xff\xffl'):packet=packet[5:]
                result.append(packet.rstrip(b'\0').decode('utf-8',errors='replace'))
                connection.settimeout(.25)
            except socket.timeout:break
        if not result:raise TimeoutError('ReHLDS did not acknowledge the command')
        output=''.join(result).replace(token,'[redacted]')
        if 'Bad rcon_password' in output:raise RuntimeError('ReHLDS RCON authentication failed')
        return output


if __name__=='__main__':
    if len(sys.argv)!=2:raise SystemExit('Usage: GoldSrc-Command.py "console command"')
    if hasattr(sys.stdout,'reconfigure'):sys.stdout.reconfigure(encoding='utf-8',errors='replace')
    print(command(sys.argv[1]))
