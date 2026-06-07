---
name: fixme
description: Attempt to diagnose and fix errors or warnings from the most recent build. Use after a failed or warning-laden build, or when user says /fixme or "fix the build errors".
user_invocable: true
argument-hint: ""
---

# Fixme Skill

**Bench defaults:** COM9 / 003C00193137510C39383538 (used if re-building is needed).

When invoked as `/fixme`:

1. Examine the most recent build output from the conversation history, build.log, build_clean.log, or the last run's console output.
2. Identify the errors and warnings (parse for counts and key messages).
3. Propose and apply fixes using search_replace (or other tools) for common issues (missing includes, syntax, logic errors visible in context, etc.).
4. After fixes, re-run the appropriate build (usually the same type as the failing one: incremental or clean).
5. Report progress: what was fixed, the BUILD_NUMBER and FIRMWARE_VERSION from the (re)build output, new error/warning counts, and whether the build is now clean.
6. If issues persist after reasonable attempts, summarize remaining problems and ask the user for guidance.

Always report the before/after build summary (error and warning counts).

This skill is intended to be used after /build, /cleanbuild, or /roundtrip has reported problems.
