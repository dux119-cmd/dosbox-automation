<!--
This file is part of the dosbox-automation Project.
License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
-->

# 303 Memory trainer

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

Routes and address forms: 203. This file is the method. Example numbers come
from a test program showing `AMMO 99`; Space takes one.

## 1. Search the shown value

```sh
dosboxctl --json mem search 99 0x0 0xA0000 --width 2 > a.json
```

- Width: 2 for most counters in DOS programs, 1 for small ones, 4 for scores
  in the millions. Not sure: search each width.
- Conventional memory: `0x0` to `0xA0000`. Extended memory (protected mode
  games): further spans of at most 16 MiB each; a larger span answers 400
  `Invalid request parameter`.
- Text output: `dosboxctl mem search 99 0x0 0xA0000 --width 2` prints one
  address per line (`00001faa`).

## 2. Change it, search again, keep what is in both

```sh
dosboxctl keys space
dosboxctl --json mem search 98 0x0 0xA0000 --width 2 > b.json
python3 -c 'import json; a = set(json.load(open("a.json"))["matches"]); b = set(json.load(open("b.json"))["matches"]); print(sorted(hex(x) for x in a & b))'
```

- Repeat with each new value until one or two addresses are left
  (`['0x1faa', '0x1fac']`).
- Change it through the program (play), never by writing memory.

## 3. Variable or copy

Two addresses left: write one, let the program use the value once, read both.

```sh
dosboxctl mem write 0x1faa 32 00
dosboxctl keys space
dosboxctl mem read 0x1faa 2; dosboxctl mem read 0x1fac 2
```

- The program counts on from the written value (50, then `AMMO 49`, both
  read `31 00`): this is the variable.
- The program overwrites it from the other one: a copy (write 30 to 0x1fac,
  Space shows `AMMO 48` from the variable).
- Bytes are little-endian: 50 is `32 00`.

## 4. Freeze

```sh
dosboxctl freeze add 0x1faa 99 --width 2
dosboxctl freeze list
dosboxctl freeze clear
```

- Written back once per frame. A program that changes and shows the value in
  the same frame shows one step off: frozen at 99, every Space shows
  `AMMO 98` and memory reads 99 again after the frame.
- At most 256 freezes; the same address again replaces its value; clear
  without an address releases all.
- Freeze the variable, not the copy.

## Unknown value (a bar, no number)

Snapshots of conventional memory before and after each change; keep the
words that moved the way the value did.

```sh
curl -s -H "$A" $U/memory/0/655360 > s1.bin
dosboxctl keys space
curl -s -H "$A" $U/memory/0/655360 > s2.bin
dosboxctl keys space
curl -s -H "$A" $U/memory/0/655360 > s3.bin
python3 -c '
a, b, c = (open(f, "rb").read() for f in ("s1.bin", "s2.bin", "s3.bin"))
w = lambda m, i: int.from_bytes(m[i:i + 2], "little")
once = [i for i in range(len(a) - 1) if w(a, i) - 1 == w(b, i)]
print([hex(i) for i in once if w(b, i) - 1 == w(c, i)])'
```

- Result here: `['0x1fa6', '0x1faa']`. 0x1faa is the variable; 0x1fa6 is the
  program's text of the number (the last digit counts down too). Step 3 tells
  them apart.
- One read of up to 128 MiB per call; 640 KB is one request.
- Other directions: `!=` (changed), `==` (did not change while the value
  stayed), `>` (grew).
- Every search empty although the value is shown: the program may keep it as
  text, BCD or with an offset; the snapshot method finds it anyway.

## Answers

| Answer | Cause |
|---|---|
| 400 `width must be 1, 2, or 4` | freeze or search width |
| 400 `Invalid request parameter` | search span over 16 MiB, malformed body |
| 404 `no freeze at that address` | clear of an address without a freeze |
| 409 `freeze limit reached (256)` | too many freezes |
| 409 with `address` | under paging the address is not mapped (203) |

Reading a value every frame instead of polling: Lua `dosbox.mem_read_word` (301).
