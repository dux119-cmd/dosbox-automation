# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#

"""PATH, APPEND and JOIN texts in the real binary; the unit tests stub messages.
Expected texts: MS-DOS 4.0 USA-MS.MSG and COMMAND.COM print_path (TCMD2A.ASM)."""

import struct
import time

import pytest


def screen_after(dosbox_e2e, tmp_path, lines):
    instance = dosbox_e2e(autoexec_lines=["cls", *lines], conf_dir=tmp_path)
    client = instance.client
    client.wait_shell(timeout=10)
    time.sleep(1.0)
    r = client.screen_text()
    assert r.status_code == 200, r.text
    return [line.rstrip() for line in r.json()["text"].splitlines()]


def output_of(screen, command, occurrence=0):
    prompts = [i for i, line in enumerate(screen) if line.endswith(">" + command)]
    prompt = prompts[occurrence]
    out = []
    for line in screen[prompt + 1 :]:
        if ">" in line and line.split(">", 1)[0].endswith(":\\"):
            break
        out.append(line)
    return [line for line in out if line]


def test_path_display_prefixes_the_variable_name(dosbox_e2e, tmp_path):
    screen = screen_after(dosbox_e2e, tmp_path, ["path C:\\ONE;C:\\TWO", "path"])
    assert output_of(screen, "path") == ["PATH=C:\\ONE;C:\\TWO"]


def test_path_cleared_prints_no_path(dosbox_e2e, tmp_path):
    screen = screen_after(dosbox_e2e, tmp_path, ["path;", "path"])
    assert output_of(screen, "path") == ["No Path"]


def test_path_value_is_upper_cased_like_command_com(dosbox_e2e, tmp_path):
    screen = screen_after(dosbox_e2e, tmp_path, ["path c:\\one;d:\\two", "path"])
    assert output_of(screen, "path") == ["PATH=C:\\ONE;D:\\TWO"]


def test_path_refuses_text_after_the_value(dosbox_e2e, tmp_path):
    screen = screen_after(
        dosbox_e2e, tmp_path, ["path C:\\KEEP", "path c:\\a b", "path c:\\dos,c:\\x", "path"]
    )
    assert output_of(screen, "path c:\\a b") == ["Too many parameters."]
    assert output_of(screen, "path c:\\dos,c:\\x") == ["Too many parameters."]
    assert output_of(screen, "path") == ["PATH=C:\\KEEP"]


def test_append_texts(dosbox_e2e, tmp_path):
    screen = screen_after(
        dosbox_e2e,
        tmp_path,
        [
            "append",
            "append /q",
            "append /e c:\\x",
            "append c:\\x",
            "append",
            "append;",
            "append",
        ],
    )
    assert output_of(screen, "append") == ["No Append"]
    assert output_of(screen, "append /q") == ["Invalid parameter"]
    assert output_of(screen, "append /e c:\\x") == ["Invalid combination of parameters"]
    assert output_of(screen, "append c:\\x") == []
    assert output_of(screen, "append", occurrence=1) == ["APPEND=c:\\x"]
    assert output_of(screen, "append;") == []
    assert output_of(screen, "append", occurrence=2) == ["No Append"]


def test_append_env_mode_sets_and_clears_the_variable(dosbox_e2e, tmp_path):
    screen = screen_after(
        dosbox_e2e,
        tmp_path,
        ["append /e", "append b:\\letters;a:\\reports", "set", "append ;", "set"],
    )
    assert "APPEND=b:\\letters;a:\\reports" in output_of(screen, "set")
    cleared = screen[screen.index("Z:\\>append ;") :]
    assert not [line for line in cleared if line.startswith("APPEND=")]


@pytest.mark.parametrize(
    "command",
    ["join d: c:\\sales", "join d: /d", "join d: c:\\", "join /q"],
)
def test_join_failures_set_errorlevel(dosbox_e2e, tmp_path, command):
    screen = screen_after(dosbox_e2e, tmp_path, [command, "if errorlevel 1 echo LEVEL1"])
    assert output_of(screen, "if errorlevel 1 echo LEVEL1") == ["LEVEL1"]


