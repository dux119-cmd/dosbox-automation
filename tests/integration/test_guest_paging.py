# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#
"""Host-side access to guest memory while the guest pages (ada-q50n): the
API answers 409, Lua raises, search reports the hole, and PGFAULT.COM, which
parks on any page fault as a DPMI host kills its client, sees none."""

import secrets
import struct
import time
import zipfile
from pathlib import Path

import pytest

from conftest import WORKSPACE, start_dosbox_instance

GUEST = Path(__file__).resolve().parents[1] / "files" / "paging" / "PGFAULT.COM"

HOLE = 0x200000
HMA_HOLE = 0x101000
SPINNING = struct.unpack("<I", b"PGON")[0]


def _find_marker(client, timeout_s: float = 20.0) -> int:
    """The guest's state block: the code holds the marker value as an
    immediate too, so the data copy is the higher of the two matches."""
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        r = client.memory_search(0x1000, 0xA0000, SPINNING, width=4)
        if r.status_code == 200 and len(r.json()["matches"]) >= 2:
            return max(r.json()["matches"])
        time.sleep(0.3)
    raise TimeoutError("PGFAULT.COM never reached protected mode")


def _guest_state(client, marker: int) -> tuple[bytes, int]:
    r = client.memory_read(marker, 8)
    assert r.status_code == 200, r.text
    tag, faults = struct.unpack("<4sI", r.content)
    return tag, faults


@pytest.fixture(scope="module")
def paged_guest():
    work = WORKSPACE / f"paging-{secrets.token_hex(4)}"
    work.mkdir(parents=True)
    with zipfile.ZipFile(work / "pg.zip", "w") as archive:
        archive.write(GUEST, "PGFAULT.COM")

    instance = start_dosbox_instance(work, autoexec_lines=["mount c pg.zip", "c:"])
    try:
        # The test types the command, not the autoexec: without page 0 the
        # readiness check's screen read would answer 409.
        instance.client.wait_autoexec_done()
        instance.client.type_string("PGFAULT 0z\n")
        marker = _find_marker(instance.client)
        yield instance.client, marker
    finally:
        instance.shutdown()


@pytest.fixture
def unharmed(paged_guest):
    """Every test ends with the guest still spinning, without a fault, and
    the emulation thread still taking commands."""
    client, marker = paged_guest
    yield client
    assert _guest_state(client, marker) == (b"PGON", 0)
    assert client.cpu_state().status_code == 200


def test_read_of_a_hole_answers_409_with_the_address(unharmed):
    r = unharmed.memory_read(HOLE - 4, 16)
    assert r.status_code == 409, r.text
    assert r.json()["address"] == HOLE


def test_write_across_a_hole_writes_nothing(unharmed):
    before = unharmed.memory_read(HOLE - 4, 4).content
    r = unharmed.memory_write(HOLE - 4, b"xxxxxxxx")
    assert r.status_code == 409, r.text
    assert r.json()["address"] == HOLE
    assert unharmed.memory_read(HOLE - 4, 4).content == before


def test_search_skips_the_hole_and_reports_it(unharmed):
    r = unharmed.memory_search(HOLE - 0x1000, HOLE + 0x2000, 0, width=1)
    assert r.status_code == 200, r.text
    body = r.json()
    assert body["skipped"] == [{"start": HOLE, "end": HOLE + 0x1000}]
    assert not [m for m in body["matches"] if HOLE <= m < HOLE + 0x1000]


def test_freeze_on_a_hole_leaves_the_guest_alone(unharmed):
    try:
        assert unharmed.memory_freeze(HOLE, 0x42).status_code == 200
        time.sleep(1.0)
    finally:
        unharmed.memory_freeze_clear()


def test_lua_read_of_a_hole_is_a_script_error(unharmed):
    assert unharmed.script_load("dosbox.mem_read_byte(0xFFFF, 0x1010)").status_code == 200
    assert unharmed.script_start().status_code == 200
    status = unharmed.wait_script_done(timeout=10)
    assert status["state"] == "error"
    assert f"0x{HMA_HOLE:x}" in status.get("error", ""), status


def test_screen_text_without_the_bios_data_area_answers_409(unharmed):
    r = unharmed.screen_text()
    assert r.status_code == 409, r.text
    assert r.json()["address"] == 0x400


def test_dos_internals_without_page_zero_answer_409(unharmed):
    r = unharmed.dos_internals()
    assert r.status_code == 409, r.text
    assert r.json()["address"] == 0
