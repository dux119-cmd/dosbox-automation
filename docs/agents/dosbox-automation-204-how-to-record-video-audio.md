<!--
This file is part of the dosbox-automation Project.
License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
-->

# 204 How to record video, audio and input

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

## Quick setup

Engine running, token in a file (101):

```sh
export DOSBOX_TOKEN_FILE=$PWD/token
A="Authorization: Bearer $(cat token)"; U=http://127.0.0.1:8386/api/v1
```

## What can be recorded

| What | File | Route |
|---|---|---|
| Video with sound | `videoNNNN.avi`: ZMBV video, PCM 16-bit stereo audio | `capture/video/*` |
| Sound only | `audioNNNN.wav` or your name: PCM 16-bit stereo, 48 kHz | `capture/audio/*` |
| Keys and mouse of a person at the window | JSON in the stop answer, no file | `input/record/*` |
| One frame | PNG or JPEG in the answer | `video/frame` (201) |

- Files go to `capture_dir`, default `<config dir>/capture/`. A relative
  `capture_dir` in a conf counts from that conf's folder (102).
- Numbers go on from the highest file there: `video0001.avi`, `video0002.avi`.

## Video

| Task | curl | dosboxctl |
|---|---|---|
| Start raw | `curl -s -H "$A" -X POST $U/capture/video/start` | `dosboxctl video start` |
| Start rendered | `curl -s -H "$A" -X POST -H 'Content-Type: application/json' -d '{"mode": "rendered"}' $U/capture/video/start` | `dosboxctl video start rendered` |
| Stop | `curl -s -H "$A" -X POST $U/capture/video/stop` | `dosboxctl video stop` |
| Status | `curl -s -H "$A" $U/capture/video/status` | `dosboxctl video status` |
| Compression | `curl -s -H "$A" $U/capture/video/compression`; `-X PUT -d '{"rendered": 5}'` | `dosboxctl video compression`, `dosboxctl video compression rendered 5` |

- Start answers `{"mode": "raw", "status": "recording"}`; stop `{"status": "stopped"}`.
- Status: `{"capturing": true, "mode": "raw", "last_stop_reason": "none"}`;
  after a stop `clean`, `write_error` or `disk_space_low` (recording stopped
  when the capture drive falls below `capture_min_free_space_mb`, default 1024).
- A start while one runs: 409 `Already capturing video`; the running one goes
  on in its mode. Stop with nothing running: 200 `stopped`.
- Compression 0..9, defaults raw 9, rendered 6; applies to the next start.
  PUT while recording: 409. Other values: 400.
- Lua: `dosbox.capture_start("rendered")`, `dosbox.capture_stop()`; a start
  while one runs raises an error.
- dosbox-mcp 0.2.1 and older: `video_capture_start` sends no mode, raw only.
  Later releases: `video_capture_start` and `screen_capture` take `mode`.

## Raw or rendered

| | raw (default) | rendered |
|---|---|---|
| Source | the video adapter's framebuffer | what the window presents, after shader and scaling |
| Size | the mode's own: text 720x400, mode 13h 320x200 | the drawn area: 758x569 headless default, 948x711 default window on a 1280x1024 screen, 1067x800 text and 1200x900 mode 13h at `window_size=1280x960` |
| Needs a window | no | frames only while presented: nothing while minimized; a gap over 2 s is skipped, not padded |
| Shader | never | only with `output = opengl` (the default); `texture` records unshaded |
| Headless (`SDL_VIDEODRIVER=offscreen`) | yes | yes; shaded on Linux, unshaded on macOS (no OpenGL there) |
| OSD text and indicators | no | no: the OSD is drawn on the window after capture |

- Default rendering: `output = opengl`, `shader = crt-auto`. crt-auto picks a
  CRT shader per video mode, but below 3x vertical scale it uses
  `interpolation/sharp`. At the headless default size every mode gets sharp.
