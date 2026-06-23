---
name: find-stlink
description: List ST-Links and COM ports for LED_Strip_Controller_G474, show bench defaults, and diagnose port/probe lock conflicts. Use when flash/smoke fails with wrong probe or 0.00V.
---

# find-stlink

## Bench target (this machine)

The expected SN / COM / baud live only in [`scripts/bench.defaults.json`](scripts/bench.defaults.json)
(see [`BENCH.md`](BENCH.md)). Print them, then compare against what's actually attached:

```powershell
python scripts\discover.py --show-bench
```

On a multi-probe bench, any *other* connected probe is **not** this board — `discover.py --list`
shows which SN/port matches the bench default so you can confirm before flash/smoke.

## Discovery

```powershell
python scripts\discover.py --list
```

Shows bench defaults, every ST-Link SN + accessibility, every COM port + lock status.

Machine-readable:

```powershell
python scripts\discover.py --list --json
```

## Override per machine

Drop a gitignored `scripts/bench.defaults.local.json` with the keys you want to override (its
keys win over the committed file). Get the real SN/port from `discover.py --list`:

```json
{
  "stlink_sn": "<your ST-Link SN>",
  "com_port": "<your COM port>",
  "baud": 921600
}
```
