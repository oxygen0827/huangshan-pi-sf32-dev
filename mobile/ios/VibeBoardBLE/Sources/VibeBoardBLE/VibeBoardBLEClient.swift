@preconcurrency import CoreBluetooth
import Foundation


public enum VibeBoardBLEError: Error {
    case bluetoothUnavailable(CBManagerState)
    case scanTimedOut
    case connectTimedOut
    case serviceMissing
    case characteristicMissing(String)
    case commandFailed(String)
}

public struct VibeBoardBLEUUIDs {
    public static var service: CBUUID { CBUUID(string: "454d5452-0100-0000-5453-4e4954524256") }
    public static var command: CBUUID { CBUUID(string: "454d5452-0200-0000-5453-4e4954524256") }
    public static var status: CBUUID { CBUUID(string: "454d5452-0300-0000-5453-4e4954524256") }
}

public enum VibeBoardRuntimeDefaults {
    public static let bleAppPageLimit = 1
    public static let dataChunkBytes: UInt32 = 160
    public static let voiceChunkBytes: UInt32 = 200
    public static let installChunkBytes = 48
    public static let maxInstallChunkBytes = 240
}

public enum VibeBoardBLEConnectionState: Equatable, Sendable {
    case idle
    case waitingForBluetooth
    case connectingCached
    case scanning
    case connectingDiscovered
    case discovering
    case connected
    case disconnected
    case failed(String)

    public var label: String {
        switch self {
        case .idle: return "Idle"
        case .waitingForBluetooth: return "Waiting for Bluetooth"
        case .connectingCached: return "Connecting cached VibeBoard"
        case .scanning: return "Scanning for VibeBoard"
        case .connectingDiscovered: return "Connecting discovered VibeBoard"
        case .discovering: return "Discovering Runtime service"
        case .connected: return "Connected"
        case .disconnected: return "Disconnected"
        case .failed(let message): return "Failed: \(message)"
        }
    }
}

public struct VibeBoardRuntimeInstallCapabilities: Codable, Equatable, Sendable {
    public let ser: Int
    public let ble: Int
    public let stage: Int
    public let max: Int

    private enum CodingKeys: String, CodingKey {
        case ser, ble, stage, max
    }

    public init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        ser = try container.decode(Int.self, forKey: .ser)
        ble = try container.decode(Int.self, forKey: .ble)
        stage = try container.decodeIfPresent(Int.self, forKey: .stage) ?? 0
        max = try container.decode(Int.self, forKey: .max)
    }
}

public struct VibeBoardRuntimeAppCapabilities: Codable, Equatable, Sendable {
    public let manifest: Int
    public let lua: String
    public let comp: Int
    public let assets: Int

    private enum CodingKeys: String, CodingKey {
        case manifest, lua, comp, assets
    }

    public init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        manifest = try container.decodeIfPresent(Int.self, forKey: .manifest) ?? 1
        lua = try container.decode(String.self, forKey: .lua)
        comp = try container.decode(Int.self, forKey: .comp)
        assets = try container.decodeIfPresent(Int.self, forKey: .assets) ?? 1
    }
}

public struct VibeBoardRuntimeHardwareCapabilities: Codable, Equatable, Sendable {
    public let disp: Int
    public let touch: Int
    public let sens: Int
    public let voice: Int
    public let audio: Int?
    public let flow: Int
    public let batt: Int
    public let chg: Int
    public let gpio: Int
    public let rgb: Int
}

public struct VibeBoardRuntimeCapabilities: Codable, Equatable, Sendable {
    public let api: String
    public let rt: String
    public let ble: String
    public let sens: String
    public let touch: String?
    public let flow: String
    public let voice: String
    public let audio: String?
    public let pwr: String?
    public let disp: String?
    public let gpio: String?
    public let rgb: String?
    public let fs: Int
    public let ins: VibeBoardRuntimeInstallCapabilities
    public let app: VibeBoardRuntimeAppCapabilities
    public let hw: VibeBoardRuntimeHardwareCapabilities

    public var summary: String {
        [
            "rt=\(rt)",
            "lua=\(app.lua)",
            "install=\(ins.ser != 0 ? "serial" : "--")/\(ins.ble != 0 ? "ble" : "--")",
            "hw=disp:\(hw.disp) touch:\(hw.touch) sensors:\(hw.sens) voice:\(hw.voice) audio:\(hw.audio ?? 0) flow:\(hw.flow) batt:\(hw.batt) chg:\(hw.chg) gpio:\(hw.gpio) rgb:\(hw.rgb)",
            "display_api=\(disp ?? "--")",
            "touch_api=\(touch ?? "--")",
            "gpio_api=\(gpio ?? "--")"
        ].joined(separator: " ")
    }
}

public struct VibeBoardPowerBattery: Codable, Equatable, Sendable {
    public let ok: Int
    public let mv: UInt32
    public let raw: UInt32
    public let dev: String
    public let ch: Int
}

public struct VibeBoardPowerCharger: Codable, Equatable, Sendable {
    public let ok: Int
    public let status: String
    public let state: Int?
    public let det: Int?
    public let en: Int?
    public let sys: UInt32?
    public let fault: UInt32?
}

public struct VibeBoardPowerSnapshot: Codable, Equatable, Sendable {
    public let api: String
    public let available: Int
    public let ready: Int
    public let battery: VibeBoardPowerBattery
    public let charger: VibeBoardPowerCharger

    public var summary: String {
        let batteryText = battery.ok != 0 ? "battery=\(battery.mv)mV raw=\(battery.raw)" : "battery=--"
        let chargerText = charger.ok != 0 ? "charger=\(charger.status)" : "charger=--"
        return "\(batteryText) \(chargerText)"
    }
}


public struct VibeBoardDisplaySnapshot: Codable, Equatable, Sendable {
    public let api: String
    public let available: Int
    public let ready: Int
    public let ok: Int
    public let dev: String
    public let width: Int
    public let height: Int
    public let bpp: Int
    public let format: Int
    public let align: Int
    public let brightness: Int
    public let state: Int
    public let stateName: String

    private enum CodingKeys: String, CodingKey {
        case api, available, ready, ok, dev, width, height, bpp, format, align, brightness, state
        case stateName = "state_name"
    }

    public var summary: String {
        ok != 0 ? "display=\(width)x\(height) \(brightness)% \(stateName)" : "display=--"
    }
}

public struct VibeBoardRuntimeAppStatus: Codable, Equatable, Sendable {
    public let api: String
    public let active: String
    public let state: String
    public let running: Int
    public let failed: Int
    public let lastStatus: Int
    public let lastError: String
    public let launches: UInt32
    public let stops: UInt32
    public let pendingReload: Int
    public let pendingStop: Int
    public let launcherPage: Int
    public let launcherTotal: Int
    public let launcherCount: Int
    public let pendingDelete: String
    public let lua: String

    private enum CodingKeys: String, CodingKey {
        case api, active, state, running, failed, launches, stops, lua
        case lastStatus = "last_status"
        case lastError = "last_error"
        case pendingReload = "pending_reload"
        case pendingStop = "pending_stop"
        case launcherPage = "launcher_page"
        case launcherTotal = "launcher_total"
        case launcherCount = "launcher_count"
        case pendingDelete = "pending_delete"
    }

    public var summary: String {
        "app active=\(active) state=\(state) running=\(running) failed=\(failed) launches=\(launches) stops=\(stops) lua=\(lua)"
    }
}

public struct VibeBoardRuntimeInstalledApp: Codable, Equatable, Identifiable, Sendable {
    public let id: String
    public let name: String
    public let description: String?
    public let category: String?
    public let icon: String?
    public let author: String?
    public let screenshot: String?
    public let requirements: [String]?
    public let active: Int
    public let compatible: Int
    public let manifest: Int
    public let appInfo: Int
    public let mainLua: Int

    private enum CodingKeys: String, CodingKey {
        case id, name, description, category, icon, author, screenshot, requirements, active, compatible, manifest
        case appInfo = "app_info"
        case mainLua = "main_lua"
    }
}

public struct VibeBoardRuntimeAppList: Codable, Equatable, Sendable {
    public let api: String
    public let active: String
    public let state: String
    public let apps: [VibeBoardRuntimeInstalledApp]
    public let count: Int
    public let included: Int
    public let truncated: Int
    public let offset: Int?
    public let limit: Int?

    public init(api: String, active: String, state: String, apps: [VibeBoardRuntimeInstalledApp], count: Int, included: Int, truncated: Int, offset: Int? = nil, limit: Int? = nil) {
        self.api = api
        self.active = active
        self.state = state
        self.apps = apps
        self.count = count
        self.included = included
        self.truncated = truncated
        self.offset = offset
        self.limit = limit
    }

    public var summary: String {
        let visible = apps.map { app in
            let marker = app.active != 0 ? "*" : "-"
            let compat = app.compatible != 0 ? "ok" : "bad"
            return "\(marker)\(app.id)(\(compat))"
        }.joined(separator: " ")
        return "apps count=\(count) active=\(active) state=\(state) \(visible)"
    }
}

public struct VibeBoardRuntimeAppCommandAck: Equatable, Sendable {
    public let command: String
    public let appId: String?
    public let resultCode: Int
    public let isOK: Bool
    public let rawStatus: String

