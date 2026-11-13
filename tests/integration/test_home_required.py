# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#
"""Without HOME the engine's folders have no place to go: it refuses to start
instead of writing a folder named '~' into its working directory. --version and
--help answer before any of that and write nothing."""

import os
import subprocess
import sys
from pathlib import Path

import pytest
from conftest import DOSBOX_BIN

HOME_VARIABLES = ("HOME", "XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME")

pytestmark = pytest.mark.skipif(sys.platform == "win32",
                                reason="Windows takes its folders from the shell, not HOME")


def run_with(tmp_path: Path, variables: dict, *words: str) -> subprocess.CompletedProcess:
    env = {k: v for k, v in os.environ.items() if k not in HOME_VARIABLES}
    env.update({"SDL_VIDEODRIVER": "offscreen", "SDL_AUDIODRIVER": "dummy", **variables})
    return subprocess.run([DOSBOX_BIN, *words], capture_output=True, text=True,  # nosec B603
                          timeout=60, env=env, cwd=tmp_path, check=False)


def files_under(path: Path) -> list:
    return sorted(str(p.relative_to(path)) for p in path.rglob("*"))


def test_without_home_the_engine_refuses_to_start(tmp_path):
    r = run_with(tmp_path, {}, "--noprimaryconf", "--nolocalconf", "-c", "exit")
    output = r.stdout + r.stderr
    assert r.returncode == 1, (r.returncode, output)
    assert "HOME, XDG_CONFIG_HOME, XDG_DATA_HOME, XDG_CACHE_HOME not set" in output, output
    assert files_under(tmp_path) == []


def test_the_config_folder_alone_is_refused(tmp_path):
    config = tmp_path / "config"
    r = run_with(tmp_path, {"XDG_CONFIG_HOME": str(config)},
                 "--noprimaryconf", "--nolocalconf", "-c", "exit")
    output = r.stdout + r.stderr
    assert r.returncode == 1, (r.returncode, output)
    assert "HOME, XDG_DATA_HOME, XDG_CACHE_HOME not set" in output, output
    assert files_under(tmp_path) == []


def test_the_three_xdg_folders_stand_in_for_home(tmp_path):
    folders = {name: str(tmp_path / name.lower()) for name in HOME_VARIABLES[1:]}
    r = run_with(tmp_path, folders, "--noprimaryconf", "--nolocalconf", "-c", "exit")
    assert r.returncode == 0, (r.returncode, r.stdout + r.stderr)
    assert not (tmp_path / "~").exists()
    assert (tmp_path / "xdg_config_home" / "dosbox-automation").is_dir()


@pytest.mark.parametrize("word", ["--version", "--help"])
def test_version_and_help_need_no_home(tmp_path, word):
    r = run_with(tmp_path, {}, word)
    assert r.returncode == 0, (r.returncode, r.stdout + r.stderr)
    assert ("dosbox-automation" if word == "--version" else "Usage:") in r.stdout
    assert files_under(tmp_path) == []


@pytest.mark.parametrize("word", ["--version", "--help"])
def test_version_and_help_write_nothing(tmp_path, word):
    home = tmp_path / "home"
    home.mkdir()
    r = run_with(tmp_path, {"HOME": str(home)}, word)
    assert r.returncode == 0, (r.returncode, r.stdout + r.stderr)
    assert files_under(home) == []
