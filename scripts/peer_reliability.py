#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import re
import time
from dataclasses import dataclass

import serial


PEER_API = "vibeboard-huangshan-peer/v1"
JSON_PATTERN = re.compile(r'\{[^\r\n]*"api":"vibeboard-huangshan-peer/v1"[^\r\n]*\}')


@dataclass
class Board:
    name: str
    port: str
    baud: int
    boot_wait: float

    def __post_init__(self) -> None:
        self.serial = serial.Serial(
            port=None,
            baudrate=self.baud,
            bytesize=8,
            parity="N",
            stopbits=1,
            timeout=0.05,
            write_timeout=1,
        )
        self.serial.dtr = False
        self.serial.rts = False
        self.serial.port = self.port
        self.serial.open()
        self.serial.dtr = False
        self.serial.rts = False
        self.drain(self.boot_wait)

    def close(self) -> None:
        self.serial.close()

    def drain(self, seconds: float) -> str:
        deadline = time.monotonic() + seconds
        output = bytearray()
        while time.monotonic() < deadline:
            chunk = self.serial.read(4096)
            if chunk:
                output.extend(chunk)
        return bytes(value & 0x7F for value in output).decode("utf-8", "replace")

    def command(self, command: str, settle: float = 0.25) -> str:
        data = (command + "\r\n").encode()
        for offset in range(0, len(data), 24):
            self.serial.write(data[offset:offset + 24])
            self.serial.flush()
            time.sleep(0.012)
        return self.drain(settle)

    def status(self) -> dict[str, object]:
        last_output = ""
        for _ in range(5):
            last_output = self.command("peer_status", 0.35)
            matches = JSON_PATTERN.findall(last_output)
            if not matches:
                continue
            status = json.loads(matches[-1])
            if status.get("api") != PEER_API:
                raise RuntimeError(f"{self.name}: unexpected peer API {status!r}")
            return status
        raise RuntimeError(f"{self.name}: peer_status JSON not found in {last_output!r}")

    def send(self, text: str) -> None:
        before = int(self.status().get("txSeq", 0))
        output = self.command(f"peer_send {text.encode().hex()}", 0.2)
        if "ok peer_send" in output:
            return
        after = int(self.status().get("txSeq", 0))
        if after == before:
            raise RuntimeError(f"{self.name}: send failed: {output!r}")

    def send_fill(self, length: int, value: int) -> None:
        before = int(self.status().get("txSeq", 0))
        output = self.command(f"peer_send_fill {length} {value:02x}", 0.2)
        if "ok peer_send_fill" in output:
            return
        after = int(self.status().get("txSeq", 0))
        if after == before:
            raise RuntimeError(f"{self.name}: fill send failed: {output!r}")

    def forget_and_assert_empty(self) -> None:
        output = self.command("peer_pair forget", 0.5)
        if "ok peer_pair" not in output:
            raise RuntimeError(f"{self.name}: forget failed: {output!r}")
        status = self.status()
        empty_fields = ("paired", "pending", "unread", "txSeq", "rxSeq")
        if any(int(status.get(field, 0)) != 0 for field in empty_fields):
            raise RuntimeError(f"{self.name}: forget left status behind: {status!r}")
        output = self.command("peer_messages_page 0 1", 0.5)
        matches = JSON_PATTERN.findall(output)
        if not matches or int(json.loads(matches[-1]).get("total", -1)) != 0:
            raise RuntimeError(f"{self.name}: forget left history behind: {output!r}")


def wait_until(boards: tuple[Board, Board], predicate, timeout: float, label: str):
    deadline = time.monotonic() + timeout
    last = None
    while time.monotonic() < deadline:
        last = tuple(board.status() for board in boards)
        if predicate(last):
            return last
        time.sleep(0.15)
    raise TimeoutError(f"timed out waiting for {label}; last={last!r}")


def pair(boards: tuple[Board, Board], timeout: float) -> None:
    for board in boards:
        board.command("peer_pair start")
    statuses = wait_until(
        boards,
        lambda pair_status: all(int(item.get("pairingCode", 0)) > 0 for item in pair_status),
        timeout,
        "matching pair codes",
    )
    codes = {int(item["pairingCode"]) for item in statuses}
    if len(codes) != 1:
        raise RuntimeError(f"boards displayed different pair codes: {statuses!r}")
    print(f"pair code {codes.pop():06d}", flush=True)
    for board in boards:
        board.command("peer_pair confirm")
    wait_until(
        boards,
        lambda pair_status: all(bool(item.get("connected")) for item in pair_status),
        timeout,
        "both boards ready",
    )


