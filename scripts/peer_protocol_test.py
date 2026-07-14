#!/usr/bin/env python3
from __future__ import annotations

import binascii
import struct
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "src/gui_apps/VibeBoard_Runtime/vb_peer_link.c"
RUNTIME_SOURCE = ROOT / "src/gui_apps/VibeBoard_Runtime/main.c"
APP_STORE_SOURCE = ROOT / "scripts/app_store_server.py"
VOICE_SUPERVISOR_SOURCE = ROOT / "scripts/pager_voice_bridges.sh"
RUNTIME_TRANSPORT_SOURCE = ROOT / "scripts/runtime_transport.py"
PAGER_VOICE_BRIDGE_SOURCE = ROOT / "scripts/pager_voice_bridge.py"
VERSION = 1
HEADER = struct.Struct("<BBIHHH")
MAX_TEXT = 192


def crc16(payload: bytes) -> int:
    return binascii.crc_hqx(payload, 0xFFFF)


def fragment(packet_type: int, message_id: int, payload: bytes, mtu: int) -> list[bytes]:
    if not 0 <= len(payload) <= MAX_TEXT:
        raise ValueError("payload exceeds Peer Link v1 limit")
    att_value = max(HEADER.size + 1, min(mtu - 3, HEADER.size + MAX_TEXT))
    capacity = att_value - HEADER.size
    checksum = crc16(payload)
    frames: list[bytes] = []
    offset = 0
    while offset < len(payload) or not frames:
        chunk = payload[offset : offset + capacity]
        frames.append(
            HEADER.pack(VERSION, packet_type, message_id, len(payload), offset, checksum)
            + chunk
        )
        offset += len(chunk)
    return frames


class Reassembler:
    def __init__(self) -> None:
        self.current: tuple[int, int, int, int] | None = None
        self.data = bytearray()

    def receive(self, frame: bytes) -> tuple[int, int, bytes] | None:
        if len(frame) < HEADER.size:
            return None
        version, packet_type, message_id, total, offset, checksum = HEADER.unpack_from(frame)
        chunk = frame[HEADER.size :]
        if version != VERSION or not 1 <= packet_type <= 4:
            return None
        if total > MAX_TEXT or offset + len(chunk) > total:
            return None
        key = (packet_type, message_id, total, checksum)
        if offset == 0:
            self.current = key
            self.data.clear()
        if self.current != key:
            return None
        if offset < len(self.data):
            return None
        if offset != len(self.data):
            self.current = None
            self.data.clear()
            return None
        self.data.extend(chunk)
        if len(self.data) != total:
            return None
        payload = bytes(self.data)
        self.current = None
        self.data.clear()
        if crc16(payload) != checksum:
            return None
        return packet_type, message_id, payload