def test_join_list_succeeds(dosbox_e2e, tmp_path):
    screen = screen_after(dosbox_e2e, tmp_path, ["join", "if errorlevel 1 echo LEVEL1"])
    assert output_of(screen, "if errorlevel 1 echo LEVEL1") == []


def test_set_skips_a_leading_comma_like_command_com(dosbox_e2e, tmp_path):
    # MS-DOS 4.0 TENV.ASM GETARG runs SCANOFF over every DELIM character.
    screen = screen_after(dosbox_e2e, tmp_path, ["set,a=1", "set a"])
    assert output_of(screen, "set a") == ["A=1"]


def test_path_skips_a_leading_comma_but_keeps_a_semicolon(dosbox_e2e, tmp_path):
    # MS-DOS 4.0 TMISC2.ASM PSCANOFF: ';' is not a delimiter for PATH.
    screen = screen_after(dosbox_e2e, tmp_path, ["path,C:\\DOS", "path"])
    assert output_of(screen, "path") == ["PATH=C:\\DOS"]


def test_path_with_only_delimiters_shows_the_path(dosbox_e2e, tmp_path):
    # PGETARG reaches the end of the line and PATH goes to disppath.
    screen = screen_after(dosbox_e2e, tmp_path, ["path C:\\DOS", "path,", "path"])
    assert output_of(screen, "path,") == ["PATH=C:\\DOS"]
    assert output_of(screen, "path") == ["PATH=C:\\DOS"]


# PROMPT (ada-nc0d): MS-DOS 5.0 User's Guide p. 545; the autoexec echoes each
# line after the prompt in force, so the prompt shows in front of it.
def test_prompt_sets_the_prompt_and_prompt_alone_restores_it(dosbox_e2e, tmp_path):
    screen = screen_after(
        dosbox_e2e, tmp_path, ["prompt $n$g", "echo one", "prompt", "echo two"]
    )
    assert "Z>echo one" in screen, screen
    assert "Z:\\>echo two" in screen, screen


def test_prompt_codes_render(dosbox_e2e, tmp_path):
    screen = screen_after(
        dosbox_e2e, tmp_path, ["prompt [$p$q$v$x]$_$n$b$g", "echo marker"]
    )
    at = screen.index("Z|>echo marker")
    assert screen[at - 1] == "[Z:\\=DOS version 5.00]", screen


def test_prompt_takes_an_equal_sign_and_shows_in_set(dosbox_e2e, tmp_path):
    screen = screen_after(dosbox_e2e, tmp_path, ["prompt=$n$g", "set"])
    assert "Z>set" in screen, screen
    assert "PROMPT=$n$g" in output_of(screen, "set"), screen


# BREAK, VERIFY, CTTY (ada-9i0i): texts from MS-DOS 4.0 USA-MS.MSG.
@pytest.mark.parametrize("command", ["break", "verify"])
def test_on_off_commands_show_and_set_their_flag(dosbox_e2e, tmp_path, command):
    screen = screen_after(
        dosbox_e2e,
        tmp_path,
        [command, f"{command} on", command, f"{command} OFF", command, f"{command} x"],
    )
    word = command.upper()
    assert output_of(screen, command) == [f"{word} is off"]
    assert output_of(screen, f"{command} on") == []
    assert output_of(screen, command, occurrence=1) == [f"{word} is on"]
    assert output_of(screen, command, occurrence=2) == [f"{word} is off"]
    assert output_of(screen, f"{command} x") == ["Must specify ON or OFF"]


def test_ctty_nul_hides_the_output_until_ctty_con(dosbox_e2e, tmp_path):
    screen = screen_after(
        dosbox_e2e,
        tmp_path,
        ["ctty nul", "echo hidden-line", "ctty con", "echo shown-line"],
    )
    text = "\n".join(screen)
    assert "hidden-line" not in text, text
    assert "shown-line" in screen, screen


