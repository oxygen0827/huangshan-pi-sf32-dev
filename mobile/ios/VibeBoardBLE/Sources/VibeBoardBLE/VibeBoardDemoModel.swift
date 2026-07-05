import Combine
import Foundation

@MainActor
public final class VibeBoardDemoModel: ObservableObject {
    private func trace(_ message: String) {
        print("[vibeboard-ios][demo] \(message)")
        NSLog("[vibeboard-ios][demo] %@", message)
    }

    @Published public private(set) var statusText = "Not connected"
    @Published public private(set) var connectionText = "Idle"
    @Published public private(set) var isBusy = false
    @Published public var infoFlowText = "Hello from iPhone"
    @Published public private(set) var infoFlowSequence: UInt32 = 0
    @Published public var rgbColor = "3366ff"
    @Published public var displayBrightness = "70"
    @Published public private(set) var latestTouchCount: UInt32 = 0
    @Published public private(set) var latestTouchPoint = "--"
    @Published public private(set) var latestTouchEvent = "idle"
    @Published public private(set) var latestTouchGesture = "none"
    @Published public private(set) var latestTouchDelta = "0,0"
    @Published public private(set) var latestTouchDurationMs: UInt32 = 0
    @Published public private(set) var latestGPIOStatus = "gpio --"
    @Published public var appManagerAppId = "flow_stage"
    @Published public private(set) var latestAppManagerStatus = "app manager --"
    @Published public private(set) var installedAppsText = "apps --"
    @Published public var voiceReplyText = "Hello from iPhone voice"
    @Published public var voiceDurationMs: Double = 1200
    @Published public private(set) var voiceSequence: UInt32 = 0
    @Published public private(set) var latestVoicePCMBytes = 0

    private var client: any VibeBoardRuntimeTransport

    public init(client: any VibeBoardRuntimeTransport = VibeBoardBLEClient()) {
        self.client = client
        self.connectionText = client.connectionState.label
        self.client.onStateChange = { [weak self] state in
            self?.connectionText = state.label
        }
    }

    public func connect() {
        run("Connecting") {
            try await self.connectAndReadStatus()
        }
    }

    public func autoConnectIfNeeded() {
        guard !isBusy, !client.isReadyForRuntimeCommands() else { return }
        trace("auto connect requested")
        run("Auto connecting") {
            try await self.connectAndReadStatus(scanTimeout: 6, connectTimeout: 8)
        }
    }

    public func refreshStatus() {
        run("Reading status") {
            if !self.client.isReadyForRuntimeCommands() {
                try await self.client.connect(scanTimeout: 6, connectTimeout: 8)
            }
            let status = try await self.client.status()
            self.statusText = status
        }
    }

    public func refreshCapabilities() {
        run("Reading capabilities") {
            if !self.client.isReadyForRuntimeCommands() {
                try await self.client.connect(scanTimeout: 6, connectTimeout: 8)
            }
            let capabilities = try await self.client.capabilities()
            self.statusText = capabilities.summary
        }
    }

    public func refreshSensors() {
        run("Reading sensors") {
            if !self.client.isReadyForRuntimeCommands() {
                try await self.client.connect(scanTimeout: 6, connectTimeout: 8)
            }
            let snapshot = try await self.client.sensors()
            self.statusText = snapshot.summary
        }
    }

    public func refreshTouch() {
        run("Reading touch") {
            if !self.client.isReadyForRuntimeCommands() {
                try await self.client.connect(scanTimeout: 6, connectTimeout: 8)
            }
            let snapshot = try await self.client.touch()
            self.latestTouchCount = snapshot.count
            self.latestTouchPoint = "\(snapshot.x),\(snapshot.y)"
            self.latestTouchEvent = snapshot.event
            self.latestTouchGesture = snapshot.gesture ?? "none"
            self.latestTouchDelta = "\(snapshot.dx ?? 0),\(snapshot.dy ?? 0)"
            self.latestTouchDurationMs = snapshot.durationMs ?? 0
            self.statusText = snapshot.summary
        }
    }