    public init(statusText: String, command: String, appId: String? = nil) throws {
        let expectedPrefix = "ok \(command) "
        let expectedErrorPrefix = "err \(command) "
        let fallbackPrefix = "\(command) "
        let line = vibeBoardLatestStatusLine(statusText) { value in
            let matchesCommand = value.hasPrefix(expectedPrefix) ||
                value.hasPrefix(expectedErrorPrefix) ||
                value.hasPrefix(fallbackPrefix)
            guard matchesCommand else { return false }
            guard let appId else { return true }
            return value.contains("app=\(appId)")
        }
        guard line.hasPrefix(expectedPrefix) ||
            line.hasPrefix(expectedErrorPrefix) ||
            line.hasPrefix(fallbackPrefix) else {
            throw VibeBoardBLEError.commandFailed(statusText)
        }
        let values = vibeBoardParseKeyValues(line)
        if let appId {
            guard values["app"] == appId else {
                throw VibeBoardBLEError.commandFailed(statusText)
            }
        }
        self.command = command
        self.appId = appId ?? values["app"]
        self.resultCode = try vibeBoardInt(values, "rc", raw: line)
        self.isOK = !line.hasPrefix(expectedErrorPrefix) && resultCode == 0
        self.rawStatus = line
    }

    public var summary: String {
        let appText = appId.map { " app=\($0)" } ?? ""
        return "\(isOK ? "ok" : "err") \(command)\(appText) rc=\(resultCode)"
    }
}

public struct VibeBoardRGBSnapshot: Codable, Equatable, Sendable {
    public let api: String
    public let available: Int
    public let ready: Int
    public let ok: Int
    public let dev: String
    public let count: Int
    public let color: String
    public let name: String

    public var summary: String {
        ok != 0 ? "rgb=#\(color) \(name)" : "rgb=--"
    }
}

public struct VibeBoardGPIOPinSnapshot: Codable, Equatable, Sendable {
    public let ok: Int
    public let pin: Int
    public let activeHigh: Int
    public let level: Int
    public let pressed: Int

    private enum CodingKeys: String, CodingKey {
        case ok, pin, level, pressed
        case activeHigh = "active_high"
    }
}

public struct VibeBoardGPIOSnapshot: Codable, Equatable, Sendable {
    public let api: String
    public let available: Int
    public let ready: Int
    public let count: Int
    public let inputsOnly: Int
    public let key1: VibeBoardGPIOPinSnapshot
    public let key2: VibeBoardGPIOPinSnapshot

    private enum CodingKeys: String, CodingKey {
        case api, available, ready, count, key1, key2
        case inputsOnly = "inputs_only"
    }

    public var summary: String {
        "gpio key1=\(key1.pressed != 0 ? "pressed" : "released")/\(key1.level) key2=\(key2.pressed != 0 ? "pressed" : "released")/\(key2.level)"
    }
}

public struct VibeBoardTouchSnapshot: Codable, Equatable, Sendable {
    public let api: String
    public let available: Int
    public let ready: Int
    public let active: Int
    public let count: UInt32
    public let x: Int
    public let y: Int
    public let event: String
    public let gesture: String?
    public let dx: Int?
    public let dy: Int?
    public let durationMs: UInt32?
    public let tick: UInt32

    private enum CodingKeys: String, CodingKey {
        case api, available, ready, active, count, x, y, event, gesture, dx, dy, tick
        case durationMs = "duration_ms"
    }

    public var summary: String {
        let gestureText = gesture ?? "none"
        let dxText = dx ?? 0
        let dyText = dy ?? 0
        let durationText = durationMs ?? 0
        return "touch count=\(count) active=\(active) event=\(event) gesture=\(gestureText) point=\(x),\(y) delta=\(dxText),\(dyText) duration=\(durationText)ms"
    }
}

public struct VibeBoardSensorVector: Codable, Equatable, Sendable {
    public let ok: Int
    public let x: Int
    public let y: Int
    public let z: Int
    public let ts: UInt32?
}

public struct VibeBoardLightSensor: Codable, Equatable, Sendable {
    public let ok: Int
    public let lux: Int
    public let ts: UInt32?
}

public struct VibeBoardStepSensor: Codable, Equatable, Sendable {
    public let ok: Int
    public let count: UInt32
    public let ts: UInt32?
}

public struct VibeBoardSensorSnapshot: Codable, Equatable, Sendable {
    public let api: String
    public let available: Int
    public let ready: Int
    public let count: UInt32
    public let light: VibeBoardLightSensor
    public let mag: VibeBoardSensorVector
    public let acce: VibeBoardSensorVector
    public let gyro: VibeBoardSensorVector
    public let step: VibeBoardStepSensor

    public var summary: String {
        [
            light.ok != 0 ? "light=\(light.lux)lux" : "light=--",
            acce.ok != 0 ? "acce=\(acce.x),\(acce.y),\(acce.z)mg" : "acce=--",
            gyro.ok != 0 ? "gyro=\(gyro.x),\(gyro.y),\(gyro.z)mdps" : "gyro=--",
            mag.ok != 0 ? "mag=\(mag.x),\(mag.y),\(mag.z)" : "mag=--",
            step.ok != 0 ? "step=\(step.count)" : "step=--"
        ].joined(separator: " ")
    }
}


public struct VibeBoardVoiceSnapshot: Codable, Equatable, Sendable {
    public let api: String
    public let available: Int
    public let built: Int
    public let ready: Int
    public let recording: Int
    public let seq: UInt32
    public let requestedMs: UInt32
    public let bytes: UInt32
    public let rate: Int
    public let bits: Int
    public let channels: Int
    public let dropped: UInt32
    public let err: Int

    private enum CodingKeys: String, CodingKey {
        case api, available, built, ready, recording, seq, bytes, rate, bits, channels, dropped, err
        case requestedMs = "requested_ms"
    }

    public var summary: String {
        "voice ready=\(ready) recording=\(recording) seq=\(seq) bytes=\(bytes) err=\(err)"
    }
}

public struct VibeBoardAudioSnapshot: Codable, Equatable, Sendable {
    public let api: String
    public let available: Int
    public let playing: Int
    public let ready: Int
    public let suspended: Int
    public let seq: UInt32
    public let rate: UInt32
    public let channels: UInt32
    public let bits: UInt32
    public let bytes: UInt32
    public let total: UInt32
    public let volume: Int
    public let err: Int
    public let path: String

    public var summary: String {
        "audio playing=\(playing) ready=\(ready) \(rate)Hz/\(channels)ch/\(bits)b bytes=\(bytes)/\(total) volume=\(volume) err=\(err)"
    }
}

public struct VibeBoardInfoFlowFrame: Equatable, Sendable {
    public static let maxPayloadBytes = 192

    public let channel: String
    public let sequence: UInt32
    public let payload: String

    public init(channel: String = "phone", sequence: UInt32, payload: String) throws {
        guard Self.isSafeChannel(channel) else {
            throw VibeBoardBLEError.commandFailed("unsafe info flow channel: \(channel)")
        }
        guard Data(payload.utf8).count <= Self.maxPayloadBytes else {
            throw VibeBoardBLEError.commandFailed("info flow payload exceeds \(Self.maxPayloadBytes) bytes")
        }
        self.channel = channel
        self.sequence = sequence
        self.payload = payload
    }

    public var command: String {
        let data = Data(payload.utf8)
        return "flow_send \(channel) \(sequence) \(data.isEmpty ? "-" : data.hexString)"
    }

    public static func isSafeChannel(_ value: String) -> Bool {
        value.range(of: #"^[A-Za-z0-9_.-]{1,23}$"#, options: .regularExpression) != nil
    }
}

public struct VibeBoardVoiceStatus: Equatable, Sendable {
    public let api: String
    public let built: Int
    public let ready: Int
    public let recording: Int
    public let sequence: UInt32
    public let bytes: UInt32
    public let rate: Int
    public let bits: Int
    public let channels: Int
    public let dropped: UInt32
    public let error: Int
    public let rawStatus: String

    public var summary: String {
        "voice seq=\(sequence) ready=\(ready) recording=\(recording) bytes=\(bytes) rate=\(rate)Hz err=\(error)"
    }

    public init(statusText: String) throws {
        let line = VibeBoardBLEStatusMatcher.voiceStatusLine(statusText)
        guard line.hasPrefix("ok voice api=") else {
            throw VibeBoardBLEError.commandFailed(statusText)
        }
        let values = vibeBoardParseKeyValues(line)
        api = try vibeBoardString(values, "api", raw: line)
        built = try vibeBoardInt(values, "built", raw: line)
        ready = try vibeBoardInt(values, "ready", raw: line)
        recording = try vibeBoardInt(values, "recording", raw: line)
        sequence = try vibeBoardUInt32(values, "seq", raw: line)
        bytes = try vibeBoardUInt32(values, "bytes", raw: line)
        rate = try vibeBoardInt(values, "rate", raw: line)
        bits = try vibeBoardInt(values, "bits", raw: line)
        channels = try vibeBoardInt(values, "channels", raw: line)
        dropped = try vibeBoardUInt32(values, "dropped", raw: line)
        error = try vibeBoardInt(values, "err", raw: line)
        rawStatus = line
    }
}

public struct VibeBoardVoiceStartAck: Equatable, Sendable {
    public let sequence: UInt32
    public let bytes: UInt32
    public let durationMs: UInt32
    public let resultCode: Int
    public let built: Int
    public let rawStatus: String

    public init(statusText: String) throws {
        let line = VibeBoardBLEStatusMatcher.voiceStartLine(statusText)
        guard line.hasPrefix("ok voice_start ") ||
            line.hasPrefix("err voice_start ") ||
            line.hasPrefix("ok voice api=") else {
            throw VibeBoardBLEError.commandFailed(statusText)
        }
        let values = vibeBoardParseKeyValues(line)
        sequence = try vibeBoardUInt32(values, "seq", raw: line)
        bytes = try vibeBoardUInt32(values, "bytes", raw: line)
        if let ms = values["ms"] {
            durationMs = try vibeBoardUInt32(["ms": ms], "ms", raw: line)
        } else if let requested = values["requested_ms"] {
            durationMs = try vibeBoardUInt32(["requested_ms": requested], "requested_ms", raw: line)
        } else {
            durationMs = 0
        }
        if line.hasPrefix("ok voice api=") {
            let err = Int(values["err"] ?? "0") ?? 0
            resultCode = err < 0 ? err : 0
        } else {
            resultCode = try vibeBoardInt(values, "rc", raw: line)
        }
        built = try vibeBoardInt(values, "built", raw: line)
        rawStatus = line
    }
}

public struct VibeBoardVoiceChunk: Equatable, Sendable {
    public let sequence: UInt32
    public let offset: UInt32
    public let bytes: UInt32
    public let payload: Data
    public let rawStatus: String

