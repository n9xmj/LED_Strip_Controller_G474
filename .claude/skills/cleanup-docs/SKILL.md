---
name: cleanup-docs
description: Prune ephemeral PLAY planning handoffs — archive completed feature briefs, delete superseded session handoffs, fix stale cross-links. Use after MSG ships or when user says cleanup-docs / clean up handoffs.
---

# cleanup-docs

Mirror of [`.grok/skills/cleanup-docs/SKILL.md`](../../.grok/skills/cleanup-docs/SKILL.md).

## Command

```powershell
python scripts/cleanup_planning_docs.py
```

Apply deletes/archives:

```powershell
python scripts/cleanup_planning_docs.py --apply
```

Then fix stale links flagged in the report (`play-v1-implementation-plan.md` Related line, MEMORY.md).

**Template (permanent):** `Docs/planning/focused-implementation-handoff-template.md`
