# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#
"""CONFIG -r takes no command line from inside DOS: its parameters would
become the restarted engine's arguments, where --set or -conf reach
[webserver]."""

from pathlib import Path

from test_archive_mount import build_program_zip, run_to_exit, sidecar_files


def test_config_restart_with_parameters_is_refused(tmp_path: Path):
    # A refused restart leaves the engine running the autoexec's next line.
    zip_path = build_program_zip(tmp_path / "game.zip", {
        "dosbox.conf": (b"[autoexec]\r\n"
                        b"mount c .\r\n"
                        b"c:\r\n"
                        b"config -r -c exit\r\n"
                        b"echo still here> R.TXT\r\n"
                        b"exit\r\n"),
        "GAME.BAT": b"@ECHO OFF\r\n",
        "OTHER.BAT": b"@ECHO OFF\r\n",
    })
    assert run_to_exit(tmp_path, [str(zip_path)]) == 0
    assert sidecar_files(tmp_path) == ["R.TXT"]


def free_port() -> int:
    import socket

    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def test_a_restart_keeps_the_api_a_package_wrote_into_the_primary_conf(tmp_path: Path):
    """ada-rafi: the restarted engine keeps the API, not the one a package
    wrote into the primary conf."""
    import os
    import subprocess

    from conftest import DOSBOX_BIN

    port = free_port()
    autoexec = ("[autoexec]\r\n"
                "mount d ~/.config/dosbox-automation\r\n"
                "if exist d:\\done.txt goto second\r\n"
                "echo x> d:\\done.txt\r\n"
                "echo [webserver]>> d:\\dosbox~1.con\r\n"
                "echo webserver_enabled = true>> d:\\dosbox~1.con\r\n"
                f"echo webserver_port = {port}>> d:\\dosbox~1.con\r\n"
                "config -r\r\n"
                "goto end\r\n"
                ":second\r\n"
                "exit\r\n"
                ":end\r\n").encode("ascii")
    zip_path = build_program_zip(tmp_path / "game.zip", {"dosbox.conf": autoexec,
                                                         "GAME.BAT": b"@ECHO OFF\r\n"})
    env = {**os.environ, "HOME": str(tmp_path),
           "XDG_CONFIG_HOME": str(tmp_path / ".config"),
           "XDG_CACHE_HOME": str(tmp_path / ".cache"),
           "SDL_VIDEODRIVER": "offscreen", "SDL_AUDIODRIVER": "dummy"}
    log = tmp_path / "engine.log"
    with open(log, "w", encoding="utf-8") as out:
        done = subprocess.run([DOSBOX_BIN, "--set", "joysticktype=none",  # nosec B603 - our engine
                               str(zip_path)],
                              env=env, cwd=tmp_path, timeout=60, stdout=out, stderr=out)
    # A restart starts a new process and the old one exits, so run() returns
    # while the restarted engine still runs; it exits on the second pass.
    import time

    deadline = time.monotonic() + 30
    text = ""
    while time.monotonic() < deadline:
        text = log.read_text(encoding="utf-8", errors="replace")
        if text.count("| atexit") >= 2:
            break
        time.sleep(0.2)
    assert done.returncode == 0
    assert text.count("arguments:") == 2, "the package did not restart the engine"
    assert "starting HTTP REST API" not in text


def test_config_writes_no_host_file_with_the_api_on(dosbox_e2e):
    """ada-u6bo: with the API on, a guest's CONFIG -wc lands nowhere on the host."""
    import time

    inst = dosbox_e2e(["config -wc wcout.txt", "config -wl wlout.txt"])
    inst.client.wait_shell(timeout=15)
    deadline = time.monotonic() + 15
    text = ""
    while time.monotonic() < deadline:
        text = inst.client.screen_text().json()["text"]
        if text.count("does not write files on the host") >= 2:
            break
        time.sleep(0.2)
    assert text.count("does not write files on the host") >= 2, text
    leftovers = [p.name for p in Path(inst.work_dir).iterdir()
                 if p.name.lower() in ("wcout.txt", "wlout.txt")]
    assert leftovers == [], leftovers