    public func refreshGPIO() {
        run("Reading GPIO") {
            if !self.client.isReadyForRuntimeCommands() {
                try await self.client.connect(scanTimeout: 6, connectTimeout: 8)
            }
            let snapshot = try await self.client.gpio()
            self.latestGPIOStatus = snapshot.summary
            self.statusText = snapshot.summary
        }
    }

    public func refreshPower() {
        run("Reading power") {
            if !self.client.isReadyForRuntimeCommands() {
                try await self.client.connect(scanTimeout: 6, connectTimeout: 8)
            }
            let snapshot = try await self.client.power()
            self.statusText = snapshot.summary
        }
    }


    public func refreshDisplay() {
        run("Reading display") {
            if !self.client.isReadyForRuntimeCommands() {
                try await self.client.connect(scanTimeout: 6, connectTimeout: 8)
            }
            let snapshot = try await self.client.display()
            self.displayBrightness = String(snapshot.brightness)
            self.statusText = snapshot.summary
        }
    }

    public func setDisplayBrightness() {
        let text = self.displayBrightness.trimmingCharacters(in: .whitespacesAndNewlines)
        guard let brightness = Int(text), brightness >= 0, brightness <= 100 else {
            statusText = "Type display brightness 0-100 before sending"
            return
        }
        run("Setting display") {
            if !self.client.isReadyForRuntimeCommands() {
                try await self.client.connect(scanTimeout: 6, connectTimeout: 8)
            }
            let snapshot = try await self.client.display(brightness: brightness)
            self.displayBrightness = String(snapshot.brightness)
            self.statusText = snapshot.summary
        }
    }

    public func refreshRGB() {
        run("Reading RGB") {
            if !self.client.isReadyForRuntimeCommands() {
                try await self.client.connect(scanTimeout: 6, connectTimeout: 8)
            }
            let snapshot = try await self.client.rgb()
            self.rgbColor = snapshot.color
            self.statusText = snapshot.summary
        }
    }

    public func setRGB() {
        let color = self.rgbColor.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !color.isEmpty else {
            statusText = "Type an RGB color before sending"
            return
        }
        run("Setting RGB") {
            if !self.client.isReadyForRuntimeCommands() {
                try await self.client.connect(scanTimeout: 6, connectTimeout: 8)
            }
            let snapshot = try await self.client.rgb(color: color)
            self.rgbColor = snapshot.color
            self.statusText = snapshot.summary
        }
    }

    public func refreshAppManagerStatus() {
        run("Reading app manager") {
            if !self.client.isReadyForRuntimeCommands() {
                try await self.client.connect(scanTimeout: 6, connectTimeout: 8)
            }
            let snapshot = try await self.client.appStatus()
            self.latestAppManagerStatus = snapshot.summary
            self.statusText = snapshot.summary
        }
    }

    public func refreshInstalledApps() {
        run("Reading apps") {
            if !self.client.isReadyForRuntimeCommands() {
                try await self.client.connect(scanTimeout: 6, connectTimeout: 8)
            }
            let snapshot = try await self.client.apps()
            self.installedAppsText = snapshot.apps.map { app in
                let marker = app.active != 0 ? "*" : "-"
                return "\(marker)\(app.id)"
            }.joined(separator: " ")
            self.statusText = snapshot.summary
        }
    }

    public func launchApp() {
        let appId = appManagerAppId.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !appId.isEmpty else {
            statusText = "Type app id before launch"
            return
        }
        run("Launching app") {
            if !self.client.isReadyForRuntimeCommands() {
                try await self.client.connect(scanTimeout: 6, connectTimeout: 8)
            }
            let ack = try await self.client.launchApp(appId)
            self.statusText = ack.summary
            self.latestAppManagerStatus = ack.summary
        }
    }

