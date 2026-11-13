# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#
"""DosboxClient resends a request the engine answered busy (503 with
Retry-After, ada-n0vh): a busy command never ran. A server on a free port
stands in for the engine."""

import json
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import pytest
from dosbox_client import DosboxClient


class FakeEngine:
    """Answers are (status, headers) or (status, headers, delay_s, body)."""

    def __init__(self, answers, default=(200, {}, 0.0, None)):
        self.answers = list(answers)
        self.bodies = []
        engine = self

        class Handler(BaseHTTPRequestHandler):
            def _answer(self):
                length = int(self.headers.get("Content-Length", 0))
                engine.bodies.append(self.rfile.read(length))
                answer = engine.answers.pop(0) if engine.answers else default
                status, headers, delay_s, payload = (*answer, 0.0, None)[:4]
                time.sleep(delay_s)
                if payload is None:
                    payload = {"error": "x"} if status != 200 else {}
                body = json.dumps(payload).encode()
                self.send_response(status)
                for k, v in headers.items():
                    self.send_header(k, v)
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

            do_GET = _answer
            do_POST = _answer
            do_PUT = _answer

            def log_message(self, *args):
                pass

        self.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        threading.Thread(target=self.server.serve_forever, daemon=True).start()
        self.url = f"http://127.0.0.1:{self.server.server_address[1]}"

    def close(self):
        self.server.shutdown()
        self.server.server_close()


@pytest.fixture
def engine_with():
    started = []

    def _start(answers, **kwargs):
        e = FakeEngine(answers, **kwargs)
        started.append(e)
        return e

    yield _start
    for e in started:
        e.close()


BUSY = (503, {"Retry-After": "0"})


def test_a_busy_answer_is_sent_again_with_its_body(engine_with):
    engine = engine_with([BUSY, BUSY])
    r = DosboxClient(engine.url).input_sequence([{"type": "key", "key": "KBD_a"}])
    assert r.status_code == 200
    assert len(engine.bodies) == 3
    assert engine.bodies[0] == engine.bodies[2]


def test_a_503_without_retry_after_is_not_busy(engine_with):
    engine = engine_with([(503, {})])
    r = DosboxClient(engine.url).screen_text()
    assert r.status_code == 503
    assert len(engine.bodies) == 1


def test_busy_past_the_deadline_comes_back(engine_with):
    engine = engine_with([BUSY] * 1000)
    client = DosboxClient(engine.url)
    client.busy_deadline_s = 0.3
    r = client.status()
    assert r.status_code == 503
    assert 1 < len(engine.bodies) < 1000


# A wait's own deadline also ends the resending of busy answers inside it.
def test_a_wait_stops_resending_busy_at_its_own_deadline(engine_with):
    engine = engine_with([], default=BUSY)
    client = DosboxClient(engine.url)
    start = time.monotonic()
    with pytest.raises(TimeoutError):
        client.wait_ready(timeout=0.5)
    assert time.monotonic() - start < 2.0


SLOW = (200, {}, 0.5, {})


@pytest.mark.parametrize("wait", ["wait_ready", "wait_autoexec_done"])
def test_a_read_timeout_inside_a_wait_is_tried_again(engine_with, wait):
    engine = engine_with([SLOW], default=(200, {}, 0.0, {"text": "C:\\>\n"}))
    client = DosboxClient(engine.url, timeout=0.2)
    getattr(client, wait)(timeout=5)
    assert len(engine.bodies) > 1
