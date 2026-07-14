#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import sys
import tempfile
import urllib.error
import urllib.request
from pathlib import Path


TRANSCRIPTION_URL = "https://open.bigmodel.cn/api/paas/v4/audio/transcriptions"
CHAT_COMPLETIONS_URL = "https://open.bigmodel.cn/api/paas/v4/chat/completions"
API_KEY_ENV_NAMES = ("ZHIPU_API_KEY", "ZHIPUAI_API_KEY", "BIGMODEL_API_KEY")


def fail(message: str, code: int = 1) -> None:
    print(message, file=sys.stderr)
    raise SystemExit(code)


def require_api_key() -> tuple[str, str]:
    for name in API_KEY_ENV_NAMES:
        key = os.environ.get(name, "").strip()
        if key:
            return key, name
    joined = " or ".join(API_KEY_ENV_NAMES)
    fail(f"{joined} is required for scripts/voice_llm_zhipu.py")


def api_request_json(url: str, api_key: str, body: bytes, content_type: str) -> dict:
    request = urllib.request.Request(
        url,
        data=body,
        headers={
            "Authorization": f"Bearer {api_key}",
            "Content-Type": content_type,
        },
        method="POST",
    )
    try:
        with urllib.request.urlopen(request, timeout=60) as response:
            return json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as exc:
        detail = exc.read().decode("utf-8", "replace")
        fail(f"Zhipu API request failed: HTTP {exc.code}: {detail}", exc.code)
    except urllib.error.URLError as exc:
        fail(f"Zhipu API request failed: {exc.reason}")


def multipart_form_data(fields: dict[str, str], file_field: str, file_path: Path) -> tuple[bytes, str]:
    boundary = "----vibeboard-zhipu-boundary"
    chunks: list[bytes] = []
    for name, value in fields.items():
        chunks.extend(
            [
                f"--{boundary}\r\n".encode("utf-8"),
                f'Content-Disposition: form-data; name="{name}"\r\n\r\n'.encode("utf-8"),
                value.encode("utf-8"),
                b"\r\n",
            ]
        )
    chunks.extend(
        [
            f"--{boundary}\r\n".encode("utf-8"),
            (
                f'Content-Disposition: form-data; name="{file_field}"; '
                f'filename="{file_path.name}"\r\n'
            ).encode("utf-8"),
            b"Content-Type: audio/wav\r\n\r\n",
            file_path.read_bytes(),
            b"\r\n",
            f"--{boundary}--\r\n".encode("utf-8"),
        ]
    )
    return b"".join(chunks), f"multipart/form-data; boundary={boundary}"


def transcribe(api_key: str, wav_path: Path, model: str, prompt: str | None) -> str:
    fields = {
        "model": model,
        "stream": "false",
    }
    if prompt:
        fields["prompt"] = prompt
    body, content_type = multipart_form_data(fields, "file", wav_path)
    response = api_request_json(TRANSCRIPTION_URL, api_key, body, content_type)
    text = str(response.get("text", "")).strip()
    if not text:
        fail(f"transcription returned no text: {response}")
    return text


def content_to_text(content) -> str:
    if isinstance(content, str):
        return content.strip()
    if isinstance(content, list):
        parts: list[str] = []
        for item in content:
            if isinstance(item, str):
                parts.append(item)
            elif isinstance(item, dict):
                text = item.get("text") or item.get("content")
                if text:
                    parts.append(str(text))
        return "\n".join(parts).strip()
    return ""


def extract_chat_text(response: dict) -> str:
    choices = response.get("choices", [])
    if not isinstance(choices, list) or not choices:
        return ""
    first = choices[0]
    if not isinstance(first, dict):
        return ""
    message = first.get("message", {})
    if not isinstance(message, dict):
        return ""
    return content_to_text(message.get("content"))