    public init(statusText: String) throws {
        let line = VibeBoardBLEStatusMatcher.voiceReadLine(statusText, expectedSequence: nil, offset: nil)
        guard line.hasPrefix("ok voice_data ") else {
            throw VibeBoardBLEError.commandFailed(statusText)
        }
        let values = vibeBoardParseKeyValues(line)
        sequence = try vibeBoardUInt32(values, "seq", raw: line)
        offset = try vibeBoardUInt32(values, "offset", raw: line)
        bytes = try vibeBoardUInt32(values, "bytes", raw: line)
        guard let hex = values["hex"] else {
            throw VibeBoardBLEError.commandFailed(line)
        }
        payload = try vibeBoardHexData(hex, raw: line)
        guard payload.count == Int(bytes) else {
            throw VibeBoardBLEError.commandFailed(line)
        }
        rawStatus = line
    }
}

private func vibeBoardMergeStatus(previous: String, current: String) -> String {
    let prev = previous.trimmingCharacters(in: .whitespacesAndNewlines)
    let curr = current.trimmingCharacters(in: .whitespacesAndNewlines)
    if curr.isEmpty { return prev }
    if prev.isEmpty { return curr }
    if curr == prev { return prev }
    if curr.hasPrefix(prev) { return curr }
    if prev.hasPrefix(curr) { return prev }

    let overlap = min(prev.count, curr.count)
    for size in stride(from: overlap, through: 1, by: -1) {
        let prevSuffix = String(prev.suffix(size))
        let currPrefix = String(curr.prefix(size))
        if prevSuffix == currPrefix {
            return prev + curr.dropFirst(size)
        }
    }
    return curr
}

private func vibeBoardJSONLineIsTruncated(_ value: String) -> Bool {
    guard let data = value.data(using: .utf8),
          let object = try? JSONSerialization.jsonObject(with: data) as? [String: Any]
    else {
        return value.contains("\"error\":\"truncated\"")
            || value.contains("\"truncated\":1")
            || value.contains("\"truncated\":true")
            || value.contains("\"truncated\":\"1\"")
    }
    if object["error"] as? String == "truncated" { return true }
    if let truncated = object["truncated"] {
        if let boolValue = truncated as? Bool { return boolValue }
        if let intValue = truncated as? Int { return intValue == 1 }
        if let stringValue = truncated as? String { return stringValue == "1" }
    }
    return false
}

func vibeBoardExtractJSONLine(_ text: String, expectedAPI: String, allowTruncated: Bool = false) -> String? {
    var best: String?
    var searchIndex = text.startIndex
    while let start = text[searchIndex...].firstIndex(of: "{") {
        var index = start
        var depth = 0
        var inString = false
        var escaped = false
        while index < text.endIndex {
            let char = text[index]
            if inString {
                if escaped {
                    escaped = false
                } else if char == "\\" {
                    escaped = true
                } else if char == Character("\"") {
                    inString = false
                }
            } else if char == Character("\"") {
                inString = true
            } else if char == "{" {
                depth += 1
            } else if char == "}" {
                depth -= 1
                if depth == 0 {
                    let end = text.index(after: index)
                    let candidate = String(text[start..<end])
                    if let data = candidate.data(using: .utf8),
                       let object = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
                       object["api"] as? String == expectedAPI {
                        if allowTruncated || !vibeBoardJSONLineIsTruncated(candidate) {
                            best = candidate
                        }
                    }
                    searchIndex = end
                    break
                }
            }
            index = text.index(after: index)
        }
        if index >= text.endIndex {
            searchIndex = text.index(after: start)
        }
    }
    return best
}

func vibeBoardContainsTruncatedJSONLine(_ text: String, expectedAPI: String) -> Bool {
    vibeBoardExtractJSONLine(text, expectedAPI: expectedAPI, allowTruncated: true).map(vibeBoardJSONLineIsTruncated) ?? false
}

func vibeBoardCombineAppPages(_ pages: [VibeBoardRuntimeAppList]) throws -> VibeBoardRuntimeAppList {
    guard let first = pages.first else {
        return VibeBoardRuntimeAppList(api: "vibeboard-huangshan-app-manager/v1", active: "", state: "unknown", apps: [], count: 0, included: 0, truncated: 0)
    }
    var apps: [VibeBoardRuntimeInstalledApp] = []
    var seenAppIds = Set<String>()
    var expectedOffset = 0
    for page in pages {
        guard page.api == first.api else { throw VibeBoardBLEError.commandFailed("app page API mismatch") }
        guard page.count == first.count else { throw VibeBoardBLEError.commandFailed("app page count changed") }
        let pageOffset = page.offset ?? expectedOffset
        guard pageOffset == expectedOffset else { throw VibeBoardBLEError.commandFailed("app page offset mismatch expected=\(expectedOffset) got=\(pageOffset)") }
        guard page.included == page.apps.count else { throw VibeBoardBLEError.commandFailed("app page included mismatch") }
        for app in page.apps {
            guard !app.id.isEmpty else { throw VibeBoardBLEError.commandFailed("app page entry missing id") }
            guard seenAppIds.insert(app.id).inserted else { throw VibeBoardBLEError.commandFailed("app page duplicate app id: \(app.id)") }
        }
        apps.append(contentsOf: page.apps)
        expectedOffset += page.included
    }
    guard apps.count == first.count else {
        throw VibeBoardBLEError.commandFailed("app pages incomplete: got=\(apps.count) count=\(first.count)")
    }
    return VibeBoardRuntimeAppList(
        api: first.api,
        active: first.active,
        state: first.state,
        apps: apps,
        count: first.count,
        included: apps.count,
        truncated: 0
    )
}

private func vibeBoardExtractJSONReadChunk(_ text: String, kind: String) -> (offset: Int, total: Int, payload: Data)? {
    for line in text.split(separator: "\n").reversed() {
        let value = line.trimmingCharacters(in: .whitespacesAndNewlines)
        guard value.hasPrefix("ok json_read ") else { continue }
        let values = vibeBoardParseKeyValues(value)
        guard values["kind"] == kind,
              let offsetText = values["offset"],
              let totalText = values["total"],
              let offset = Int(offsetText),
              let total = Int(totalText),
              let hex = values["hex"]
        else {
            continue
        }
        guard let payload = try? vibeBoardHexData(hex, raw: value) else {
            continue
        }
        return (offset, total, payload)
    }
    return nil
}
private func vibeBoardStripLogPrefix(_ rawValue: String) -> String {
    var value = rawValue.trimmingCharacters(in: .whitespacesAndNewlines)
    for marker in ["[vb_runtime][flow] ", "[vb_runtime][voice] ", "[vb_runtime] "] {
        if let range = value.range(of: marker) {
            return String(value[range.upperBound...]).trimmingCharacters(in: .whitespacesAndNewlines)
        }
    }
    for prompt in ["msh />", "msh >"] {
        if let range = value.range(of: prompt, options: .backwards) {
            value = String(value[range.upperBound...]).trimmingCharacters(in: .whitespacesAndNewlines)
        }
    }
    if let range = value.range(of: "] ", options: .backwards) {
        return String(value[range.upperBound...]).trimmingCharacters(in: .whitespacesAndNewlines)
    }
    return value
}

private func vibeBoardLatestStatusLine(_ text: String, matching matcher: (String) -> Bool) -> String {
    for rawLine in text.split(whereSeparator: \.isNewline).reversed() {
        let value = vibeBoardStripLogPrefix(String(rawLine))
        if matcher(value) { return value }
    }
    let value = vibeBoardStripLogPrefix(text)
    if matcher(value) { return value }
    return value
}

private func vibeBoardStatusLineIsError(_ text: String, matching matcher: (String) -> Bool) -> Bool {
    vibeBoardLatestStatusLine(text, matching: matcher).hasPrefix("err ")
}

private func vibeBoardParseKeyValues(_ text: String) -> [String: String] {
    var values: [String: String] = [:]
    for token in text.split(separator: " ", omittingEmptySubsequences: true) {
        guard let separator = token.firstIndex(of: "=") else { continue }
        let key = String(token[..<separator])
        let value = String(token[token.index(after: separator)...])
        values[key] = value
    }
    return values
}

private func vibeBoardString(_ values: [String: String], _ key: String, raw: String) throws -> String {
    guard let value = values[key], !value.isEmpty else {
        throw VibeBoardBLEError.commandFailed(raw)
    }
    return value
}

private func vibeBoardInt(_ values: [String: String], _ key: String, raw: String) throws -> Int {
    let value = try vibeBoardString(values, key, raw: raw)
    guard let parsed = Int(value) else {
        throw VibeBoardBLEError.commandFailed(raw)
    }
    return parsed
}

private func vibeBoardUInt32(_ values: [String: String], _ key: String, raw: String) throws -> UInt32 {
    let value = try vibeBoardString(values, key, raw: raw)
    guard let parsed = UInt32(value) else {
        throw VibeBoardBLEError.commandFailed(raw)
    }
    return parsed
}

private func vibeBoardHexData(_ hex: String, raw: String) throws -> Data {
    if hex == "-" {
        return Data()
    }
    guard hex.count.isMultiple(of: 2) else {
        throw VibeBoardBLEError.commandFailed(raw)
    }
    var data = Data(capacity: hex.count / 2)
    var index = hex.startIndex
    while index < hex.endIndex {
        let next = hex.index(index, offsetBy: 2)
        guard let byte = UInt8(hex[index..<next], radix: 16) else {
            throw VibeBoardBLEError.commandFailed(raw)
        }
        data.append(byte)
        index = next
    }
    return data
}

enum VibeBoardBLEStatusMatcher {
    static func installAckMatches(_ status: String, command: String) -> Bool {
        let parts = command.split(separator: " ", omittingEmptySubsequences: true).map(String.init)
        guard let name = parts.first else { return false }
        switch name {
        case "vb_runtime_install_begin":
            guard parts.count >= 2 else { return false }
            let appId = parts[1]
            return status.contains("install_begin app=\(appId) rc=") || status.contains("install_begin \(appId) rc=")
        case "vb_runtime_install_file":
            guard parts.count >= 5 else { return false }
            let appId = parts[1]
            let path = parts[2]
            let offset = parts[3]
            return status.contains("install_file app=\(appId) path=\(path) offset=\(offset) rc=") ||
                status.contains("install_file \(appId)/\(path) rc=")
        case "vb_runtime_install_end":
            guard parts.count >= 2 else { return false }
            let appId = parts[1]
            return status.contains("install_end app=\(appId) ") || status.contains("install_end \(appId) rc=")
        case "vb_runtime_install_abort":
            guard parts.count >= 2 else { return false }
            let appId = parts[1]
            return status.contains("install_abort app=\(appId) rc=")
        default:
            return false
        }
    }

