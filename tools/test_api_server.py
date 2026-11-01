#!/usr/bin/env python3
"""Tiny stand-in for the remote API, for testing before the real one exists.

Run on a PC on the same network as the speedometer (e.g. both on the phone hotspot):
    python tools/test_api_server.py 8080
Then on the speedometer web page set API URL to  http://<pc-ip>:8080/points
Every batch the device sends is printed and appended to points.jsonl.
"""
import json
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer


class Handler(BaseHTTPRequestHandler):
    def do_POST(self):
        length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(length)
        try:
            data = json.loads(body)
        except ValueError:
            self.send_response(400)
            self.end_headers()
            return
        auth = self.headers.get("Authorization", "")
        pts = data.get("points", [])
        print(f"{data.get('device')} sent {len(pts)} point(s) auth={'yes' if auth else 'no'}")
        for p in pts:
            print(f"  {p['t']}  {p['lat']},{p['lon']}  {p['speed_mph']} mph  hdg {p['heading']}  sats {p['sats']}")
        with open("points.jsonl", "a") as f:
            for p in pts:
                f.write(json.dumps({"device": data.get("device"), **p}) + "\n")
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.end_headers()
        self.wfile.write(b'{"ok":true}')

    def log_message(self, *args):
        pass


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
    print(f"Listening on 0.0.0.0:{port}")
    HTTPServer(("0.0.0.0", port), Handler).serve_forever()