def generate_reply(api_key: str, transcript: str, model: str, max_tokens: int, temperature: float) -> str:
    payload = {
        "model": model,
        "messages": [
            {
                "role": "system",
                "content": (
                    "你是黄山派开发板上的简短语音助手。"
                    "请用中文回答，适合在小屏幕信息流显示，最多两句话。"
                ),
            },
            {"role": "user", "content": transcript},
        ],
        "stream": False,
        "temperature": temperature,
        "max_tokens": max_tokens,
    }
    body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
    response = api_request_json(CHAT_COMPLETIONS_URL, api_key, body, "application/json")
    text = extract_chat_text(response)
    if not text:
        fail(f"chat completion returned no text: {response}")
    return text


def write_metadata(path: Path | None, payload: dict) -> None:
    if not path:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, ensure_ascii=False, sort_keys=True) + "\n", encoding="utf-8")


def self_test() -> int:
    with tempfile.TemporaryDirectory(prefix="vibeboard_voice_zhipu_") as tmpdir:
        wav_path = Path(tmpdir) / "sample.wav"
        wav_path.write_bytes(b"RIFF$\x00\x00\x00WAVEfmt ")
        body, content_type = multipart_form_data(
            {"model": "glm-asr-2512", "stream": "false"},
            "file",
            wav_path,
        )
        assert "multipart/form-data; boundary=" in content_type
        assert b'name="model"\r\n\r\nglm-asr-2512' in body
        assert b'name="stream"\r\n\r\nfalse' in body
        assert b'filename="sample.wav"' in body
        assert b"RIFF" in body

    response = {
        "choices": [
            {
                "message": {
                    "role": "assistant",
                    "content": " 你好，黄山派。 ",
                }
            }
        ]
    }
    assert extract_chat_text(response) == "你好，黄山派。"
    blocks = {"choices": [{"message": {"content": [{"text": "hello"}, {"content": " world"}]}}]}
    assert extract_chat_text(blocks) == "hello\n world"
    print("voice_llm_zhipu self-test ok")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Transcribe a VibeBoard WAV and optionally print a Zhipu GLM reply.")
    parser.add_argument("--wav", type=Path)
    parser.add_argument("--transcribe-model", default="glm-asr-2512")
    parser.add_argument("--reply-model", default="glm-4.5-flash")
    parser.add_argument("--language", default="zh", help="Kept for voice_terminal compatibility; not sent to Zhipu ASR")
    parser.add_argument("--prompt", help="Optional ASR context prompt sent to Zhipu")
    parser.add_argument("--max-output-tokens", type=int, default=120)
    parser.add_argument("--temperature", type=float, default=0.2)
    parser.add_argument("--print-transcript", action="store_true")
    parser.add_argument("--transcribe-only", action="store_true", help="Print the ASR transcript without calling a chat model")
    parser.add_argument("--metadata-json", type=Path, help="Write transcript/model/reply metadata to this JSON file")
    parser.add_argument("--self-test", action="store_true", help="Run offline helper checks without network or API key")
    args = parser.parse_args()

    if args.self_test:
        return self_test()
    if not args.wav:
        fail("--wav is required unless --self-test is used", 2)
    if not args.wav.exists():
        fail(f"WAV file not found: {args.wav}")

    api_key, api_key_env = require_api_key()
    transcript = transcribe(api_key, args.wav, args.transcribe_model, args.prompt)
    if args.print_transcript:
        print(f"transcript: {transcript}", file=sys.stderr)
    if args.transcribe_only:
        write_metadata(
            args.metadata_json,
            {
                "provider": "zhipu",
                "transcribe_model": args.transcribe_model,
                "language": args.language,
                "transcript": transcript,
                "api_key_env": api_key_env,
            },
        )
        print(transcript)
        return 0
    reply = generate_reply(api_key, transcript, args.reply_model, args.max_output_tokens, args.temperature)
    write_metadata(
        args.metadata_json,
        {
            "provider": "zhipu",
            "transcribe_model": args.transcribe_model,
            "reply_model": args.reply_model,
            "language": args.language,
            "transcript": transcript,
            "reply": reply,
            "api_key_env": api_key_env,
        },
    )
    print(reply)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
