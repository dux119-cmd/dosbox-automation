# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#
"""scripts/clients/dosboxctl against a live engine: every route, the token
lookup, the exit codes, and text that must reach the engine unchanged."""

import base64
import json
import os
import secrets
import shutil
import subprocess
import time
from pathlib import Path

import pytest
from conftest import WORKSPACE, start_dosbox_instance
from test_archive_mount import floppy_bytes

# DOSBOXCTL points the tests at another copy, e.g. the one in a release package.
DOSBOXCTL = Path(os.environ.get(
    "DOSBOXCTL",
    Path(__file__).resolve().parents[2] / "scripts" / "clients" / "dosboxctl"))

pytestmark = pytest.mark.skipif(shutil.which("bash") is None or shutil.which("curl") is None,
                                reason="dosboxctl needs bash and curl")


class Engine:
    def __init__(self, inst):
        self.inst = inst
        self.client = inst.client
        self.url = inst.client.base_url
        self.token = inst.client.session.headers["Authorization"].removeprefix("Bearer ")


def clean_env(home: Path) -> dict[str, str]:
    """No DOSBOX_* from the caller, and a HOME without a token file."""
    env = {k: v for k, v in os.environ.items() if not k.startswith("DOSBOX_")}
    env["HOME"] = str(home)
    env["XDG_CONFIG_HOME"] = str(home / ".config")
    return env


def ctl(engine: Engine | None, *args: str, env: dict[str, str] | None = None,
        home: Path | None = None, timeout: float = 60) -> subprocess.CompletedProcess:
    if env is None:
        if home is None:
            raise ValueError("ctl needs env or home")
        env = clean_env(home)
        if engine is not None:
            env["DOSBOX_API_URL"] = engine.url
            env["DOSBOX_API_TOKEN"] = engine.token
    return subprocess.run(["bash", str(DOSBOXCTL), *args], env=env, capture_output=True,
                          text=True, timeout=timeout)


def ok(proc: subprocess.CompletedProcess) -> str:
    assert proc.returncode == 0, (
        f"exit {proc.returncode}\nstdout:\n{proc.stdout}\nstderr:\n{proc.stderr}")
    return proc.stdout


def screen(engine: Engine) -> str:
    r = engine.client.screen_text()
    assert r.status_code == 200, r.text
    return r.json()["text"]


def wait_screen(engine: Engine, needle: str, timeout: float = 15) -> str:
    deadline = time.monotonic() + timeout
    text = ""
    while time.monotonic() < deadline:
        text = screen(engine)
        if needle in text:
            return text
        time.sleep(0.2)
    raise AssertionError(f"{needle!r} not on screen after {timeout}s:\n{text}")


def wait_input_idle(engine: Engine, timeout: float = 15) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if engine.client.input_sequence([]).status_code == 200:
            return
        time.sleep(0.1)
    raise AssertionError("input still pending")


def mem(engine: Engine, addr: int, length: int) -> bytes:
    r = engine.client.memory_read(addr, length)
    assert r.status_code == 200, r.text
    return r.content


def hexdump_bytes(out: str) -> bytes:
    """The bytes of dosboxctl's `ADDR: bb bb ...` lines."""
    data = bytearray()
    for line in out.splitlines():
        if not line.strip():
            continue
        _, _, hexpart = line.partition(":")
        data.extend(bytes.fromhex(hexpart))
    return bytes(data)


@pytest.fixture(scope="module")
def engine():
    work = WORKSPACE / f"dosboxctl-{secrets.token_hex(4)}"
    inst = start_dosbox_instance(work, settings={"keyboard_layout": "us"})
    try:
        inst.client.wait_shell(timeout=15)
        yield Engine(inst)
    finally:
        inst.shutdown()
        shutil.rmtree(work, ignore_errors=True)


@pytest.fixture
def home(tmp_path):
    h = tmp_path / "home"
    h.mkdir()
    return h


# -- The request core --

def test_status_prints_fields(engine, home):
    out = ok(ctl(engine, "status", home=home))
    assert "is_shell: true" in out.splitlines(), out


def test_json_flag_prints_the_api_answer(engine, home):
    out = ok(ctl(engine, "--json", "status", home=home))
    assert json.loads(out)["is_shell"] is True


