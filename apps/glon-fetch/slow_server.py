# apps/glon-fetch/slow_server.py -- test fixture: a deliberately slow download.
# Used only by tests to prove real cancellation. Not part of the application.
import http.server
import socketserver
import sys
import time

TOTAL = 8 * 1024 * 1024


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200)
        self.send_header("Content-Length", str(TOTAL))
        self.end_headers()
        chunk = b"x" * 65536
        sent = 0
        try:
            while sent < TOTAL:
                self.wfile.write(chunk)
                self.wfile.flush()
                sent += len(chunk)
                time.sleep(0.05)
        except Exception:
            pass

    def log_message(self, *args):
        pass


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


port = int(sys.argv[1]) if len(sys.argv) > 1 else 8798
with Server(("127.0.0.1", port), Handler) as srv:
    srv.serve_forever()
