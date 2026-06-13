# Scripts for LED_Strip_Controller_G474

This document is written for both humans and AI agents.

The goal is that you (the agent) can read this once and then reliably carry out user requests like:
- "build debug"
- "flash it"
- "run a smoke test on the second ST-Link"
- "build release and flash the V3SET on COM15"

All automation lives in the `scripts/` directory at the project root.

## Quick Reference (for agents)

| Command (natural language) | What to run |
|----------------------------|-------------|
| build debug                | `scripts/build.ps1` (or `scripts/build.sh`) |
| build release              | `scripts/build.ps1 Release` |
| build test                 | `scripts/build.ps1 Test` |
| build debug clean          | `scripts/build.ps1 Debug --clean` |
| build debug incremental    | `scripts/build.ps1 Debug --incremental` |
| flash (current debug build)| `scripts/flash.ps1` |
| flash a specific build     | `scripts/flash.ps1 Release` |
| list available hardware    | `scripts/flash.ps1 --list` or `python scripts/discover.py --list` |
| flash using a specific ST-Link | `scripts/flash.ps1 --stlink-sn 066AFF123456789012345678` |
| smoke test (reset + capture console) | `scripts/smoke-test.ps1` |
| smoke test with explicit hardware | `scripts/smoke-test.ps1 --stlink-sn XXX --port COM15 --baud 921600` |
| PLAY inline string on board | `python scripts/play_bench.py str "CQ4DEFGABC5 *"` (skill: `/playstr`; saves last body for replay) |
| Replay last `/playstr` | `python scripts/play_bench.py replay` (skill: `/replay`) |
| PLAY from `.play` file | `python scripts/play_bench.py file scripts/play_golden/smoke.play` (skill: `/playfile`) |
| PLAY golden test by name | `python scripts/play_bench.py test smoke` (skill: `/playtest`) |
| List PLAY golden tests | `python scripts/play_bench.py list` |
| PLAY P0 scenario batch | `python scripts/play_scenarios.py --scenario P0` or `scripts/run_play_tests.ps1` |

## Environment Variables (stock, preferred)

- `STM32CUBEIDE` — full path to `stm32cubeidec.exe` (or the launcher). The scripts discover this automatically if not set.
- `STM32_PROGRAMMER_CLI` — full path to `STM32_Programmer_CLI.exe`. This is the **canonical** variable created by the STM32Cube installer. Scripts derive the bin directory from it when needed.

**Do not rely on `STM32_PRG_PATH`** unless the user explicitly has it. The scripts prefer the stock `STM32_PROGRAMMER_CLI`.

## Bench defaults (multi-ST-Link setups)

When more than one ST-Link is connected, auto-selection **cannot** guess which probe is your board. This project pins the local bench in:

- **`scripts/bench.defaults.json`** — committed defaults for this workspace
- **`scripts/bench.defaults.local.json`** — optional per-machine override (gitignored)

Current G474 bench values:

| Key | Value |
|-----|-------|
| `stlink_sn` | `003C00193137510C39383538` |
| `com_port` | `COM9` |
| `baud` | `921600` |

`discover.py --default-stlink` and `--default-port` read these files. Flash/smoke scripts call discover when `--stlink-sn` / `--port` are omitted.

Agent skills: `.claude/skills/` (Cursor) and `.grok/skills/` (Grok) both reference the same file.

## Discovery (the key for multi-probe benches)

Run this to see everything the machine can see:

```powershell
python scripts/discover.py --list
# or
scripts/flash.ps1 --list
scripts/smoke-test.ps1 --list
```

Output includes:
- ST-Link serial numbers + **accessibility report** (free / in-use by another app / error)
- For each COM port:
  - Friendly name, Manufacturer, VID:PID (matches Windows Device Manager / TeraTerm)
  - **Accessibility report**: whether the port can currently be opened exclusively (detects TeraTerm, another terminal, CubeIDE debug session, etc. holding the port)

On a machine with an STLINK-V3SET (two VCPs) + possibly other probes, the list will clearly show multiple "STMicroelectronics" ports **and their lock status** so the correct one can be chosen and in-use situations diagnosed before running an operation.

## Build

```powershell
scripts\build.ps1                    # Debug (default, clean build)
scripts\build.ps1 Release
scripts\build.ps1 --config Test
scripts\build.ps1 Debug --clean
scripts\build.ps1 Debug --incremental
```

- Configs: `Debug` (default), `Release`, `Test`
- `--clean` : explicitly force a clean build (this is the default behavior).
- `--incremental` : request an incremental build instead of a full clean.
- Uses headless CubeIDE with a **unique temporary workspace** created each time in:
  - Windows: `%TEMP%\led-g474-headless-ws-YYYYMMDD-HHMMSS`
  - Linux/macOS: `$TMPDIR` (or `/tmp`) `/led-g474-headless-ws-YYYYMMDD-HHMMSS`