    static func installConfirmed(_ status: String, appId: String) -> Bool {
        status.contains("active=\(appId)")
    }

    static func infoFlowAckMatches(_ status: String, channel: String, sequence: UInt32) -> Bool {
        let line = vibeBoardLatestStatusLine(status) { value in
            value.hasPrefix("ok flow_send ") || value.hasPrefix("err flow_send ") || value.hasPrefix("recv ")
        }
        return (line.hasPrefix("ok flow_send ") || line.hasPrefix("err flow_send ") || line.hasPrefix("recv ")) &&
            line.contains("channel=\(channel)") &&
            line.contains("seq=\(sequence)")
    }

    static func infoFlowStatusMatches(_ status: String) -> Bool {
        let line = vibeBoardLatestStatusLine(status) { value in
            value.hasPrefix("ok flow api=") || value.hasPrefix("total=")
        }
        return line.hasPrefix("ok flow api=") || line.hasPrefix("total=")
    }

    static func infoFlowClearMatches(_ status: String) -> Bool {
        let line = vibeBoardLatestStatusLine(status) { value in
            value.hasPrefix("ok flow_clear") || value.hasPrefix("err flow_clear") || value == "cleared"
        }
        return line.hasPrefix("ok flow_clear") || line.hasPrefix("err flow_clear") || line == "cleared"
    }

    static func appLaunchMatches(_ status: String, appId: String) -> Bool {
        let line = vibeBoardLatestStatusLine(status) { value in
            (value.hasPrefix("ok launch ") || value.hasPrefix("err launch ") || value.hasPrefix("launch ")) &&
                value.contains("app=\(appId)")
        }
        return (line.hasPrefix("ok launch ") || line.hasPrefix("err launch ") || line.hasPrefix("launch ")) &&
            line.contains("app=\(appId)")
    }

    static func appStopMatches(_ status: String) -> Bool {
        let line = vibeBoardLatestStatusLine(status) { value in
            value.hasPrefix("ok stop ") || value.hasPrefix("err stop ") || value.hasPrefix("stop ")
        }
        return line.hasPrefix("ok stop ") || line.hasPrefix("err stop ") || line.hasPrefix("stop ")
    }

    static func appDeleteMatches(_ status: String, appId: String) -> Bool {
        let line = vibeBoardLatestStatusLine(status) { value in
            (value.hasPrefix("ok delete ") || value.hasPrefix("err delete ") || value.hasPrefix("delete ")) &&
                value.contains("app=\(appId)")
        }
        return (line.hasPrefix("ok delete ") || line.hasPrefix("err delete ") || line.hasPrefix("delete ")) &&
            line.contains("app=\(appId)")
    }

    static func voiceStatusLine(_ status: String, expectedSequence: UInt32? = nil) -> String {
        vibeBoardLatestStatusLine(status) { value in
            guard value.hasPrefix("ok voice api=") else { return false }
            guard let expectedSequence else { return true }
            return value.contains("seq=\(expectedSequence)")
        }
    }

    static func voiceStatusMatches(_ status: String, expectedSequence: UInt32? = nil) -> Bool {
        voiceStatusLine(status, expectedSequence: expectedSequence).hasPrefix("ok voice api=")
    }

    static func voiceStartLine(_ status: String, expectedSequence: UInt32? = nil, durationMs: UInt32? = nil) -> String {
        vibeBoardLatestStatusLine(status) { value in
            if value.hasPrefix("err voice_start ") {
                return true
            }
            if value.hasPrefix("ok voice_start ") {
                if let expectedSequence, !value.contains("seq=\(expectedSequence)") { return false }
                if let durationMs,
                   !value.contains("ms=\(durationMs)"),
                   !value.contains("requested_ms=\(durationMs)") {
                    return false
                }
                return true
            }
            if value.hasPrefix("ok voice api=") {
                if let expectedSequence, !value.contains("seq=\(expectedSequence)") { return false }
                return true
            }
            return false
        }
    }

    static func voiceStartMatches(_ status: String, expectedSequence: UInt32, durationMs: UInt32) -> Bool {
        let line = voiceStartLine(status, expectedSequence: expectedSequence, durationMs: durationMs)
        return line.hasPrefix("ok voice_start ") ||
            line.hasPrefix("err voice_start ") ||
            line.hasPrefix("ok voice api=")
    }

    static func voiceReadLine(_ status: String, expectedSequence: UInt32?, offset: UInt32?) -> String {
        vibeBoardLatestStatusLine(status) { value in
            if value.hasPrefix("ok voice_data ") {
                if let expectedSequence, !value.contains("seq=\(expectedSequence)") { return false }
                if let offset, !value.contains("offset=\(offset)") { return false }
                return true
            }
            if value.hasPrefix("err voice_read ") {
                if let offset, !value.contains("offset=\(offset)") { return false }
                return true
            }
            return false
        }
    }

    static func voiceReadMatches(_ status: String, expectedSequence: UInt32, offset: UInt32) -> Bool {
        let line = voiceReadLine(status, expectedSequence: expectedSequence, offset: offset)
        if line.hasPrefix("ok voice_data ") {
            return true
        }
        if line.hasPrefix("err voice_read ") {
            return true
        }
        return false
    }

    static func voiceClearMatches(_ status: String) -> Bool {
        let line = vibeBoardLatestStatusLine(status) { value in
            value.hasPrefix("ok voice_clear") || value.hasPrefix("err voice_clear") || value == "cleared"
        }
        return line.hasPrefix("ok voice_clear") || line.hasPrefix("err voice_clear") || line == "cleared"
    }

    static func voiceStopMatches(_ status: String) -> Bool {
        let line = vibeBoardLatestStatusLine(status) { value in
            value.hasPrefix("ok voice_stop ") || value.hasPrefix("err voice_stop ")
        }
        return line.hasPrefix("ok voice_stop ") || line.hasPrefix("err voice_stop ")
    }
}



@MainActor
public protocol VibeBoardRuntimeTransport: AnyObject {
    var latestStatus: String { get }
    var connectionState: VibeBoardBLEConnectionState { get }
    var onStateChange: ((VibeBoardBLEConnectionState) -> Void)? { get set }

    func connect(scanTimeout: TimeInterval, connectTimeout: TimeInterval) async throws
    func disconnect()
    func isReadyForRuntimeCommands() -> Bool

    func status() async throws -> String
    func capabilities() async throws -> VibeBoardRuntimeCapabilities
    func sensors() async throws -> VibeBoardSensorSnapshot
    func power() async throws -> VibeBoardPowerSnapshot
    func display(brightness: Int?) async throws -> VibeBoardDisplaySnapshot
    func touch() async throws -> VibeBoardTouchSnapshot
    func gpio() async throws -> VibeBoardGPIOSnapshot
    func rgb(color: String?) async throws -> VibeBoardRGBSnapshot
    func voice() async throws -> VibeBoardVoiceSnapshot
    func audio() async throws -> VibeBoardAudioSnapshot
    func audioPlay(appId: String, path: String) async throws -> VibeBoardAudioSnapshot
    func audioStop() async throws -> VibeBoardAudioSnapshot
    func audioVolume(_ volume: Int) async throws -> VibeBoardAudioSnapshot

    func appStatus() async throws -> VibeBoardRuntimeAppStatus
    func apps() async throws -> VibeBoardRuntimeAppList
    func launchApp(_ appId: String) async throws -> VibeBoardRuntimeAppCommandAck
    func stopApp() async throws -> VibeBoardRuntimeAppCommandAck
    func deleteApp(_ appId: String) async throws -> VibeBoardRuntimeAppCommandAck
    func abortInstall(_ appId: String) async throws -> String

    func sendInfoFlow(channel: String, sequence: UInt32, payload: String) async throws -> String
    func infoFlowStatus() async throws -> String
    func clearInfoFlow() async throws -> String