    public func stopApp() {
        run("Stopping app") {
            if !self.client.isReadyForRuntimeCommands() {
                try await self.client.connect(scanTimeout: 6, connectTimeout: 8)
            }
            let ack = try await self.client.stopApp()
            self.statusText = ack.summary
            self.latestAppManagerStatus = ack.summary
        }
    }

    public func deleteApp() {
        let appId = appManagerAppId.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !appId.isEmpty else {
            statusText = "Type app id before delete"
            return
        }
        run("Deleting app") {
            if !self.client.isReadyForRuntimeCommands() {
                try await self.client.connect(scanTimeout: 6, connectTimeout: 8)
            }
            let ack = try await self.client.deleteApp(appId)
            let list = try await self.client.apps()
            self.statusText = ack.summary
            self.latestAppManagerStatus = ack.summary
            self.installedAppsText = list.apps.map { app in
                let marker = app.active != 0 ? "*" : "-"
                return "\(marker)\(app.id)"
            }.joined(separator: " ")
        }
    }

    public func sendInfoFlow() {
        let text = infoFlowText.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !text.isEmpty else {
            statusText = "Type a message before sending"
            return
        }
        run("Sending info flow") {
            if !self.client.isReadyForRuntimeCommands() {
                try await self.client.connect(scanTimeout: 6, connectTimeout: 8)
            }
            let nextSequence = self.infoFlowSequence + 1
            let ack = try await self.client.sendInfoFlow(
                channel: "phone",
                sequence: nextSequence,
                payload: text
            )
            self.infoFlowSequence = nextSequence
            self.statusText = ack
        }
    }

    public func refreshInfoFlowStatus() {
        run("Reading info flow") {
            if !self.client.isReadyForRuntimeCommands() {
                try await self.client.connect(scanTimeout: 6, connectTimeout: 8)
            }
            self.statusText = try await self.client.infoFlowStatus()
        }
    }

    public func clearInfoFlow() {
        run("Clearing info flow") {
            if !self.client.isReadyForRuntimeCommands() {
                try await self.client.connect(scanTimeout: 6, connectTimeout: 8)
            }
            self.statusText = try await self.client.clearInfoFlow()
            self.infoFlowSequence = 0
        }
    }

    public func refreshVoiceStatus() {
        run("Reading voice status") {
            if !self.client.isReadyForRuntimeCommands() {
                try await self.client.connect(scanTimeout: 6, connectTimeout: 8)
            }
            let snapshot = try await self.client.voice()
            self.voiceSequence = snapshot.seq
            self.latestVoicePCMBytes = Int(snapshot.bytes)
            self.statusText = snapshot.summary
        }
    }

    public func captureVoice() {
        let boundedDuration = UInt32(max(300, min(Int(self.voiceDurationMs.rounded()), 5000)))
        run("Capturing voice") {
            if !self.client.isReadyForRuntimeCommands() {
                try await self.client.connect(scanTimeout: 6, connectTimeout: 8)
            }
            let capture = try await self.client.captureVoice(durationMs: boundedDuration)
            self.voiceSequence = capture.status.sequence
            self.latestVoicePCMBytes = capture.pcm.count
            self.statusText = "captured seq=\(capture.status.sequence) bytes=\(capture.pcm.count)"
        }
    }

    public func clearVoice() {
        run("Clearing voice") {
            if !self.client.isReadyForRuntimeCommands() {
                try await self.client.connect(scanTimeout: 6, connectTimeout: 8)
            }
            self.statusText = try await self.client.voiceClear()
            self.latestVoicePCMBytes = 0
        }
    }

    public func sendVoiceReply() {
        let text = voiceReplyText.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !text.isEmpty else {
            statusText = "Type a voice reply before sending"
            return
        }
        run("Sending voice reply") {
            if !self.client.isReadyForRuntimeCommands() {
                try await self.client.connect(scanTimeout: 6, connectTimeout: 8)
            }
            let nextSequence = UInt32(Date().timeIntervalSince1970) &+ UInt32(self.infoFlowSequence)
            let ack = try await self.client.sendVoiceReply(sequence: nextSequence, payload: text)
            self.statusText = ack
        }
    }