- On **successful** build: the temporary workspace is automatically deleted to prevent clutter.
- On **failed** build: the temporary workspace is left behind (with a message) so you or an agent can inspect Eclipse logs for diagnosis.
- The "real" build artifacts (`.elf`, `.bin`, etc.) are placed in the project's normal `Debug/`, `Release/`, or `Test/` folders. The script reports the filesystem modify/create timestamp of the standard ELF (the one without a timestamp in its name). You can see this with `dir` (Windows) or `ls -l` (Unix). No timestamp is baked into the filename itself.
- Uses the same temp-workspace + lock-file handling pattern as the legacy mirror reference scripts to avoid "workspace in use" problems on repeated runs.

## Flash

```powershell
scripts\flash.ps1                    # flashes the Debug build using auto-detected ST-Link
scripts\flash.ps1 Release
scripts\flash.ps1 --stlink-sn 066AFF123456789012345678
scripts\flash.ps1 --list             # show hardware first
```

- Uses `STM32_Programmer_CLI` directly (no bootloader / signing dance — this is a plain hobby project).
- `--stlink-sn` is optional. If omitted and only one ST-Link is present, it auto-selects.
- On complex benches, first run `--list`, then pass the exact serial.
- The `--port` option exists for consistency with smoke-test but is not required for flashing.

## Smoke Test (reset + capture debug console)

This is the "observe what the firmware actually prints after reset" tool.

```powershell
scripts\smoke-test.ps1
scripts\smoke-test.ps1 --stlink-sn 066AFF... --port COM15 --capture-seconds 10 --baud 921600
scripts\smoke-test.ps1 --list
```

What it does:
1. Resets the target using the programmer CLI (reliable even with V3SET multi-VCP setups).
2. Waits ~2 seconds (configurable via `--capture-seconds`).
3. Opens the debug COM port (the one connected to the target's USART2 console) at the specified baud rate (default 115200; override with `--baud`, e.g. 921600).
4. Captures output for the requested number of seconds.
5. Prints everything to the console **and** writes a timestamped log file (e.g. `smoke-2026-06-15-142301.log`).
6. Prints the full path to the log at the end so an agent can read it.

The log contains exactly what a human would see in TeraTerm right after reset — perfect for verifying the banner (project name, TARGET_MCU, FIRMWARE_VERSION, BUILD_NUMBER, reset cause, etc.).

Baud rate is CLI-settable because different projects (or high-speed debug logging) may use rates other than the common 115200 default. The underlying `smoke_capture.py` helper already supported `--baud`; it is now properly exposed on the main `smoke-test.ps1` / `.sh` wrappers.

## Common Agent Patterns

**"Build debug and flash it"**
1. `scripts/build.ps1`
2. `scripts/flash.ps1`

**"Run a smoke test on the V3SET's second port"**
1. `scripts/smoke-test.ps1 --list`   (agent shows the list to the user or parses it)
2. `scripts/smoke-test.ps1 --stlink-sn <the-v3set-sn> --port <the-second-com-port> --baud 921600`

**"Just do a smoke test" (simple bench)**
`scripts/smoke-test.ps1`   — auto everything.

## File Layout

- `scripts/` — the clean, project-specific automation (what you commit and use).
- `not-in-project/` — reference copies of the old mirror-project scripts (for historical patterns only; never committed).
- `SCRIPTS.md` — this file (the single source of truth an agent should read).

## Implementation Notes for Agents

- All scripts prefer stock environment variables.
- Explicit CLI flags (`--stlink-sn`, `--port`, `--baud`, `--config`, path overrides) always win.
- All main scripts support `--help` / `-h` (concise usage diagram + 1-2 line description per option, exactly like standard Linux CLI tools such as `git`, `ls`, or `rsync`). The PowerShell scripts intercept --help before parameter binding; the Python ones use standard argparse.
- When hardware is ambiguous, `--list` is the recommended first step. The list now includes **accessibility reports** for every ST-Link and COM port (free vs. locked/in-use by another app).
- The Python helper (`discover.py` + `smoke_capture.py`) handles the cross-platform / rich USB enumeration + lock detection.
- Build workspaces are created in the OS temp directory with a per-run timestamp and are cleaned up on success (left only on failure for diagnostics). This prevents long-term clutter while still allowing inspection when needed.
- **Clear error messages on resource locks**: If TeraTerm (or any other app) is holding the debug COM port when you try a smoke test, `smoke_capture.py` (and thus the smoke-test wrapper) will print a detailed ERROR message naming the port and common causes, then exit non-zero. The agent should surface this to the user ("The port needed for smoke-testing is in use by another application. Close TeraTerm and retry.").
- Similar lock detection happens in discovery for both ports and ST-Links (e.g. another CubeIDE debug session holding the ST-Link).
- Exit codes are meaningful (0 = success, non-zero = something an agent should report or escalate).

If the user says something casual like "build it and smoke test on the bench ST-Link", you now have everything you need to turn that into the correct sequence of commands, check for locks via --list, run the operation, and diagnose/report "resource in-use" problems clearly.