    func voiceStatus(expectedSequence: UInt32?) async throws -> VibeBoardVoiceStatus
    func voiceStart(durationMs: UInt32) async throws -> VibeBoardVoiceStartAck
    func voiceStop() async throws -> String
    func voiceRead(offset: UInt32, maxBytes: UInt32, expectedSequence: UInt32) async throws -> VibeBoardVoiceChunk
    func voiceClear() async throws -> String
    func captureVoice(durationMs: UInt32, chunkBytes: UInt32, pollInterval: TimeInterval, readyTimeout: TimeInterval) async throws -> (status: VibeBoardVoiceStatus, pcm: Data)
    func sendVoiceReply(sequence: UInt32, payload: String, channel: String) async throws -> String

    func install(_ package: RuntimePackage, chunkBytes: Int) async throws
}

public extension VibeBoardRuntimeTransport {
    func connect() async throws {
        try await connect(scanTimeout: 12, connectTimeout: 12)
    }

    func display() async throws -> VibeBoardDisplaySnapshot {
        try await display(brightness: nil)
    }

    func rgb() async throws -> VibeBoardRGBSnapshot {
        try await rgb(color: nil)
    }

    func voiceStatus() async throws -> VibeBoardVoiceStatus {
        try await voiceStatus(expectedSequence: nil)
    }

    func voiceStart() async throws -> VibeBoardVoiceStartAck {
        try await voiceStart(durationMs: 1500)
    }

    func captureVoice(
        durationMs: UInt32 = 1500,
        chunkBytes: UInt32 = VibeBoardRuntimeDefaults.voiceChunkBytes,
        pollInterval: TimeInterval = 0.25,
        readyTimeout: TimeInterval = 8.0
    ) async throws -> (status: VibeBoardVoiceStatus, pcm: Data) {
        try await captureVoice(
            durationMs: durationMs,
            chunkBytes: chunkBytes,
            pollInterval: pollInterval,
            readyTimeout: readyTimeout
        )
    }

    func sendVoiceReply(sequence: UInt32, payload: String) async throws -> String {
        try await sendVoiceReply(sequence: sequence, payload: payload, channel: "pc.voice")
    }

    func install(_ package: RuntimePackage) async throws {
        try await install(package, chunkBytes: VibeBoardRuntimeDefaults.installChunkBytes)
    }
}

@MainActor
public final class VibeBoardBLEClient: NSObject {
    private func trace(_ message: String) {
        print("[vibeboard-ios][ble] \(message)")
        NSLog("[vibeboard-ios][ble] %@", message)
    }

    public private(set) var latestStatus = ""
    public private(set) var connectionState: VibeBoardBLEConnectionState = .idle {
        didSet {
            trace("state=\(connectionState.label)")
            onStateChange?(connectionState)
        }
    }
    public var onStateChange: ((VibeBoardBLEConnectionState) -> Void)?

    private let deviceName: String
    private let defaults: UserDefaults
    private let cacheKey: String
    private var deviceScopedCacheKey: String { "\(cacheKey).\(deviceName)" }
    private var central: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var commandCharacteristic: CBCharacteristic?
    private var statusCharacteristic: CBCharacteristic?

    private var stateContinuation: CheckedContinuation<Void, Error>?
    private var scanContinuation: CheckedContinuation<CBPeripheral, Error>?
    private var connectContinuation: CheckedContinuation<Void, Error>?
    private var discoveryContinuation: CheckedContinuation<Void, Error>?
    private var readContinuation: CheckedContinuation<String, Error>?
    private var writeContinuation: CheckedContinuation<Void, Error>?

    public init(
        deviceName: String = "VibeBoard",
        defaults: UserDefaults = .standard,
        cacheKey: String = "VibeBoardBLE.lastPeripheralIdentifier"
    ) {
        self.deviceName = deviceName
        self.defaults = defaults
        self.cacheKey = cacheKey
        super.init()
        central = CBCentralManager(delegate: self, queue: nil)
    }

    public func connect(scanTimeout: TimeInterval = 12, connectTimeout: TimeInterval = 12) async throws {
        if case .connected = connectionState, commandCharacteristic != nil, statusCharacteristic != nil {
            return
        }

        connectionState = .waitingForBluetooth
        do {
            try await waitUntilPoweredOn()
        } catch {
            connectionState = .failed(String(describing: error))
            throw error
        }

        if let cached = cachedPeripheral() {
            do {
                connectionState = .connectingCached
                trace("connecting cached peripheral \(cached.identifier.uuidString)")
                try await connect(to: cached, timeout: connectTimeout)
                return
            } catch {
                trace("cached connect failed: \(error)")
                defaults.removeObject(forKey: deviceScopedCacheKey)
                central.cancelPeripheralConnection(cached)
                clearRuntimeConnection()
            }
        }

        do {
            connectionState = .scanning
            trace("scanning for \(deviceName)")
            let candidate = try await scan(timeout: scanTimeout)
            connectionState = .connectingDiscovered
            trace("connecting discovered peripheral \(candidate.identifier.uuidString)")
            try await connect(to: candidate, timeout: connectTimeout)
        } catch {
            connectionState = .failed(String(describing: error))
            throw error
        }
    }

    private func connect(to candidate: CBPeripheral, timeout: TimeInterval) async throws {
        peripheral = candidate
        candidate.delegate = self

        var timeoutItem: DispatchWorkItem?
        defer { timeoutItem?.cancel() }
        try await withCheckedThrowingContinuation { continuation in
            self.connectContinuation = continuation
            timeoutItem = DispatchWorkItem { [weak self] in
                guard let self, self.connectContinuation != nil else { return }
                self.central.cancelPeripheralConnection(candidate)
                self.connectContinuation?.resume(throwing: VibeBoardBLEError.connectTimedOut)
                self.connectContinuation = nil
            }
            DispatchQueue.main.asyncAfter(deadline: .now() + timeout, execute: timeoutItem!)
            self.central.connect(candidate, options: nil)
        }

        defaults.set(candidate.identifier.uuidString, forKey: deviceScopedCacheKey)
        trace("connected peripheral \(candidate.identifier.uuidString)")
        connectionState = .discovering
        do {
            try await discoverRuntimeService()
            try? await enableStatusNotify()
            connectionState = .connected
        } catch {
            clearRuntimeConnection()
            connectionState = .failed(String(describing: error))
            throw error
        }
    }

    public func status() async throws -> String {
        try await send("status")
        let status = try await readStatus()
        trace("runtime status \(status)")
        return status
    }

    public func capabilities() async throws -> VibeBoardRuntimeCapabilities {
        let status = try await fetchRuntimeJSON(command: "capabilities", expectedAPI: "vibeboard-huangshan-capabilities/v1")
        trace("runtime capabilities \(status)")
        guard let data = status.data(using: .utf8) else {
            throw VibeBoardBLEError.commandFailed(status)
        }
        return try JSONDecoder().decode(VibeBoardRuntimeCapabilities.self, from: data)
    }

    public func sensors() async throws -> VibeBoardSensorSnapshot {
        let status = try await fetchRuntimeJSON(command: "sensors", expectedAPI: "vibeboard-huangshan-sensors/v1")
        guard let data = status.data(using: .utf8) else {
            throw VibeBoardBLEError.commandFailed(status)
        }
        let snapshot = try JSONDecoder().decode(VibeBoardSensorSnapshot.self, from: data)
        trace("runtime sensors \(snapshot.summary)")
        return snapshot
    }

    public func power() async throws -> VibeBoardPowerSnapshot {
        let status = try await fetchRuntimeJSON(command: "power", expectedAPI: "vibeboard-huangshan-power/v1")
        guard let data = status.data(using: .utf8) else {
            throw VibeBoardBLEError.commandFailed(status)
        }
        let snapshot = try JSONDecoder().decode(VibeBoardPowerSnapshot.self, from: data)
        trace("runtime power \(snapshot.summary)")
        return snapshot
    }


    public func display(brightness: Int? = nil) async throws -> VibeBoardDisplaySnapshot {
        let command = brightness.map { "display \($0)" } ?? "display"
        let status = try await fetchRuntimeJSON(command: command, expectedAPI: "vibeboard-huangshan-display/v1")
        guard let data = status.data(using: .utf8) else {
            throw VibeBoardBLEError.commandFailed(status)
        }
        let snapshot = try JSONDecoder().decode(VibeBoardDisplaySnapshot.self, from: data)
        trace("runtime display \(snapshot.summary)")
        return snapshot
    }

    public func touch() async throws -> VibeBoardTouchSnapshot {
        let status = try await fetchRuntimeJSON(command: "touch", expectedAPI: "vibeboard-huangshan-touch/v1")
        guard let data = status.data(using: .utf8) else {
            throw VibeBoardBLEError.commandFailed(status)
        }
        let snapshot = try JSONDecoder().decode(VibeBoardTouchSnapshot.self, from: data)
        trace("runtime touch \(snapshot.summary)")
        return snapshot
    }

    public func gpio() async throws -> VibeBoardGPIOSnapshot {
        let status = try await fetchRuntimeJSON(command: "gpio", expectedAPI: "vibeboard-huangshan-gpio/v1")
        guard let data = status.data(using: .utf8) else {
            throw VibeBoardBLEError.commandFailed(status)
        }
        let snapshot = try JSONDecoder().decode(VibeBoardGPIOSnapshot.self, from: data)
        trace("runtime gpio \(snapshot.summary)")
        return snapshot
    }

    public func rgb(color: String? = nil) async throws -> VibeBoardRGBSnapshot {
        let command = (color?.isEmpty == false) ? "rgb \(color!)" : "rgb"
        let status = try await fetchRuntimeJSON(command: command, expectedAPI: "vibeboard-huangshan-rgb/v1")
        guard let data = status.data(using: .utf8) else {
            throw VibeBoardBLEError.commandFailed(status)
        }
        let snapshot = try JSONDecoder().decode(VibeBoardRGBSnapshot.self, from: data)
        trace("runtime rgb \(snapshot.summary)")
        return snapshot
    }


