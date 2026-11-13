# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#

"""Integration tests for the ZMBV video capture REST endpoints."""

import secrets
import time
from pathlib import Path


def test_capture_status_when_idle(dosbox):
    r = dosbox.capture_status()
    assert r.status_code == 200
    assert r.json()["capturing"] is False


def test_capture_start_stop(dosbox):
    r = dosbox.capture_start()
    assert r.status_code == 200

    r = dosbox.capture_status()
    assert r.status_code == 200
    assert r.json()["capturing"] is True

    r = dosbox.capture_stop()
    assert r.status_code == 200

    r = dosbox.capture_status()
    assert r.status_code == 200
    assert r.json()["capturing"] is False


def test_capture_double_start_answers_409(dosbox):
    assert dosbox.capture_start().status_code == 200
    try:
        r = dosbox.capture_start()
        assert r.status_code == 409, r.text
        assert r.json()["error"] == "Already capturing video"
    finally:
        dosbox.capture_stop()


def test_capture_start_in_another_mode_keeps_the_running_one(dosbox):
    assert dosbox.capture_start().status_code == 200
    try:
        r = dosbox._post("/api/v1/capture/video/start", json={"mode": "rendered"})
        assert r.status_code == 409, r.text
        status = dosbox.capture_status().json()
        assert status["capturing"] is True
        assert status["mode"] == "raw"
    finally:
        dosbox.capture_stop()


def unique_wav(stem: str) -> str:
    return f"{stem}-{secrets.token_hex(4)}.wav"


def test_audio_double_start_answers_409_and_keeps_the_first_file(dosbox):
    r = dosbox.audio_capture_start(unique_wav("first"))
    assert r.status_code == 200, r.text
    first = Path(r.json()["path"])
    second_name = unique_wav("second")
    try:
        r = dosbox.audio_capture_start(second_name)
        assert r.status_code == 409, r.text
        assert r.json()["error"] == "Already capturing audio"
        time.sleep(0.5)
    finally:
        dosbox.audio_capture_stop()
    assert first.exists()
    assert not (first.parent / second_name).exists()
    first.unlink()


# -- An explicit audio path stays in the capture folder (ada-11qi) --

def test_audio_name_lands_in_the_capture_folder(dosbox):
    name = unique_wav("named")
    r = dosbox.audio_capture_start(name)
    assert r.status_code == 200, r.text
    try:
        time.sleep(0.5)
    finally:
        dosbox.audio_capture_stop()
    path = Path(r.json()["path"])
    assert path.name == name and path.parent.name == "capture", path
    assert path.exists()
    path.unlink()


def refused(dosbox, path: str):
    r = dosbox.audio_capture_start(path)
    if r.status_code == 200:
        dosbox.audio_capture_stop()
    return r


def test_audio_path_outside_the_capture_folder_is_refused(dosbox, tmp_path):
    target = tmp_path / "outside.wav"
    r = refused(dosbox, str(target))
    assert r.status_code == 403, r.text
    time.sleep(0.5)
    assert not target.exists()


def capture_folder(dosbox) -> Path:
    r = dosbox.audio_capture_start(unique_wav("probe"))
    assert r.status_code == 200, r.text
    dosbox.audio_capture_stop()
    probe = Path(r.json()["path"])
    probe.unlink(missing_ok=True)
    return probe.parent


def test_audio_path_climbing_out_is_refused(dosbox):
    folder = capture_folder(dosbox)
    name = unique_wav("climb")
    r = refused(dosbox, f"../{name}")
    assert r.status_code == 403, r.text
    time.sleep(0.5)
    assert not (folder.parent / name).exists()


def test_audio_name_the_filesystem_refuses_is_refused_not_redirected(dosbox):
    folder = capture_folder(dosbox)
    before = set(folder.iterdir())
    r = refused(dosbox, "a" * 300 + ".wav")
    assert r.status_code == 403, r.text
    time.sleep(0.5)
    assert dosbox.audio_capture_status().json()["capturing"] is False
    assert set(folder.iterdir()) == before


def test_audio_capture_never_follows_a_dangling_link(dosbox, tmp_path):
    folder = capture_folder(dosbox)
    ghost = tmp_path / "ghost.wav"
    link = folder / unique_wav("dangling")
    link.symlink_to(ghost)
    try:
        r = refused(dosbox, link.name)
        assert r.status_code == 403, r.text
        time.sleep(0.5)
        assert not ghost.exists()
    finally:
        link.unlink()


def test_audio_capture_never_leaves_through_a_linked_folder(dosbox, tmp_path):
    folder = capture_folder(dosbox)
    outside = tmp_path / "outside"
    outside.mkdir()
    linkdir = folder / f"linkdir-{secrets.token_hex(4)}"
    linkdir.symlink_to(outside, target_is_directory=True)
    try:
        r = refused(dosbox, f"{linkdir.name}/{unique_wav('via')}")
        assert r.status_code == 403, r.text
        time.sleep(0.5)
        assert list(outside.iterdir()) == []
    finally:
        linkdir.unlink()


def test_audio_capture_never_overwrites_a_file(dosbox):
    r = dosbox.audio_capture_start(unique_wav("kept"))
    assert r.status_code == 200, r.text
    dosbox.audio_capture_stop()
    kept = Path(r.json()["path"])
    kept.write_bytes(b"precious")
    for given in (kept.name, str(kept)):
        r = refused(dosbox, given)
        assert r.status_code == 403, (given, r.text)
    time.sleep(0.5)
    assert kept.read_bytes() == b"precious"
    kept.unlink()


def test_audio_capture_never_follows_a_planted_link(dosbox, tmp_path):
    r = dosbox.audio_capture_start(unique_wav("probe"))
    assert r.status_code == 200, r.text
    dosbox.audio_capture_stop()
    probe = Path(r.json()["path"])
    victim = tmp_path / "victim.txt"
    victim.write_bytes(b"precious")
    link = probe.parent / unique_wav("link")
    link.symlink_to(victim)
    try:
        r = refused(dosbox, link.name)
        assert r.status_code == 403, r.text
        time.sleep(0.5)
        assert victim.read_bytes() == b"precious"
    finally:
        link.unlink()
        probe.unlink(missing_ok=True)


def test_capture_stop_when_not_recording(dosbox):
    r = dosbox.capture_stop()
    assert r.status_code == 200


def test_capture_from_lua(dosbox):
    time.sleep(2.1)  # rate limiter
    r = dosbox.script_load(
        "dosbox.capture_start()\n"
        "dosbox.wait_frames(10)\n"
        "dosbox.capture_stop()\n"
        "dosbox.output.done = 'yes'\n",
        name="capture-lua",
    )
    assert r.status_code == 200

    dosbox.script_start()
    data = dosbox.wait_script_done()
    assert data["state"] == "completed"
    assert data["output"]["done"] == "yes"
