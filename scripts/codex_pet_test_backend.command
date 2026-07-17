#!/bin/zsh

set -u

ROOT="${0:A:h:h}"
LOG="/private/tmp/huangshan_codex_pet_test.log"
BOARD="83ECC050-7656-62E0-746A-7B5F0DDBA396"
SECURITY_BIN="${CODEX_PET_SECURITY_BIN:-/usr/bin/security}"
KEYCHAIN_SERVICE="com.huangshan-pi.codex-pet.glm-asr"
KEYCHAIN_ACCOUNT="glm-asr-2512"
KEYCHAIN_LABEL="Huangshan Codex Pet GLM-ASR API Key"

usage() {
    cat <<'EOF'
Usage: codex_pet_test_backend.command [--replace-api-key | --forget-api-key]

  --replace-api-key  Replace the saved GLM API key, then start the Bridge.
  --forget-api-key   Delete the saved GLM API key and exit.
EOF
}

read_api_key() {
    "$SECURITY_BIN" find-generic-password \
        -a "$KEYCHAIN_ACCOUNT" \
        -s "$KEYCHAIN_SERVICE" \
        -w 2>/dev/null
}

save_api_key() {
    printf '%s\n' 'Enter the GLM API key at the macOS Keychain prompt.'
    printf '%s\n' 'The value will not be written to this project or the test log.'
    "$SECURITY_BIN" add-generic-password \
        -U \
        -a "$KEYCHAIN_ACCOUNT" \
        -s "$KEYCHAIN_SERVICE" \
        -D "application password" \
        -l "$KEYCHAIN_LABEL" \
        -j "Used by the local Huangshan Codex Pet GLM-ASR test backend" \
        -w
}

delete_api_key() {
    "$SECURITY_BIN" delete-generic-password \
        -a "$KEYCHAIN_ACCOUNT" \
        -s "$KEYCHAIN_SERVICE" >/dev/null 2>&1
}

replace_api_key=0
case "${1:-}" in
    "") ;;
    --replace-api-key) replace_api_key=1 ;;
    --forget-api-key)
        if delete_api_key; then
            printf '%s\n' 'Saved GLM API key deleted from macOS Keychain.'
        else
            printf '%s\n' 'No saved GLM API key was found.'
        fi
        exit 0
        ;;
    --self-test)
        false | cat >/dev/null
        bridge_exit_code=${pipestatus[1]}
        if [[ "$bridge_exit_code" -ne 1 ]]; then
            printf '%s\n' 'codex_pet_test_backend self-test failed' >&2
            exit 1
        fi
        printf '%s\n' 'codex_pet_test_backend self-test ok'
        exit 0
        ;;
    -h|--help)
        usage
        exit 0
        ;;
    *)
        usage >&2
        exit 2
        ;;
esac

clear
printf '\033]0;%s\007' 'Codex Pet LIVE Console'
printf '%s\n' 'Huangshan Codex Pet test backend'
printf '%s\n\n' 'The GLM API key is stored only in macOS Keychain.'
cd "$ROOT" || exit 1

unset ZHIPU_API_KEY ZHIPUAI_API_KEY BIGMODEL_API_KEY
if (( replace_api_key )); then
    delete_api_key || true
fi

ZHIPU_API_KEY="$(read_api_key)"
if [[ -z "$ZHIPU_API_KEY" ]]; then
    if ! save_api_key; then
        printf '%s\n' 'The API key was not saved; backend was not started.' >&2
        exit 1
    fi
    ZHIPU_API_KEY="$(read_api_key)"
fi

if [[ -z "$ZHIPU_API_KEY" ]]; then
    printf '%s\n' 'The saved API key could not be read from macOS Keychain.' >&2
    exit 1
fi

export ZHIPU_API_KEY
trap 'unset ZHIPU_API_KEY ZHIPUAI_API_KEY BIGMODEL_API_KEY' EXIT
: > "$LOG"
printf '%s\n' 'Loaded GLM API key from macOS Keychain.'
printf '%s\n' 'Starting Codex Pet Bridge...'
printf '%s\n' 'Keep this terminal open during testing. Press Ctrl-C to stop.'
printf '%s\n\n' 'LIVE CODEX REPLIES WILL STREAM BELOW.'

PYTHONUNBUFFERED=1 "$ROOT/.venv/bin/python" "$ROOT/scripts/codex_pet_bridge.py" \
    --mode voice \
    --workspace "$ROOT" \
    --address "$BOARD" \
    2>&1 | tee "$LOG"
bridge_exit_code=${pipestatus[1]}

printf '\nCodex Pet Bridge exited with code %d.\n' "$bridge_exit_code"
exit "$bridge_exit_code"