def test_wrong_token_exits_1_and_names_401(engine, home):
    env = clean_env(home)
    env["DOSBOX_API_URL"] = engine.url
    env["DOSBOX_API_TOKEN"] = "0" * 64
    proc = ctl(None, "status", env=env)
    assert proc.returncode == 1, proc
    assert "401" in proc.stderr, proc.stderr


def test_api_error_text_reaches_stderr(engine, home):
    proc = ctl(engine, "reg", "set", "zz", "1", home=home)
    assert proc.returncode == 1, proc
    assert "400" in proc.stderr and "Unknown register: zz" in proc.stderr, proc.stderr


def test_video_start_while_recording_exits_1_with_409(engine, home):
    ok(ctl(engine, "video", "start", "raw", home=home))
    try:
        proc = ctl(engine, "video", "start", "rendered", home=home)
        assert proc.returncode == 1, proc
        assert "409" in proc.stderr and "Already capturing video" in proc.stderr, proc.stderr
        assert "rendered" not in proc.stdout, proc.stdout
    finally:
        ok(ctl(engine, "video", "stop", home=home))


@pytest.mark.parametrize("token", ["abc", "0" * 63 + "\nX-Injected: 1", "0" * 64 + "x"])
def test_malformed_token_is_refused_before_any_request(engine, home, token):
    env = clean_env(home)
    env["DOSBOX_API_URL"] = engine.url
    env["DOSBOX_API_TOKEN"] = token
    proc = ctl(None, "status", env=env)
    assert proc.returncode == 1, proc
    assert "64 hex characters" in proc.stderr, proc.stderr


def test_no_token_anywhere_exits_1(engine, home):
    env = clean_env(home)
    env["DOSBOX_API_URL"] = engine.url
    proc = ctl(None, "status", env=env)
    assert proc.returncode == 1, proc
    assert "DOSBOX_API_TOKEN" in proc.stderr, proc.stderr


def test_token_from_dosbox_token_file(engine, home):
    token_file = home / "my token"
    token_file.write_text(engine.token + "\n", encoding="utf-8")
    env = clean_env(home)
    env["DOSBOX_API_URL"] = engine.url
    env["DOSBOX_TOKEN_FILE"] = str(token_file)
    ok(ctl(None, "status", env=env))


def test_token_from_the_config_dir_file(engine, home):
    token_file = home / ".config" / "dosbox-automation" / "webserver" / "api_token"
    token_file.parent.mkdir(parents=True)
    token_file.write_text(engine.token, encoding="utf-8")
    env = clean_env(home)
    env["DOSBOX_API_URL"] = engine.url
    ok(ctl(None, "status", env=env))


def test_port_alone_builds_the_url(engine, home):
    env = clean_env(home)
    env["DOSBOX_PORT"] = engine.url.rsplit(":", 1)[1]
    env["DOSBOX_API_TOKEN"] = engine.token
    ok(ctl(None, "status", env=env))


def test_non_loopback_url_is_refused(home):
    env = clean_env(home)
    env["DOSBOX_API_URL"] = "http://192.0.2.1:8386"
    env["DOSBOX_API_TOKEN"] = "0" * 64
    proc = ctl(None, "status", env=env, timeout=20)
    assert proc.returncode == 1, proc
    assert "loopback" in proc.stderr, proc.stderr


def test_unknown_command_exits_2_with_usage(home):
    proc = ctl(None, "frobnicate", home=home)
    assert proc.returncode == 2, proc
    assert "Usage:" in proc.stderr, proc.stderr


def test_no_command_exits_2(home):
    proc = ctl(None, home=home)
    assert proc.returncode == 2, proc


def test_help_exits_0_and_lists_every_command(home):
    out = ok(ctl(None, "--help", home=home))
    for word in ["status", "info", "program", "shutdown", "screen", "screen-info", "frame",
                 "type", "key", "keys", "mouse", "wait", "mem", "freeze", "regs", "reg",
                 "cycles", "port", "dos-internals", "swap", "mount-lock", "run", "file",
                 "script-status", "script-stop", "video", "audio", "input-record", "mixer"]:
        assert f"  {word} " in out or f"  {word}\n" in out, f"{word} missing from help"


