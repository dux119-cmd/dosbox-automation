<!--
This file is part of the dosbox-automation Project.
License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
-->

# 201 Basic usage

## Minimum (same in every file)

- dosbox-automation: DOS emulator, controlled over an HTTP REST API.
- API off by default. Enable at start: `--set webserver_enabled=true`, or
  `webserver_enabled = true` in `[webserver]` of a conf. Not switchable at runtime.
- Base URL: `http://127.0.0.1:8386/api/v1/`. Port `webserver_port` (default 8386).
  Binds 127.0.0.1 only.
- Every `/api/v1/` call needs header `Authorization: Bearer <token>`. Else 401.
  - Preferred: env `DOSBOX_API_TOKEN=<64 hex chars>` set before start.
  - Or `--set webserver_token_file=true`: engine writes
    `<config dir>/webserver/api_token`.
  - Neither: random token, log shows 8 chars only. Unusable.
- Config dir: Linux, BSD `~/.config/dosbox-automation` (`$XDG_CONFIG_HOME`
  wins), macOS `~/Library/Preferences/dosbox-automation`, Windows
  `%LOCALAPPDATA%\dosbox-automation` (`%XDG_CONFIG_HOME%` wins when set).
- No token needed: `/` (start page), `/api.html` (Swagger UI), `/openapi.json`
  (full spec; source copy `resources/webserver/openapi.json`).
- Interfaces:
  - REST: curl or any HTTP client.
  - dosbox-mcp: MCP server. `pipx install dosbox-mcp` or `uvx dosbox-mcp`.
    Token from env `DOSBOX_API_TOKEN` or the token file.
  - dosboxctl: command-line client for every route. Needs bash, curl, python3
    (Windows: Git Bash, MSYS2 or WSL). Release packages: at the package root,
    beside the binary; AppImage: `dosbox-automation-*.AppImage --dosboxctl ...`;
    source tree: `scripts/clients/dosboxctl`. Token from `DOSBOX_API_TOKEN`,
    `DOSBOX_TOKEN_FILE` or the token file; port `DOSBOX_PORT`. `dosboxctl --help`.
- Stop: `POST /api/v1/dosbox/shutdown`. Fallback: kill your own PID. Never
  `pkill dosbox`: other sessions on the machine die with it.

```sh
TOKEN=$(python3 -c "import secrets; print(secrets.token_hex(32))")
DOSBOX_API_TOKEN=$TOKEN dosbox-automation --set webserver_enabled=true &
curl -s -H "Authorization: Bearer $TOKEN" http://127.0.0.1:8386/api/v1/status
curl -s -X POST -H "Authorization: Bearer $TOKEN" http://127.0.0.1:8386/api/v1/dosbox/shutdown
# the same with dosboxctl:
DOSBOX_API_TOKEN=$TOKEN dosboxctl status
DOSBOX_API_TOKEN=$TOKEN dosboxctl shutdown
```

## Setup for the commands below

Engine running with the API on and `$TOKEN` set (101).

```sh
A="Authorization: Bearer $TOKEN"; U=http://127.0.0.1:8386/api/v1
J='Content-Type: application/json'
export DOSBOX_API_TOKEN=$TOKEN    # dosboxctl; another port: export DOSBOX_PORT=<port>
```

## Type text

| Task | curl | dosboxctl |
|---|---|---|
| Line plus Enter | `curl -s -H "$A" -H "$J" -X POST -d '{"text": "dir\n"}' $U/input/type` | `dosboxctl type dir` |
| Without Enter | `-d '{"text": "dir"}'` | `dosboxctl type --no-enter dir` |
| Speed | `-d '{"text": "dir\n", "cps": 100}'` (default 30 per s) | `dosboxctl type --cps 100 dir` |

- Answer at once: `{"chars": 4, "status": "ok"}`. Typing runs on for chars / cps seconds.
- Input while typing runs: 409 `{"error": "Replay already in progress"}`. Wait
  for idle: POST `{"events": []}` to `$U/input/sequence` until 200.
  dosboxctl waits before and after on its own.
- Characters: printable ASCII, `\n` = Enter, `\t` = Tab. Anything else (é, `\r`)
  is dropped without an error.
- Sent as US key positions. DOS layout must be US: `keyboard_layout = us`,
  the default (`auto` also means `us`). Another layout (`de`: `y`/`z`
  swapped, `-` wrong) breaks typing. The layout never follows the host; a
  human at a German keyboard in the same engine sets `de` or runs `KEYB GR`.
- At most 4096 characters per call.
- curl: `"` and `\` inside the text need JSON escapes (`\"`, `\\`). dosboxctl
  takes the text as given.

## Keys

| Task | curl | dosboxctl |
|---|---|---|
| Tap Esc | `curl -s -H "$A" -H "$J" -X POST -d '{"events": [{"type": "key", "key": "KBD_esc", "pressed": true}, {"type": "key", "key": "KBD_esc", "pressed": false, "delay_ms": 50}]}' $U/input/sequence` | `dosboxctl key esc` |
| Down three times | three taps in one `events` list | `dosboxctl key down 3` |
| Several keys in order | taps in one list | `dosboxctl keys f1 enter` |
| Hold a key (walk, run) | release with `"delay_ms": 900` | `dosboxctl key right --hold 900` |
| Holds and taps in order | one list | `dosboxctl keys right:900 leftctrl:300 space` |

- A held key repeats at the keyboard's typematic rate, as on real hardware:
  at the DOS prompt a 1.5 s hold of `a` types about 30 of them.
- dosboxctl returns after the last release, so the next call starts fresh.
- A game that polls the keyboard (a "press a key" screen) can miss a 50 ms tap.
  Hold the key a frame or two: `dosboxctl key enter --hold 100`.

