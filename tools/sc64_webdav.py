#!/usr/bin/env python3
"""
SC64 WebDAV server — mounts the SummerCart64 SD card as a drive in macOS Finder.
No external dependencies required.

Usage:
    python3 tools/sc64_webdav.py
    python3 tools/sc64_webdav.py --remote localhost:9064   # via sc64dev server
    python3 tools/sc64_webdav.py --port 8765

Mount in Finder:
    Go > Connect to Server (⌘K) > http://localhost:8765
    Click Connect (no username/password needed)

Env:
    SC64_DEPLOYER   sc64deployer binary path (default: sc64deployer)
"""

import concurrent.futures
import http.server
import os
import subprocess
import sys
import tempfile
import time
import xml.etree.ElementTree as ET
from urllib.parse import unquote, urlparse

DEFAULT_PORT = 8765
SC64 = os.environ.get("SC64_DEPLOYER", "sc64deployer")
_remote = []   # e.g. ["-r", "localhost:9064"]


# ---------------------------------------------------------------------------
# SC64 SD primitives
# ---------------------------------------------------------------------------

def _sd(*args):
    return subprocess.run([SC64] + _remote + ["sd"] + list(args),
                          capture_output=True, text=True)


def sd_isdir(path):
    if not path or path == "/":
        return True
    return _sd("ls", path).returncode == 0


def sd_exists(path):
    if not path or path == "/":
        return True
    return _sd("stat", path).returncode == 0


def sd_size(path):
    r = _sd("stat", path)
    if r.returncode != 0:
        return 0
    for tok in r.stdout.split():
        try:
            v = int(tok)
            if v >= 0:
                return v
        except ValueError:
            pass
    return 0


def sd_ls(path):
    r = _sd("ls", path) if (path and path != "/") else _sd("ls")
    if r.returncode != 0:
        return []
    return [l.strip() for l in r.stdout.splitlines() if l.strip()]


# ---------------------------------------------------------------------------
# WebDAV XML
# ---------------------------------------------------------------------------

def _multistatus(entries):
    """entries: list of (href, is_dir, size)"""
    ms = ET.Element("{DAV:}multistatus")
    ts = time.strftime("%a, %d %b %Y %H:%M:%S GMT", time.gmtime())
    for href, is_dir, size in entries:
        resp = ET.SubElement(ms, "{DAV:}response")
        ET.SubElement(resp, "{DAV:}href").text = href
        ps = ET.SubElement(resp, "{DAV:}propstat")
        prop = ET.SubElement(ps, "{DAV:}prop")
        rt = ET.SubElement(prop, "{DAV:}resourcetype")
        if is_dir:
            ET.SubElement(rt, "{DAV:}collection")
        if not is_dir:
            ET.SubElement(prop, "{DAV:}getcontentlength").text = str(size)
            ET.SubElement(prop, "{DAV:}getcontenttype").text = "application/octet-stream"
        ET.SubElement(prop, "{DAV:}getlastmodified").text = ts
        ET.SubElement(prop, "{DAV:}getetag").text = f'"{abs(hash((href, size)))}"'
        ET.SubElement(ps, "{DAV:}status").text = "HTTP/1.1 200 OK"
    return ('<?xml version="1.0" encoding="utf-8"?>'
            + ET.tostring(ms, encoding="unicode")).encode()


# ---------------------------------------------------------------------------
# macOS metadata files we don't want cluttering the SD card
# ---------------------------------------------------------------------------

_APPLE_JUNK = frozenset({
    ".DS_Store", ".metadata_never_index", ".Trashes",
    ".fseventsd", ".Spotlight-V100", ".TemporaryItems",
})


def _is_apple_junk(sd_path):
    name = os.path.basename(sd_path)
    return name.startswith("._") or name in _APPLE_JUNK


# ---------------------------------------------------------------------------
# HTTP handler
# ---------------------------------------------------------------------------

