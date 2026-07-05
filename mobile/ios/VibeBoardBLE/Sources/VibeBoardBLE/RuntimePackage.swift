import Foundation

public enum RuntimePackageError: Error, Equatable {
    case unsafeAppId(String)
    case unsafePath(String)
    case missingMainLua
    case missingManifest
    case invalidManifest(String)
    case emptyDirectory(URL)
}

extension RuntimePackageError: LocalizedError {
    public var errorDescription: String? {
        switch self {
        case .unsafeAppId(let value):
            return "Runtime app id is unsafe: \(value)"
        case .unsafePath(let value):
            return "Runtime package path is unsafe: \(value)"
        case .missingMainLua:
            return "Runtime package must include main.lua"
        case .missingManifest:
            return "Runtime package must include manifest.json or app.info"
        case .invalidManifest(let message):
            return message
        case .emptyDirectory(let url):
            return "Runtime package folder is empty or unreadable: \(url.lastPathComponent)"
        }
    }
}

public struct RuntimePackage: Sendable {
    public let appId: String
    public let files: [String: Data]

    public init(appId: String, files: [String: Data]) throws {
        guard Self.isSafeAppId(appId) else {
            throw RuntimePackageError.unsafeAppId(appId)
        }

        var normalizedFiles: [String: Data] = [:]
        for (path, data) in files {
            let normalizedPath = Self.normalizedPath(path)
            guard Self.isSafePath(normalizedPath) else {
                throw RuntimePackageError.unsafePath(normalizedPath)
            }
            guard normalizedFiles[normalizedPath] == nil else {
                throw RuntimePackageError.unsafePath("duplicate package path after normalization: \(normalizedPath)")
            }
            normalizedFiles[normalizedPath] = data
        }

        guard let mainLua = normalizedFiles["main.lua"] else {
            throw RuntimePackageError.missingMainLua
        }
        try Self.validateLuaSubset(mainLua)
        guard normalizedFiles["manifest.json"] != nil || normalizedFiles["app.info"] != nil else {
            throw RuntimePackageError.missingManifest
        }
        if let manifestData = normalizedFiles["manifest.json"] {
            try Self.validateManifest(manifestData, appId: appId)
        }
        self.appId = appId
        self.files = normalizedFiles
    }

    public static func fromDirectory(_ directory: URL, appId overrideAppId: String? = nil) throws -> RuntimePackage {
        let root = directory.standardizedFileURL
        guard root.hasDirectoryPath else {
            throw RuntimePackageError.emptyDirectory(directory)
        }

        let keys: Set<URLResourceKey> = [.isRegularFileKey, .isDirectoryKey]
        guard let enumerator = FileManager.default.enumerator(
            at: root,
            includingPropertiesForKeys: Array(keys),
            options: [.skipsHiddenFiles, .skipsPackageDescendants]
        ) else {
            throw RuntimePackageError.emptyDirectory(directory)
        }

        let rootPath = root.path.hasSuffix("/") ? root.path : root.path + "/"
        var files: [String: Data] = [:]
        for case let fileURL as URL in enumerator {
            let values = try fileURL.resourceValues(forKeys: keys)
            if values.isDirectory == true {
                continue
            }
            guard values.isRegularFile == true else {
                continue
            }
            let filePath = fileURL.standardizedFileURL.path
            let relativePath = filePath.hasPrefix(rootPath) ? String(filePath.dropFirst(rootPath.count)) : fileURL.lastPathComponent
            let normalizedPath = relativePath.replacingOccurrences(of: "\\", with: "/")
            guard isSafePath(normalizedPath) else {
                throw RuntimePackageError.unsafePath(normalizedPath)
            }
            guard files[normalizedPath] == nil else {
                throw RuntimePackageError.unsafePath("duplicate package path after normalization: \(normalizedPath)")
            }
            files[normalizedPath] = try Data(contentsOf: fileURL)
        }

        guard !files.isEmpty else {
            throw RuntimePackageError.emptyDirectory(directory)
        }

        let manifestAppId = manifestId(from: files["manifest.json"])
        let packageId = overrideAppId ?? manifestAppId ?? root.lastPathComponent
        return try RuntimePackage(appId: packageId, files: files)
    }