def test_ctty_refuses_what_is_not_a_device(dosbox_e2e, tmp_path):
    screen = screen_after(dosbox_e2e, tmp_path, ["ctty", "ctty nosuchdev", "ctty con nul"])
    assert output_of(screen, "ctty") == ["Invalid device"]
    assert output_of(screen, "ctty nosuchdev") == ["Invalid device"]
    assert output_of(screen, "ctty con nul") == ["Invalid device"]


# The prompt reads end of input from NUL at once; without an idle the shell
# spun inside one call and the web API stopped answering.
def test_the_api_answers_after_ctty_nul_at_the_prompt(dosbox_e2e, tmp_path):
    instance = dosbox_e2e(autoexec_lines=["ctty nul"], conf_dir=tmp_path)
    time.sleep(2.0)
    for _ in range(5):
        r = instance.client.screen_text()
        assert r.status_code == 200, r.text
        time.sleep(0.2)


# DOSKEY (ada-ld1z): the shell has its own history, so DOSKEY loads nothing;
# macros are refused.
def test_doskey_says_it_once_and_loads_high(dosbox_e2e, tmp_path):
    screen = screen_after(
        dosbox_e2e, tmp_path, ["doskey", "lh doskey /bufsize=512 /insert", "doskey /m"]
    )
    assert output_of(screen, "doskey") == [
        "DOSKey installed (the command history is built into the shell)."
    ]
    assert output_of(screen, "lh doskey /bufsize=512 /insert") == []
    assert output_of(screen, "doskey /m") == []


def test_doskey_refuses_macros_and_unknown_switches(dosbox_e2e, tmp_path):
    screen = screen_after(dosbox_e2e, tmp_path, ["doskey ls=dir $*", "doskey /x"])
    assert output_of(screen, "doskey ls=dir $*") == ["DOSKEY macros are not supported."]
    assert output_of(screen, "doskey /x") == ["Illegal switch: /x"]


def test_doskey_history_lists_the_typed_commands(dosbox_e2e, tmp_path):
    instance = dosbox_e2e(autoexec_lines=["cls"], conf_dir=tmp_path)
    client = instance.client
    client.wait_shell(timeout=10)
    for line in ["echo first-typed\n", "doskey /h\n"]:
        deadline = time.monotonic() + 10
        while client.type_string(line).status_code == 409:
            assert time.monotonic() < deadline
            time.sleep(0.1)
        time.sleep(1.5)
    screen = [row.rstrip() for row in client.screen_text().json()["text"].splitlines()]
    assert output_of(screen, "doskey /h")[-2:] == ["echo first-typed", "doskey /h"], screen


# Volume labels and serials (ada-9j99); texts from MS-DOS 4.0 LABL.SKL.
def write_floppy(path, serial=None):
    """A 360K DOS 2 floppy, FAT12; an extended BPB only with a serial.
    The same image as WriteFloppy in tests/volume_label_tests.cpp."""
    image = bytearray(368640)
    image[0:3] = b"\xeb\x3c\x90"
    image[3:11] = b"MSDOS5.0"
    struct.pack_into("<HBHBHHBHHH", image, 11, 512, 2, 1, 2, 112, 720, 0xFD, 2, 9, 2)
    if serial is not None:
        image[0x26] = 0x29
        struct.pack_into("<I", image, 0x27, serial)
        image[0x2B:0x3E] = b"NO NAME    FAT12   "
    image[510:512] = b"\x55\xaa"
    for fat_sector in (1, 3):
        image[fat_sector * 512 : fat_sector * 512 + 3] = b"\xfd\xff\xff"
    path.write_bytes(bytes(image))
    return path


def test_vol_shows_the_serial_of_a_floppy_image(dosbox_e2e, tmp_path):
    disk = write_floppy(tmp_path / "serial.img", 0x1234ABCD)
    screen = screen_after(dosbox_e2e, tmp_path, [f"@mount a {disk} -t floppy", "vol a:"])
    assert " Volume Serial Number is 1234-ABCD" in output_of(screen, "vol a:")