- CRT look in a rendered recording: a big enough window, e.g.
  `--set window_size=1280x960` (text: `crt/vga-1080p`, mode 13h:
  `crt/vga-1080p-fake-double-scan`). The log names the shader in use:
  `RENDER: Auto-switched to shader '...'`.
- Check the look before recording: `dosboxctl frame f.png --mode rendered`
  has the rendered video's size and shader.

## One recording, several files

- A new AVI starts whenever width, height, pixel format or frame rate change:
  every video mode switch, in both modes. Text to mode 13h and back: three
  files, even in rendered mode where the size stays (frame rate differs).
- Sort the files by number; each has its own size.

## Converting

- Players want H.264 in 4:2:0, which needs even width and height. Rendered
  sizes are often odd (758x569, 1067x800):

```sh
ffmpeg -i video0001.avi -vf "crop=trunc(iw/2)*2:trunc(ih/2)*2" -c:v libx264 -pix_fmt yuv420p -crf 18 -c:a aac video0001.mp4
```

- Without `-pix_fmt yuv420p` ffmpeg writes H.264 4:4:4, which browsers do not play.

## Clean screen for publication

Mounts in the conf's `[autoexec]`, silent, then a clear screen; the recording
starts at a bare `C:\>`:

```ini
[autoexec]
@echo off
mount c c -pr > nul
c:
cls
```

## Audio

| Task | curl | dosboxctl |
|---|---|---|
| Start, numbered file | `curl -s -H "$A" -X POST $U/capture/audio/start` | `dosboxctl audio start` |
| Start, your name | `curl -s -H "$A" -X POST -H 'Content-Type: application/json' -d '{"path": "out.wav"}' $U/capture/audio/start` | `dosboxctl audio start out.wav` |
| Stop, status | `-X POST $U/capture/audio/stop`, `$U/capture/audio/status` | `dosboxctl audio stop`, `dosboxctl audio status` |

- The file goes into the capture directory: a bare name is taken there, a
  path must lead into it, and an existing file or link is never overwritten.
  The file is created at the start: else 403 `path must name a new file in the
  capture folder`, never a recording elsewhere. The answer's
  `path` is the full host path.
- A start while one runs: 409 `Already capturing audio`; the file stays the
  first one. Stop with nothing running: 200.
- Works headless with `SDL_AUDIODRIVER=dummy`.
- Lua: `dosbox.audio_capture_start("out.wav")`, `dosbox.audio_capture_stop()`.
- Video already carries the sound; record audio alone only for a WAV.

## Input

| Task | curl | dosboxctl |
|---|---|---|
| Start | `curl -s -H "$A" -X POST $U/input/record/start` | `dosboxctl input-record start` |
| Pause, resume (toggle) | `curl -s -H "$A" -X POST $U/input/record/pause` | `dosboxctl input-record pause` |
| Status | `curl -s -H "$A" $U/input/record/status` | `dosboxctl input-record status` |
| Stop, get the events | `curl -s -H "$A" -X POST $U/input/record/stop > rec.json` | `dosboxctl --json input-record stop > rec.json` |
| Replay | `curl -s -H "$A" -X POST -H 'Content-Type: application/json' --data-binary @replay.json $U/input/sequence` | |

- Records only host input: a person typing or moving the mouse in the window.
  Input sent through the API or Lua is never recorded.
- Stop answers `{"event_count", "duration_ms", "events": [{"t", "frame",
  "type", "key", "pressed"}, ...]}`.
- Replay: put the `events` list into `{"events": [...]}` (replay.json) and post
  it to `input/sequence`; answer `{"events_scheduled": N}`. Events play at
  their recorded frames.
- While recording, `PUT cpu/cycles` answers 409 (a speed change breaks replay).
- Second start: 409 `Already recording`. Pause or stop with nothing running:
  409 `Not recording`.

More on replays: 302. Frames and screenshots: 201.