    public func installCommands(chunkBytes: Int = VibeBoardRuntimeDefaults.installChunkBytes) -> [String] {
        let chunkSize = min(max(chunkBytes, 16), VibeBoardRuntimeDefaults.maxInstallChunkBytes)
        var commands = ["vb_runtime_install_begin \(appId)"]
        for path in files.keys.sorted() {
            let data = files[path] ?? Data()
            if data.isEmpty {
                commands.append("vb_runtime_install_file \(appId) \(path) 0 -")
                continue
            }
            var offset = 0
            while offset < data.count {
                let end = min(offset + chunkSize, data.count)
                let chunk = data[offset..<end]
                commands.append("vb_runtime_install_file \(appId) \(path) \(offset) \(chunk.hexString)")
                offset = end
            }
        }
        commands.append("vb_runtime_install_end \(appId)")
        return commands
    }

    public static func isSafeAppId(_ value: String) -> Bool {
        value.range(of: #"^[a-z][a-z0-9_]{0,14}$"#, options: .regularExpression) != nil
    }

    public static func isSafePath(_ rawValue: String) -> Bool {
        let value = normalizedPath(rawValue)
        if value.hasPrefix("/") || value.contains("..") || value.contains("//") {
            return false
        }
        let pattern = #"^(manifest\.json|app\.info|main\.lua|files\.txt|README\.md|(?:assets|images|fonts|lib)/[A-Za-z0-9_./-]+\.(?:json|txt|png|jpg|jpeg|bin|ttf|otf|lua))$"#
        return value.range(of: pattern, options: .regularExpression) != nil
    }

    private static func normalizedPath(_ rawValue: String) -> String {
        rawValue.replacingOccurrences(of: "\\", with: "/")
    }

    private static let manifestCapabilities: Set<String> = [
        "status",
        "clock",
        "reload",
        "vibeboard.launcher.reload",
        "game",
        "weather.current",
        "display.brightness",
        "display.size",
        "display.resolution",
        "display.state",
        "display.bpp",
        "screen.brightness",
        "screen.size",
        "vibeboard.display.brightness",
        "vibeboard.display.size",
        "vibeboard.display.state",
        "vibeboard.display.bpp",
        "battery",
        "charger",
        "power.battery",
        "power.charger",
        "power.charger.status",
        "power.charger.state",
        "power.charger.det",
        "power.charger.en",
        "power.charger.fault",
        "vibeboard.power.battery",
        "vibeboard.power.charger",
        "vibeboard.power.charger.status",
        "vibeboard.power.charger.state",
        "vibeboard.power.charger.det",
        "vibeboard.power.charger.en",
        "vibeboard.power.charger.fault",
        "flow.latest",
        "flow.summary",
        "flow.payload",
        "flow.text",
        "flow.channel",
        "flow.seq",
        "flow.sequence",
        "flow.bytes",
        "flow.total",
        "flow.retained",
        "flow.count",
        "flow.capacity",
        "vibeboard.flow.latest",
        "vibeboard.flow.summary",
        "vibeboard.flow.payload",
        "vibeboard.flow.text",
        "vibeboard.flow.channel",
        "vibeboard.flow.seq",
        "vibeboard.flow.sequence",
        "vibeboard.flow.bytes",
        "vibeboard.flow.total",
        "vibeboard.flow.retained",
        "vibeboard.flow.count",
        "vibeboard.flow.capacity",
        "voice.ready",
        "voice.recording",
        "voice.state",
        "voice.seq",
        "voice.bytes",
        "voice.duration",
        "voice.dropped",
        "voice.error",
        "voice.rate",
        "voice.built",
        "voice.available",
        "voice.start",
        "voice.record",
        "voice.clear",
        "vibeboard.voice.ready",
        "vibeboard.voice.recording",
        "vibeboard.voice.state",
        "vibeboard.voice.seq",
        "vibeboard.voice.bytes",
        "vibeboard.voice.duration",
        "vibeboard.voice.dropped",
        "vibeboard.voice.error",
        "vibeboard.voice.rate",
        "vibeboard.voice.built",
        "vibeboard.voice.available",
        "vibeboard.voice.start",
        "vibeboard.voice.record",
        "vibeboard.voice.clear",
        "sensor.light",
        "sensor.mag",
        "sensor.acce",
        "sensor.accel",
        "sensor.gyro",
        "sensor.step",
        "vibeboard.sensor.light",
        "vibeboard.sensor.mag",
        "vibeboard.sensor.acce",
        "vibeboard.sensor.gyro",
        "vibeboard.sensor.step",
        "touch.last",
        "touch.count",
        "touch.event",
        "touch.gesture",
        "touch.delta",
        "touch.duration",
        "touch.active",
        "vibeboard.touch.last",
        "vibeboard.touch.count",
        "vibeboard.touch.event",
        "vibeboard.touch.gesture",
        "vibeboard.touch.delta",
        "vibeboard.touch.duration",
        "vibeboard.touch.active",
        "gpio.key1",
        "gpio.key1.level",
        "gpio.key2",
        "gpio.key2.level",
        "vibeboard.gpio.key1",
        "vibeboard.gpio.key1.level",
        "vibeboard.gpio.key2",
        "vibeboard.gpio.key2.level",
    ]

    private static let manifestDeclaredCapabilities = manifestCapabilities.union([
        "assets",
        "ble",
        "bridge",
        "display",
        "flow",
        "fs",
        "gpio",
        "huangshan",
        "launcher",
        "lua-subset",
        "manifest",
        "power",
        "rgb",
        "screen",
        "sensor",
        "sensors",
        "serial",
        "storage",
        "touch",
        "voice",
        "weather",
    ])

    private static let huangshanProfileAliases: Set<String> = [
        "huangshan",
        "huangshan-pi",
        "vibeboard-huangshan",
        "sf32",
        "sf32lb52",
        "sf32lb525",
        "sf32lb525uc6",
    ]

    private static let manifestProfileFields = ["runtimeProfile", "runtime_profile", "targetProfile", "target"]
    private static let manifestCapabilityListFields = ["capabilities", "requires", "permissions"]

    private static let esp32NativeCapabilityNames: Set<String> = [
        "audio",
        "board_ip",
        "bluetooth.pan",
        "camera",
        "gamepad",
        "http",
        "i2s",
        "native",
        "network",
        "ntp",
        "pan",
        "wifi",
    ]

    private static let manifestComponentTypes: Set<String> = ["status", "clock", "action", "label"]

    private static let supportedLuaCalls: Set<String> = [
        "lv_scr_act",
        "lv_obj_clean",
        "lv_obj_create",
        "lv_label_create",
        "lv_btn_create",
        "lv_img_create",
        "lv_obj_set_size",
        "lv_obj_set_width",
        "lv_obj_set_height",
        "lv_obj_set_pos",
        "lv_obj_align",
        "lv_obj_center",
        "lv_obj_set_style_bg_color",
        "lv_obj_set_style_text_color",
        "lv_obj_set_style_radius",
        "lv_obj_set_style_border_width",
        "lv_obj_set_style_border_color",
        "lv_obj_clear_flag",
        "lv_label_set_text",
        "lv_label_set_long_mode",
        "lv_img_set_src",
        "print",
        "vibe_label",
        "vibe_button",
        "vibe_image",
        "vibe_read_file",
        "vibe_timer_label",
        "vibe_sensor_label",
        "vibe_touch_label",
        "vibe_gpio_label",
        "vibe_power_label",
        "vibe_display_label",
        "vibe_display_brightness",
        "vibe_voice_start",
        "vibe_voice_clear",
        "vibe_voice_label",
        "vibe_flow_label",
        "vibe_rgb",
        "vibe_snake_autoplay",
        "vibe_weather_pet",
    ]

    private static let unsupportedLuaStatements: Set<String> = [
        "function", "for", "while", "repeat", "if", "elseif", "else", "end", "return",
        "require", "dofile", "load", "loadfile", "pcall", "xpcall"
    ]

    private static func validateLuaSubset(_ data: Data) throws {
        guard let script = String(data: data, encoding: .utf8) else {
            throw RuntimePackageError.invalidManifest("main.lua must be UTF-8")
        }
        guard !script.contains("\0") else {
            throw RuntimePackageError.invalidManifest("main.lua must not contain NUL bytes")
        }
        for (offset, rawLine) in script.split(separator: "\n", omittingEmptySubsequences: false).enumerated() {
            let line = stripLuaComment(String(rawLine)).trimmingCharacters(in: .whitespacesAndNewlines)
            guard !line.isEmpty else { continue }
            let first = line.split(whereSeparator: { $0 == " " || $0 == "\t" }).first.map(String.init) ?? ""
            if unsupportedLuaStatements.contains(first) {
                throw RuntimePackageError.invalidManifest("main.lua line \(offset + 1) uses unsupported Lua statement \(first)")
            }
            guard let callName = luaCallName(line) else {
                throw RuntimePackageError.invalidManifest("main.lua line \(offset + 1) is outside Runtime script subset")
            }
            guard supportedLuaCalls.contains(callName) else {
                throw RuntimePackageError.invalidManifest("main.lua line \(offset + 1) calls unsupported Runtime Lua helper \(callName)")
            }
        }
    }

    private static func stripLuaComment(_ line: String) -> String {
        var result = ""
        var inString: Character?
        var escaped = false
        var index = line.startIndex
        while index < line.endIndex {
            let ch = line[index]
            if let quote = inString {
                result.append(ch)
                if escaped {
                    escaped = false
                } else if ch == "\\" {
                    escaped = true
                } else if ch == quote {
                    inString = nil
                }
            } else if ch == "'" || ch == "\"" {
                inString = ch
                result.append(ch)
            } else if ch == "-" {
                let next = line.index(after: index)
                if next < line.endIndex, line[next] == "-" {
                    return result
                }
                result.append(ch)
            } else {
                result.append(ch)
            }
            index = line.index(after: index)
        }
        return result
    }

    private static func luaCallName(_ statement: String) -> String? {
        let value: String
        if statement.hasPrefix("local ") {
            let rest = String(statement.dropFirst("local ".count)).trimmingCharacters(in: .whitespacesAndNewlines)
            guard let equal = rest.firstIndex(of: "=") else { return nil }
            let lhs = String(rest[..<equal]).trimmingCharacters(in: .whitespacesAndNewlines)
            guard lhs.range(of: #"^[A-Za-z_][A-Za-z0-9_]*$"#, options: .regularExpression) != nil else {
                return nil
            }
            value = String(rest[rest.index(after: equal)...]).trimmingCharacters(in: .whitespacesAndNewlines)
        } else {
            value = statement
        }
        guard let paren = value.firstIndex(of: "(") else { return nil }
        let name = String(value[..<paren]).trimmingCharacters(in: .whitespacesAndNewlines)
        guard !name.isEmpty, name.range(of: #"^[A-Za-z_][A-Za-z0-9_]*$"#, options: .regularExpression) != nil else {
            return nil
        }
        return value.hasSuffix(")") ? name : nil
    }

    private static func validateManifest(_ data: Data, appId: String) throws {
        let object = try manifestObject(from: data)
        let version = intValue(object["schemaVersion"] ?? object["version"])
        guard version == 1 else {
            throw RuntimePackageError.invalidManifest("manifest.json must declare schemaVersion/version 1")
        }
        guard object["kind"] as? String == "huangshan-runtime-app-manifest" else {
            throw RuntimePackageError.invalidManifest("manifest.json kind must be huangshan-runtime-app-manifest")
        }
        guard let manifestAppId = object["id"] as? String, manifestAppId == appId else {
            throw RuntimePackageError.invalidManifest("manifest.json id must match app id \(appId)")
        }
        if let entryValue = object["entry"] {
            guard let entry = entryValue as? String, entry == "main.lua" else {
                throw RuntimePackageError.invalidManifest("manifest.json entry must be main.lua")
            }
        }
        try validateManifestProfile(object)
        try validateManifestCapabilityLists(object)

        guard let componentsValue = object["components"] else { return }
        guard let components = componentsValue as? [Any] else {
            throw RuntimePackageError.invalidManifest("manifest.json components must be a list")
        }
        guard components.count <= 8 else {
            throw RuntimePackageError.invalidManifest("manifest.json supports at most 8 components")
        }
        for (offset, componentValue) in components.enumerated() {
            guard let component = componentValue as? [String: Any] else {
                throw RuntimePackageError.invalidManifest("manifest.json component #\(offset + 1) must be an object")
            }
            try validateManifestComponent(component, index: offset + 1)
        }
    }

    private static func isESP32NativeCapability(_ value: String) -> Bool {
        let normalized = value.trimmingCharacters(in: .whitespacesAndNewlines).lowercased()
        if esp32NativeCapabilityNames.contains(normalized) {
            return true
        }
        return esp32NativeCapabilityNames.contains { normalized.hasPrefix("\($0).") }
    }

    private static func validateHuangshanCapability(_ value: String, context: String, allowed: Set<String>? = nil) throws {
        guard !value.isEmpty else {
            throw RuntimePackageError.invalidManifest("\(context) must be a non-empty string")
        }
        if isESP32NativeCapability(value) {
            throw RuntimePackageError.invalidManifest("\(context) \(value) is not supported by Huangshan Runtime profile; use BLE/serial plus a phone or desktop bridge instead")
        }
        if let allowed, !allowed.contains(value) {
            throw RuntimePackageError.invalidManifest("\(context) \(value) is not supported by Huangshan Runtime profile")
        }
    }

    private static func validateManifestProfile(_ object: [String: Any]) throws {
        for key in manifestProfileFields {
            guard let value = object[key] else { continue }
            guard let text = value as? String else {
                throw RuntimePackageError.invalidManifest("manifest.json \(key) must be a string when present")
            }
            guard huangshanProfileAliases.contains(text.trimmingCharacters(in: .whitespacesAndNewlines).lowercased()) else {
                throw RuntimePackageError.invalidManifest("manifest.json \(key) \(text) is not compatible with Huangshan Runtime profile")
            }
        }
    }

    private static func validateManifestCapabilityLists(_ object: [String: Any]) throws {
        for key in manifestCapabilityListFields {
            guard let value = object[key] else { continue }
            guard let items = value as? [Any] else {
                throw RuntimePackageError.invalidManifest("manifest.json \(key) must be a list of capability strings when present")
            }
            for (offset, item) in items.enumerated() {
                guard let capability = item as? String else {
                    throw RuntimePackageError.invalidManifest("manifest.json \(key)[\(offset + 1)] must be a capability string")
                }
                try validateHuangshanCapability(
                    capability,
                    context: "manifest.json \(key)[\(offset + 1)]",
                    allowed: manifestDeclaredCapabilities
                )
            }
        }
    }

    private static func validateManifestComponent(_ component: [String: Any], index: Int) throws {
        let type: String
        if let typeValue = component["type"] {
            guard let typeString = typeValue as? String else {
                throw RuntimePackageError.invalidManifest("manifest.json component #\(index) type must be a string")
            }
            type = typeString
        } else {
            type = "status"
        }
        guard manifestComponentTypes.contains(type) else {
            throw RuntimePackageError.invalidManifest("manifest.json component #\(index) type \(type) is not supported")
        }
        if type == "label" {
            guard component["text"] is String else {
                throw RuntimePackageError.invalidManifest("manifest.json component #\(index) text must be a string for label")
            }
            for key in ["x", "y", "w"] {
                if let value = component[key], intValue(value) == nil {
                    throw RuntimePackageError.invalidManifest("manifest.json component #\(index) \(key) must be an integer for label")
                }
            }
            if let font = component["font"], (intValue(font) ?? 0) <= 0 {
                throw RuntimePackageError.invalidManifest("manifest.json component #\(index) font must be a positive integer for label")
            }
            if let color = component["color"] as? String, color.range(of: #"^#[0-9a-fA-F]{6}$"#, options: .regularExpression) == nil {
                throw RuntimePackageError.invalidManifest("manifest.json component #\(index) color must be #RRGGBB for label")
            } else if component["color"] != nil && !(component["color"] is String) {
                throw RuntimePackageError.invalidManifest("manifest.json component #\(index) color must be #RRGGBB for label")
            }
            return
        }
        guard let capability = component["capability"] as? String else {
            throw RuntimePackageError.invalidManifest("manifest.json component #\(index) capability must be a string")
        }
        try validateHuangshanCapability(
            capability,
            context: "manifest.json component #\(index) capability",
            allowed: manifestCapabilities
        )
        if let label = component["label"], !(label is String) {
            throw RuntimePackageError.invalidManifest("manifest.json component #\(index) label must be a string")
        }
        if let value = component["value"], !(value is String) {
            throw RuntimePackageError.invalidManifest("manifest.json component #\(index) value must be a string")
        }
        if type == "action", !(component["label"] is String) {
            throw RuntimePackageError.invalidManifest("manifest.json component #\(index) label must be a string for action")
        }
    }

    private static func manifestObject(from data: Data) throws -> [String: Any] {
        guard String(data: data, encoding: .utf8) != nil else {
            throw RuntimePackageError.invalidManifest("manifest.json must be UTF-8")
        }
        do {
            guard let object = try JSONSerialization.jsonObject(with: data) as? [String: Any] else {
                throw RuntimePackageError.invalidManifest("manifest.json must contain a JSON object")
            }
            return object
        } catch let error as RuntimePackageError {
            throw error
        } catch {
            throw RuntimePackageError.invalidManifest("manifest.json is not valid JSON")
        }
    }

    private static func intValue(_ value: Any?) -> Int? {
        guard let value else { return nil }
        if let number = value as? NSNumber {
            guard CFGetTypeID(number as CFTypeRef) != CFBooleanGetTypeID() else { return nil }
            let type = String(cString: number.objCType)
            let integerTypes: Set<String> = ["c", "i", "s", "l", "q", "C", "I", "S", "L", "Q"]
            guard integerTypes.contains(type) else { return nil }
            return number.intValue
        }
        return value as? Int
    }

    private static func manifestId(from data: Data?) -> String? {
        guard let data,
              let object = try? manifestObject(from: data),
              let id = object["id"] as? String
        else {
            return nil
        }
        return id
    }
}

extension Data {
    var hexString: String {
        map { String(format: "%02x", $0) }.joined()
    }
}
