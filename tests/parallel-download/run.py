"""Exercise real WinHTTP workers against a concurrent fault-injecting server."""
import argparse, http.server, json, subprocess, threading, time
from pathlib import Path

class Handler(http.server.BaseHTTPRequestHandler):
    active = peak = 0
    lock = threading.Lock()
    def log_message(self, *args): pass
    def do_GET(self):
        with self.lock:
            Handler.active += 1
            Handler.peak = max(Handler.peak, Handler.active)
        try:
            if self.path == '/hang': time.sleep(3); return
            if self.path == '/missing': self.send_error(404); return
            if self.path == '/redirect':
                self.send_response(302); self.send_header('Location', '/ok'); self.end_headers(); return
            self.send_response(200)
            if self.path == '/chunked':
                self.send_header('Transfer-Encoding', 'chunked'); self.end_headers()
                self.wfile.write(b'10000\r\n' + b'x'*65536 + b'\r\n0\r\n\r\n'); return
            self.send_header('Content-Length', str(2097152 if self.path == '/large' else 65536)); self.end_headers()
            if self.path == '/truncated': self.wfile.write(b'x'*8); return
            if self.path == '/large': return
            time.sleep(.2); self.wfile.write(b'x'*65536)
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError): pass
        finally:
            with self.lock: Handler.active -= 1

def main():
    p = argparse.ArgumentParser(); p.add_argument('exe', type=Path); p.add_argument('output', type=Path); p.add_argument('--bridge',action='store_true'); a = p.parse_args()
    a.output.mkdir(parents=True, exist_ok=True)
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True); thread.start()
    try:
        result = subprocess.run([str(a.exe.resolve()), f'http://127.0.0.1:{server.server_port}/', str(a.output/'temporary')], text=True, capture_output=True, timeout=30)
        receipt = dict(returncode=result.returncode, stdout=result.stdout, stderr=result.stderr, peakRequests=Handler.peak)
        (a.output/'transport.json').write_text(json.dumps(receipt, indent=2), encoding='utf-8')
        print(json.dumps(receipt)); return result.returncode or (1 if not a.bridge and Handler.peak < 4 else 0)
    finally: server.shutdown(); server.server_close()

if __name__ == '__main__': raise SystemExit(main())
