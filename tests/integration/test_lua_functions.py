# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#

"""Integration tests for individual dosbox.* Lua API functions.

Each test loads a script that exercises one function, writes results
to dosbox.output, and verifies via the status endpoint.
"""

import secrets
import time
from pathlib import Path

import pytest


# Helper: load, start, wait for completion, return output.
def run_script(dosbox, source, name="func-test", seed=None, timeout=10):
    time.sleep(2.1)  # rate limiter
    r = dosbox.script_load(source, name=name, seed=seed)
    assert r.status_code == 200, f"Load failed: {r.json()}"
    r = dosbox.script_start()
    assert r.status_code == 200, f"Start failed: {r.json()}"
    data = dosbox.wait_script_done(timeout=timeout)
    assert data["state"] == "completed", (
        f"Script failed: state={data['state']}, error={data.get('error', '')}"
    )
    return data.get("output", {})


def test_frame_returns_number(dosbox):
    out = run_script(dosbox, "dosbox.output.frame = dosbox.frame()\n")
    assert isinstance(out["frame"], int)
    assert out["frame"] >= 0


def test_wait_frames_advances(dosbox):
    out = run_script(dosbox, (
        "local before = dosbox.frame()\n"
        "dosbox.wait_frames(5)\n"
        "local after = dosbox.frame()\n"
        "dosbox.output.delta = after - before\n"
    ))
    assert out["delta"] >= 5


def test_log_does_not_error(dosbox):
    out = run_script(dosbox, (
        "dosbox.log('test message')\n"
        "dosbox.output.ok = 'yes'\n"
    ))
    assert out["ok"] == "yes"


def test_screen_text_returns_string(dosbox):
    out = run_script(dosbox, (
        "local t = dosbox.screen_text()\n"
        "dosbox.output.len = #t\n"
        "dosbox.output.ttype = type(t)\n"
    ))
    assert out["ttype"] == "string"
    assert out["len"] > 0


def test_screen_text_contains_typed_content(dosbox):
    # Type a unique marker at the DOS prompt and verify screen_text
    # returns it. Uses wait_for_text for deterministic timing.
    out = run_script(dosbox, (
        "dosbox.wait_frames(10)\n"
        "dosbox.type('echo MARKER7Q3X')\n"
        "dosbox.key('KBD_enter', true)\n"
        "dosbox.key('KBD_enter', false)\n"
        "dosbox.wait_for_text('MARKER7Q3X', 5000)\n"
        "local t = dosbox.screen_text()\n"
        "dosbox.output.has_marker = string.find(t, 'MARKER7Q3X') and 'yes' or 'no'\n"
    ), timeout=15)
    assert out["has_marker"] == "yes"


def test_type_all_digits_echo(dosbox):
    # End-to-end guard for the digit mapping. Every digit must reach the shell;
    # the old arithmetic from KBD_0 turned e.g. '7' into 'u'.
    out = run_script(dosbox, (
        "dosbox.wait_frames(10)\n"
        "dosbox.type('echo 0123456789')\n"
        "dosbox.key('KBD_enter', true)\n"
        "dosbox.key('KBD_enter', false)\n"
        "dosbox.wait_for_text('0123456789', 5000)\n"
        "local t = dosbox.screen_text()\n"
        "dosbox.output.found = string.find(t, '0123456789') and 'yes' or 'no'\n"
    ), timeout=15)
    assert out["found"] == "yes"


def test_screen_text_columns_match_bios(dosbox):
    # Verify that screen_text uses the live BIOS column count, not a
    # hardcoded default. Read BIOSMEM_NB_COLS (seg 0x40, off 0x4a) and
    # compare to the line length in screen_text output.
    out = run_script(dosbox, (
        "dosbox.wait_frames(10)\n"
        "local cols = dosbox.mem_read_word(0x40, 0x4a)\n"
        "local t = dosbox.screen_text()\n"
        "local first_line = string.match(t, '([^\\n]*)')\n"
        "dosbox.output.bios_cols = cols\n"
        "dosbox.output.line_len = #first_line\n"
    ))
    assert out["bios_cols"] == out["line_len"]