class _DAVHandler(http.server.BaseHTTPRequestHandler):

    def log_message(self, fmt, *args):
        name = os.path.basename(unquote(urlparse(self.path).path))
        print(f"  {self.command:10s} {name or '/'}", flush=True)

    # ---- helpers ----

    def _sd_path(self):
        p = unquote(urlparse(self.path).path)
        s = p.strip("/")
        return "/" + s if s else "/"

    def _drain_body(self):
        n = int(self.headers.get("Content-Length", 0))
        if n:
            self.rfile.read(n)

    def _send(self, status, headers=None, body=b""):
        self.send_response(status)
        for k, v in (headers or {}).items():
            self.send_header(k, v)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        if body:
            self.wfile.write(body)

    def _send_xml(self, body, status=207):
        self._send(status, {"Content-Type": "application/xml; charset=utf-8"}, body)

    # ---- WebDAV verbs ----

    def do_OPTIONS(self):
        self._send(200, {
            "DAV": "1",
            "Allow": "OPTIONS,HEAD,GET,PUT,DELETE,MKCOL,MOVE,PROPFIND,LOCK,UNLOCK",
        })

    def do_HEAD(self):
        sd = self._sd_path()
        if _is_apple_junk(sd) or not sd_exists(sd):
            self._send(404); return
        h = {"Content-Type": "application/octet-stream"}
        if not sd_isdir(sd):
            h["Content-Length"] = str(sd_size(sd))
        self._send(200, h)

    def do_PROPFIND(self):
        sd = self._sd_path()
        self._drain_body()

        if _is_apple_junk(sd):
            self._send(404); return

        is_dir = sd_isdir(sd)
        if not is_dir and not sd_exists(sd):
            self._send(404); return

        depth = self.headers.get("Depth", "0")
        href = self.path.rstrip("/") + ("/" if is_dir else "")
        entries = [(href, is_dir, 0 if is_dir else sd_size(sd))]

        if depth == "1" and is_dir:
            names = [n for n in sd_ls(sd) if not _is_apple_junk("/" + n)]

            def _classify(name):
                child_sd = sd.rstrip("/") + "/" + name
                child_is_dir = sd_isdir(child_sd)
                size = 0 if child_is_dir else sd_size(child_sd)
                child_href = href.rstrip("/") + "/" + name + ("/" if child_is_dir else "")
                return (child_href, child_is_dir, size)

            with concurrent.futures.ThreadPoolExecutor(max_workers=8) as ex:
                entries += list(ex.map(_classify, names))

        self._send_xml(_multistatus(entries))

    def do_GET(self):
        sd = self._sd_path()
        if _is_apple_junk(sd) or sd_isdir(sd) or not sd_exists(sd):
            self._send(404); return

        fd, tmp = tempfile.mkstemp(suffix=".sc64dl")
        os.close(fd)
        try:
            r = _sd("download", sd, tmp)
            if r.returncode != 0:
                self._send(502); return
            size = os.path.getsize(tmp)
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(size))
            self.end_headers()
            with open(tmp, "rb") as f:
                while True:
                    chunk = f.read(65536)
                    if not chunk:
                        break
                    self.wfile.write(chunk)
        finally:
            if os.path.exists(tmp):
                os.unlink(tmp)

    def do_PUT(self):
        sd = self._sd_path()
        if _is_apple_junk(sd):
            self._drain_body(); self._send(204); return

        length = int(self.headers.get("Content-Length", 0))
        fd, tmp = tempfile.mkstemp(suffix=".sc64ul")
        try:
            with os.fdopen(fd, "wb") as f:
                remaining = length
                while remaining > 0:
                    chunk = self.rfile.read(min(remaining, 65536))
                    if not chunk:
                        break
                    f.write(chunk)
                    remaining -= len(chunk)
            r = _sd("upload", tmp, sd)
            self._send(201 if r.returncode == 0 else 502)
        finally:
            if os.path.exists(tmp):
                os.unlink(tmp)

    def do_MKCOL(self):
        self._drain_body()
        sd = self._sd_path()
        r = _sd("mkdir", sd)
        self._send(201 if r.returncode == 0 else 405)

    def do_DELETE(self):
        sd = self._sd_path()
        if _is_apple_junk(sd):
            self._send(204); return
        _sd("rm", sd)
        self._send(204)

    def do_MOVE(self):
        self._drain_body()
        dest_raw = unquote(urlparse(self.headers.get("Destination", "")).path)
        dest_sd = "/" + dest_raw.strip("/") if dest_raw.strip("/") else "/"
        r = _sd("mv", self._sd_path(), dest_sd)
        self._send(201 if r.returncode == 0 else 502)

    def do_LOCK(self):
        self._drain_body()
        token = f"opaquelocktoken:{abs(hash(self.path))}"
        body = (
            '<?xml version="1.0" encoding="utf-8"?>'
            '<D:prop xmlns:D="DAV:"><D:lockdiscovery><D:activelock>'
            f'<D:locktoken><D:href>{token}</D:href></D:locktoken>'
            '<D:lockscope><D:exclusive/></D:lockscope>'
            '<D:locktype><D:write/></D:locktype>'
            '<D:depth>0</D:depth><D:timeout>Second-3600</D:timeout>'
            '</D:activelock></D:lockdiscovery></D:prop>'
        ).encode()
        self._send(200, {
            "Content-Type": "application/xml",
            "Lock-Token": f"<{token}>",
        }, body)

    def do_UNLOCK(self):
        self._drain_body()
        self._send(204)


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main():
    global _remote

    args = sys.argv[1:]
    port = DEFAULT_PORT
    i = 0
    while i < len(args):
        if args[i] in ("--port", "-p") and i + 1 < len(args):
            port = int(args[i + 1]); i += 2
        elif args[i] in ("--remote", "-r") and i + 1 < len(args):
            _remote = ["-r", args[i + 1]]; i += 2
        else:
            print(__doc__, file=sys.stderr); sys.exit(2)

    server = http.server.ThreadingHTTPServer(("127.0.0.1", port), _DAVHandler)
    via = f"  via sc64deployer {' '.join(_remote)}" if _remote else ""
    print(f"SC64 WebDAV  →  http://localhost:{port}{via}")
    print(f"Finder: ⌘K  →  http://localhost:{port}")
    print("Ctrl-C to stop\n")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nstopped")


if __name__ == "__main__":
    main()
