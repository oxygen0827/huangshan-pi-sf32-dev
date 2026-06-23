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

    private let client: VibeBoardBLEClient

    public init(client: VibeBoardBLEClient = VibeBoardBLEClient()) {
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

    public func refreshSensors() {
        run("Reading sensors") {
            if !self.client.isReadyForRuntimeCommands() {
                try await self.client.connect(scanTimeout: 6, connectTimeout: 8)
            }
            let snapshot = try await self.client.sensors()
            self.statusText = snapshot.summary
        }
    }

    public func installDemoApp() {
        run("Installing demo app") {
            try await self.client.connect()
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
            try await self.client.connect()
            try await self.client.install(package)
            self.statusText = try await self.client.status()
            self.trace("imported app installed over BLE app=\(package.appId)")
        }
    }

    public func showError(_ error: Error) {
        statusText = "Error: \(error)"
    }

    private func connectAndReadStatus(scanTimeout: TimeInterval = 12, connectTimeout: TimeInterval = 12) async throws {
        try await client.connect(scanTimeout: scanTimeout, connectTimeout: connectTimeout)
        let status = try await client.status()
        statusText = status
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
                self.statusText = "Error: \(error)"
            }
        }
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
