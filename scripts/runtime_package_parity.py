#!/usr/bin/env python3
from __future__ import annotations

import ast
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PYTHON_PACKAGE = ROOT / "scripts" / "runtime_package.py"
SWIFT_PACKAGE = ROOT / "mobile" / "ios" / "VibeBoardBLE" / "Sources" / "VibeBoardBLE" / "RuntimePackage.swift"


class ParityError(RuntimeError):
    pass


def extract_python_assignment(source: str, name: str) -> str:
    marker = f"{name} ="
    start = source.find(marker)
    if start < 0:
        raise ParityError(f"missing Python constant {name}")
    eq = source.find("=", start)
    lines: list[str] = []
    for line in source[eq + 1 :].splitlines():
        if lines and re.match(r"^[A-Z][A-Z0-9_]+\s*=", line):
            break
        if lines and re.match(r"^(def|class)\s+", line):
            break
        lines.append(line)
        joined = "\n".join(lines).strip()
        if joined.endswith(("}", ")")) and joined.count("{") == joined.count("}") and joined.count("(") == joined.count(")"):
            break
    expr = "\n".join(lines).strip()
    if not expr:
        raise ParityError(f"empty Python constant {name}")
    return expr


def python_set(source: str, name: str) -> set[str]:
    expr = extract_python_assignment(source, name)
    if "|" in expr:
        left_expr, right_expr = expr.split("|", 1)
        left_expr = left_expr.strip()
        left = python_set(source, left_expr) if left_expr.isidentifier() else set(ast.literal_eval(left_expr))
        return left | set(ast.literal_eval(right_expr.strip()))
    value = ast.literal_eval(expr)
    if not isinstance(value, (set, tuple, list)):
        raise ParityError(f"Python constant {name} is not a collection")
    return set(value)


def swift_bracket_payload(source: str, name: str) -> str:
    marker = f"private static let {name}"
    start = source.find(marker)
    if start < 0:
        raise ParityError(f"missing Swift constant {name}")
    eq = source.find("=", start)
    lb = source.find("[", eq)
    if lb < 0:
        raise ParityError(f"Swift constant {name} does not contain a list literal")
    depth = 0
    for index in range(lb, len(source)):
        char = source[index]
        if char == "[":
            depth += 1
        elif char == "]":
            depth -= 1
            if depth == 0:
                return source[lb + 1 : index]
    raise ParityError(f"unclosed Swift list literal for {name}")


def swift_set(source: str, name: str) -> set[str]:
    return set(re.findall(r'"([^"]+)"', swift_bracket_payload(source, name)))


def compare(label: str, left: set[str], right: set[str]) -> bool:
    only_left = sorted(left - right)
    only_right = sorted(right - left)
    ok = not only_left and not only_right
    print(f"{label}: python={len(left)} swift={len(right)} {'ok' if ok else 'mismatch'}")
    if only_left:
        print(f"  only in Python: {only_left}")
    if only_right:
        print(f"  only in Swift: {only_right}")
    return ok


def main() -> int:
    py = PYTHON_PACKAGE.read_text(encoding="utf-8")
    swift = SWIFT_PACKAGE.read_text(encoding="utf-8")

    swift_manifest_capabilities = swift_set(swift, "manifestCapabilities")
    swift_declared_capabilities = swift_manifest_capabilities | swift_set(swift, "manifestDeclaredCapabilities")
    checks = [
        ("manifestCapabilities", python_set(py, "MANIFEST_CAPABILITIES"), swift_manifest_capabilities),
        ("manifestDeclaredCapabilities", python_set(py, "MANIFEST_DECLARED_CAPABILITIES"), swift_declared_capabilities),
        ("huangshanProfileAliases", python_set(py, "HUANGSHAN_PROFILE_ALIASES"), swift_set(swift, "huangshanProfileAliases")),
        ("manifestProfileFields", python_set(py, "MANIFEST_PROFILE_FIELDS"), swift_set(swift, "manifestProfileFields")),
        ("manifestCapabilityListFields", python_set(py, "MANIFEST_CAPABILITY_LIST_FIELDS"), swift_set(swift, "manifestCapabilityListFields")),
        ("esp32NativeCapabilityNames", python_set(py, "ESP32_NATIVE_CAPABILITY_NAMES"), swift_set(swift, "esp32NativeCapabilityNames")),
        ("manifestComponentTypes", python_set(py, "MANIFEST_COMPONENT_TYPES"), swift_set(swift, "manifestComponentTypes")),
        ("luaSupportedCalls", python_set(py, "LUA_SUPPORTED_CALLS"), swift_set(swift, "supportedLuaCalls")),
        ("luaUnsupportedStatements", python_set(py, "LUA_UNSUPPORTED_PREFIXES"), swift_set(swift, "unsupportedLuaStatements")),
    ]
    ok = True
    for label, left, right in checks:
        ok = compare(label, left, right) and ok
    if ok:
        print("runtime_package_parity ok")
        return 0
    return 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except ParityError as exc:
        print(f"runtime_package_parity failed: {exc}", file=sys.stderr)
        raise SystemExit(2)
