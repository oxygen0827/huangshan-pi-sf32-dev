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

_sifli_env_had_nounset=0
case $- in
    *u*) _sifli_env_had_nounset=1; set +u ;;
esac
_sifli_env_args=("$@")
set --
source "$SDK_DIR/export.sh" >/tmp/huangshan-sifli-export.log
set -- "${_sifli_env_args[@]}"
unset _sifli_env_args
if [[ "$_sifli_env_had_nounset" == "1" ]]; then
    set -u
fi
unset _sifli_env_had_nounset
