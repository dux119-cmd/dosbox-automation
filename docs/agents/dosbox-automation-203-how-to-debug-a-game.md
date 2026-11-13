<!--
This file is part of the dosbox-automation Project.
License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
-->

# 203 How to debug a game

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

## Addresses

| Form | Example | Means |
|---|---|---|
| linear | `0x410` | the physical byte address |
| segment:offset | `0x40:0x10` | segment * 16 + offset (here 0x410) |
| register:offset | `ds:0x7a58`, `cs:0x100` | the register's base now (read and write only) |

- `mem search`, `freeze`, `mem free` take linear or numeric seg:off only.
- Words and dwords are little-endian: 99 as a word is `63 00`.

## Registers

| Task | curl | dosboxctl |
|---|---|---|
| All registers | `curl -s -H "$A" $U/cpu/state` | `dosboxctl regs` |
| Set one | `curl -s -H "$A" -X PUT -H 'Content-Type: application/json' -d '{"register": "eax", "value": 4660}' $U/cpu/register` | `dosboxctl reg set eax 0x1234` |

- Readable: `eax`..`ebp`, `cs ds es ss fs gs`, `eip`, `flags`. Writable:
  `eax`..`ebp` and the segment registers (segments 0..0xFFFF). No 16-bit names
  (`ax` is `Unknown register: ax`).
- The values are those between two emulated instructions, not "the game's
  variables": a register changes thousands of times per frame.

## Memory: read, write, search

| Task | curl | dosboxctl |
|---|---|---|
| Read 16 bytes | `curl -s -H "$A" $U/memory/0x40/0x10/16` (raw bytes) | `dosboxctl mem read 0x40:0x10 16` (hex dump) |
| Read as JSON | add `-H 'Accept: application/json'`: `{"memory": {"addr", "data": base64}, "registers"}` | `dosboxctl --json mem read 0x410 16` |
| Write bytes | `curl -s -H "$A" -X PUT -H 'Content-Type: application/octet-stream' --data-binary @bytes.bin $U/memory/0x50/0x0` | `dosboxctl mem write 0x50:0x0 63 00` |
| Write only if unchanged | add `-H 'If-Match: <base64 of the expected bytes>'` | |
| Search a value | `curl -s -H "$A" -X POST -H 'Content-Type: application/json' -d '{"value": 99, "width": 2, "start": 0, "end": 655360}' $U/memory/search` | `dosboxctl mem search 99 0x0 0xA0000 --width 2` |

- Read routes take the length in the path (`/memory/SEG/OFF/LEN`), write routes
  do not (`/memory/SEG/OFF`). `PUT /memory/0x3b655/2` reads 0x3b655 as a segment
  and answers 400 `Invalid request parameter`.
- Write bodies: `application/octet-stream` (raw bytes) or `application/json`
  with `{"data": base64}`. Raw bytes from a file (`--data-binary @file`), never
  through `$(printf ...)`: the shell drops zero bytes.
- `If-Match` mismatch: 412 with the bytes that are there; nothing written.
- Search: one span of at most 16 MiB per call; width 1, 2 or 4; answer
  `{"matches": [linear addresses], "skipped": [unmapped ranges]}`.
  Conventional memory is 0x0..0xA0000.

## Narrowing a value down (the trainer loop)

1. Search the value as the program shows it (`mem search 5 0x0 0xA0000 --width 2`).
2. Change it in the program (fire a shot, spend a coin), search the new value,
   keep the addresses in both lists.
3. Repeat until one or two are left.
4. Two left: write one, wait a frame, read both. The one that holds is the
   variable; the one that falls back is a copy the program rewrites from it.
5. Prove it through the program: use it once more and watch it count from the
   value you wrote.

- A shown value can mislead (an unlit LED segment read as a digit: Keen 4's
  `85` is 5). When every search comes back empty, doubt the reading first.
- Known neighbours help: search for the bytes around a value (score, then
  next extra life) instead of the value itself.

## Freeze a value

| Task | curl | dosboxctl |
|---|---|---|
| Hold 99 at 0x12340 | `curl -s -H "$A" -X POST -H 'Content-Type: application/json' -d '{"address": 74560, "value": 99, "width": 2}' $U/memory/freeze` | `dosboxctl freeze add 0x12340 99 --width 2` |
| List | `curl -s -H "$A" $U/memory/freeze` | `dosboxctl freeze list` |
| Release one, all | `-X DELETE -d '{"address": 74560}'`, `-X DELETE` without a body | `dosboxctl freeze clear 0x12340`, `dosboxctl freeze clear` |

- 0x12340 stands for any address. A game's values sit at different addresses
  in every game and often in every version: find yours first (303).

- Written back once per frame: between two frames the program can see its own
  value. Freeze the variable, not a copy (a frozen copy hides the mistake).
- Freezes stay until cleared or the engine restarts.

## Ports, cycles, DOS

| Task | curl | dosboxctl |
|---|---|---|
| Read a port | `curl -s -H "$A" "$U/io/port?port=0x3da"` | `dosboxctl port read 0x3da` |
| Write a port | `curl -s -H "$A" -X PUT -H 'Content-Type: application/json' -d '{"port": 128, "value": 18}' $U/io/port` | `dosboxctl port write 0x80 0x12` |
| Speed | `curl -s -H "$A" $U/cpu/cycles`; `-X PUT -d '{"cycles": 12000}'` | `dosboxctl cycles`, `dosboxctl cycles 12000` |
| DOS memory blocks | `curl -s -H "$A" $U/dos/internals` | `dosboxctl dos-internals` |
| Allocate, free | `POST $U/memory/allocate {"size": 16}` (bytes), `POST $U/memory/free {"addr": N}` | `dosboxctl mem alloc 16`, `dosboxctl mem free <addr>` |

- Ports: 0x0000..0xFFFF, width 1 or 2. A port read can change hardware state
  (reading a status port clears it): read what you understand.
- `dos/internals`: `listOfLists`, `dosSwappableArea`, `firstShell`, and
  `memoryMap`, every DOS memory block (`segment`, `pspSegment`, `sizeParas`,
  `sizeBytes`, `filename`, `isLast`); `filename` is often empty.
- `PUT cpu/cycles` sets fixed cycles in real and protected mode and answers
  `{"cycles": 12000, "status": "ok"}`. 409 while input is being recorded, and
  409 `Legacy cycles mode active` when the conf uses the old `cycles` key
  (write `cpu_cycles`, 102).

## Errors

| Answer | Meaning |
|---|---|
| 409 `{"error": ..., "address": N}` | under paging the guest's page tables do not map that address; nothing touched, no fault in the guest |
| 400 `Address out of range` | the range runs past the end of memory |
| 400 `Invalid request parameter` | a malformed path or body (see the read and write route shapes above) |
| 412 | `If-Match` did not match |

More on the trainer loop: 303. Lua for reading memory every frame: 301.

