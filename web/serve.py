#!/usr/bin/env python3
"""Serve the restunts2 web demo with isolation headers js-dos needs."""
import functools, http.server
class Handler(http.server.SimpleHTTPRequestHandler):
    extensions_map = {**http.server.SimpleHTTPRequestHandler.extensions_map,
        ".jsdos": "application/zip", ".wasm": "application/wasm"}
    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        super().end_headers()
if __name__ == "__main__":
    http.server.ThreadingHTTPServer(("0.0.0.0", 8080),
        functools.partial(Handler, directory="web")).serve_forever()
