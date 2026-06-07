---
name: fullbuild
description: Alias for /cleanbuild. Performs a clean debug build for this project, auto-incrementing the BUILD_NUMBER. Use for /fullbuild or when user asks for a full/clean build.
user_invocable: true
argument-hint: ""
---

# Fullbuild Skill (alias for /cleanbuild)

**This is an alias for the cleanbuild skill.** It performs exactly the same actions as /cleanbuild.

**Bench defaults (source of truth for this local setup):**
- COM port: COM9
- ST-Link SN: 003C00193137510C39383538

When invoked as `/fullbuild`, or natural language requesting a full/clean debug build:

1. Run the build script with clean + bump:
   ```
   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 --clean --bump-build-count
   ```
   (The --bump-build-count causes the script to increment BUILD_NUMBER in App/Inc/platform.h *before* the actual compile. This is required for clean-build skills per spec. Incremental builds must not bump.)

2. After completion:
   - Report success/failure with exit code.
   - Extract and prominently report the final error and warning counts from the build output (e.g. "Build finished with 0 errors and 1 warning" or the exact summary lines).
   - Note the BUILD_NUMBER and FIRMWARE_VERSION reported by the script (they appear after the Mode line; the script always prints both for the build in effect. Also note the bump message for BUILD_NUMBER).
   - On success, report the artifact (Debug\LED_Strip_Controller_G474.elf) and its timestamp.

3. Brief diagnostics: if the script reports issues with the environment (CubeIDE not found, etc.), surface them. For compile errors/warnings, summarize the counts and suggest using /fixme if needed.

This skill always performs a full clean build with bump (same as /cleanbuild). Do not use for incremental requests (use /build instead).

If errors or warnings are present, report them clearly. Higher-level skills like /roundtrip will check this and may abort.

Reference SCRIPTS.md for general invocation patterns.
