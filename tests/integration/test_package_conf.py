# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#
"""A conf inside the command line's archive loads where a working folder's
dosbox.conf would, and its [autoexec] runs in place of the generated mount."""

from pathlib import Path

import pytest
from test_archive_mount import DONE_BAT, build_program_zip, run_to_exit, sidecar_files

PACKAGE_BAT = b"@ECHO OFF\r\nECHO package> PKG.TXT\r\n"

PACKAGE_CONF = (b"[cpu]\r\n"
                b"cputype = 386\r\n"
                b"[autoexec]\r\n"
                b"mount c .\r\n"
                b"c:\r\n"
                b"config -get cpu cputype > CPU.TXT\r\n"
                b"call OTHER.BAT\r\n"
                b"exit\r\n")


def build_package(tmp_path: Path) -> Path:
    # Two programs: without the package conf the mount would open the menu.
    return build_program_zip(tmp_path / "game.zip", {
        "dosbox.conf": PACKAGE_CONF,
        "GAME.BAT": DONE_BAT,
        "OTHER.BAT": PACKAGE_BAT,
    })


def saved(tmp_path: Path, name: str) -> str:
    found = list((tmp_path / "saves").rglob(name))
    assert len(found) == 1, sidecar_files(tmp_path)
    return found[0].read_text(encoding="ascii", errors="replace")


def test_the_package_conf_sets_and_starts_as_a_working_folder_would(tmp_path):
    zip_path = build_package(tmp_path)
    assert run_to_exit(tmp_path, [str(zip_path)]) == 0
    assert sidecar_files(tmp_path) == ["CPU.TXT", "PKG.TXT"]
    assert "386" in saved(tmp_path, "CPU.TXT")


def test_a_dash_conf_wins_and_the_package_still_starts(tmp_path):
    zip_path = build_package(tmp_path)
    extra = tmp_path / "launcher.conf"
    extra.write_text("[cpu]\ncputype = 486\n", encoding="utf-8")
    assert run_to_exit(tmp_path, ["-conf", str(extra), str(zip_path)]) == 0
    assert sidecar_files(tmp_path) == ["CPU.TXT", "PKG.TXT"]
    assert "486" in saved(tmp_path, "CPU.TXT")


@pytest.mark.parametrize("section", ["join", "overwrite"])
def test_run_name_takes_the_package_drives_and_starts_the_program(tmp_path, section):
    zip_path = build_package(tmp_path)
    assert run_to_exit(tmp_path, ["--set", f"autoexec_section={section}",
                                  "--run", "GAME.BAT", str(zip_path)]) == 0
    assert sidecar_files(tmp_path) == ["DONE.TXT"]


def test_run_name_without_a_package_c_mount_mounts_the_archive(tmp_path):
    zip_path = build_program_zip(tmp_path / "game.zip", {
        "dosbox.conf": b"[cpu]\r\ncputype = 386\r\n[autoexec]\r\ncall OTHER.BAT\r\n",
        "GAME.BAT": DONE_BAT,
        "OTHER.BAT": PACKAGE_BAT,
    })
    assert run_to_exit(tmp_path, ["--run", "GAME.BAT", str(zip_path)]) == 0
    assert sidecar_files(tmp_path) == ["DONE.TXT"]


def test_a_package_that_mounts_no_drive_keeps_the_generated_mount(tmp_path):
    zip_path = build_program_zip(tmp_path / "game.zip", {
        "dosbox.conf": b"[cpu]\r\ncputype = 386\r\n[autoexec]\r\necho hi\r\n",
        "GAME.BAT": DONE_BAT,
    })
    assert run_to_exit(tmp_path, [str(zip_path)]) == 0
    assert sidecar_files(tmp_path) == ["DONE.TXT"]


def test_set_on_the_command_line_turns_the_package_conf_off(tmp_path):
    """The start-up read sees --set."""
    zip_path = build_program_zip(tmp_path / "game.zip", {
        "dosbox.conf": PACKAGE_CONF,
        "GAME.BAT": DONE_BAT,
    })
    assert run_to_exit(tmp_path, ["--set", "package_conf=off",
                                  str(zip_path)]) == 0
    assert sidecar_files(tmp_path) == ["DONE.TXT"]


def test_the_start_up_read_uses_the_cache_set_on_the_command_line(tmp_path,
                                                                 monkeypatch):
    xdg_cache = tmp_path / "xdg-cache"
    monkeypatch.setenv("XDG_CACHE_HOME", str(xdg_cache))
    zip_path = build_package(tmp_path)
    assert run_to_exit(tmp_path, [str(zip_path)]) == 0
    assert not (xdg_cache / "dosbox-automation" / "archive-cache").exists()
    assert not (tmp_path / ".cache" / "dosbox-automation" / "archive-cache").exists()
    assert (tmp_path / "cache").is_dir()


def build_folder(tmp_path: Path, files: dict[str, bytes]) -> Path:
    folder = tmp_path / "Game Folder"
    for name, data in files.items():
        path = folder / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    return folder


FOLDER_CONF = (b"[cpu]\r\n"
               b"cputype = 386\r\n"
               b"[autoexec]\r\n"
               b"mount c ..\\DATA\r\n"
               b"c:\r\n"
               b"config -get cpu cputype > CPU.TXT\r\n"
               b"exit\r\n")


def test_a_game_folder_starts_through_its_package_conf(tmp_path):
    """ada-i8ec."""
    folder = build_folder(tmp_path, {"_DOSBOX/dosbox.conf": FOLDER_CONF,
                                     "DATA/GAME.BAT": DONE_BAT})
    assert run_to_exit(tmp_path, [str(folder)]) == 0
    assert "386" in (folder / "DATA" / "CPU.TXT").read_text(encoding="ascii")
    assert not (folder / "CPU.TXT").exists()


