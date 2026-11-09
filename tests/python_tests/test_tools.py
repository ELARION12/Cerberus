"""
The built-in Python tools. They run with the caller's permissions and are not a sandbox,
but they should still refuse what they say they refuse.
"""

import http.server
import threading

import pytest

import geryon
from geryon import ToolInput, http_get_tool, read_file_tool, write_file_tool

pytestmark = pytest.mark.skipif(
    not geryon._BACKEND_AVAILABLE, reason="native module is not built"
)


def call(tool, args):
    inp = ToolInput()
    inp.args = args
    return tool(inp)


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        body = b"x" * 1_500_000 if self.path == "/big" else b"hello"
        self.send_response(200)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *args):
        pass


@pytest.fixture
def server():
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    yield f"http://127.0.0.1:{srv.server_address[1]}"
    srv.shutdown()


def test_http_get_returns_the_body(server):
    out = call(http_get_tool, server + "/")
    assert out.success and out.result == "hello"


def test_http_get_caps_a_large_response(server):
    out = call(http_get_tool, server + "/big")
    assert out.success and len(out.result) == geryon.tools.MAX_HTTP_BYTES


@pytest.mark.parametrize("url", ["file:///etc/passwd", "ftp://example.com/x", "/etc/passwd", "gopher://example.com"])
def test_http_get_refuses_other_schemes(url):
    out = call(http_get_tool, url)
    assert not out.success and "http" in out.error.lower()
    assert out.result == ""


def test_write_then_read_a_file(tmp_path):
    path = tmp_path / "sub" / "note.txt"
    assert call(write_file_tool, f"{path}|hello there").success
    out = call(read_file_tool, str(path))
    assert out.success and out.result == "hello there"


def test_write_file_needs_a_path_and_content():
    out = call(write_file_tool, "no-separator")
    assert not out.success and "path|content" in out.error