- Answer: `{"events_scheduled": 2, "status": "ok"}`.
- Event: `{"type": "key", "key": "KBD_<name>", "pressed": true}`. Tap = press,
  then release.
- Timing: `delay_ms` = ms after the previous event; `t` = ms from the start of
  the list. Not both in one event. Neither: same moment as the previous one.
- Combination (Ctrl+C): press `KBD_leftctrl`, press `KBD_c`, release `KBD_c`,
  release `KBD_leftctrl`.
- Names always with `KBD_` (bare `esc`: 400 `Unknown key: esc`). dosboxctl adds it.
- At most 32000 events per call. Unknown field: 400 naming the allowed fields.

Key names (after `KBD_`):
- Letters `a`..`z`, digits `0`..`9`, `f1`..`f12`
- `esc tab backspace enter space capslock scrolllock numlock`
- `leftshift rightshift leftctrl rightctrl leftalt rightalt leftgui rightgui`
- `grave minus equals backslash leftbracket rightbracket semicolon quote comma
  period slash oem102 abnt1`
- `printscreen pause insert home pageup delete end pagedown left up down right`
- `kp0`..`kp9 kpdivide kpmultiply kpminus kpplus kpenter kpperiod`

## Mouse

| Task | curl | dosboxctl |
|---|---|---|
| Move | `-d '{"events": [{"type": "mouse_move", "x_rel": 10, "y_rel": -4}]}'` | `dosboxctl mouse move 10 -4` |
| Click | `mouse_button` press and release, `"button": "left"` | `dosboxctl mouse click left` |
| Wheel | `-d '{"events": [{"type": "mouse_wheel", "delta": 1}]}'` | (curl only) |

- Same route as keys (`$U/input/sequence`), same timing fields.
- `x_rel`, `y_rel`: relative motion, scaled by the `[mouse]` sensitivity setting.
  `x_abs`, `y_abs` are accepted and recorded, not applied.
- Buttons: `left`, `right`, `middle`.
- DOS programs see it through the built-in mouse driver
  (`builtin_dos_mouse_driver = on`, the default).
- No route or Lua function reports where the cursor is. Look at a frame after
  each move.

## Read the screen

| Task | curl | dosboxctl |
|---|---|---|
| Screen text | `curl -s -H "$A" $U/video/text` | `dosboxctl screen` |
| Wait for text | loop below | `dosboxctl wait 'bytes free' 30` (exit 1 on timeout) |

```sh
until curl -s -H "$A" $U/video/text | grep -q 'bytes free'; do sleep 0.2; done
```

- Answer: `{"text", "rows", "columns", "bios_mode", "is_text_mode", "page"}`.
  Rows joined by `\n`, each padded to `columns`. DOS characters converted to
  UTF-8 (box drawing included).
- Text modes only. Graphics mode: 200 with `"is_text_mode": false`, `"text": ""`,
  rows and columns 0. Use a screenshot.
- In the raw JSON a backslash is `\\`: grep for `C:\\\\>` or use dosboxctl.

## Screenshot

| Task | curl | dosboxctl |
|---|---|---|
| PNG | `curl -s -H "$A" -o shot.png "$U/video/frame?format=png"` | `dosboxctl frame shot.png` |
| As shown in the window | `...&mode=rendered` | `dosboxctl frame shot.png --mode rendered` |
| JPEG | `?format=jpeg&quality=90` (default format, quality 98) | `dosboxctl frame shot.jpg --format jpeg --quality 90` |
| Mode and size | `curl -s -H "$A" $U/video/frame/info` | `dosboxctl screen-info` |

- `mode=raw` (default): the adapter's framebuffer, no shader, no scaling.
  `mode=rendered`: after shader and scaling; works headless too.
- 503 with `Retry-After: 1` until the first frame is drawn. Rendered: 503 when
  nothing is presented (paused, minimized).
- `format=raw`: little-endian header, then pixels. uint32 width, uint32 height,
  int32 pitch, uint8 pixel format, uint16 palette count, palette (3 bytes per
  entry), height * |pitch| bytes.
- frame/info: `width`, `height`, `pitch`, `pixel_format`, `is_paletted`,
  `video_mode` (`bios_mode_number`, `is_graphics_mode`, `graphics_standard`,
  `color_depth`, `width`, `height`, `is_double_scanned`).

## What is running

| Task | curl | dosboxctl |
|---|---|---|
| Program | `curl -s -H "$A" $U/program/state` | `dosboxctl program` |

- At the prompt: `"segment_name": ""`. A program: `"segment_name": "EDIT"`,
  `"canonical_name": "Y:\\DOS\\EDIT.EXE"`.
- `is_shell` stays true while a program runs from the shell. Booted from a disk
  image: `is_booted` true.
- `/status` has `program` too: the window title's name, kept after the program
  ends. For "what runs now" use `/program/state`.

## Memory in short

| Task | curl | dosboxctl |
|---|---|---|
| 8 bytes at 0040:0010 | `curl -s -H "$A" $U/memory/0x40/0x10/8` (raw bytes) | `dosboxctl mem read 0x40:0x10 8` (hex dump) |

More: 203.

## Errors

| Answer | Meaning | Do |
|---|---|---|
| 409 `Replay already in progress` | earlier input still running | wait for idle, send again |
| 400 `Unknown key: X` | name without `KBD_` or not in the list | fix the name |
| 400 `Unknown field 'x' ...` | misspelled event field | use the listed fields |
| 503 + `Retry-After` | emulation busy, nothing done | send again |

Frame-exact input or a reaction inside the same frame: Lua (301).
