# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#
"""MOUNT reads a relative image path as a host path first, from any current
drive: the type follows the file and a host file wins over a DOS file of the
same name."""

import time

from test_archive_images import iso_with_files
from test_archive_mount import floppy_bytes


def wait_screen(client, needle: str, timeout: float = 15) -> str:
    """The screen with its 80-column rows joined, so wrapped lines match."""
    deadline = time.monotonic() + timeout
    text = ""
    while time.monotonic() < deadline:
        r = client.screen_text()
        if r.status_code == 200:
            text = r.json()["text"].replace("\n", "")
            if needle in text:
                return text
        time.sleep(0.2)
    raise AssertionError(f"{needle!r} not on screen after {timeout}s:\n{text}")


def type_line(client, line: str) -> None:
    client.type_string(line + "\n")
    deadline = time.monotonic() + 15
    while client.input_sequence([]).status_code != 200:
        assert time.monotonic() < deadline, "input still pending"
        time.sleep(0.1)


def start_on_host_folder_drive(dosbox_e2e, tmp_path):
    """C: is a host folder and the current drive, as after a usual start; the
    conf is named relative to the working directory, as a user types it."""
    work = tmp_path / "work"
    game = work / "game"
    (game / "c").mkdir(parents=True)
    (game / "cd").mkdir()
    (game / "cd" / "game.iso").write_bytes(iso_with_files({"CD.TXT": b"cd file\r\n"}))
    (game / "dosbox.conf").write_text("[autoexec]\nmount c c -pr\nc:\n", encoding="utf-8")
    inst = dosbox_e2e(work_dir=work, settings={"keyboard_layout": "us"},
                      extra_args=["-conf", "game/dosbox.conf"])
    inst.client.wait_autoexec_done()
    return inst, work, game


def test_relative_iso_without_type_mounts_as_cd_from_a_folder_drive(dosbox_e2e, tmp_path):
    inst, _, _ = start_on_host_folder_drive(dosbox_e2e, tmp_path)
    type_line(inst.client, "mount d cd/game.iso -pr")
    text = wait_screen(inst.client, "mounted as D drive")
    assert "CD-ROM image" in text, text
    type_line(inst.client, "dir d:")
    wait_screen(inst.client, "CD       TXT")


def test_working_dir_relative_iso_without_type_mounts_as_cd(dosbox_e2e, tmp_path):
    inst, _, _ = start_on_host_folder_drive(dosbox_e2e, tmp_path)
    type_line(inst.client, "mount d game/cd/game.iso")
    text = wait_screen(inst.client, "mounted as D drive")
    assert "CD-ROM image" in text, text


def test_host_file_wins_over_a_dos_file_of_the_same_name(dosbox_e2e, tmp_path):
    inst, _, game = start_on_host_folder_drive(dosbox_e2e, tmp_path)
    (game / "same.img").write_bytes(floppy_bytes("HOSTSIDE"))
    (game / "c" / "GAME").mkdir()
    (game / "c" / "GAME" / "SAME.IMG").write_bytes(floppy_bytes("DOSSIDE"))
    type_line(inst.client, "mount a game/same.img")
    wait_screen(inst.client, "mounted as A drive")
    type_line(inst.client, "vol a:")
    text = wait_screen(inst.client, "Volume in drive A is")
    assert "Volume in drive A is HOSTSIDE" in text, text