# -- Session and screen --

@pytest.mark.parametrize("command,route", [
    (["info"], "/api/v1/dosbox/info"),
    (["program"], "/api/v1/program/state"),
    (["screen-info"], "/api/v1/video/frame/info"),
    (["dos-internals"], "/api/v1/dos/internals"),
    (["regs"], "/api/v1/cpu/state"),
    (["cycles"], "/api/v1/cpu/cycles"),
    (["mount-lock", "status"], "/api/v1/mount/lock"),
    (["freeze", "list"], "/api/v1/memory/freeze"),
    (["script-status"], "/api/v1/script/status"),
    (["video", "status"], "/api/v1/capture/video/status"),
    (["video", "compression"], "/api/v1/capture/video/compression"),
    (["audio", "status"], "/api/v1/capture/audio/status"),
    (["input-record", "status"], "/api/v1/input/record/status"),
    (["mixer"], "/api/v1/mixer"),
])
def test_read_command_json_matches_the_route(engine, home, command, route):
    out = ok(ctl(engine, "--json", *command, home=home))
    got = json.loads(out)
    want = engine.client._get(route).json()
    assert sorted(got) == sorted(want), f"{command}: {sorted(got)} vs {sorted(want)}"


def test_regs_prints_register_names(engine, home):
    out = ok(ctl(engine, "regs", home=home))
    for name in ["eax", "ebx", "cs", "ds", "eip", "flags"]:
        assert any(line.startswith(f"{name}:") for line in out.splitlines()), out


def test_screen_prints_the_prompt(engine, home):
    out = ok(ctl(engine, "screen", home=home))
    assert "Z:\\>" in out, out


def test_frame_writes_a_png(engine, home, tmp_path):
    target = tmp_path / "shot one.png"
    ok(ctl(engine, "frame", str(target), "--format", "png", home=home))
    assert target.read_bytes()[:8] == b"\x89PNG\r\n\x1a\n"


def test_frame_jpeg_with_quality(engine, home, tmp_path):
    target = tmp_path / "shot.jpg"
    ok(ctl(engine, "frame", str(target), "--format", "jpeg", "--quality", "50", home=home))
    assert target.read_bytes()[:3] == b"\xff\xd8\xff"


# -- Input --

def test_type_sends_quotes_and_backslashes_unchanged(engine, home):
    wait_input_idle(engine)
    ok(ctl(engine, "type", 'echo "a\\b"', home=home))
    wait_screen(engine, '\n"a\\b"')


def test_type_no_enter_leaves_the_line_unsubmitted(engine, home):
    wait_input_idle(engine)
    ok(ctl(engine, "type", "--no-enter", "cls", home=home))
    wait_input_idle(engine)
    time.sleep(0.5)
    lines = [ln.rstrip() for ln in screen(engine).split("\n") if ln.strip()]
    assert lines[-1].endswith(">cls"), lines[-3:]
    ok(ctl(engine, "key", "esc", home=home))


def test_type_waits_for_pending_input(engine, home):
    wait_input_idle(engine)
    ok(ctl(engine, "type", "rem one", home=home))
    ok(ctl(engine, "type", "rem two", home=home))
    wait_screen(engine, "rem two")


def test_key_unknown_name_is_an_api_error(engine, home):
    proc = ctl(engine, "key", "nosuchkey", home=home)
    assert proc.returncode == 1, proc
    assert "Unknown key: KBD_nosuchkey" in proc.stderr, proc.stderr


def test_key_with_count_and_keys_sequence(engine, home):
    wait_input_idle(engine)
    ok(ctl(engine, "key", "esc", "2", home=home))
    ok(ctl(engine, "keys", "esc", "esc", home=home))


def typed_run_of(engine: Engine, char: str) -> int:
    """Length of the run of `char` at the end of the screen's input line."""
    text = screen(engine).replace("\n", "").rstrip()
    return len(text) - len(text.rstrip(char))


def clear_line(engine: Engine, home) -> None:
    ok(ctl(engine, "key", "esc", home=home))
    time.sleep(0.3)


