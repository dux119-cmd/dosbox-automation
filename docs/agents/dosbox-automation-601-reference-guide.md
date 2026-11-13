<!--
This file is part of the dosbox-automation Project.
License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
-->

# 601 Reference

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

## Routes

All under `/api/v1`, all need the token. Request: `?` query parameters,
`{}` JSON body fields. Answer: the JSON fields of a 200 from a live call.
Errors: besides 401 and 503 (below). Full shapes: `/openapi.json`, Swagger at
`/api.html`. Every route has a `dosboxctl` command: `dosboxctl --help`.

### Engine

| Method | Path | Request | Answer | Errors |
|---|---|---|---|---|
| GET | `/status` |  | canonical_name, is_booted, is_shell, program, running, shutdown_requested |  |
| GET | `/dosbox/info` |  | appimage, binary, features, version |  |
| GET | `/program/state` |  | canonical_name, is_booted, is_shell, segment_name |  |
| POST | `/dosbox/shutdown` |  | (shutdown; 101) |  |
| POST | `/control/shutdown` |  | status |  |

### Screen

| Method | Path | Request | Answer | Errors |
|---|---|---|---|---|
| GET | `/video/text` |  | bios_mode, columns, is_text_mode, page, rows, text | 409 |
| GET | `/video/frame` | ?format ?mode ?quality | image/png |  |
| GET | `/video/frame/info` |  | double_height, double_width, height, is_paletted, pitch, pixel_format, rendered_double_scan, video_mode, width |  |

### Input

| Method | Path | Request | Answer | Errors |
|---|---|---|---|---|
| POST | `/input/type` | {text, cps} | chars, status | 400 409 |
| POST | `/input/sequence` | {events} | events_scheduled, status | 400 |
| POST | `/input/record/start` |  | status | 409 |
| POST | `/input/record/pause` |  | status | 409 |
| POST | `/input/record/stop` |  | duration_ms, event_count, events | 409 |
| GET | `/input/record/status` |  | duration_ms, event_count, paused, recording |  |

### Memory, CPU, ports

| Method | Path | Request | Answer | Errors |
|---|---|---|---|---|
| GET | `/memory/{offset}/{len}` |  | application/octet-stream | 400 409 |
| GET | `/memory/{segment}/{offset}/{len}` |  | application/octet-stream | 400 409 |
| PUT | `/memory/{offset}` | application/octet-stream {data} | memory | 400 409 412 |
| PUT | `/memory/{segment}/{offset}` | application/octet-stream {data} | memory | 400 409 412 |
| POST | `/memory/search` | {value, width, start, end} | matches, skipped | 400 |
| POST | `/memory/allocate` | {size, area, strategy} | addr | 400 409 |
| POST | `/memory/free` | {addr} | empty | 400 409 |
| POST | `/memory/freeze` | {address, value, width} | address, status, value, width | 400 409 |
| GET | `/memory/freeze` |  | count, freezes |  |
| DELETE | `/memory/freeze` | {address} | status | 404 |
| GET | `/cpu/state` |  | registers |  |
| PUT | `/cpu/register` | {register, value} | register, status, value | 400 |
| GET | `/cpu/cycles` |  | auto_adjust, current_max, mode, pmode, protected_mode, protected_mode_auto, real_mode, throttle |  |
| PUT | `/cpu/cycles` | {cycles} | cycles, status | 400 409 |
| GET | `/io/port` | ?port ?width | port, value, width | 400 |
| PUT | `/io/port` | {port, value, width} | port, status, value, width | 400 |
| GET | `/dos/internals` |  | dosSwappableArea, firstShell, listOfLists, memoryMap | 409 |

### Drives

| Method | Path | Request | Answer | Errors |
|---|---|---|---|---|
| POST | `/drive/swap` | {drive, image} | drive, status | 400 403 |
| POST | `/mount/lock` |  | status |  |
| GET | `/mount/lock` |  | locked |  |

### Lua

| Method | Path | Request | Answer | Errors |
|---|---|---|---|---|
| POST | `/script/load` | ?name ?seed ?debug text/plain | name, status | 400 413 415 429 |
| POST | `/script/start` |  | status | 400 |
| GET | `/script/status` |  | frame, name, state |  |
| POST | `/script/stop` |  | status |  |

### Recording

| Method | Path | Request | Answer | Errors |
|---|---|---|---|---|
| POST | `/capture/video/start` | {mode} | mode, status | 400 409 500 |
| POST | `/capture/video/stop` |  | status | 500 |
| GET | `/capture/video/status` |  | capturing, last_stop_reason, mode |  |
| GET | `/capture/video/compression` |  | raw, rendered |  |
| PUT | `/capture/video/compression` | {raw, rendered} | raw, rendered | 400 409 |
| POST | `/capture/audio/start` | {path} | status | 400 403 409 500 |
| POST | `/capture/audio/stop` |  | status | 500 |
| GET | `/capture/audio/status` |  | capturing | 500 |

### Mixer

| Method | Path | Request | Answer | Errors |
|---|---|---|---|---|
| GET | `/mixer` |  | channels, master_volume, muted, sample_rate | 500 |
| PUT | `/mixer/volume` | {left, right} | left, right | 400 500 |
| POST | `/mixer/mute` |  | muted | 500 |
| POST | `/mixer/unmute` |  | muted | 500 |
| PUT | `/mixer/channel/{name}` | {left, right} | channel, left, right | 400 404 500 |

## Answers every route can give

| Answer | Meaning |
|---|---|
| 401 | missing or wrong token |
| 503 `Emulation busy, retry later`, `Retry-After: 1` | the emulation thread did not take the command in time; nothing ran, send it again |
| 409 `{"error": ..., "address": N}` | under paging the address is not mapped; nothing touched (203) |
| 400 `Invalid request parameter` | a path or query value that does not parse |

## Where things are

| What | Where |
|---|---|
| Config dir | Linux, BSD `~/.config/dosbox-automation`; macOS `~/Library/Preferences/dosbox-automation`; Windows `%LOCALAPPDATA%\dosbox-automation` |
| Token file | `<config dir>/webserver/api_token` (`webserver_token_file = true`) |
| Recordings | `<config dir>/capture/` (`capture_dir`), 204 |
| Lua debug traces | `<config dir>/logs/lua-debug-<date>-<time>-<name>.log`, 301 |
| Key names | `KBD_` names, 201 |
| Lua functions | 301 |
| Settings for automation | 102 |

## Settings an agent touches

| Section | Setting | Default |
|---|---|---|
| `[webserver]` | `webserver_enabled` | false |
| | `webserver_port` | 8386 |
| | `webserver_token_file` | false |
| | `webserver_osd` | true |
| | `webserver_allow_remote` | false (leave it) |
| | `webserver_tool_token` | embed |
| | `mount_allowed_bases`, `mount_allowed_image_roots` | empty; read from the primary conf only (103) |
| `[capture]` | `capture_dir` | capture |
| | `capture_min_free_space_mb` | 1024 |
| | `capture_video_compression`, `capture_video_compression_rendered` | 9, 6 |
| `[cpu]` | `cpu_cycles` | 3000 |
| `[sdl]` | `output` | opengl |
| | `window_size` | default |
| | `fullscreen` | false |
| `[render]` | `shader` | crt-auto |
| `[dos]` | `keyboard_layout` | us (`auto` is us too; never follows the host) |
| `[dosbox]` | `machine`, `memsize` | svga_s3, 16 |