def test_screen_match_finds_text(dosbox):
    # Default DOSBox shell is on Z: drive with no config loaded.
    out = run_script(dosbox, (
        "dosbox.wait_frames(30)\n"
        "local found = dosbox.screen_match('Z:')\n"
        "dosbox.output.found = found and 'yes' or 'no'\n"
    ))
    assert out["found"] == "yes"


def test_screen_match_ignorecase(dosbox):
    out = run_script(dosbox, (
        "dosbox.wait_frames(30)\n"
        "local found = dosbox.screen_match('z:', {ignorecase=true})\n"
        "dosbox.output.found = found and 'yes' or 'no'\n"
    ))
    assert out["found"] == "yes"


def test_wait_for_text_immediate(dosbox):
    # Z: should already be on screen from the shell prompt.
    out = run_script(dosbox, (
        "dosbox.wait_frames(30)\n"
        "local found = dosbox.wait_for_text('Z:', 60)\n"
        "dosbox.output.found = found and 'yes' or 'no'\n"
    ))
    assert out["found"] == "yes"


def test_wait_for_text_timeout(dosbox):
    out = run_script(dosbox, (
        "local found = dosbox.wait_for_text('NONEXISTENT_XYZ', 30)\n"
        "dosbox.output.found = found and 'yes' or 'no'\n"
    ))
    assert out["found"] == "no"


def test_key_does_not_error(dosbox):
    out = run_script(dosbox, (
        "dosbox.key('KBD_a', true)\n"
        "dosbox.key('KBD_a', false)\n"
        "dosbox.output.ok = 'yes'\n"
    ))
    assert out["ok"] == "yes"


def test_type_does_not_error(dosbox):
    out = run_script(dosbox, (
        "dosbox.type('hello')\n"
        "dosbox.output.ok = 'yes'\n"
    ))
    assert out["ok"] == "yes"


def test_osd_does_not_error(dosbox):
    out = run_script(dosbox, (
        "dosbox.osd('test overlay')\n"
        "dosbox.output.ok = 'yes'\n"
    ))
    assert out["ok"] == "yes"


def test_osd_clear_does_not_error(dosbox):
    out = run_script(dosbox, (
        "dosbox.osd('temp')\n"
        "dosbox.osd_clear()\n"
        "dosbox.output.ok = 'yes'\n"
    ))
    assert out["ok"] == "yes"


def test_stuck_script_does_not_poison_fixture(dosbox):
    # A script that never finishes must be stopped when the client gives up, or
    # the shared module fixture is poisoned: the next load returns 400 "a
    # script is already running" and every later test in the file cascades.
    time.sleep(2.1)  # rate limiter
    r = dosbox.script_load(
        "dosbox.wait_for_text('NEVERSHOWS_ZZZ', 100000)\n", name="hang-test")
    assert r.status_code == 200
    assert dosbox.script_start().status_code == 200
    with pytest.raises(TimeoutError):
        dosbox.wait_script_done(timeout=3)
    # wait_script_done stopped the stuck script, so a fresh load works at once.
    out = run_script(dosbox, "dosbox.output.ok = 'recovered'\n")
    assert out["ok"] == "recovered"


def test_mem_read_byte_returns_integer(dosbox):
    # Read from BIOS data area (segment 0x0040, offset 0x0010) which
    # contains the equipment word. Should be a non-negative integer.
    out = run_script(dosbox, (
        "local val = dosbox.mem_read_byte(0x0040, 0x0010)\n"
        "dosbox.output.val = val\n"
        "dosbox.output.vtype = type(val)\n"
    ))
    assert out["vtype"] == "number"
    assert isinstance(out["val"], int)
    assert 0 <= out["val"] <= 255


def test_mem_read_returns_correct_length(dosbox):
    out = run_script(dosbox, (
        "local data = dosbox.mem_read(0x0040, 0x0000, 16)\n"
        "dosbox.output.len = #data\n"
    ))
    assert out["len"] == 16