def test_keys_hold_repeats_like_a_held_key(engine, home):
    clear_line(engine, home)
    start = time.monotonic()
    ok(ctl(engine, "keys", "a:1500", home=home))
    assert time.monotonic() - start >= 1.5, "keys must return after the release"
    time.sleep(0.3)
    held = typed_run_of(engine, "a")
    clear_line(engine, home)
    ok(ctl(engine, "keys", "a", home=home))
    time.sleep(0.3)
    tapped = typed_run_of(engine, "a")
    clear_line(engine, home)
    assert tapped == 1, f"a tap typed {tapped}"
    assert held >= 10, f"a 1.5 s hold typed only {held}"


def test_key_hold_option(engine, home):
    clear_line(engine, home)
    ok(ctl(engine, "key", "b", "--hold", "1500", home=home))
    time.sleep(0.3)
    held = typed_run_of(engine, "b")
    clear_line(engine, home)
    assert held >= 10, f"a 1.5 s hold typed only {held}"


def test_keys_mixed_taps_and_holds_in_order(engine, home):
    clear_line(engine, home)
    out = ok(ctl(engine, "--json", "keys", "x", "y:1200", "z", home=home))
    assert json.loads(out)["events_scheduled"] == 6
    time.sleep(0.3)
    text = screen(engine).replace("\n", "").rstrip()
    clear_line(engine, home)
    assert text.endswith("z") and "xy" in text, text[-60:]
    assert text.count("y") >= 8, text[-60:]


@pytest.mark.parametrize("args", [["keys", "a:abc"], ["keys", "a:0"], ["keys", "a:-5"],
                                  ["keys", "a:60001"], ["key", "a", "--hold", "x"],
                                  ["key", "a", "--hold"]])
def test_bad_hold_is_usage(engine, home, args):
    proc = ctl(engine, *args, home=home)
    assert proc.returncode == 2, proc


def test_help_puts_the_environment_first(home):
    out = ok(ctl(None, "--help", home=home))
    lines = out.splitlines()
    env = next(i for i, ln in enumerate(lines) if ln.startswith("Environment:"))
    session = next(i for i, ln in enumerate(lines) if ln.startswith("Session:"))
    assert env < session, "the environment block must come before the commands"
    assert env < 15, f"the environment block starts at line {env}"


def test_mouse_move_and_click(engine, home):
    wait_input_idle(engine)
    ok(ctl(engine, "mouse", "move", "5", "-3", home=home))
    ok(ctl(engine, "mouse", "click", "left", home=home))


def test_mouse_bad_button_is_usage(engine, home):
    proc = ctl(engine, "mouse", "click", "fourth", home=home)
    assert proc.returncode == 2, proc


def test_wait_finds_text_without_a_script(engine, home):
    before = engine.client.script_status().json()
    ok(ctl(engine, "wait", "Cheat Workbench", "5", home=home))
    ok(ctl(engine, "wait", "Z:\\>", "5", home=home))
    after = engine.client.script_status().json()
    assert after == before, "wait must not touch the Lua script slot"


def test_wait_keeps_trying_while_the_engine_is_not_up(home):
    from conftest import find_free_port
    env = clean_env(home)
    env["DOSBOX_PORT"] = str(find_free_port())
    env["DOSBOX_API_TOKEN"] = "0" * 64
    start = time.monotonic()
    proc = ctl(None, "wait", "C:\\>", "3", env=env)
    elapsed = time.monotonic() - start
    assert proc.returncode == 1, proc
    assert elapsed >= 2.5, f"wait gave up after {elapsed:.1f} s"
    assert "cannot reach" in proc.stderr, proc.stderr