    public func voice() async throws -> VibeBoardVoiceSnapshot {
        let status = try await fetchRuntimeJSON(command: "voice", expectedAPI: "vibeboard-huangshan-voice-bridge/v1")
        guard let data = status.data(using: .utf8) else {
            throw VibeBoardBLEError.commandFailed(status)
        }
        let snapshot = try JSONDecoder().decode(VibeBoardVoiceSnapshot.self, from: data)
        trace("runtime voice \(snapshot.summary)")
        return snapshot
    }

    public func audio() async throws -> VibeBoardAudioSnapshot {
        try await audioCommand("playback")
    }

    public func audioPlay(appId: String, path: String) async throws -> VibeBoardAudioSnapshot {
        guard RuntimePackage.isSafeAppId(appId), RuntimePackage.isSafePath(path),
              path.hasPrefix("assets/"), path.lowercased().hasSuffix(".wav") else {
            throw VibeBoardBLEError.commandFailed("unsafe Runtime audio target: \(appId)/\(path)")
        }
        return try await audioCommand("playback_play \(appId) \(path)")
    }

    public func audioStop() async throws -> VibeBoardAudioSnapshot {
        try await audioCommand("playback_stop")
    }

    public func audioVolume(_ volume: Int) async throws -> VibeBoardAudioSnapshot {
        guard (0...15).contains(volume) else {
            throw VibeBoardBLEError.commandFailed("Runtime audio volume must be 0...15")
        }
        return try await audioCommand("playback_volume \(volume)")
    }

    private func audioCommand(_ command: String) async throws -> VibeBoardAudioSnapshot {
        let status = try await fetchRuntimeJSON(
            command: command,
            expectedAPI: "vibeboard-huangshan-audio-playback/v1"
        )
        guard let data = status.data(using: .utf8) else {
            throw VibeBoardBLEError.commandFailed(status)
        }
        let snapshot = try JSONDecoder().decode(VibeBoardAudioSnapshot.self, from: data)
        trace("runtime \(snapshot.summary)")
        return snapshot
    }

    public func appStatus() async throws -> VibeBoardRuntimeAppStatus {
        let status = try await fetchRuntimeJSON(command: "app", expectedAPI: "vibeboard-huangshan-app-manager/v1")
        guard let data = status.data(using: .utf8) else {
            throw VibeBoardBLEError.commandFailed(status)
        }
        let snapshot = try JSONDecoder().decode(VibeBoardRuntimeAppStatus.self, from: data)
        trace("runtime app \(snapshot.summary)")
        return snapshot
    }

    private func appPage(offset: Int, limit: Int = VibeBoardRuntimeDefaults.bleAppPageLimit) async throws -> VibeBoardRuntimeAppList {
        let status = try await fetchRuntimeJSONInline(command: "apps_page \(offset) \(limit)", expectedAPI: "vibeboard-huangshan-app-manager/v1", timeout: 4.0)
        guard let data = status.data(using: .utf8) else {
            throw VibeBoardBLEError.commandFailed(status)
        }
        return try JSONDecoder().decode(VibeBoardRuntimeAppList.self, from: data)
    }

    public func apps() async throws -> VibeBoardRuntimeAppList {
        do {
            var pages: [VibeBoardRuntimeAppList] = []
            var offset = 0
            let limit = VibeBoardRuntimeDefaults.bleAppPageLimit
            while true {
                let page = try await appPage(offset: offset, limit: limit)
                pages.append(page)
                offset += page.included
                if page.included <= 0 || offset >= page.count { break }
            }
            let snapshot = try vibeBoardCombineAppPages(pages)
            trace("runtime apps \(snapshot.summary)")
            return snapshot
        } catch {
            let status = try await fetchRuntimeJSON(command: "apps", expectedAPI: "vibeboard-huangshan-app-manager/v1", timeout: 6.0)
            guard let data = status.data(using: .utf8) else {
                throw VibeBoardBLEError.commandFailed(status)
            }
            let snapshot = try JSONDecoder().decode(VibeBoardRuntimeAppList.self, from: data)
            trace("runtime apps \(snapshot.summary)")
            return snapshot
        }
    }

    public func launchApp(_ appId: String) async throws -> VibeBoardRuntimeAppCommandAck {
        guard RuntimePackage.isSafeAppId(appId) else {
            throw RuntimePackageError.unsafeAppId(appId)
        }
        try await send("launch \(appId)")
        let status = try await readStatusMatching {
            VibeBoardBLEStatusMatcher.appLaunchMatches($0, appId: appId)
        }
        let ack = try VibeBoardRuntimeAppCommandAck(statusText: status, command: "launch", appId: appId)
        trace("runtime launch \(ack.summary)")
        if !ack.isOK {
            throw VibeBoardBLEError.commandFailed(status)
        }
        return ack
    }

    public func stopApp() async throws -> VibeBoardRuntimeAppCommandAck {
        try await send("stop")
        let status = try await readStatusMatching { VibeBoardBLEStatusMatcher.appStopMatches($0) }
        let ack = try VibeBoardRuntimeAppCommandAck(statusText: status, command: "stop")
        trace("runtime stop \(ack.summary)")
        if !ack.isOK {
            throw VibeBoardBLEError.commandFailed(status)
        }
        return ack
    }

    public func deleteApp(_ appId: String) async throws -> VibeBoardRuntimeAppCommandAck {
        guard RuntimePackage.isSafeAppId(appId) else {
            throw RuntimePackageError.unsafeAppId(appId)
        }
        try await send("delete \(appId)")
        let status = try await readStatusMatching {
            VibeBoardBLEStatusMatcher.appDeleteMatches($0, appId: appId)
        }
        let ack = try VibeBoardRuntimeAppCommandAck(statusText: status, command: "delete", appId: appId)
        trace("runtime delete \(ack.summary)")
        if !ack.isOK {
            throw VibeBoardBLEError.commandFailed(status)
        }
        return ack
    }

    public func abortInstall(_ appId: String) async throws -> String {
        guard RuntimePackage.isSafeAppId(appId) else {
            throw RuntimePackageError.unsafeAppId(appId)
        }
        let command = "vb_runtime_install_abort \(appId)"
        try await send(command)
        let status = try await readStatusMatching {
            VibeBoardBLEStatusMatcher.installAckMatches($0, command: command)
        }
        trace("runtime install abort app=\(appId) status=\(status)")
        if status.hasPrefix("err ") || status.contains(" rc=-") {
            throw VibeBoardBLEError.commandFailed(status)
        }
        return status
    }

    public func sendInfoFlow(channel: String = "phone", sequence: UInt32, payload: String) async throws -> String {
        let frame = try VibeBoardInfoFlowFrame(channel: channel, sequence: sequence, payload: payload)
        try await send(frame.command)
        let status = try await readStatusMatching {
            VibeBoardBLEStatusMatcher.infoFlowAckMatches($0, channel: frame.channel, sequence: frame.sequence)
        }
        trace("info flow ack \(status)")
        if status.hasPrefix("err ") {
            throw VibeBoardBLEError.commandFailed(status)
        }
        let ackLine = vibeBoardLatestStatusLine(status) { value in
            value.hasPrefix("ok flow_send ") || value.hasPrefix("err flow_send ") || value.hasPrefix("recv ")
        }
        if ackLine.hasPrefix("err ") {
            throw VibeBoardBLEError.commandFailed(ackLine)
        }
        let values = vibeBoardParseKeyValues(ackLine)
        let bytes = try vibeBoardUInt32(values, "bytes", raw: ackLine)
        guard bytes == UInt32(Data(frame.payload.utf8).count) else {
            throw VibeBoardBLEError.commandFailed(status)
        }
        return status
    }

    public func infoFlowStatus() async throws -> String {
        try await send("flow_status")
        let status = try await readStatusMatching { VibeBoardBLEStatusMatcher.infoFlowStatusMatches($0) }
        trace("info flow status \(status)")
        return status
    }

    public func clearInfoFlow() async throws -> String {
        try await send("flow_clear")
        let status = try await readStatusMatching { VibeBoardBLEStatusMatcher.infoFlowClearMatches($0) }
        trace("info flow clear \(status)")
        if vibeBoardStatusLineIsError(status, matching: { value in
            value.hasPrefix("ok flow_clear") || value.hasPrefix("err flow_clear") || value == "cleared"
        }) {
            throw VibeBoardBLEError.commandFailed(status)
        }
        return status
    }

    public func voiceStatus(expectedSequence: UInt32? = nil) async throws -> VibeBoardVoiceStatus {
        let snapshot = try await fetchVoiceStatus(expectedSequence: expectedSequence, traceResult: true)
        return snapshot
    }

    public func voiceStart(durationMs: UInt32 = 1500) async throws -> VibeBoardVoiceStartAck {
        let current = try await fetchVoiceStatus(expectedSequence: nil, traceResult: false)
        if current.built == 0 {
            throw VibeBoardBLEError.commandFailed(current.rawStatus)
        }
        let expectedSequence = current.sequence &+ 1
        try await send("voice_start \(durationMs)")
        let status = try await readStatusMatching {
            VibeBoardBLEStatusMatcher.voiceStartMatches($0, expectedSequence: expectedSequence, durationMs: durationMs)
        }
        let ack = try VibeBoardVoiceStartAck(statusText: status)
        trace("voice start \(status)")
        if vibeBoardStatusLineIsError(status, matching: { value in
            value.hasPrefix("ok voice_start ") || value.hasPrefix("err voice_start ") || value.hasPrefix("ok voice api=")
        }) || ack.resultCode != 0 {
            throw VibeBoardBLEError.commandFailed(status)
        }
        return ack
    }

