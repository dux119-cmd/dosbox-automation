<!--
This file is part of the dosbox-automation Project.
License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
-->

# 202 How to run a game

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

### Token setup

Agent (every tool call a new shell): token in a file, engine detached (101).

```sh
python3 -c "import secrets; print(secrets.token_hex(32))" > token; chmod 600 token
export DOSBOX_TOKEN_FILE=$PWD/token        # repeat in every later call
```

### Mount root

One folder per game, the conf in it: the folder is the conf anchor, so MOUNT
and the swap route reach everything below it (103).

```
game/
  dosbox.conf
  c/                 empty folder: the hard disk the game installs to
  disks/disk1.ima    the install floppies
  disks/disk2.ima
```

`game/dosbox.conf`:

```ini
[dos]
keyboard_layout = us

[autoexec]
mount c c -pr
mount a disks/disk1.ima -t floppy -pr
c:
```

- A game already installed: `c/` holds it, no floppies.
- A game in a zip, rar, 7z: `mount c game.zip -pr` (104).
- Write MOUNT lines in `[autoexec]`, not typed later: the drives are there
  before the first API call.

## How to start

```sh
DOSBOX_API_TOKEN=$(cat token) setsid nohup dosbox-automation -conf game/dosbox.conf \
    --set webserver_enabled=true > dosbox.log 2>&1 < /dev/null &
echo $! > dosbox.pid
dosboxctl wait 'C:\>' 30
```

macOS has no `setsid`: `nohup ... & disown` instead (101).

### Install from floppies (worked example: DOOM shareware, two disks)

| Step | Command | Wait for |
|---|---|---|
| Go to the install drive | `dosboxctl type 'a:'` | `dosboxctl wait 'A:\>' 10` |
| Start the installer | `dosboxctl type 'setup'` | `dosboxctl wait 'Available Drives' 30` |
| Answer a one-key prompt | `dosboxctl type --no-enter C` | `dosboxctl wait 'disk DOOM 2' 120` |
| Change the disk | `dosboxctl swap a disks/disk2.ima` | |
| Tell the installer | `dosboxctl key enter` | the next screen |
| Menus with defaults | `dosboxctl key enter`, one at a time | check the screen after each |
| Game started | | `dosboxctl screen-info`: `is_graphics_mode: true` |

- Start installers from their own drive. DOOM's setup run as `a:\setup` from
  C: answers `Can't find SETUP.DTA on drive C: - switch to the floppy drive and
  try again.`
- One-key prompts (a drive letter, Y/N): `type --no-enter`, or `key <name>`.
- A menu sequence differs between versions: loop until the target state (a
  text, graphics mode, a program name), never a fixed number of Enters.
- Disk change: swap when the program asks ("insert disk 2"), then send the key
  it wants. Agents swap only through the route; Ctrl+F4 injected through the
  input API does not change disks (103).
- Proof the install landed: the files on the host, here
  `game/c/GOLD/DOOM/DOOM.EXE` with its 1994 date.

### Start an installed game

| Task | Command |
|---|---|
| From its folder | `dosboxctl type 'cd \gold\doom'`, then `dosboxctl type 'doom'` |
| From a mount | `mount c c -pr -run GOLD\DOOM\DOOM` in `[autoexec]` (8.3 path on the drive, 104) |
| Is it running? | `dosboxctl program` (`segment_name`), `dosboxctl screen-info` (graphics) |

- `segment_name` names the program DOS started last from the shell. A game
  started by another program (DOOM by its setup) still shows the parent:
  trust the screen or a frame.

## How to interface

Text-mode screens (installers, setup menus): `dosboxctl screen`, `dosboxctl wait
<text>`. Graphics: `dosboxctl frame shot.png`, then look at it.

| Rule | Why |
|---|---|
| Before typing, the prompt must be back: `dosboxctl wait 'C:\>'` or `program` with `segment_name` empty | keys typed while a program waits for "press a key" go to that program (the first key ends it, the rest lands at the prompt as junk) |
| After Escape, Enter or a menu key, look at a frame before the next input | Escape opens many games' menus; every move afterwards goes into the menu |
| Hold keys to move: `dosboxctl keys right:900 leftctrl:300` | taps of 50 ms move a game character a pixel or not at all |
| A game's "press a key" screen: `dosboxctl key enter --hold 100` | a game that polls the keyboard can miss a 50 ms tap between two polls |
| Check each move with a frame; two identical frames mean nothing moved | walls, trees and idle animations swallow input (Keen sits down to read; the next key only stands him up) |
| Fire, jump and use keys differ per game; try, then look | Keen 4: Ctrl jumps, Alt is the pogo stick, Space fires |
| A HUD can mislead: unlit LED segments read as digits | Keen 4's shot counter `85` at the start is 5 |
| Game knowledge (where a level entrance is) is not in the API | world maps cost every model in the cold tests the most time |

- Frame-exact input, or a reaction in the same frame: Lua (301).
- Memory and the trainer loop (the 99-shots kind): 203 and 303.
- Recording the run: 204.

## How to shutdown

```sh
dosboxctl shutdown
sleep 2; kill -0 "$(cat dosbox.pid)" 2>/dev/null && kill "$(cat dosbox.pid)"
```

- Quit the game first through its own menu when it saves settings on exit (DOOM
  writes `DEFAULT.CFG`).
- Never `pkill dosbox`: other engines on the machine die with it.