def test_vol_keeps_the_fixed_serial_for_an_image_without_one(dosbox_e2e, tmp_path):
    disk = write_floppy(tmp_path / "plain.img")
    screen = screen_after(dosbox_e2e, tmp_path, [f"@mount a {disk} -t floppy", "vol a:"])
    assert " Volume Serial Number is 0000-1234" in output_of(screen, "vol a:")


# COMMAND.COM message 33 (MS-DOS 4.0 USA-MS.MSG), shared by VOL and DIR.
def test_vol_and_dir_say_a_drive_has_no_label(dosbox_e2e, tmp_path):
    disk = write_floppy(tmp_path / "nolabel.img", 0x1234ABCD)
    screen = screen_after(
        dosbox_e2e, tmp_path, [f"@mount a {disk} -t floppy", "vol a:", "dir a:"]
    )
    assert output_of(screen, "vol a:") == [
        " Volume in drive A has no label",
        " Volume Serial Number is 1234-ABCD",
    ]
    assert output_of(screen, "dir a:")[0] == " Volume in drive A has no label"


# MS-DOS 5.00 prints the label's 11 root entry bytes in VOL and DIR as they
# stand, blanks inside and no dot.
@pytest.mark.parametrize("name11", [b"QUEST_FOR_G", b"DISK      1"])
def test_vol_and_dir_print_the_label_bytes_as_dos_5_does(dosbox_e2e, tmp_path, name11):
    disk = write_floppy(tmp_path / "label.img", 0x1234ABCD)
    image = bytearray(disk.read_bytes())
    root_at = 5 * 512
    image[root_at : root_at + 12] = name11 + b"\x08"
    disk.write_bytes(bytes(image))
    screen = screen_after(
        dosbox_e2e, tmp_path, [f"@mount a {disk} -t floppy", "vol a:", "dir a:"]
    )
    expected = " Volume in drive A is " + name11.decode("ascii")
    assert output_of(screen, "vol a:")[0] == expected
    assert output_of(screen, "dir a:")[0] == expected


def root_label_entry(path):
    """The first live root entry with the volume bit alone, or None."""
    image = path.read_bytes()
    reserved, fats, root_count = struct.unpack_from("<HBH", image, 14)
    per_fat = struct.unpack_from("<H", image, 22)[0]
    root_at = (reserved + fats * per_fat) * 512
    for i in range(root_count):
        entry = image[root_at + i * 32 : root_at + i * 32 + 32]
        if entry[0] == 0x00:
            break
        if entry[0] != 0xE5 and entry[11] == 0x08:
            return entry[:11].decode("ascii")
    return None


def type_line(client, text):
    deadline = time.monotonic() + 10
    while (r := client.type_string(text)).status_code == 409:
        assert time.monotonic() < deadline
        time.sleep(0.1)
    assert r.status_code == 200, r.text


def wait_screen(client, needle, timeout=10):
    deadline = time.monotonic() + timeout
    while True:
        rows = [row.rstrip() for row in client.screen_text().json()["text"].splitlines()]
        if any(needle in row for row in rows):
            return rows
        assert time.monotonic() < deadline, (needle, rows)
        time.sleep(0.2)


def vol_after_cls(client, drive):
    type_line(client, "cls\n")
    type_line(client, f"vol {drive}:\n")
    return wait_screen(client, "Volume Serial Number")


# The engine's stdio buffer reaches the image file at the latest on unmount.
def label_after_unmount(client, disk):
    type_line(client, "mount -u a\n")
    deadline = time.monotonic() + 10
    while True:
        rows = [row.rstrip() for row in client.screen_text().json()["text"].splitlines()]
        if any("removed" in row.lower() for row in rows):
            return root_label_entry(disk)
        assert time.monotonic() < deadline, rows
        time.sleep(0.2)


def test_label_on_a_floppy_image_survives_a_remount(dosbox_e2e, tmp_path):
    disk = write_floppy(tmp_path / "label.img", 0x1234ABCD)
    screen = screen_after(
        dosbox_e2e,
        tmp_path,
        [
            f"@mount a {disk} -t floppy",
            "label a:test1",
            "@mount -u a",
            f"@mount a {disk} -t floppy",
            "vol a:",
        ],
    )
    assert output_of(screen, "vol a:") == [
        " Volume in drive A is TEST1",
        " Volume Serial Number is 1234-ABCD",
    ]
    assert root_label_entry(disk) == "TEST1      "


