# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#
"""Game packages through the engine: the shapes the profiles name, built
here from constructed files."""

import time
import zipfile
from pathlib import Path

from test_archive_images import iso_with_files
from test_archive_mount import DONE_BAT, floppy_bytes, run_to_exit, sidecar_files, wait_text
from test_package_conf import launcher, saved

OTHER_BAT = b"@ECHO OFF\r\nECHO other> OTHER.TXT\r\n"


def write_zip(path: Path, members: dict[str, bytes]) -> Path:
    with zipfile.ZipFile(path, "w") as z:
        for name, data in members.items():
            z.writestr(name, data)
    return path


def wait_for_log(log: Path, needle: str, timeout_s: float = 20.0) -> str:
    deadline = time.time() + timeout_s
    text = ""
    while time.time() < deadline:
        if log.exists():
            text = log.read_text(encoding="utf-8", errors="replace")
            if needle in text:
                return text
        time.sleep(0.25)
    return text


VIEW_COM = Path(__file__).resolve().parents[2] / "resources" / "drives" / "y" / "dos" / "view.com"


# A synthetic GOG release (ada-ubkn): the conf pair at the root and DOSBox's
# folder beside it, with GOG's lines; GOG starts DOSBox in DOSBOX\, so its
# mount C ".." is the root. The game is our own VIEW.COM.
def gog_shaped_zip(path: Path) -> Path:
    return write_zip(path, {
        "dosboxVIEW.conf": b"[sdl]\r\nfullscreen = false\r\n[cpu]\r\ncputype = 386\r\n",
        "dosboxVIEW_single.conf": (b"[autoexec]\r\n@echo off\r\nmount C \"..\"\r\n"
                                   b"mount C \"..\\cloud_saves\" -t overlay\r\nc:\r\ncls\r\n"
                                   b"config -get cpu cputype > CPU.TXT\r\n"
                                   b"VIEW.COM README.TXT\r\nexit\r\n"),
        "DOSBOX/README.TXT": b"GOG keeps its DOSBox here\r\n",
        "VIEW.COM": VIEW_COM.read_bytes(),
        "README.TXT": b"A synthetic GOG release\r\n",
    })


def test_a_gog_root_pair_starts_its_game_from_the_command_line(dosbox_e2e, tmp_path):
    zip_path = gog_shaped_zip(tmp_path / "view.zip")
    # With the REST API on, the archive has to lie under an allowed image root.
    inst = dosbox_e2e(extra_args=[str(zip_path)], allowed_image_roots=[tmp_path],
                      settings={"archive_sidecar_path": str(tmp_path / "saves"),
                                "archive_cache_path": str(tmp_path / "cache")})
    wait_text(inst.client, "A synthetic GOG release", timeout=30)
    assert "386" in saved(tmp_path, "CPU.TXT")


def test_a_gog_pair_in_a_game_folder_starts_from_the_command_line(tmp_path):
    zip_path = write_zip(tmp_path / "collection.zip", {
        "PQ1/DOSBOX/dosboxPQ1.conf": b"[cpu]\r\ncputype = 386\r\n",
        "PQ1/DOSBOX/dosboxPQ1_single.conf": (b"[autoexec]\r\nmount C \"..\"\r\nc:\r\n"
                                             b"config -get cpu cputype > CPU.TXT\r\n"
                                             b"call GAME.BAT\r\nexit\r\n"),
        "PQ1/GAME.BAT": DONE_BAT,
        "PQ1/OTHER.BAT": OTHER_BAT,
    })
    assert run_to_exit(tmp_path, [str(zip_path)]) == 0
    assert sidecar_files(tmp_path) == ["CPU.TXT", "DONE.TXT"]
    assert "386" in saved(tmp_path, "CPU.TXT")