def send_and_wait(
    boards: tuple[Board, Board], sender_index: int, text: str, timeout: float
) -> None:
    sender = boards[sender_index]
    receiver_index = (sender_index + 1) % 2
    receiver = boards[receiver_index]
    before = int(receiver.status().get("rxSeq", 0))
    sender.send(text)
    wait_until(
        boards,
        lambda pair_status:
            int(pair_status[receiver_index].get("rxSeq", 0)) != before and
            int(pair_status[sender_index].get("pending", 1)) == 0,
        timeout,
        f"delivery of {len(text.encode())} bytes from {sender.name}",
    )


def exchange(boards: tuple[Board, Board], messages: int, timeout: float, interval: float) -> None:
    for index in range(messages):
        text = f"pager-{index + 1:04d}-{boards[index % 2].name}"
        send_and_wait(boards, index % 2, text, timeout)
        if index == 0 or (index + 1) % 10 == 0:
            print(f"delivered {index + 1}/{messages}", flush=True)
        if interval:
            time.sleep(interval)


def exchange_boundaries(boards: tuple[Board, Board], timeout: float) -> None:
    payloads = [
        "a" * 20,
        "b" * 23,
        "c" * 80,
        "d" * 192,
        "你好",
        "收到",
        "我到了",
        "稍等一下",
    ]
    for index, payload in enumerate(payloads):
        if len(payload.encode()) == 192:
            sender_index = index % 2
            receiver_index = (sender_index + 1) % 2
            before = int(boards[receiver_index].status().get("rxSeq", 0))
            boards[sender_index].send_fill(192, ord("d"))
            wait_until(
                boards,
                lambda status:
                    int(status[receiver_index].get("rxSeq", 0)) != before and
                    int(status[sender_index].get("pending", 1)) == 0,
                timeout,
                "delivery of 192-byte generated payload",
            )
        else:
            send_and_wait(boards, index % 2, payload, timeout)
        print(f"boundary {len(payload.encode())} bytes ok", flush=True)


def main() -> int:
    parser = argparse.ArgumentParser(description="Two-board Huangshan Peer Link reliability test")
    parser.add_argument("--port-a", required=True)
    parser.add_argument("--port-b", required=True)
    parser.add_argument("--baud", type=int, default=1_000_000)
    parser.add_argument("--messages", type=int, default=100)
    parser.add_argument("--timeout", type=float, default=15.0)
    parser.add_argument("--interval", type=float, default=0.05)
    parser.add_argument(
        "--boot-wait",
        type=float,
        default=9.0,
        help="seconds to wait after opening each UART bridge (opening resets Huangshan Pi)",
    )
    parser.add_argument("--pair", action="store_true", help="forget existing peers and perform confirmation pairing")
    parser.add_argument("--boundary", action="store_true", help="send boundary-size and Chinese UTF-8 payloads")
    args = parser.parse_args()
    boards = (
        Board("A", args.port_a, args.baud, args.boot_wait),
        Board("B", args.port_b, args.baud, args.boot_wait),
    )
    started = time.monotonic()
    try:
        if args.pair:
            for board in boards:
                board.forget_and_assert_empty()
            pair(boards, args.timeout)
        else:
            for board in boards:
                output = board.command("vb_runtime_launch pager", 2.0)
                if "ok launch app=pager" not in output:
                    raise RuntimeError(f"{board.name}: Pager launch failed: {output!r}")
            wait_until(
                boards,
                lambda pair_status: all(bool(item.get("connected")) for item in pair_status),
                args.timeout,
                "existing peer reconnect",
            )
        exchange(boards, args.messages, args.timeout, args.interval)
        if args.boundary:
            exchange_boundaries(boards, args.timeout)
        print(f"PASS messages={args.messages} boundary={int(args.boundary)} "
              f"elapsed={time.monotonic() - started:.1f}s", flush=True)
        return 0
    finally:
        for board in boards:
            board.close()


if __name__ == "__main__":
    raise SystemExit(main())
