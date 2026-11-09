"""
A small forwarding proxy between the agent frameworks and Ollama. Standard library only.

It does two jobs:

1. Times every model request, so "time waiting for the model" is measured the
   same way for every framework no matter which HTTP client it uses. Whatever
   is left of a task's time is the framework's own cost.
2. Pins the sampling and context settings (temperature 0, fixed seed, 4096
   token context, capped output) so every framework gets the same model
   behaviour instead of its own defaults.

Counters are read from GET /__stats.
"""

import http.client
import json
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse

MODEL_PATHS = ("/api/chat", "/api/generate", "/v1/chat/completions", "/v1/completions")

NATIVE_OPTIONS = {"temperature": 0, "seed": 0, "num_ctx": 4096, "num_predict": 400}
OPENAI_FIELDS = {"temperature": 0, "seed": 0, "max_tokens": 400}


class Stats:
    def __init__(self):
        self.lock = threading.Lock()
        self.model_seconds = 0.0
        self.calls = 0
        self.prompt_tokens = 0
        self.output_tokens = 0

    def add(self, seconds, prompt_tokens=0, output_tokens=0):
        with self.lock:
            self.model_seconds += seconds
            self.calls += 1
            self.prompt_tokens += prompt_tokens
            self.output_tokens += output_tokens

    def snapshot(self):
        with self.lock:
            return {
                "model_seconds": self.model_seconds, "calls": self.calls,
                "prompt_tokens": self.prompt_tokens, "output_tokens": self.output_tokens,
            }


def _pin(path, body):
    """Overwrite the sampling settings in a request body."""
    try:
        data = json.loads(body)
    except (ValueError, TypeError):
        return body
    if path.startswith("/api/"):
        opts = dict(data.get("options") or {})
        opts.update(NATIVE_OPTIONS)
        data["options"] = opts
    else:
        data.update(OPENAI_FIELDS)
    return json.dumps(data).encode()


def _token_counts(last_chunk):
    """Ollama reports token counts in its final message; the OpenAI route in 'usage'."""
    try:
        data = json.loads(last_chunk)
    except (ValueError, TypeError):
        return 0, 0
    if "eval_count" in data or "prompt_eval_count" in data:
        return data.get("prompt_eval_count", 0), data.get("eval_count", 0)
    usage = data.get("usage") or {}
    return usage.get("prompt_tokens", 0), usage.get("completion_tokens", 0)


def make_handler(upstream, stats):
    host, port = upstream

    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.0"      # one request per connection, body ends at close

        def log_message(self, *args):
            pass

        def _stats(self):
            data = json.dumps(stats.snapshot()).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def _forward(self):
            path = urlparse(self.path).path
            if path == "/__stats":
                return self._stats()
            length = int(self.headers.get("Content-Length") or 0)
            body = self.rfile.read(length) if length else b""
            is_model_call = self.command == "POST" and path in MODEL_PATHS
            if is_model_call:
                body = _pin(path, body)

            headers = {k: v for k, v in self.headers.items()
                       if k.lower() not in ("host", "content-length", "connection", "accept-encoding")}
            if body:
                headers["Content-Length"] = str(len(body))

            started = time.perf_counter()
            conn = http.client.HTTPConnection(host, port, timeout=900)
            last = b""
            try:
                conn.request(self.command, self.path, body=body or None, headers=headers)
                resp = conn.getresponse()
                self.send_response(resp.status)
                for k, v in resp.getheaders():
                    if k.lower() not in ("transfer-encoding", "content-length", "connection"):
                        self.send_header(k, v)
                self.send_header("Connection", "close")
                self.end_headers()
                while True:
                    chunk = resp.read(4096)
                    if not chunk:
                        break
                    last = (last + chunk)[-8192:]
                    self.wfile.write(chunk)
                    self.wfile.flush()
            except (BrokenPipeError, ConnectionResetError):
                pass
            finally:
                conn.close()
                if is_model_call:
                    tail = [ln for ln in last.decode("utf-8", "replace").splitlines() if ln.strip()]
                    if tail and tail[-1].startswith("data: "):
                        tail = [ln for ln in tail if ln.startswith("data: ") and "[DONE]" not in ln] or tail
                        tail[-1] = tail[-1][6:]
                    pt, ot = _token_counts(tail[-1]) if tail else (0, 0)
                    stats.add(time.perf_counter() - started, pt, ot)

        do_GET = do_POST = do_DELETE = _forward

    return Handler


def start(upstream=("127.0.0.1", 11434), port=0):
    """Start the proxy on a free port. Returns (server, stats, base_url)."""
    stats = Stats()
    server = ThreadingHTTPServer(("127.0.0.1", port), make_handler(upstream, stats))
    server.daemon_threads = True
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server, stats, f"http://127.0.0.1:{server.server_address[1]}"
