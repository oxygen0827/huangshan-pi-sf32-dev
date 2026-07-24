import AppKit
import Foundation

final class CompanionAppDelegate: NSObject, NSApplicationDelegate {
    private let dashboardBase = URL(string: "http://127.0.0.1:8790/")!
    private var companionProcess: Process?
    private var logHandle: FileHandle?
    private var pendingDashboardURL: URL?
    private var readinessAttempts = 0

    func applicationDidFinishLaunching(_ notification: Notification) {
        NSApp.setActivationPolicy(.accessory)
        pendingDashboardURL = dashboardBase
        waitForCompanion()
    }

    func application(_ application: NSApplication, open urls: [URL]) {
        guard let source = urls.first, let dashboardURL = localDashboardURL(for: source) else { return }
        pendingDashboardURL = dashboardURL
        waitForCompanion()
    }

    func applicationWillTerminate(_ notification: Notification) {
        if companionProcess?.isRunning == true {
            companionProcess?.terminate()
        }
        try? logHandle?.close()
    }

    private func repositoryRoot() -> URL {
        Bundle.main.bundleURL.deletingLastPathComponent().deletingLastPathComponent()
    }

    private func ensureCompanion() {
        guard companionProcess?.isRunning != true else { return }
        let root = repositoryRoot()
        let python = root.appendingPathComponent(".venv/bin/python").path
        let bridge = root.appendingPathComponent("scripts/codex_pet_bridge.py").path
        let helper = Bundle.main.bundleURL.appendingPathComponent("Contents/Resources/CodexPetDesktopApproval").path
        guard FileManager.default.isExecutableFile(atPath: python), FileManager.default.fileExists(atPath: bridge) else {
            presentError("Companion 运行环境不完整，请重新安装 VibeBoard Companion。")
            return
        }

        let logDirectory = FileManager.default.homeDirectoryForCurrentUser
            .appendingPathComponent(".vibeboard/companion", isDirectory: true)
        try? FileManager.default.createDirectory(at: logDirectory, withIntermediateDirectories: true)
        let logURL = logDirectory.appendingPathComponent("companion.log")
        if !FileManager.default.fileExists(atPath: logURL.path) {
            FileManager.default.createFile(atPath: logURL.path, contents: nil)
        }
        logHandle = try? FileHandle(forWritingTo: logURL)
        _ = try? logHandle?.seekToEnd()

        let process = Process()
        process.executableURL = URL(fileURLWithPath: python)
        process.currentDirectoryURL = root
        process.arguments = [
            bridge, "--mode", "monitor", "--workspace", root.path,
            "--approval-helper", helper, "--companion-no-open",
        ]
        process.standardOutput = logHandle
        process.standardError = logHandle
        process.terminationHandler = { [weak self] task in
            guard task.terminationStatus != 0 else { return }
            DispatchQueue.main.async {
                self?.presentError("VibeBoard Companion 已停止，请查看 ~/.vibeboard/companion/companion.log。")
            }
        }
        do {
            try process.run()
            companionProcess = process
        } catch {
            presentError("无法启动 VibeBoard Companion：\(error.localizedDescription)")
        }
    }

    private func waitForCompanion() {
        readinessAttempts = 0
        probeCompanion()
    }

    private func probeCompanion() {
        var request = URLRequest(url: dashboardBase.appendingPathComponent("v1/status"))
        request.timeoutInterval = 0.8
        URLSession.shared.dataTask(with: request) { [weak self] _, response, _ in
            DispatchQueue.main.async {
                guard let self else { return }
                if (response as? HTTPURLResponse)?.statusCode == 200 {
                    let target = self.pendingDashboardURL ?? self.dashboardBase
                    self.pendingDashboardURL = nil
                    NSWorkspace.shared.open(target)
                    return
                }
                if self.readinessAttempts == 0 {
                    self.ensureCompanion()
                }
                self.readinessAttempts += 1
                if self.readinessAttempts < 80 {
                    DispatchQueue.main.asyncAfter(deadline: .now() + 0.25) { self.probeCompanion() }
                } else {
                    self.presentError("本地 Companion 服务未能在 20 秒内启动。")
                }
            }
        }.resume()
    }

    private func localDashboardURL(for source: URL) -> URL? {
        guard source.scheme == "vibeboard", source.host == "pet", source.path == "/install",
              let components = URLComponents(url: source, resolvingAgainstBaseURL: false) else { return nil }
        let values = Dictionary(uniqueKeysWithValues: (components.queryItems ?? []).map { ($0.name, $0.value ?? "") })
        guard values["source"] == "petdex", let slug = values["slug"],
              slug.range(of: "^[a-z0-9][a-z0-9-]{0,23}$", options: .regularExpression) != nil else { return nil }
        if let digest = values["digest"],
           digest.range(of: "^[0-9a-f]{64}$", options: .regularExpression) == nil { return nil }
        var dashboard = URLComponents(url: dashboardBase, resolvingAgainstBaseURL: false)!
        dashboard.queryItems = [
            URLQueryItem(name: "source", value: "petdex"),
            URLQueryItem(name: "install", value: slug),
        ]
        if let digest = values["digest"] {
            dashboard.queryItems?.append(URLQueryItem(name: "digest", value: digest))
        }
        return dashboard.url
    }

    private func presentError(_ message: String) {
        let alert = NSAlert()
        alert.alertStyle = .critical
        alert.messageText = "VibeBoard Companion"
        alert.informativeText = message
        alert.runModal()
    }
}

let application = NSApplication.shared
let delegate = CompanionAppDelegate()
application.delegate = delegate
application.run()