    public func voiceRead(offset: UInt32, maxBytes: UInt32 = VibeBoardRuntimeDefaults.voiceChunkBytes, expectedSequence: UInt32) async throws -> VibeBoardVoiceChunk {
        let boundedBytes = min(max(maxBytes, 1), VibeBoardRuntimeDefaults.voiceChunkBytes)
        try await send("voice_read \(offset) \(boundedBytes)")
        let status = try await readStatusMatching {
            VibeBoardBLEStatusMatcher.voiceReadMatches($0, expectedSequence: expectedSequence, offset: offset)
        }
        if vibeBoardStatusLineIsError(status, matching: { value in
            value.hasPrefix("ok voice_data ") || value.hasPrefix("err voice_read ")
        }) {
            throw VibeBoardBLEError.commandFailed(status)
        }
        return try VibeBoardVoiceChunk(statusText: status)
    }

    public func voiceStop() async throws -> String {
        try await send("voice_stop")
        let status = try await readStatusMatching { VibeBoardBLEStatusMatcher.voiceStopMatches($0) }
        trace("voice stop \(status)")
        if vibeBoardStatusLineIsError(status, matching: { value in
            value.hasPrefix("ok voice_stop ") || value.hasPrefix("err voice_stop ")
        }) {
            throw VibeBoardBLEError.commandFailed(status)
        }
        return status
    }

    public func voiceClear() async throws -> String {
        try await send("voice_clear")
        let status = try await readStatusMatching { VibeBoardBLEStatusMatcher.voiceClearMatches($0) }
        trace("voice clear \(status)")
        if vibeBoardStatusLineIsError(status, matching: { value in
            value.hasPrefix("ok voice_clear") || value.hasPrefix("err voice_clear") || value == "cleared"
        }) {
            throw VibeBoardBLEError.commandFailed(status)
        }
        return status
    }

    public func captureVoice(
        durationMs: UInt32 = 1500,
        chunkBytes: UInt32 = VibeBoardRuntimeDefaults.voiceChunkBytes,
        pollInterval: TimeInterval = 0.25,
        readyTimeout: TimeInterval = 8.0
    ) async throws -> (status: VibeBoardVoiceStatus, pcm: Data) {
        let ack = try await voiceStart(durationMs: durationMs)
        let deadline = Date().timeIntervalSinceReferenceDate + readyTimeout
        var lastStatus = try await fetchVoiceStatus(expectedSequence: ack.sequence, traceResult: false)
        while true {
            if lastStatus.built == 0 {
                throw VibeBoardBLEError.commandFailed(lastStatus.rawStatus)
            }
            if lastStatus.recording == 0 && lastStatus.ready != 0 {
                break
            }
            if lastStatus.recording == 0 && lastStatus.error != 0 && lastStatus.error != 1 {
                throw VibeBoardBLEError.commandFailed(lastStatus.rawStatus)
            }
            if Date().timeIntervalSinceReferenceDate >= deadline {
                throw VibeBoardBLEError.commandFailed(lastStatus.rawStatus)
            }
            try await Task.sleep(nanoseconds: Self.nanoseconds(from: pollInterval))
            lastStatus = try await fetchVoiceStatus(expectedSequence: ack.sequence, traceResult: false)
        }
        trace("voice capture ready seq=\(lastStatus.sequence) bytes=\(lastStatus.bytes)")
        if lastStatus.bytes == 0 {
            throw VibeBoardBLEError.commandFailed(lastStatus.rawStatus)
        }

        let boundedChunkBytes = min(max(chunkBytes, 16), VibeBoardRuntimeDefaults.voiceChunkBytes)
        var pcm = Data()
        pcm.reserveCapacity(Int(lastStatus.bytes))
        var offset: UInt32 = 0
        while offset < lastStatus.bytes {
            let chunk = try await voiceRead(
                offset: offset,
                maxBytes: min(boundedChunkBytes, lastStatus.bytes - offset),
                expectedSequence: ack.sequence
            )
            guard chunk.bytes > 0 else {
                throw VibeBoardBLEError.commandFailed("voice_read returned empty chunk before complete offset=\(offset) total=\(lastStatus.bytes)")
            }
            pcm.append(chunk.payload)
            offset &+= chunk.bytes
        }
        guard pcm.count == Int(lastStatus.bytes) else {
            throw VibeBoardBLEError.commandFailed("voice capture length mismatch expected=\(lastStatus.bytes) got=\(pcm.count)")
        }
        trace("voice capture pulled \(pcm.count) bytes seq=\(ack.sequence)")
        return (lastStatus, pcm)
    }

    public func sendVoiceReply(sequence: UInt32, payload: String, channel: String = "pc.voice") async throws -> String {
        try await sendInfoFlow(channel: channel, sequence: sequence, payload: payload)
    }

    private func fetchRuntimeJSONInline(command: String, expectedAPI: String, timeout: TimeInterval = 4.0) async throws -> String {
        try await send(command)
        let status = try await readStatusMatching(timeout: timeout) { status in
            vibeBoardExtractJSONLine(status, expectedAPI: expectedAPI) != nil
                || vibeBoardContainsTruncatedJSONLine(status, expectedAPI: expectedAPI)
                || status.hasPrefix("err ")
        }
        if status.hasPrefix("err ") {
            throw VibeBoardBLEError.commandFailed(status)
        }
        if let line = vibeBoardExtractJSONLine(status, expectedAPI: expectedAPI) {
            return line
        }
        throw VibeBoardBLEError.commandFailed("Runtime JSON response for \(command) was truncated and has no safe json_read fallback")
    }

    private func fetchRuntimeJSON(command: String, expectedAPI: String, timeout: TimeInterval = 4.0) async throws -> String {
        try await send(command)
        let initial = try await readStatusMatching(timeout: min(timeout, 1.0)) { status in
            vibeBoardExtractJSONLine(status, expectedAPI: expectedAPI) != nil
                || vibeBoardContainsTruncatedJSONLine(status, expectedAPI: expectedAPI)
                || status.hasPrefix("err ")
        }
        if initial.hasPrefix("err ") {
            throw VibeBoardBLEError.commandFailed(initial)
        }
        if let line = vibeBoardExtractJSONLine(initial, expectedAPI: expectedAPI) {
            return line
        }
        let kind = command.split(separator: " ", maxSplits: 1, omittingEmptySubsequences: true).first.map(String.init) ?? command
        return try await fetchRuntimeJSONChunks(kind: kind, expectedAPI: expectedAPI, timeout: max(timeout, 4.0))
    }

    private func fetchRuntimeJSONChunks(kind: String, expectedAPI: String, timeout: TimeInterval) async throws -> String {
        let deadline = Date().timeIntervalSinceReferenceDate + timeout
        var offset = 0
        var total: Int?
        var buffer = Data()
        while true {
            try await send("json_read \(kind) \(offset) \(VibeBoardRuntimeDefaults.dataChunkBytes)")
            let status = try await readStatusMatching(timeout: min(1.0, timeout)) {
                $0.hasPrefix("ok json_read ") || $0.hasPrefix("err ")
            }
            if status.hasPrefix("err ") {
                throw VibeBoardBLEError.commandFailed(status)
            }
            guard let chunk = vibeBoardExtractJSONReadChunk(status, kind: kind) else {
                if Date().timeIntervalSinceReferenceDate >= deadline {
                    throw VibeBoardBLEError.commandFailed(status)
                }
                try await Task.sleep(nanoseconds: Self.nanoseconds(from: 0.12))
                continue
            }
            if chunk.offset != offset {
                throw VibeBoardBLEError.commandFailed("json_read offset mismatch for \(kind): expected \(offset) got \(chunk.offset)")
            }
            if total == nil {
                total = chunk.total
                buffer = Data(count: chunk.total)
            } else if total != chunk.total {
                throw VibeBoardBLEError.commandFailed("json_read total changed for \(kind)")
            }
            let end = chunk.offset + chunk.payload.count
            guard end <= buffer.count else {
                throw VibeBoardBLEError.commandFailed("json_read overflow for \(kind): \(end)>\(buffer.count)")
            }
            if chunk.payload.isEmpty && chunk.total > chunk.offset {
                throw VibeBoardBLEError.commandFailed("json_read returned empty chunk for \(kind) at offset \(chunk.offset) before total \(chunk.total)")
            }
            buffer.replaceSubrange(chunk.offset..<end, with: chunk.payload)
            offset = end
            if let total, offset >= total {
                let text = String(decoding: buffer, as: UTF8.self)
                if let line = vibeBoardExtractJSONLine(text, expectedAPI: expectedAPI) {
                    return line
                }
                throw VibeBoardBLEError.commandFailed(text)
            }
            if Date().timeIntervalSinceReferenceDate >= deadline {
                throw VibeBoardBLEError.commandFailed(String(decoding: buffer, as: UTF8.self))
            }
        }
    }

    private func fetchVoiceStatus(expectedSequence: UInt32?, traceResult: Bool) async throws -> VibeBoardVoiceStatus {
        try await send("voice_status")
        let status = try await readStatusMatching {
            VibeBoardBLEStatusMatcher.voiceStatusMatches($0, expectedSequence: expectedSequence)
        }
        let snapshot = try VibeBoardVoiceStatus(statusText: status)
        if traceResult {
            trace("voice status \(snapshot.summary)")
        }
        return snapshot
    }