class PeerProtocolTests(unittest.TestCase):
    def test_crc_known_vector(self) -> None:
        self.assertEqual(crc16(b"123456789"), 0x29B1)

    def test_fragment_round_trips_all_acceptance_lengths(self) -> None:
        for mtu in (23, 64, 247):
            for length in (1, 20, 23, 80, 192):
                payload = bytes((index * 37) & 0xFF for index in range(length))
                reassembler = Reassembler()
                result = None
                for frame in fragment(2, 0x12345678, payload, mtu):
                    self.assertLessEqual(len(frame), max(20, mtu - 3))
                    result = reassembler.receive(frame) or result
                self.assertEqual(result, (2, 0x12345678, payload))

    def test_utf8_quick_phrases(self) -> None:
        payload = "你好，收到，我到了，稍等一下".encode()
        result = None
        reassembler = Reassembler()
        for frame in fragment(2, 9, payload, 23):
            result = reassembler.receive(frame) or result
        self.assertEqual(result, (2, 9, payload))

    def test_duplicate_fragment_is_ignored(self) -> None:
        frames = fragment(2, 7, b"x" * 80, 23)
        reassembler = Reassembler()
        self.assertIsNone(reassembler.receive(frames[0]))
        self.assertIsNone(reassembler.receive(frames[0]))
        result = None
        for frame in frames[1:]:
            result = reassembler.receive(frame) or result
        self.assertEqual(result, (2, 7, b"x" * 80))

    def test_out_of_order_fragment_resets_packet(self) -> None:
        frames = fragment(2, 7, b"x" * 80, 23)
        reassembler = Reassembler()
        self.assertIsNone(reassembler.receive(frames[0]))
        self.assertIsNone(reassembler.receive(frames[2]))
        self.assertIsNone(reassembler.receive(frames[1]))

    def test_corruption_fails_crc(self) -> None:
        frames = fragment(2, 8, b"payload", 23)
        damaged = bytearray(frames[-1])
        damaged[-1] ^= 0x01
        frames[-1] = bytes(damaged)
        reassembler = Reassembler()
        result = None
        for frame in frames:
            result = reassembler.receive(frame) or result
        self.assertIsNone(result)

    def test_deterministic_role_election(self) -> None:
        left = bytes.fromhex("001122334455")
        right = bytes.fromhex("001122334456")
        self.assertTrue(left < right)
        self.assertFalse(right < left)

    def test_public_uuid_namespace_is_present_in_firmware_source(self) -> None:
        source = SOURCE.read_text(encoding="utf-8")
        self.assertIn("0x50, 0x45, 0x45, 0x52", source)
        self.assertIn("VB_PEER_UUID128(0x01)", source)
        self.assertIn("VB_PEER_UUID128(0x02)", source)
        self.assertIn("VB_PEER_UUID128(0x03)", source)

    def test_peer_connection_does_not_claim_runtime_host_slot(self) -> None:
        source = RUNTIME_SOURCE.read_text(encoding="utf-8")
        connected = source.split("case BLE_GAP_CONNECTED_IND:", 1)[1].split(
            "case BLE_GAP_DISCONNECTED_IND:", 1
        )[0]
        gatts = source.split("static uint8_t vb_ble_gatts_set_cbk", 1)[1].split(
            "static uint8_t vb_ble_advertising_event", 1
        )[0]
        self.assertNotIn("g_vb_ble.connected = 1", connected)
        self.assertIn("vb_ble_claim_host_connection(conn_idx)", gatts)
        self.assertIn("link connected idx=", connected)

    def test_peer_disable_clears_scan_state_before_restart(self) -> None:
        source = SOURCE.read_text(encoding="utf-8")
        disabled = source.split("int vb_peer_enable(int enabled)", 1)[1].split(
            "void vb_peer_release_host_connection", 1
        )[0]
        self.assertIn("g_peer.scanning = 0", disabled)
        self.assertIn("if (was_scanning) ble_gap_scan_stop()", disabled)

    def test_pager_voice_capture_has_minimum_window_and_fails_fast(self) -> None:
        source = RUNTIME_SOURCE.read_text(encoding="utf-8")
        self.assertIn("#define VB_PAGER_VOICE_MIN_MS 700", source)
        self.assertIn("#define VB_PAGER_VOICE_STARTUP_GRACE_MS 1500", source)
        self.assertIn("#define VB_VOICE_OPEN_ATTEMPTS 3", source)
        self.assertIn("voice_release_pending", source)
        self.assertIn("g_vb_voice.recorded_bytes > 0", source)
        self.assertIn("g_vb_voice.open_attempts = attempt + 1", source)
        self.assertIn("app_cache_alloc(required_bytes, IMAGE_CACHE_PSRAM)", source)
        self.assertIn("app_cache_free(g_vb_voice.buffer)", source)
        self.assertIn('vb_pager_voice_capture_error("No audio captured")', source)
        self.assertIn("g_vb_voice.last_error < 0", source)

    def test_pager_voice_streams_until_release(self) -> None:
        runtime = RUNTIME_SOURCE.read_text(encoding="utf-8")
        transport = RUNTIME_TRANSPORT_SOURCE.read_text(encoding="utf-8")
        bridge = PAGER_VOICE_BRIDGE_SOURCE.read_text(encoding="utf-8")
        self.assertIn("VB_VOICE_HOLD_UNTIL_RELEASE_MS", runtime)
        self.assertIn("VB_BLE_INSTALL_VOICE_VALUE", runtime)
        self.assertIn("VB_VOICE_STREAM_END", runtime)
        self.assertIn("#define VB_PAGER_ASR_TIMEOUT_MS 120000", runtime)
        self.assertIn("VOICE_STREAM_UUID", transport)
        self.assertIn("write_with_response: bool = True", transport)
        self.assertIn("VoiceStreamCollector", bridge)
        self.assertIn("write_with_response=False", bridge)

    def test_pager_voice_ui_uses_slide_to_cancel_without_shortcuts_or_close_button(self) -> None:
        runtime = RUNTIME_SOURCE.read_text(encoding="utf-8")
        messages = runtime.split("static void vb_pager_render_messages", 1)[1].split(
            "static void vb_pager_render_compose", 1
        )[0]
        header = runtime.split("static void vb_pager_render_header", 1)[1].split(
            "static void vb_pager_render_pairing", 1
        )[0]
        for phrase in ("你好", "收到", "我到了", "稍等一下"):
            self.assertNotIn(phrase, messages)
        self.assertNotIn('vb_pager_button("<"', header)
        self.assertIn("VB_PAGER_VOICE_CANCEL_Y", runtime)
        self.assertIn("VB_PAGER_VOICE_CANCEL_DY", runtime)
        self.assertNotIn('LV_SYMBOL_UP "  Cancel"', messages)
        self.assertIn("LV_EVENT_PRESSING", messages)
        self.assertIn("vb_pager_cancel_voice_capture", runtime)
        self.assertIn("VB_VOICE_STREAM_CANCEL", runtime)

    def test_voice_supervisor_blocks_competing_ble_app_store(self) -> None:
        app_store = APP_STORE_SOURCE.read_text(encoding="utf-8")
        supervisor = VOICE_SUPERVISOR_SOURCE.read_text(encoding="utf-8")
        self.assertIn('PAGER_VOICE_ACTIVE_FILE.exists()', app_store)
        self.assertIn('pager-voice-active', app_store)
        self.assertIn('touch "$ACTIVE_FILE"', supervisor)
        self.assertIn('rm -f "$ACTIVE_FILE"', supervisor)


if __name__ == "__main__":
    unittest.main()
