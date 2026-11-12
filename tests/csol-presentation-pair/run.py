#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise paired compiled modules, real resources and bounded HTTP byte checks."""
import argparse
import importlib.util
import json
from pathlib import Path
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import urllib.parse

ROOT = Path(__file__).resolve().parents[2]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('server', 'client', 'legacy-server', 'resources', 'output'):
        parser.add_argument('--'+name, type=Path, required=True)
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location('pair', ROOT/'tools/Verify-CSOLPresentationPair.py')
    pair = importlib.util.module_from_spec(spec);spec.loader.exec_module(pair)
    name = pair.compiled_menu(args.client)
    files = {n: args.resources/n for n in pair.menu_resources(name, (args.resources/name).read_bytes())}
    receipt = pair.verify_pair(args.server, args.client, files)
    checks = []
    def rejects(label, callback, text):
        try: callback()
        except (ValueError, OSError) as error:
            assert text in str(error), (label, type(error), str(error))
            checks.append(label);return
        raise AssertionError('Accepted invalid pairing: '+label)
    assert pair.compiled_menu(args.legacy_server)!=name
    rejects('actual compiled revision mismatch', lambda: pair.verify_pair(args.legacy_server,args.client,files), 'revisions differ')
    rejects('missing compiled JSON', lambda: pair.verify_pair(args.server,args.client,{n:p for n,p in files.items() if n!=name}), 'absent')
    image = next(n for n in files if n.startswith('gfx/'))
    rejects('missing referenced image', lambda: pair.verify_pair(args.server,args.client,{n:p for n,p in files.items() if n!=image}), 'Missing required')
    rejects('traversal', lambda: pair.resource_path('../models/menu.json'), 'Unsafe')
    checks.append('actual module pair and resource closure')
    fault = ''
    class Handler(BaseHTTPRequestHandler):
        def log_message(self,*_): pass
        def do_GET(self):
            key = urllib.parse.unquote(self.path.removeprefix('/goldcraft/'))
            if key not in files:
                self.send_error(404);return
            data = files[key].read_bytes()
            if key==name and fault=='status':
                self.send_error(404);return
            if key==name and fault=='truncate':data=data[:-1]
            if key==name and fault=='alter':data=bytes([data[0]^1])+data[1:]
            self.send_response(200);self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
    server = ThreadingHTTPServer(('127.0.0.1',0),Handler)
    worker = threading.Thread(target=server.serve_forever,daemon=True);worker.start()
    url = 'http://127.0.0.1:'+str(server.server_port)+'/goldcraft/'
    try:
        pair.verify_pair(args.server,args.client,files,url);checks.append('real HTTP closure')
        for mode,text in [('truncate','HTTP bytes differ'),('alter','HTTP bytes differ'),('status','404')]:
            fault=mode;rejects('HTTP '+mode,lambda:pair.verify_pair(args.server,args.client,files,url),text)
    finally:
        server.shutdown();server.server_close();worker.join()
    output = args.output.absolute()
    assert not output.exists() and output.resolve().is_relative_to(ROOT/'build')
    output.parent.mkdir(parents=True,exist_ok=True)
    with output.open('x',encoding='utf8') as stream:
        json.dump(dict(success=True,checks=checks,resources=len(receipt['resources']),serverSha256=receipt['serverSha256'],clientSha256=receipt['clientSha256'],gameplay=False,performance=False),stream,indent=2)
    print(json.dumps(dict(success=True,checks=len(checks),resources=len(receipt['resources']))))

if __name__=='__main__':main()