    public func install(_ package: RuntimePackage, chunkBytes: Int = VibeBoardRuntimeDefaults.installChunkBytes) async throws {
        trace("install begin app=\(package.appId) files=\(package.files.count)")
        var lastStatus = ""
        var installStarted = false
        do {
            for command in package.installCommands(chunkBytes: chunkBytes) {
                if command.hasPrefix("vb_runtime_install_begin ") {
                    installStarted = true
                }
                try await send(command)
                let status = try await readStatusMatching {
                    VibeBoardBLEStatusMatcher.installAckMatches($0, command: command)
                }
                trace("install ack \(status)")
                if status.hasPrefix("err ") || status.contains(" rc=-") {
                    throw VibeBoardBLEError.commandFailed(status)
                }
                lastStatus = status
            }
            let finalStatus: String
            if VibeBoardBLEStatusMatcher.installConfirmed(lastStatus, appId: package.appId) {
                finalStatus = lastStatus
            } else {
                try await send("status")
                finalStatus = try await readStatusMatching {
                    VibeBoardBLEStatusMatcher.installConfirmed($0, appId: package.appId)
                }
            }
            guard VibeBoardBLEStatusMatcher.installConfirmed(finalStatus, appId: package.appId) else {
                throw VibeBoardBLEError.commandFailed(finalStatus)
            }
            trace("install complete app=\(package.appId) status=\(finalStatus)")
        } catch {
            if installStarted {
                do {
                    _ = try await abortInstall(package.appId)
                } catch {
                    trace("warning: install abort failed app=\(package.appId): \(error)")
                }
            }
            throw error
        }
    }

    public func disconnect() {
        if let peripheral {
            central.cancelPeripheralConnection(peripheral)
        }
        clearRuntimeConnection()
        connectionState = .disconnected
    }

    public func isReadyForRuntimeCommands() -> Bool {
        if case .connected = connectionState, commandCharacteristic != nil, statusCharacteristic != nil {
            return true
        }
        return false
    }

    private func waitUntilPoweredOn() async throws {
        if central.state == .poweredOn { return }
        try await withCheckedThrowingContinuation { continuation in
            stateContinuation = continuation
        }
    }

    private func cachedPeripheral() -> CBPeripheral? {
        guard let value = defaults.string(forKey: deviceScopedCacheKey),
              let uuid = UUID(uuidString: value)
        else {
            return nil
        }
        let retrieved = central.retrievePeripherals(withIdentifiers: [uuid])
        return retrieved.first
    }

    private func scan(timeout: TimeInterval) async throws -> CBPeripheral {
        var timeoutItem: DispatchWorkItem?
        defer { timeoutItem?.cancel() }
        return try await withCheckedThrowingContinuation { continuation in
            self.scanContinuation = continuation
            timeoutItem = DispatchWorkItem { [weak self] in
                guard let self, self.scanContinuation != nil else { return }
                self.central.stopScan()
                self.scanContinuation?.resume(throwing: VibeBoardBLEError.scanTimedOut)
                self.scanContinuation = nil
            }
            DispatchQueue.main.asyncAfter(deadline: .now() + timeout, execute: timeoutItem!)
            self.central.scanForPeripherals(
                withServices: [VibeBoardBLEUUIDs.service],
                options: [CBCentralManagerScanOptionAllowDuplicatesKey: false]
            )
        }
    }

    private func discoverRuntimeService() async throws {
        guard let peripheral else { throw VibeBoardBLEError.serviceMissing }
        try await withCheckedThrowingContinuation { continuation in
            discoveryContinuation = continuation
            peripheral.discoverServices([VibeBoardBLEUUIDs.service])
        }
    }

    private func enableStatusNotify() async throws {
        guard let peripheral, let statusCharacteristic else {
            throw VibeBoardBLEError.characteristicMissing("status")
        }
        peripheral.setNotifyValue(true, for: statusCharacteristic)
    }

    private func send(_ command: String) async throws {
        guard let peripheral, let commandCharacteristic else {
            throw VibeBoardBLEError.characteristicMissing("command")
        }
        guard let data = "\(command)\n".data(using: .utf8) else {
            throw VibeBoardBLEError.commandFailed(command)
        }
        try await withCheckedThrowingContinuation { continuation in
            writeContinuation = continuation
            peripheral.writeValue(data, for: commandCharacteristic, type: .withResponse)
        }
    }

    private func readStatus() async throws -> String {
        guard let peripheral, let statusCharacteristic else {
            throw VibeBoardBLEError.characteristicMissing("status")
        }
        return try await withCheckedThrowingContinuation { continuation in
            readContinuation = continuation
            peripheral.readValue(for: statusCharacteristic)
        }
    }

    private func readStatusMatching(
        timeout: TimeInterval = 4.0,
        pollInterval: TimeInterval = 0.12,
        predicate: (String) -> Bool
    ) async throws -> String {
        let deadline = Date().timeIntervalSinceReferenceDate + timeout
        var lastStatus = ""
        while true {
            let status = try await readStatus()
            lastStatus = vibeBoardMergeStatus(previous: lastStatus, current: status)
            if predicate(lastStatus) {
                return lastStatus
            }
            if Date().timeIntervalSinceReferenceDate >= deadline {
                throw VibeBoardBLEError.commandFailed(lastStatus)
            }
            try await Task.sleep(nanoseconds: Self.nanoseconds(from: pollInterval))
        }
    }

    private static func nanoseconds(from seconds: TimeInterval) -> UInt64 {
        let bounded = max(0, seconds)
        return UInt64((bounded * 1_000_000_000).rounded())
    }

    private func completeDiscoveryIfReady() {
        guard let peripheral, let services = peripheral.services else { return }
        if let service = services.first(where: { $0.uuid == VibeBoardBLEUUIDs.service }) {
            let characteristics = service.characteristics ?? []
            commandCharacteristic = characteristics.first { $0.uuid == VibeBoardBLEUUIDs.command }
            statusCharacteristic = characteristics.first { $0.uuid == VibeBoardBLEUUIDs.status }
        }
        guard commandCharacteristic != nil, statusCharacteristic != nil else { return }
        trace("runtime service discovered")
        discoveryContinuation?.resume()
        discoveryContinuation = nil
    }

    private func clearRuntimeConnection() {
        peripheral = nil
        commandCharacteristic = nil
        statusCharacteristic = nil
    }

    private func failPendingOperations(_ error: Error) {
        connectContinuation?.resume(throwing: error)
        connectContinuation = nil
        discoveryContinuation?.resume(throwing: error)
        discoveryContinuation = nil
        readContinuation?.resume(throwing: error)
        readContinuation = nil
        writeContinuation?.resume(throwing: error)
        writeContinuation = nil
    }
}

extension VibeBoardBLEClient: VibeBoardRuntimeTransport {}


extension VibeBoardBLEClient: @preconcurrency CBCentralManagerDelegate {
    public func centralManagerDidUpdateState(_ central: CBCentralManager) {
        if central.state == .poweredOn {
            stateContinuation?.resume()
        } else if central.state != .unknown && central.state != .resetting {
            stateContinuation?.resume(throwing: VibeBoardBLEError.bluetoothUnavailable(central.state))
        }
        stateContinuation = nil
    }

    public func centralManager(
        _ central: CBCentralManager,
        didDiscover peripheral: CBPeripheral,
        advertisementData: [String: Any],
        rssi RSSI: NSNumber
    ) {
        let localName = advertisementData[CBAdvertisementDataLocalNameKey] as? String
        guard peripheral.name == deviceName || localName == deviceName else { return }
        trace("discovered \(deviceName) id=\(peripheral.identifier.uuidString) rssi=\(RSSI.intValue)")
        central.stopScan()
        scanContinuation?.resume(returning: peripheral)
        scanContinuation = nil
    }

    public func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        trace("didConnect \(peripheral.identifier.uuidString)")
        connectContinuation?.resume()
        connectContinuation = nil
    }

    public func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
        let failure = error ?? VibeBoardBLEError.connectTimedOut
        trace("didFailToConnect \(peripheral.identifier.uuidString): \(failure)")
        connectionState = .failed(String(describing: failure))
        connectContinuation?.resume(throwing: failure)
        connectContinuation = nil
    }

    public func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        clearRuntimeConnection()
        if let error {
            trace("didDisconnect \(peripheral.identifier.uuidString): \(error)")
            connectionState = .failed(String(describing: error))
            failPendingOperations(error)
        } else {
            trace("didDisconnect \(peripheral.identifier.uuidString)")
            connectionState = .disconnected
            failPendingOperations(VibeBoardBLEError.connectTimedOut)
        }
    }
}

extension VibeBoardBLEClient: @preconcurrency CBPeripheralDelegate {
    public func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        if let error {
            discoveryContinuation?.resume(throwing: error)
            discoveryContinuation = nil
            return
        }
        guard let service = peripheral.services?.first(where: { $0.uuid == VibeBoardBLEUUIDs.service }) else {
            discoveryContinuation?.resume(throwing: VibeBoardBLEError.serviceMissing)
            discoveryContinuation = nil
            return
        }
        peripheral.discoverCharacteristics([VibeBoardBLEUUIDs.command, VibeBoardBLEUUIDs.status], for: service)
    }

    public func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        if let error {
            discoveryContinuation?.resume(throwing: error)
            discoveryContinuation = nil
            return
        }
        completeDiscoveryIfReady()
    }

    public func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        if let error {
            readContinuation?.resume(throwing: error)
            readContinuation = nil
            return
        }
        let text = String(data: characteristic.value ?? Data(), encoding: .utf8)?
            .replacingOccurrences(of: "\0", with: "")
            .trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
        latestStatus = text
        readContinuation?.resume(returning: text)
        readContinuation = nil
    }

    public func peripheral(
        _ peripheral: CBPeripheral,
        didWriteValueFor characteristic: CBCharacteristic,
        error: Error?
    ) {
        if let error {
            writeContinuation?.resume(throwing: error)
        } else {
            writeContinuation?.resume()
        }
        writeContinuation = nil
    }
}
