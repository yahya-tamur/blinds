#!/usr/bin/env python3
"""Serves the soldering checklist from this folder.

    python3 docs/serve.py           -> http://127.0.0.1:8000/  (this computer only)
    python3 docs/serve.py --lan     -> also reachable from a phone on the same Wi-Fi
    python3 docs/serve.py 9000      -> another port

The page file has no <!doctype>/<head> of its own (the artifact publisher adds them),
so this wraps it in the same minimal skeleton before sending it.
"""
import http.server, os, socket, sys

DIR = os.path.dirname(os.path.abspath(__file__))
PAGE = "rev2_soldering_guide.html"
HEAD = ('<!doctype html><html lang="en"><head><meta charset="utf-8">'
        '<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">'
        '<style>:root{padding-top:env(safe-area-inset-top);padding-bottom:env(safe-area-inset-bottom)}'
        'body{margin:0}img{max-width:100%}[hidden]{display:none!important}</style></head><body>')


class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=DIR, **kwargs)

    def do_GET(self):
        if self.path == "/favicon.ico":          # browsers ask for one; there isn't one
            self.send_response(204); self.end_headers(); return
        if self.path.split("?")[0] in ("/", "/" + PAGE):
            body = (HEAD + open(os.path.join(DIR, PAGE), encoding="utf-8").read() + "</body></html>").encode()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)
        else:
            super().do_GET()


def lan_ip():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("10.255.255.255", 1))   # no packet is sent; this just picks the outgoing interface
        return s.getsockname()[0]
    except OSError:
        return None
    finally:
        s.close()


if __name__ == "__main__":
    lan = "--lan" in sys.argv
    ports = [a for a in sys.argv[1:] if a.isdigit()]
    port = int(ports[0]) if ports else 8000
    server = http.server.ThreadingHTTPServer(("0.0.0.0" if lan else "127.0.0.1", port), Handler)
    print(f"Soldering checklist: http://127.0.0.1:{port}/")
    if lan and lan_ip():
        print(f"On your phone (same Wi-Fi): http://{lan_ip()}:{port}/")
    print("Ctrl+C to stop")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
