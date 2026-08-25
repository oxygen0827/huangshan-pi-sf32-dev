#!/usr/bin/env bash

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORKSPACE_DIR="$(cd "$ROOT_DIR/.." && pwd)"

if [[ -n "${SIFLI_SDK_PATH:-}" ]]; then
    SDK_DIR="$SIFLI_SDK_PATH"
else
    SDK_DIR=""
    for candidate in \
        "$WORKSPACE_DIR/sifli-sdk" \
        "$WORKSPACE_DIR/vibeboard/hardware/huangshan/sifli-sdk" \
        "$ROOT_DIR/hardware/huangshan/sifli-sdk" \
        "$HOME/vibe-coding/vibeboard/hardware/huangshan/sifli-sdk"; do
        if [[ -f "$candidate/export.sh" ]]; then
            SDK_DIR="$candidate"
            break
        fi
    done
fi

if [[ ! -f "$SDK_DIR/export.sh" ]]; then
    cat >&2 <<EOF
SIFLI SDK not found${SDK_DIR:+ at: $SDK_DIR}
Set SIFLI_SDK_PATH first, for example:
  export SIFLI_SDK_PATH=/Users/hushaohong/vibe-coding/vibeboard/hardware/huangshan/sifli-sdk
EOF
    return 2
fi

export SIFLI_SDK_PATH="$SDK_DIR"

if [[ -z "${SIFLI_PYTHON_ENV_PATH:-}" ]]; then
    for env_dir in "$HOME"/.sifli/python_env/sifli-sdk*_py3.*_env; do
        if [[ -x "$env_dir/bin/python" ]]; then
            export SIFLI_PYTHON_ENV_PATH="$env_dir"
            export PATH="$env_dir/bin:$PATH"
            break
        fi
    done
fi

_sifli_configure_offline() {
    local toolchain_bin=""
    local sftool_bin=""
    local yaml_site=""
    local version_file="${SIFLI_PYTHON_ENV_PATH:-}/sifli_sdk_version.txt"

    for candidate in "$HOME"/.sifli/tools/arm-none-eabi-gcc/*/bin; do
        if [[ -x "$candidate/arm-none-eabi-gcc" ]]; then
            toolchain_bin="$candidate"
            break
        fi
    done
    for candidate in "$HOME"/.sifli/tools/sftool/*; do
        if [[ -x "$candidate/sftool" ]]; then
            sftool_bin="$candidate"
            break
        fi
    done
    if [[ -z "$toolchain_bin" || -z "${SIFLI_PYTHON_ENV_PATH:-}" ||
          ! -x "$SIFLI_PYTHON_ENV_PATH/bin/scons" ]]; then
        return 1
    fi

    export SIFLI_SDK="$SDK_DIR/"
    if [[ -f "$version_file" ]]; then
        SIFLI_SDK_VERSION="$(<"$version_file")"
    else
        SIFLI_SDK_VERSION="${SIFLI_SDK_VERSION:-2.4}"
    fi
    export SIFLI_SDK_VERSION
    export SIFLI_SDK_TOOLS_PATH="${SIFLI_SDK_TOOLS_PATH:-$HOME/.sifli}"
    export RTT_CC="${RTT_CC:-gcc}"
    export RTT_EXEC_PATH="$toolchain_bin"
    export PATH="$toolchain_bin${sftool_bin:+:$sftool_bin}:$SIFLI_PYTHON_ENV_PATH/bin:$PATH"
    export PYTHONPATH="$SDK_DIR/tools/build${PYTHONPATH:+:$PYTHONPATH}"

    # The installed SiFli 2.4 venv may lack PyYAML while the matching local
    # SDK environment already contains it. Add that read-only site-packages
    # directory instead of asking uv to fetch anything from the network.
    for candidate in "$HOME"/.sifli/envs/default/*/python/lib/python*/site-packages; do
        if [[ -f "$candidate/yaml/__init__.py" ]]; then
            yaml_site="$candidate"
            break
        fi
    done
    if [[ -n "$yaml_site" ]]; then
        export PYTHONPATH="$yaml_site:$PYTHONPATH"
    fi
    return 0
}

_sifli_env_had_nounset=0
case $- in
    *u*) _sifli_env_had_nounset=1; set +u ;;
esac
_sifli_env_args=("$@")
set --
_sifli_export_ok=0
if [[ "${SIFLI_SDK_FORCE_ONLINE:-0}" != "1" ]] && _sifli_configure_offline; then
    _sifli_export_ok=1
    echo "Using locally installed SiFli toolchain (offline mode)." >&2
elif [[ "${SIFLI_SDK_OFFLINE:-0}" != "1" ]] && source "$SDK_DIR/export.sh" >/tmp/huangshan-sifli-export.log; then
    _sifli_export_ok=1
fi
set -- "${_sifli_env_args[@]}"
unset _sifli_env_args
if [[ "$_sifli_env_had_nounset" == "1" ]]; then
    set -u
fi
unset _sifli_env_had_nounset

if [[ "$_sifli_export_ok" != "1" ]]; then
    if ! _sifli_configure_offline; then
        echo "SiFli SDK environment export failed; no complete local toolchain fallback was found." >&2
        echo "See /tmp/huangshan-sifli-export.log or install the SDK Python environment." >&2
        return 2
    fi
    echo "Using locally installed SiFli toolchain (offline mode)." >&2
fi
unset _sifli_export_ok
unset -f _sifli_configure_offline
