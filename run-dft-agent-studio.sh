#!/usr/bin/env bash
set -euo pipefail

studio_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
agent_root="${studio_root}"
build_dir="${studio_root}/build"

# A source-tree move can leave an otherwise valid build directory pointing at
# the old checkout. Keep that build intact and configure this source tree in a
# separate directory instead of letting CMake fail on the stale cache.
cache_file="${build_dir}/CMakeCache.txt"
if [[ -f "${cache_file}" ]]; then
  cached_source="$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "${cache_file}" | tail -n 1)"
  if [[ -n "${cached_source}" && "${cached_source}" != "${studio_root}" ]]; then
    build_dir="${studio_root}/build-studio"
  fi
fi

# Persistent user data lives in ~/.dft_agent_studio. Ignore stale overrides
# from earlier storage layouts so every entry point resolves the same root.
unset DFT_STUDIO_DATA_DIR
unset DFT_STUDIO_PROJECTS_FILE
unset DFT_STUDIO_MODELS_FILE
unset DFT_STUDIO_CAPABILITIES_FILE

cmake -S "${studio_root}" -B "${build_dir}" -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=OFF
build_jobs="${DFT_AGENT_STUDIO_BUILD_JOBS:-8}"
if [[ ! "${build_jobs}" =~ ^[1-9][0-9]*$ ]]; then
  printf 'DFT_AGENT_STUDIO_BUILD_JOBS must be a positive integer, got: %s\n' "${build_jobs}" >&2
  exit 2
fi
cmake --build "${build_dir}" --parallel "${build_jobs}"

# API endpoint, model name, and credential-file path are selected in Model
# Settings. The GUI communicates with the DFT Agent runtime directly.

gpu="${DFT_AGENT_STUDIO_GPU:-auto}"
renderer=""
egl_wayland2_root="${DFT_AGENT_STUDIO_EGL_WAYLAND2_HOME:-${HOME}/.local/opt/egl-wayland2}"
egl_wayland2_config="${egl_wayland2_root}/share/egl/egl_external_platform.d/09_nvidia_wayland2.json"
egl_wayland2_lib="${egl_wayland2_root}/lib64/libnvidia-egl-wayland2.so.1.0.2"

if [[ "${gpu}" == auto ]]; then
  renderer="$( { glxinfo -B 2>/dev/null || true; } | sed -n 's/^OpenGL renderer string: //p' | head -n 1)"
  case "${renderer}" in
    *Intel*|*intel*)
    gpu=intel
      ;;
    *NVIDIA*|*Nvidia*|*nvidia*)
      if [[ -f "${egl_wayland2_config}" && -f "${egl_wayland2_lib}" ]]; then
        gpu=nvidia
      else
        gpu=software
      fi
      ;;
    *)
      gpu=software
      ;;
  esac
fi

case "${gpu}" in
  software)
    export QT_QUICK_BACKEND=software
    ;;
  intel)
    export __EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json
    export __GLX_VENDOR_LIBRARY_NAME=mesa
    export DRI_PRIME="${DFT_AGENT_STUDIO_DRI_PRIME:-pci-0000_00_02_0}"
    if [[ -n "${WAYLAND_DISPLAY:-}" ]]; then
      export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-wayland}"
    fi
    ;;
  nvidia)
    if [[ -f "${egl_wayland2_config}" && -f "${egl_wayland2_lib}" ]]; then
      export __EGL_EXTERNAL_PLATFORM_CONFIG_FILENAMES="${egl_wayland2_config}"
      export LD_LIBRARY_PATH="${egl_wayland2_root}/lib64${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
    fi
    ;;
  *)
    printf 'Unsupported DFT_AGENT_STUDIO_GPU value: %s (use intel, software, nvidia, or auto)\n' "${gpu}" >&2
    exit 2
    ;;
esac

printf 'DFT Studio GPU mode: %s%s\n' "${gpu}" "${renderer:+ (${renderer})}" >&2

exec "${build_dir}/dft-agent-studio" \
  --agent-root "${agent_root}" \
  "$@"