def test_wait_finds_the_prompt_of_an_engine_that_starts_later(tmp_path, home):
    from conftest import DOSBOX_BIN, find_free_port
    port = find_free_port()
    token = secrets.token_hex(32)
    env = clean_env(home)
    env["DOSBOX_PORT"] = str(port)
    env["DOSBOX_API_TOKEN"] = token
    waiter = subprocess.Popen(["bash", str(DOSBOXCTL), "wait", "Z:\\>", "40"], env=env,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    time.sleep(2)
    work = tmp_path / "late"
    work.mkdir()
    engine_env = {**clean_env(work), "DOSBOX_API_TOKEN": token,
                  "SDL_VIDEODRIVER": "offscreen", "SDL_AUDIODRIVER": "dummy"}
    late = subprocess.Popen([DOSBOX_BIN, "-noprimaryconf", "-nolocalconf",
                             "--set", "webserver_enabled=true", "--set", f"webserver_port={port}",
                             "--set", "joysticktype=none"],
                            env=engine_env, cwd=work, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    try:
        _, err = waiter.communicate(timeout=60)
        assert waiter.returncode == 0, err
    finally:
        env_ctl = clean_env(home)
        env_ctl.update(DOSBOX_PORT=str(port), DOSBOX_API_TOKEN=token)
        ctl(None, "shutdown", env=env_ctl)
        try:
            late.wait(timeout=15)
        except subprocess.TimeoutExpired:
            late.kill()
            late.wait(timeout=5)


def test_wait_outlasts_a_busy_engine(home):
    """An engine held by a long MOUNT answers 503 with Retry-After for longer
    than one request's own retry window; the wait's limit decides."""
    import http.server
    import threading
    busy_until = time.monotonic() + 12

    class Busy(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path == "/api/v1/video/text" and time.monotonic() < busy_until:
                body, code = b'{"error": "Emulation busy, retry later"}', 503
            elif self.path == "/api/v1/video/text":
                body, code = b'{"text": "C:\\\\>"}', 200
            else:
                body, code = b"ok", 200
            self.send_response(code)
            if code == 503:
                self.send_header("Retry-After", "1")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, *args):
            pass

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Busy)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        env = clean_env(home)
        env["DOSBOX_API_URL"] = f"http://127.0.0.1:{server.server_address[1]}"
        env["DOSBOX_API_TOKEN"] = "0" * 64
        proc = ctl(None, "wait", "C:\\>", "25", env=env)
        assert proc.returncode == 0, proc
    finally:
        server.shutdown()


def test_wait_waits_for_a_token_file_that_is_not_there_yet(home):
    from conftest import find_free_port
    env = clean_env(home)
    env["DOSBOX_PORT"] = str(find_free_port())
    env["DOSBOX_TOKEN_FILE"] = str(home / "not-yet" / "api_token")
    start = time.monotonic()
    proc = ctl(None, "wait", "C:\\>", "3", env=env)
    elapsed = time.monotonic() - start
    assert proc.returncode == 1, proc
    assert elapsed >= 2.5, f"wait gave up after {elapsed:.1f} s"
    assert "api_token" in proc.stderr, proc.stderr


def test_wait_reads_the_token_file_of_an_engine_that_starts_later(tmp_path, home):
    from conftest import DOSBOX_BIN, find_free_port
    port = find_free_port()
    work = tmp_path / "late"
    work.mkdir()
    env = clean_env(home)
    env["DOSBOX_PORT"] = str(port)
    env["XDG_CONFIG_HOME"] = str(work / ".config")
    waiter = subprocess.Popen(["bash", str(DOSBOXCTL), "wait", "Z:\\>", "40"], env=env,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    time.sleep(2)
    engine_env = {**clean_env(work), "SDL_VIDEODRIVER": "offscreen", "SDL_AUDIODRIVER": "dummy"}
    late = subprocess.Popen([DOSBOX_BIN, "-noprimaryconf", "-nolocalconf",
                             "--set", "webserver_enabled=true", "--set", f"webserver_port={port}",
                             "--set", "webserver_token_file=true", "--set", "joysticktype=none"],
                            env=engine_env, cwd=work, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    try:
        _, err = waiter.communicate(timeout=60)
        assert waiter.returncode == 0, err
    finally:
        ctl(None, "shutdown", env=env)
        try:
            late.wait(timeout=15)
        except subprocess.TimeoutExpired:
            late.kill()
            late.wait(timeout=5)


def test_wait_times_out_with_exit_1(engine, home):
    start = time.monotonic()
    proc = ctl(engine, "wait", "NEVER ON SCREEN", "1", home=home)
    assert proc.returncode == 1, proc
    assert time.monotonic() - start < 15


# -- Memory, registers, ports --

def test_mem_read_segment_offset_prints_hex(engine, home):
    out = ok(ctl(engine, "mem", "read", "0x40:0x10", "8", home=home))
    assert out.splitlines()[0].startswith("00000410:"), out
    assert hexdump_bytes(out) == mem(engine, 0x410, 8)


def test_mem_read_json(engine, home):
    out = ok(ctl(engine, "--json", "mem", "read", "0x410", "4", home=home))
    got = json.loads(out)
    assert got["memory"]["addr"] == 0x410
    assert base64.b64decode(got["memory"]["data"]) == mem(engine, 0x410, 4)


def test_mem_write_then_read_back(engine, home):
    ok(ctl(engine, "mem", "write", "0x50:0x0", "01", "02", home=home))
    assert mem(engine, 0x500, 2) == b"\x01\x02"
    ok(ctl(engine, "mem", "write", "0x500", "a1b2c3", home=home))
    assert mem(engine, 0x500, 3) == b"\xa1\xb2\xc3"


def test_mem_write_odd_hex_is_usage(engine, home):
    proc = ctl(engine, "mem", "write", "0x500", "abc", home=home)
    assert proc.returncode == 2, proc


def test_mem_search_finds_a_written_word(engine, home):
    ok(ctl(engine, "mem", "write", "0x500", "abcd", home=home))
    out = ok(ctl(engine, "mem", "search", "0xcdab", "0x500", "0x510", "--width", "2",
                 home=home))
    assert "00000500" in out.split(), out


def test_mem_alloc_and_free(engine, home):
    out = ok(ctl(engine, "--json", "mem", "alloc", "16", home=home))
    addr = json.loads(out)["addr"]
    ok(ctl(engine, "mem", "free", hex(addr), home=home))


def test_freeze_add_list_clear(engine, home):
    ok(ctl(engine, "freeze", "add", "0x50:0x8", "0x41", home=home))
    listed = json.loads(ok(ctl(engine, "--json", "freeze", "list", home=home)))
    assert {"address": 0x508, "value": 0x41, "width": 1} in listed["freezes"]
    ok(ctl(engine, "freeze", "clear", "0x508", home=home))
    listed = json.loads(ok(ctl(engine, "--json", "freeze", "list", home=home)))
    assert all(f["address"] != 0x508 for f in listed["freezes"])


def test_zero_padded_offset_is_decimal_like_mem_read(engine, home):
    ok(ctl(engine, "freeze", "add", "0x50:0010", "0x41", home=home))
    listed = json.loads(ok(ctl(engine, "--json", "freeze", "list", home=home)))
    assert {"address": 0x50A, "value": 0x41, "width": 1} in listed["freezes"], listed
    ok(ctl(engine, "freeze", "clear", "0x50:0010", home=home))
    listed = json.loads(ok(ctl(engine, "--json", "freeze", "list", home=home)))
    assert all(f["address"] != 0x50A for f in listed["freezes"]), listed


def test_zero_padded_value_is_decimal(engine, home):
    ok(ctl(engine, "cycles", "05000", home=home))
    assert engine.client.cpu_cycles().json()["real_mode"] == 5000


def test_bad_address_is_usage_and_sends_nothing(engine, home):
    ok(ctl(engine, "freeze", "add", "0x50:0x8", "0x41", home=home))
    for args in (("freeze", "clear", "0x5O8"), ("freeze", "add", "zz", "1"),
                 ("mem", "free", "zz")):
        proc = ctl(engine, *args, home=home)
        assert proc.returncode == 2, (args, proc)
        assert "Traceback" not in proc.stderr, (args, proc.stderr)
    listed = json.loads(ok(ctl(engine, "--json", "freeze", "list", home=home)))
    assert {"address": 0x508, "value": 0x41, "width": 1} in listed["freezes"], listed
    ok(ctl(engine, "freeze", "clear", "0x508", home=home))


def test_bad_key_name_is_usage(engine, home):
    proc = ctl(engine, "keys", "bad-key!", home=home)
    assert proc.returncode == 2, proc


def test_reg_set_then_regs(engine, home):
    ok(ctl(engine, "reg", "set", "gs", "0x1234", home=home))
    assert engine.client.cpu_state().json()["registers"]["gs"] == 0x1234


def test_cycles_set(engine, home):
    ok(ctl(engine, "cycles", "5000", home=home))
    assert engine.client.cpu_cycles().json()["real_mode"] == 5000


def test_port_read_and_write(engine, home):
    out = ok(ctl(engine, "port", "read", "0x60", home=home))
    assert "value:" in out, out
    ok(ctl(engine, "port", "write", "0x80", "0x12", home=home))


# -- Scripts --

def test_run_and_script_status(engine, home):
    ok(ctl(engine, "run", 'dosbox.output["said"] = "it\'s \\"quoted\\""', home=home))
    deadline = time.monotonic() + 10
    out = ""
    while time.monotonic() < deadline:
        out = ok(ctl(engine, "--json", "script-status", home=home))
        if json.loads(out).get("output"):
            break
        time.sleep(0.2)
    assert json.loads(out)["output"]["said"] == 'it\'s "quoted"', out


# -- Capture --

def test_video_start_stop(engine, home):
    ok(ctl(engine, "video", "start", "raw", home=home))
    assert engine.client._get("/api/v1/capture/video/status").json()["capturing"] is True
    ok(ctl(engine, "video", "stop", home=home))


def test_record_aliases_still_work(engine, home):
    ok(ctl(engine, "record-start", home=home))
    ok(ctl(engine, "record-status", home=home))
    ok(ctl(engine, "record-stop", home=home))


def test_video_compression_set(engine, home):
    ok(ctl(engine, "video", "compression", "raw", "7", "rendered", "5", home=home))
    got = engine.client.capture_compression().json()
    assert got == {"raw": 7, "rendered": 5}


def test_audio_start_stop_to_a_name(engine, home):
    name = f"out file {secrets.token_hex(4)}.wav"
    out = ok(ctl(engine, "--json", "audio", "start", name, home=home))
    target = Path(json.loads(out)["path"])
    assert target.name == name, target
    time.sleep(0.5)
    ok(ctl(engine, "audio", "stop", home=home))
    assert target.read_bytes()[:4] == b"RIFF"
    target.unlink()


def test_input_record_cycle(engine, home):
    ok(ctl(engine, "input-record", "start", home=home))
    ok(ctl(engine, "input-record", "pause", home=home))
    ok(ctl(engine, "input-record", "stop", home=home))


def test_mixer_volume_mute_channel(engine, home):
    ok(ctl(engine, "mixer", "volume", "0.5", "0.25", home=home))
    master = engine.client._get("/api/v1/mixer").json()["master_volume"]
    assert master == {"left": 0.5, "right": 0.25}, master
    ok(ctl(engine, "mixer", "mute", home=home))
    assert engine.client._get("/api/v1/mixer").json()["muted"] is True
    ok(ctl(engine, "mixer", "unmute", home=home))
    ok(ctl(engine, "mixer", "channel", "OPL", "0.5", "0.5", home=home))
    ok(ctl(engine, "mixer", "volume", "1", "1", home=home))


# -- Drives and shutdown, on an engine of their own --

def test_swap_sends_the_path_unchanged(dosbox_e2e, tmp_path):
    game = tmp_path / "game"
    game.mkdir()
    (game / "one.img").write_bytes(floppy_bytes("CTLONE"))
    name = 'disk "two" b.img' if os.name != "nt" else "disk 'two' b.img"
    (game / name).write_bytes(floppy_bytes("CTLTWO"))
    inst = dosbox_e2e(autoexec_lines=[f"mount a {game / 'one.img'}"], conf_dir=game,
                      allowed_image_roots=[game], settings={"keyboard_layout": "us"})
    eng = Engine(inst)
    eng.client.wait_autoexec_done()
    home = tmp_path / "home"
    home.mkdir()
    ok(ctl(eng, "swap", "a", str(game / name), home=home))
    ok(ctl(eng, "type", "vol a:", home=home))
    wait_screen(eng, "CTLTWO")


def test_mount_lock_then_shutdown(dosbox_e2e, tmp_path):
    inst = dosbox_e2e()
    eng = Engine(inst)
    home = tmp_path / "home"
    home.mkdir()
    ok(ctl(eng, "mount-lock", home=home))
    assert inst.client.mount_lock_status().json()["locked"] is True
    ok(ctl(eng, "shutdown", home=home))
    inst.proc.wait(timeout=15)