def test_label_on_a_folder_shows_in_vol_and_makes_no_file(dosbox_e2e, tmp_path):
    folder = tmp_path / "hd"
    folder.mkdir()
    screen = screen_after(
        dosbox_e2e, tmp_path, [f"@mount c {folder}", "label c:hostlbl", "vol c:"]
    )
    assert output_of(screen, "vol c:")[0] == " Volume in drive C is HOSTLBL"
    assert list(folder.iterdir()) == []


def test_label_refuses_a_cd(dosbox_e2e, tmp_path):
    from test_archive_images import minimal_iso

    iso = tmp_path / "cd.iso"
    iso.write_bytes(minimal_iso())
    screen = screen_after(dosbox_e2e, tmp_path, [f"@mount d {iso} -t iso", "label d:x"])
    assert output_of(screen, "label d:x") == ["Cannot LABEL a network drive"]


def test_label_refuses_a_read_only_drive(dosbox_e2e, tmp_path):
    folder = tmp_path / "ro"
    folder.mkdir()
    disk = write_floppy(tmp_path / "ro.img", 0x1234ABCD)
    screen = screen_after(
        dosbox_e2e,
        tmp_path,
        [f"@mount d {folder} -ro", f"@mount a {disk} -t floppy -ro", "label d:x", "label a:x"],
    )
    assert output_of(screen, "label d:x") == ["Drive D is read-only; its label cannot change"]
    assert output_of(screen, "label a:x") == ["Drive A is read-only; its label cannot change"]
    assert root_label_entry(disk) is None


def test_label_with_bad_characters_asks_again(dosbox_e2e, tmp_path):
    disk = write_floppy(tmp_path / "bad.img", 0x1234ABCD)
    instance = dosbox_e2e(autoexec_lines=[f"@mount a {disk} -t floppy", "cls"], conf_dir=tmp_path)
    client = instance.client
    client.wait_shell(timeout=10)
    type_line(client, "label a:bad*\n")
    rows = wait_screen(client, "ENTER for none)?")
    assert "Volume in drive A has no label" in rows
    assert "Volume Serial Number is 1234-ABCD" in rows
    assert "Invalid characters in volume label" in rows
    type_line(client, "good disk\n")
    assert " Volume in drive A is GOOD DISK" in vol_after_cls(client, "a")
    assert label_after_unmount(client, disk) == "GOOD DISK  "


def test_label_prompt_enter_then_y_deletes_the_label(dosbox_e2e, tmp_path):
    disk = write_floppy(tmp_path / "del.img", 0x1234ABCD)
    instance = dosbox_e2e(
        autoexec_lines=[f"@mount a {disk} -t floppy", "label a:old", "cls"], conf_dir=tmp_path
    )
    client = instance.client
    client.wait_shell(timeout=10)
    type_line(client, "label a:\n")
    rows = wait_screen(client, "ENTER for none)?")
    assert "Volume in drive A is OLD" in rows
    type_line(client, "\n")
    wait_screen(client, "Delete current volume label (Y/N)?")
    type_line(client, "y")
    assert " Volume in drive A has no label" in vol_after_cls(client, "a")
    assert label_after_unmount(client, disk) is None


# Batch files answer LABEL from a file: each answer is one line of it.
def test_label_answers_from_a_redirected_file_line_by_line(dosbox_e2e, tmp_path):
    disk = write_floppy(tmp_path / "redir.img", 0x1234ABCD)
    folder = tmp_path / "c"
    folder.mkdir()
    (folder / "ANSWERS.TXT").write_bytes(b"\r\nY\r\n")
    screen_after(
        dosbox_e2e,
        tmp_path,
        [
            f"@mount a {disk} -t floppy",
            f"@mount c {folder}",
            "label a:old",
            "label a: < c:\\answers.txt",
            "@mount -u a",
        ],
    )
    assert root_label_entry(disk) is None
