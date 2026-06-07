#!/bin/bash
# build.sh -- headless STM32CubeIDE build for LED_Strip_Controller_G474
#
# Usage:
#   scripts/build.sh                    # Debug (default, clean build)
#   scripts/build.sh Release
#   scripts/build.sh --config Test
#   scripts/build.sh Debug --clean
#   scripts/build.sh Debug --incremental
#
# Configs: Debug (default), Release, Test
# Use --clean to explicitly force a clean build (default behavior).
# Use --incremental to request an incremental build instead of clean.

set -e

if [[ "$1" == "--help" || "$1" == "-h" ]]; then
  cat <<'EOF'
Usage: scripts/build.sh [CONFIG] [OPTIONS]

  CONFIG                 Debug (default), Release, or Test

Options:
  --clean                Force a clean build (default behavior)
  --incremental          Request an incremental build
  --help, -h             Show this help

Locates stm32cubeidec via $STM32CUBEIDE or common paths.
EOF
  exit 0
fi

config="${1:-Debug}"
case "${config}" in
  Debug|Release|Test) ;;
  --config)
    config="${2:-Debug}"
    shift 2
    ;;
  *)
    echo "build.sh: invalid config '${config}' (expected Debug|Release|Test)" >&2
    exit 2
    ;;
esac

clean=false
incremental=false
for arg in "$@"; do
  case "$arg" in
    --clean) clean=true ;;
    --incremental) incremental=true ;;
  esac
done

project_name="LED_Strip_Controller_G474"
script_dir="$(cd "$(dirname "$0")" && pwd)"
repo_root="$(dirname "${script_dir}")"

# Find CubeIDE (stock env or common paths)
if [ -n "${STM32CUBEIDE}" ] && [ -x "${STM32CUBEIDE}" ]; then
  cubeide="${STM32CUBEIDE}"
else
  for root in /opt/st /usr/local/STMicroelectronics "${HOME}/STMicroelectronics" "${HOME}/st"; do
    [ -d "${root}" ] || continue
    for d in "${root}"/STM32CubeIDE_* "${root}"/stm32cubeide; do
      [ -d "${d}" ] || continue
      for cand in "${d}/STM32CubeIDE/stm32cubeidec" "${d}/stm32cubeidec"; do
        if [ -x "${cand}" ]; then
          cubeide="${cand}"
          break 3
        fi
      done
    done
  done
fi

if [ -z "${cubeide}" ] || [ ! -x "${cubeide}" ]; then
  echo "STM32CubeIDE not found. Set \$STM32CUBEIDE to the launcher." >&2
  exit 3
fi

ts=$(date +%Y%m%d-%H%M%S)
workspace="${TMPDIR:-/tmp}/led-g474-headless-ws-$ts"
mkdir -p "${workspace}"

echo "=== LED_Strip_Controller_G474 build ==="
echo "Project:   ${project_name}"
echo "Config:    ${config}"
echo "CubeIDE:   ${cubeide}"
echo "Workspace: ${workspace}"
if [ "$incremental" = true ]; then
  echo "Mode:      incremental (-build)"
elif [ "$clean" = true ]; then
  echo "Mode:      clean (-cleanBuild)"
else
  echo "Mode:      clean (-cleanBuild)  [default]"
fi
echo ""

build_verb="-cleanBuild"
if [ "$incremental" = true ]; then
  build_verb="-build"
elif [ "$clean" = true ]; then
  build_verb="-cleanBuild"
fi

"${cubeide}" \
  -nosplash \
  -application org.eclipse.cdt.managedbuilder.core.headlessbuild \
  -data "${workspace}" \
  -import "${repo_root}" \
  ${build_verb} "${project_name}/${config}" \
  -consoleLog

rc=$?
if [ $rc -eq 0 ]; then
  echo "Build OK. Artifacts in ${config}/"
  # Report the filesystem timestamp of the standard ELF (no filename timestamping)
  std_elf="${repo_root}/${config}/${project_name}.elf"
  if [ -f "$std_elf" ]; then
    elf_time=$(date -r "$std_elf" "+%Y-%m-%d %H:%M:%S")
    echo "ELF timestamp (filesystem): $elf_time"
  fi

  # Clean up the temporary workspace to avoid clutter
  if [ -d "${workspace}" ]; then
    rm -rf "${workspace}"
    echo "Cleaned up temporary workspace: ${workspace}"
  fi
else
  echo "Build failed (exit $rc)" >&2
  echo "Temporary workspace left for inspection: ${workspace}"
fi
exit $rc