    public func installDemoApp() {
        run("Installing demo app") {
            try await self.client.connect(scanTimeout: 12, connectTimeout: 12)
            let package = try RuntimePackage(
                appId: "ios_demo",
                files: [
                    "manifest.json": Data(Self.demoManifest.utf8),
                    "main.lua": Data(Self.demoLua.utf8)
                ]
            )
            try await self.client.install(package)
            self.statusText = try await self.client.status()
            self.trace("demo app installed over BLE")
        }
    }

    public func installPackage(from directory: URL) {
        run("Installing imported app") {
            let didStartAccessing = directory.startAccessingSecurityScopedResource()
            defer {
                if didStartAccessing {
                    directory.stopAccessingSecurityScopedResource()
                }
            }

            let package = try RuntimePackage.fromDirectory(directory)
            self.statusText = "Installing \(package.appId)"
            try await self.client.connect(scanTimeout: 12, connectTimeout: 12)
            try await self.client.install(package)
            self.statusText = try await self.client.status()
            self.trace("imported app installed over BLE app=\(package.appId)")
        }
    }

    public func showError(_ error: Error) {
        statusText = "Error: \(Self.describe(error))"
    }

    private func connectAndReadStatus(scanTimeout: TimeInterval = 12, connectTimeout: TimeInterval = 12) async throws {
        try await client.connect(scanTimeout: scanTimeout, connectTimeout: connectTimeout)
        let capabilities = try await client.capabilities()
        statusText = capabilities.summary
    }

    private func run(_ label: String, operation: @escaping () async throws -> Void) {
        guard !isBusy else { return }
        isBusy = true
        statusText = label
        Task { @MainActor in
            defer { self.isBusy = false }
            do {
                try await operation()
            } catch {
                self.trace("\(label) failed: \(error)")
                self.statusText = "Error: \(Self.describe(error))"
            }
        }
    }

    private static func describe(_ error: Error) -> String {
        if let localized = error as? LocalizedError, let description = localized.errorDescription {
            return description
        }
        return String(describing: error)
    }

    private static let demoManifest = """
    {
      "schemaVersion": 1,
      "kind": "huangshan-runtime-app-manifest",
      "id": "ios_demo",
      "name": "iOS BLE Demo",
      "description": "Installed from iPhone BLE central",
      "entry": "main.lua",
      "components": [
        {
          "type": "status",
          "capability": "status",
          "label": "Source",
          "value": "iOS BLE"
        }
      ]
    }
    """

    private static let demoLua = """
    local root = lv_scr_act()
    lv_obj_clean(root)
    lv_obj_set_style_bg_color(root, 0x111827)
    lv_obj_set_style_text_color(root, 0xf8fafc)

    local title = lv_label_create(root)
    lv_label_set_text(title, "iOS BLE Demo")
    lv_obj_set_style_text_color(title, 0x5eead4)
    lv_obj_set_width(title, 320)
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 32, 58)

    local body = lv_label_create(root)
    lv_label_set_text(body, "Installed from phone over BLE")
    lv_obj_set_style_text_color(body, 0xcbd5e1)
    lv_obj_set_width(body, 330)
    lv_obj_align(body, LV_ALIGN_TOP_LEFT, 32, 112)

    local tick = lv_label_create(root)
    lv_label_set_text(tick, "Phone BLE Tick 0")
    lv_obj_set_style_text_color(tick, 0xfbbf24)
    lv_obj_align(tick, LV_ALIGN_TOP_LEFT, 32, 172)
    vibe_timer_label(tick, 1000, "Phone BLE Tick")
    print("[ios_demo] installed over BLE")
    """
}
