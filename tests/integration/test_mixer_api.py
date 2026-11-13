# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#
"""A mute from the API or Lua is a manual mute: the mixer status reports it,
and the other side can lift it."""

import time


def run_script(dosbox, source):
    time.sleep(2.1)  # script load rate limiter
    r = dosbox.script_load(source, name="mixer-test")
    assert r.status_code == 200, r.text
    r = dosbox.script_start()
    assert r.status_code == 200, r.text
    data = dosbox.wait_script_done(timeout=10)
    assert data["state"] == "completed", data
    return data.get("output", {})


def muted(dosbox) -> bool:
    r = dosbox.mixer_info()
    assert r.status_code == 200, r.text
    return r.json()["muted"]


def test_api_mute_shows_in_the_status(dosbox):
    r = dosbox.mixer_mute()
    assert r.status_code == 200, r.text
    assert r.json() == {"muted": True}
    assert muted(dosbox) is True

    r = dosbox.mixer_unmute()
    assert r.status_code == 200, r.text
    assert r.json() == {"muted": False}
    assert muted(dosbox) is False


def test_lua_mute_shows_in_mixer_info(dosbox):
    out = run_script(dosbox, (
        "dosbox.mixer_mute()\n"
        "dosbox.output.after_mute = dosbox.mixer_info().muted\n"
        "dosbox.mixer_unmute()\n"
        "dosbox.output.after_unmute = dosbox.mixer_info().muted\n"
    ))
    assert out == {"after_mute": True, "after_unmute": False}, out


def test_api_unmute_lifts_a_lua_mute(dosbox):
    run_script(dosbox, "dosbox.mixer_mute()\n")
    assert muted(dosbox) is True
    assert dosbox.mixer_unmute().status_code == 200
    assert muted(dosbox) is False
