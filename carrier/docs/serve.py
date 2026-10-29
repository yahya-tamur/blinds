#!/usr/bin/env python3
"""Serves this folder to preview local changes; files are read fresh on every request, so reload to see edits.

    python3 docs/serve.py           -> http://127.0.0.1:8000/  (this computer only)
    python3 docs/serve.py --lan     -> also reachable from a phone on the same Wi-Fi
    python3 docs/serve.py 9000      -> another port

The landing page is / and the soldering guide /rev2_soldering_guide.html (on the public site,
build_site.py moves it to /soldering-guide/).
"""
import http.server, os, socket, sys

DIR = os.path.dirname(os.path.abspath(__file__))


class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=DIR, **kwargs)

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")   # always show the file as it is on disk
        super().end_headers()

    def do_GET(self):
        if self.path == "/favicon.ico":                  # the pages link favicon.svg; answer the default request quietly
            self.send_response(204); self.end_headers(); return
        path = self.path.split("?")[0]
        if path.startswith("/soldering-guide/"):           # same addresses as on the public site (build_site.py)
            rest = path[len("/soldering-guide/"):]
            self.path = "/" + (rest or "rev2_soldering_guide.html")
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
    print(f"Soldering guide: http://127.0.0.1:{port}/soldering-guide/")
    if lan and lan_ip():
        print(f"On your phone (same Wi-Fi): http://{lan_ip()}:{port}/soldering-guide/")
    print("Ctrl+C to stop")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
