#!/usr/bin/env python3
"""Serve the restunts2 web demo with the isolation headers js-dos needs.

If a request for a `.jsdos` bundle carries a `nick` query parameter, the
bundle's `.jsdos/dosbox.conf` autoexec is rewritten on the fly to pass
`/nNICK` to `REST2.EXE`, prefilling the high-score name field without having to
pre-build a bundle per nickname.
"""
import functools
import http.server
import io
import os
import re
import urllib.parse
import zipfile


def _inject_nick(bundle, nick):
    """Return an in-memory .jsdos with /n<nick> appended to the REST2.EXE line."""
    out = io.BytesIO()
    with zipfile.ZipFile(io.BytesIO(bundle), "r") as zin, \
            zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as zout:
        for item in zin.infolist():
            data = zin.read(item.filename)
            if item.filename == ".jsdos/dosbox.conf":
                text = data.decode("latin-1")
                patched = []
                for line in text.splitlines(keepends=True):
                    if "REST2.EXE" in line and " /n" not in line:
                        body = line.rstrip("\r\n")
                        eol = line[len(body):]
                        line = body + " /n" + nick + eol
                    patched.append(line)
                data = "".join(patched).encode("latin-1")
            zout.writestr(item, data)
    return out.getvalue()


class Handler(http.server.SimpleHTTPRequestHandler):
    extensions_map = {**http.server.SimpleHTTPRequestHandler.extensions_map,
        ".jsdos": "application/zip", ".wasm": "application/wasm"}

    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        super().end_headers()

    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        nick = (urllib.parse.parse_qs(parsed.query).get("nick") or [""])[0].strip()
        # Keep it a single safe DOS token (the CLI /n parse stops at spaces).
        nick = re.sub(r"[^\w.\-]", "", nick)[:16]
        if nick and parsed.path.endswith(".jsdos"):
            path = self.translate_path(parsed.path)
            if os.path.isfile(path):
                with open(path, "rb") as fh:
                    body = _inject_nick(fh.read(), nick)
                self.send_response(200)
                self.send_header("Content-Type", "application/zip")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
                return
        super().do_GET()


if __name__ == "__main__":
    http.server.ThreadingHTTPServer(("0.0.0.0", 8080),
        functools.partial(Handler, directory="web")).serve_forever()
