"""Offline integration through real HTTP, CLI, configuration, and CSV output."""
import http.server
import json
import pathlib
import subprocess
import sys
import tempfile
import threading
import time
import unittest
BINARY = str(pathlib.Path(sys.argv.pop(1)).resolve())
HISTORY = str(pathlib.Path(sys.argv.pop(1)).resolve())
class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args): pass
    def do_GET(self):
        self.server.calls += 1
        if self.headers.get('APCA-API-KEY-ID') != 'test-key' or self.headers.get('APCA-API-SECRET-KEY') != 'test-secret':
            self.send_error(401)
            return
        if self.server.mode == 'timeout': time.sleep(0.2)
        code = 429 if self.server.mode == 'retry' and self.server.calls == 1 else 200
        if self.server.mode == 'auth': code = 401
        if self.server.mode == 'rate': code = 429
        if self.server.mode == 'redirect': code = 302
        self.send_response(code)
        self.send_header('Retry-After', '0')
        if self.server.mode == 'redirect': self.send_header('Location', '/another-endpoint')
        self.end_headers()
        body = {'symbol':'AAPL','quote':{'bp':100,'ap':102,'t':'2026-09-25T14:00:00Z'}}
        if '/bars?' in self.path:
            from urllib.parse import parse_qs, urlparse
            query = parse_qs(urlparse(self.path).query)
            self.server.queries.append(query)
            body = {'symbol':'AAPL', 'bars':[{'t':'2026-09-24T04:00:00Z','o':99,'h':103,'l':98,'c':101,'v':500}],
                    'next_page_token': None if 'page_token' in query else 'next+page/='}
        try: self.wfile.write(b'broken' if self.server.mode == 'malformed' else json.dumps(body).encode())
        except (BrokenPipeError, ConnectionResetError): pass
class Integration(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.path = pathlib.Path(self.tmp.name)
        self.server = http.server.ThreadingHTTPServer(('127.0.0.1',0), Handler)
        self.server.calls = 0
        self.server.mode = 'ok'
        self.server.queries = []
        self.thread = threading.Thread(target=self.server.serve_forever,daemon=True)
        self.thread.start()
    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join()
        self.tmp.cleanup()
    def run_cli(self, source='alpaca', port=None, fmp=False, filters=''):
        port = port or self.server.server_port
        (self.path/'input.csv').write_text('ticker,name,exchange,sector,spot_price,pe,forward_pe,pb,ps,ev_ebitda,roa,roe,roic\nAAPL,Apple,NASDAQ,Technology,55,,,,,,,,\n')
        (self.path/'screener.ini').write_text(
            f'source = {source}\ninput = input.csv\n[alpaca]\n'
            f'base_url = http://127.0.0.1:{port}\napi_key = test-key\nsecret_key = test-secret\n'
            'symbols = AAPL\ntimeout_ms = 50\n' +
            ('[fmp]\napi_key = unused\n' if fmp else '') +
            '[screen:live]\noutput = result.csv\n' + filters)
        return subprocess.run([BINARY],cwd=self.path,capture_output=True,text=True,timeout=15)
    def test_history_pagination_and_encoding(self):
        r = subprocess.run([HISTORY, f'http://127.0.0.1:{self.server.server_port}'], capture_output=True, text=True, timeout=15)
        self.assertEqual(r.returncode,0,r.stderr)
        self.assertEqual(r.stdout,'2')
        self.assertEqual(self.server.queries[1]['page_token'],['next+page/='])
        self.assertEqual(self.server.queries[0]['timeframe'],['1Day'])
    def test_live_and_retry(self):
        self.server.mode = 'retry'
        r = self.run_cli()
        self.assertEqual(r.returncode,0,r.stderr)
        self.assertEqual(self.server.calls,2)
        self.assertIn('101',(self.path/'result.csv').read_text())
    def test_csv_never_calls_api(self):
        r = self.run_cli('csv', fmp=True)
        self.assertEqual(r.returncode,0,r.stderr)
        self.assertEqual(self.server.calls,0)
        self.assertIn('55',(self.path/'result.csv').read_text())
    def test_auth(self):
        self.server.mode = 'auth'
        r = self.run_cli()
        self.assertNotEqual(r.returncode,0)
        self.assertEqual(self.server.calls,1)
        self.assertNotIn('test-secret',r.stderr)
    def test_malformed(self):
        self.server.mode = 'malformed'
        self.assertNotEqual(self.run_cli().returncode,0)
        self.assertFalse((self.path/'result.csv').exists())
    def test_timeout(self):
        self.server.mode = 'timeout'
        r = self.run_cli()
        self.assertNotEqual(r.returncode,0)
        self.assertIn('timeout',r.stderr.lower())
    def test_persistent_rate_limit(self):
        self.server.mode = 'rate'
        r = self.run_cli()
        self.assertNotEqual(r.returncode,0)
        self.assertEqual(self.server.calls,3)
    def test_redirect_is_not_followed(self):
        self.server.mode = 'redirect'
        r = self.run_cli()
        self.assertNotEqual(r.returncode,0)
        self.assertEqual(self.server.calls,1)
    def test_missing_fundamentals_excluded(self):
        r = self.run_cli(filters='filter = PE:0:30\n')
        self.assertEqual(r.returncode,0,r.stderr)
        self.assertNotIn('AAPL',(self.path/'result.csv').read_text())
        self.assertIn('missing PE',r.stderr)
    def test_connection_failure(self):
        import socket
        with socket.socket() as sock:
            sock.bind(('127.0.0.1',0))
            r = self.run_cli(port=sock.getsockname()[1])
        self.assertNotEqual(r.returncode,0)
if __name__ == '__main__': unittest.main()