# A package with explicit mounts, built here: a floppy image and
# a folder as drives, machine=cga from the package before the machine starts.
def test_a_cga_package_with_explicit_mounts_starts_as_cga(tmp_path):
    zip_path = write_zip(tmp_path / "arctic.zip", {
        "dosbox.conf": (b"[dosbox]\r\nmachine = cga\r\n[autoexec]\r\n"
                        b"mount a a_drive/floppy.ima\r\nmount c c_drive\r\nc:\r\ncd arctic\r\n"
                        b"config -get dosbox machine > M.TXT\r\n"
                        b"if exist a:\\nul echo floppy> A.TXT\r\nexit\r\n"),
        "a_drive/floppy.ima": floppy_bytes("ARCTIC"),
        "c_drive/ARCTIC/ONE.BAT": DONE_BAT,
        "c_drive/ARCTIC/TWO.BAT": OTHER_BAT,
    })
    assert run_to_exit(tmp_path, [str(zip_path)]) == 0
    assert "cga" in saved(tmp_path, "M.TXT")
    assert "floppy" in saved(tmp_path, "A.TXT")


# The same package mounted at the prompt on another letter: the package's own
# drives take the place of that MOUNT, so its C: is c_drive.
def test_a_package_mounted_later_brings_its_own_drives(tmp_path):
    zip_path = write_zip(tmp_path / "arctic.zip", {
        "dosbox.conf": (b"[autoexec]\r\nmount c c_drive\r\nc:\r\ncd arctic\r\n"
                        b"call ONE.BAT\r\nexit\r\n"),
        "c_drive/ARCTIC/ONE.BAT": DONE_BAT,
        "c_drive/ARCTIC/TWO.BAT": OTHER_BAT,
    })
    conf = launcher(tmp_path, f'mount d "{zip_path}" -run -exit\n')
    assert run_to_exit(tmp_path, ["-conf", str(conf)]) == 0
    assert sidecar_files(tmp_path) == ["DONE.TXT"]


# The batch a MOUNT queues runs as an autoexec runs: labels and GOTO.
def test_a_package_mounted_later_runs_its_labels(tmp_path):
    zip_path = write_zip(tmp_path / "loop.zip", {
        "dosbox.conf": (b"[autoexec]\r\nmount c .\r\nc:\r\n:AGAIN\r\n"
                        b"if exist LOOP.TXT goto OUT\r\necho once> LOOP.TXT\r\ngoto AGAIN\r\n"
                        b":OUT\r\necho out> OUT.TXT\r\nexit\r\n"),
        "GAME.BAT": DONE_BAT,
        "OTHER.BAT": OTHER_BAT,
    })
    conf = launcher(tmp_path, f'mount c "{zip_path}" -run\n')
    assert run_to_exit(tmp_path, ["-conf", str(conf)]) == 0
    assert sidecar_files(tmp_path) == ["LOOP.TXT", "OUT.TXT"]


def test_an_image_inside_the_archive_mounts_from_a_dos_path(tmp_path):
    zip_path = write_zip(tmp_path / "cd.zip", {
        "GAME.EXE": b"MZ",
        "cd/game.iso": iso_with_files({"README.TXT": b"from the iso\r\n"}),
    })
    conf = launcher(tmp_path, f'mount c "{zip_path}"\n'
                              "imgmount d c:\\cd\\game.iso -t iso\n"
                              "copy d:\\README.TXT c:\\R.TXT\n"
                              "exit\n")
    assert run_to_exit(tmp_path, ["-conf", str(conf)]) == 0
    assert "from the iso" in saved(tmp_path, "R.TXT")


# With the REST API on, a host path in a package goes through the mount
# policy as from any configuration.
def test_a_host_path_in_a_package_meets_the_mount_policy(dosbox_e2e, tmp_path):
    zip_path = write_zip(tmp_path / "bad.zip", {
        "dosbox.conf": b"[autoexec]\r\nmount e /etc\r\n",
        "GAME.BAT": DONE_BAT,
    })
    inst = dosbox_e2e(extra_args=[str(zip_path)])
    log = wait_for_log(inst.work_dir / "dosbox-stderr.log", "Blocked '/etc'")
    assert "Blocked '/etc'" in log