def test_mount_lock(dosbox):
    out = run_script(dosbox, (
        "dosbox.mount_lock()\n"
        "dosbox.output.locked = 'yes'\n"
    ))
    assert out["locked"] == "yes"


def test_capture_start_stop_does_not_error(dosbox):
    out = run_script(dosbox, (
        "dosbox.capture_start()\n"
        "dosbox.wait_frames(5)\n"
        "dosbox.capture_stop()\n"
        "dosbox.output.ok = 'yes'\n"
    ))
    assert out["ok"] == "yes"


def run_failing_script(dosbox, source, name="func-test-error"):
    time.sleep(2.1)  # rate limiter
    r = dosbox.script_load(source, name=name)
    assert r.status_code == 200, f"Load failed: {r.json()}"
    r = dosbox.script_start()
    assert r.status_code == 200, f"Start failed: {r.json()}"
    return dosbox.wait_script_done()


def test_capture_start_while_capturing_raises(dosbox):
    try:
        data = run_failing_script(dosbox, (
            "dosbox.capture_start('raw')\n"
            "dosbox.wait_frames(5)\n"
            "dosbox.capture_start('rendered')\n"
            "dosbox.output.ok = 'yes'\n"
        ))
        assert data["state"] == "error", data
        assert "already capturing video" in data.get("error", ""), data
        status = dosbox.capture_status().json()
        assert status["capturing"] is True
        assert status["mode"] == "raw"
    finally:
        dosbox.capture_stop()


def test_audio_capture_start_while_capturing_raises(dosbox):
    try:
        data = run_failing_script(dosbox, (
            "dosbox.audio_capture_start()\n"
            "dosbox.wait_frames(5)\n"
            "dosbox.audio_capture_start()\n"
            "dosbox.output.ok = 'yes'\n"
        ))
        assert data["state"] == "error", data
        assert "already capturing audio" in data.get("error", ""), data
    finally:
        dosbox.audio_capture_stop()


def test_audio_capture_path_outside_the_capture_folder_raises(dosbox, tmp_path):
    target = tmp_path / "lua-outside.wav"
    try:
        data = run_failing_script(dosbox, (
            f"dosbox.audio_capture_start({str(target)!r})\n"
            "dosbox.output.ok = 'yes'\n"
        ))
        assert data["state"] == "error", data
        assert "capture folder" in data.get("error", ""), data
    finally:
        dosbox.audio_capture_stop()
    assert not target.exists()


def test_a_cancelled_named_capture_leaves_no_file_and_no_name_behind(dosbox):
    """A named start stopped before any audio arrives leaves nothing, and the
    next start without a name gets a numbered file (ada-11qi)."""
    name = f"cancel-{secrets.token_hex(4)}.wav"
    probe = dosbox.audio_capture_start(f"probe-{secrets.token_hex(4)}.wav")
    assert probe.status_code == 200, probe.text
    dosbox.audio_capture_stop()
    folder = Path(probe.json()["path"]).parent
    Path(probe.json()["path"]).unlink(missing_ok=True)
    run_script(dosbox, (
        f"dosbox.audio_capture_start({name!r})\n"
        "dosbox.audio_capture_stop()\n"
    ))
    r = dosbox.audio_capture_start()
    assert r.status_code == 200, r.text
    try:
        time.sleep(0.5)
    finally:
        dosbox.audio_capture_stop()
    assert not (folder / name).exists()


def test_output_table_multiple_values(dosbox):
    out = run_script(dosbox, (
        "dosbox.output.str_val = 'hello'\n"
        "dosbox.output.int_val = 42\n"
        "dosbox.output.bool_val = true\n"
    ))
    assert out["str_val"] == "hello"
    assert out["int_val"] == 42
    assert out["bool_val"] is True


def test_is_text_mode_at_dos_prompt(dosbox):
    out = run_script(dosbox, (
        'dosbox.output["text_mode"] = dosbox.is_text_mode() and "yes" or "no"'
    ))
    assert out["text_mode"] == "yes"
