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


TRANSCRIPTION_URL = "https://api.openai.com/v1/audio/transcriptions"
RESPONSES_URL = "https://api.openai.com/v1/responses"


def fail(message: str, code: int = 1) -> None:
    print(message, file=sys.stderr)
    raise SystemExit(code)


def require_api_key() -> str:
    key = os.environ.get("OPENAI_API_KEY", "").strip()
    if not key:
        fail("OPENAI_API_KEY is required for scripts/voice_llm_openai.py")
    return key


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
        fail(f"OpenAI API request failed: HTTP {exc.code}: {detail}", exc.code)
    except urllib.error.URLError as exc:
        fail(f"OpenAI API request failed: {exc.reason}")


def multipart_form_data(fields: dict[str, str], file_field: str, file_path: Path) -> tuple[bytes, str]:
    boundary = "----vibeboard-openai-boundary"
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


def transcribe(api_key: str, wav_path: Path, model: str, language: str | None) -> str:
    fields = {"model": model}
    if language:
        fields["language"] = language
    body, content_type = multipart_form_data(fields, "file", wav_path)
    response = api_request_json(TRANSCRIPTION_URL, api_key, body, content_type)
    text = str(response.get("text", "")).strip()
    if not text:
        fail(f"transcription returned no text: {response}")
    return text


def extract_response_text(response: dict) -> str:
    if isinstance(response.get("output_text"), str):
        return response["output_text"].strip()
    parts: list[str] = []
    for item in response.get("output", []):
        for content in item.get("content", []):
            if content.get("type") in ("output_text", "text"):
                text = content.get("text", "")
                if text:
                    parts.append(str(text))
    return "\n".join(parts).strip()


def generate_reply(api_key: str, transcript: str, model: str, max_output_tokens: int) -> str:
    payload = {
        "model": model,
        "instructions": (
            "你是黄山派开发板上的简短语音助手。"
            "请用中文回答，适合在小屏幕信息流显示，最多两句话。"
        ),
        "input": transcript,
        "max_output_tokens": max_output_tokens,
    }
    body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
    response = api_request_json(RESPONSES_URL, api_key, body, "application/json")
    text = extract_response_text(response)
    if not text:
        fail(f"response generation returned no text: {response}")
    return text


def write_metadata(path: Path | None, payload: dict) -> None:
    if not path:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, ensure_ascii=False, sort_keys=True) + "\n", encoding="utf-8")


def self_test() -> int:
    with tempfile.TemporaryDirectory(prefix="vibeboard_voice_llm_") as tmpdir:
        wav_path = Path(tmpdir) / "sample.wav"
        wav_path.write_bytes(b"RIFF$\x00\x00\x00WAVEfmt ")
        body, content_type = multipart_form_data(
            {"model": "gpt-4o-mini-transcribe", "language": "zh"},
            "file",
            wav_path,
        )
        assert "multipart/form-data; boundary=" in content_type
        assert b'name="model"\r\n\r\ngpt-4o-mini-transcribe' in body
        assert b'name="language"\r\n\r\nzh' in body
        assert b'filename="sample.wav"' in body
        assert b"RIFF" in body

    assert extract_response_text({"output_text": " ok "}) == "ok"
    nested = {
        "output": [
            {"content": [{"type": "output_text", "text": "hello"}, {"type": "text", "text": " world"}]}
        ]
    }
    assert extract_response_text(nested) == "hello\n world"
    print("voice_llm_openai self-test ok")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Transcribe a VibeBoard WAV and print an OpenAI reply.")
    parser.add_argument("--wav", type=Path)
    parser.add_argument("--transcribe-model", default="gpt-4o-mini-transcribe")
    parser.add_argument("--reply-model", default="gpt-4.1-mini")
    parser.add_argument("--language", default="zh")
    parser.add_argument("--max-output-tokens", type=int, default=120)
    parser.add_argument("--print-transcript", action="store_true")
    parser.add_argument("--metadata-json", type=Path, help="Write transcript/model/reply metadata to this JSON file")
    parser.add_argument("--self-test", action="store_true", help="Run offline helper checks without network or API key")
    args = parser.parse_args()

    if args.self_test:
        return self_test()
    if not args.wav:
        fail("--wav is required unless --self-test is used", 2)
    if not args.wav.exists():
        fail(f"WAV file not found: {args.wav}")

    api_key = require_api_key()
    transcript = transcribe(api_key, args.wav, args.transcribe_model, args.language or None)
    if args.print_transcript:
        print(f"transcript: {transcript}", file=sys.stderr)
    reply = generate_reply(api_key, transcript, args.reply_model, args.max_output_tokens)
    write_metadata(
        args.metadata_json,
        {
            "provider": "openai",
            "transcribe_model": args.transcribe_model,
            "reply_model": args.reply_model,
            "language": args.language,
            "transcript": transcript,
            "reply": reply,
        },
    )
    print(reply)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