def test_run_name_starts_a_program_in_a_game_folder(tmp_path):
    folder = build_folder(tmp_path, {"GAME.BAT": DONE_BAT,
                                     "OTHER.BAT": PACKAGE_BAT})
    assert run_to_exit(tmp_path, ["--run", "GAME.BAT", str(folder)]) == 0
    assert (folder / "DONE.TXT").exists()
    assert not (folder / "PKG.TXT").exists()


@pytest.mark.parametrize("section", ["join", "overwrite"])
def test_run_name_takes_the_folder_package_drives(tmp_path, section):
    folder = build_folder(tmp_path, {"_DOSBOX/dosbox.conf": FOLDER_CONF,
                                     "DATA/GAME.BAT": DONE_BAT})
    assert run_to_exit(tmp_path, ["--set", f"autoexec_section={section}",
                                  "--run", "GAME.BAT", str(folder)]) == 0
    assert (folder / "DATA" / "DONE.TXT").exists()
    assert not (folder / "DATA" / "CPU.TXT").exists()


def run_with_primary_conf(tmp_path: Path, primary: str, args: list[str]) -> int:
    """run_to_exit with a primary conf: package_conf is read before
    the -conf files, so only the primary or the local conf or --set can set
    it."""
    import os
    import subprocess

    from conftest import DOSBOX_BIN

    conf_dir = tmp_path / ".config" / "dosbox-automation"
    conf_dir.mkdir(parents=True, exist_ok=True)
    (conf_dir / "dosbox-automation.conf").write_text(primary, encoding="utf-8")
    env = {**os.environ, "HOME": str(tmp_path), "XDG_CONFIG_HOME": str(tmp_path / ".config"),
           "SDL_VIDEODRIVER": "offscreen", "SDL_AUDIODRIVER": "dummy"}
    cmd = [DOSBOX_BIN, "--nolocalconf",
           "--set", "webserver_enabled=false",
           "--set", f"archive_cache_path={tmp_path / 'cache'}",
           "--set", f"archive_sidecar_path={tmp_path / 'saves'}",
           *args]
    done = subprocess.run(cmd, env=env, cwd=tmp_path, timeout=60,  # nosec B603 - our engine
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return done.returncode


@pytest.mark.parametrize("name", ["package_conf", "archive_package_conf"])
def test_package_conf_off_starts_the_archive_as_before(tmp_path, name):
    """The setting's old name stays as an alias."""
    zip_path = build_program_zip(tmp_path / "game.zip", {
        "dosbox.conf": b"[autoexec]\r\nmount c .\r\nc:\r\necho package> PKG.TXT\r\nexit\r\n",
        "GAME.BAT": DONE_BAT,
    })
    assert run_with_primary_conf(tmp_path, f"[dos]\n{name} = false\n",
                                 [str(zip_path)]) == 0
    assert sidecar_files(tmp_path) == ["DONE.TXT"]


# A MOUNT after start reads the package conf too (ada-nb2a).

def launcher(tmp_path: Path, autoexec: str, settings: str = "") -> Path:
    conf = tmp_path / "launcher.conf"
    conf.write_text(settings + "[autoexec]\n" + autoexec, encoding="utf-8")
    return conf


def test_a_mount_after_start_runs_the_package_under_run(tmp_path):
    zip_path = build_package(tmp_path)
    conf = launcher(tmp_path, f'mount c "{zip_path}" -run -exit\n')
    assert run_to_exit(tmp_path, ["-conf", str(conf)]) == 0
    assert sidecar_files(tmp_path) == ["CPU.TXT", "PKG.TXT"]
    assert "386" in saved(tmp_path, "CPU.TXT")


def test_a_dash_conf_wins_over_a_package_mounted_after_start(tmp_path):
    zip_path = build_package(tmp_path)
    conf = launcher(tmp_path, f'mount c "{zip_path}" -run -exit\n',
                    "[cpu]\ncputype = 486\n")
    assert run_to_exit(tmp_path, ["-conf", str(conf)]) == 0
    assert "486" in saved(tmp_path, "CPU.TXT")


# The package mounts its own C:, which takes the place of the D: named here.
def test_a_plain_mount_applies_the_settings_and_starts_nothing(tmp_path):
    zip_path = build_package(tmp_path)
    conf = launcher(tmp_path, f'mount d "{zip_path}"\n'
                              "c:\n"
                              "config -get cpu cputype > CPU.TXT\n"
                              "exit\n")
    assert run_to_exit(tmp_path, ["-conf", str(conf)]) == 0
    assert sidecar_files(tmp_path) == ["CPU.TXT"]
    assert "386" in saved(tmp_path, "CPU.TXT")


def test_a_start_only_setting_from_a_mount_after_start_keeps_its_value(tmp_path):
    zip_path = build_program_zip(tmp_path / "game.zip", {
        "dosbox.conf": (b"[dosbox]\r\nmachine = cga\r\n[autoexec]\r\nmount c .\r\nc:\r\n"
                        b"config -get dosbox machine > M.TXT\r\nexit\r\n"),
        "GAME.BAT": DONE_BAT,
        "OTHER.BAT": PACKAGE_BAT,
    })
    conf = launcher(tmp_path, f'mount c "{zip_path}" -run -exit\n')
    assert run_to_exit(tmp_path, ["-conf", str(conf)]) == 0
    machine = saved(tmp_path, "M.TXT")
    assert "cga" not in machine and machine.strip(), machine
