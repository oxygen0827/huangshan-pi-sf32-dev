import AppKit
import Darwin
import Foundation
import ServiceManagement

final class CompanionAppDelegate: NSObject, NSApplicationDelegate {
    private let defaultCompanionPort = 8790
    private let maximumCompanionPort = 8899
    private let companionPortDefaultsKey = "companionPort"
    private let defaults = UserDefaults.standard
    private var companionPort = 8790
    private var companionProcess: Process?
    private var logHandle: FileHandle?
    private var pendingDashboardURL: URL?
    private var readinessAttempts = 0
    private var statusItem: NSStatusItem!
    private var boardStatusItem: NSMenuItem!
    private var codexStatusItem: NSMenuItem!
    private var loginItem: NSMenuItem!
    private var updateItem: NSMenuItem!
    private var statusTimer: Timer?
    private var restartWorkItem: DispatchWorkItem?
    private var isTerminating = false

    private var dashboardBase: URL {
        dashboardURL(for: companionPort)
    }

    private var resourcesURL: URL {
        Bundle.main.resourceURL!
    }

    private var agentURL: URL {
        resourcesURL.appendingPathComponent("Agent/VibeBoardCompanionAgent")
    }

    private var agentRootURL: URL {
        resourcesURL.appendingPathComponent("AgentRoot", isDirectory: true)
    }

    private var helperURL: URL {
        resourcesURL.appendingPathComponent("CodexPetDesktopApproval")
    }

    private var logURL: URL {
        FileManager.default.homeDirectoryForCurrentUser
            .appendingPathComponent(".vibeboard/companion/companion.log")
    }

    private var publicSiteURL: URL? {
        guard let value = Bundle.main.object(forInfoDictionaryKey: "VibeBoardPublicSiteURL") as? String,
              !value.isEmpty, let url = URL(string: value), url.scheme == "https" else { return nil }
        return url
    }

    private var updateManifestURL: URL? {
        guard let value = Bundle.main.object(forInfoDictionaryKey: "VibeBoardUpdateManifestURL") as? String,
              !value.isEmpty, let url = URL(string: value), url.scheme == "https" else { return nil }
        return url
    }

    private var firmwareManifestURL: URL? {
        guard let value = Bundle.main.object(forInfoDictionaryKey: "VibeBoardFirmwareManifestURL") as? String,
              !value.isEmpty, let url = URL(string: value), url.scheme == "https" else { return nil }
        return url
    }

    func applicationDidFinishLaunching(_ notification: Notification) {
        NSApp.setActivationPolicy(.accessory)
        companionPort = rememberedCompanionPort()
        configureMenuBar()
        updateLoginItem()
        statusTimer = Timer.scheduledTimer(withTimeInterval: 5, repeats: true) { [weak self] _ in
            self?.refreshStatus()
        }
        ensureCompanion()

        let firstLaunch = !defaults.bool(forKey: "completedFirstLaunch")
        if firstLaunch {
            presentWelcome()
            defaults.set(true, forKey: "completedFirstLaunch")
        }
        var target = dashboardBase
        if firstLaunch {
            var components = URLComponents(url: dashboardBase, resolvingAgainstBaseURL: false)!
            components.queryItems = [URLQueryItem(name: "setup", value: "1")]
            target = components.url ?? dashboardBase
        }
        openWhenReady(target)
        refreshStatus()
    }

    func applicationShouldHandleReopen(_ sender: NSApplication, hasVisibleWindows flag: Bool) -> Bool {
        openDashboard()
        return true
    }

    func application(_ application: NSApplication, open urls: [URL]) {
        guard let source = urls.first, let dashboardURL = localDashboardURL(for: source) else {
            presentError("这个部署链接无效或已被篡改。")
            return
        }
        openWhenReady(dashboardURL)
    }

    func applicationWillTerminate(_ notification: Notification) {
        isTerminating = true
        statusTimer?.invalidate()
        restartWorkItem?.cancel()
        if companionProcess?.isRunning == true {
            companionProcess?.terminate()
        }
        try? logHandle?.close()
    }

    private func configureMenuBar() {
        statusItem = NSStatusBar.system.statusItem(withLength: NSStatusItem.squareLength)
        statusItem.button?.image = NSImage(systemSymbolName: "pawprint.fill", accessibilityDescription: "VibeBoard Companion")
        statusItem.button?.toolTip = "VibeBoard Companion"

        let menu = NSMenu()
        let title = NSMenuItem(title: "VibeBoard Companion", action: nil, keyEquivalent: "")
        title.isEnabled = false
        menu.addItem(title)
        menu.addItem(.separator())

        boardStatusItem = NSMenuItem(title: "板子：正在检测", action: nil, keyEquivalent: "")
        boardStatusItem.isEnabled = false
        menu.addItem(boardStatusItem)
        codexStatusItem = NSMenuItem(title: "Codex：正在检测", action: nil, keyEquivalent: "")
        codexStatusItem.isEnabled = false
        menu.addItem(codexStatusItem)
        menu.addItem(.separator())

        menu.addItem(NSMenuItem(title: "打开 Companion", action: #selector(openDashboard), keyEquivalent: "o"))
        menu.addItem(NSMenuItem(title: "打开宠物图库", action: #selector(openPetGallery), keyEquivalent: "g"))
        menu.addItem(NSMenuItem(title: "连接或更换板子", action: #selector(openSetup), keyEquivalent: ""))
        menu.addItem(.separator())

        loginItem = NSMenuItem(title: "登录时自动启动", action: #selector(toggleLoginItem), keyEquivalent: "")
        menu.addItem(loginItem)
        updateItem = NSMenuItem(title: "检查更新", action: #selector(checkForUpdatesFromMenu), keyEquivalent: "")
        updateItem.isHidden = updateManifestURL == nil
        menu.addItem(updateItem)
        menu.addItem(NSMenuItem(title: "打开诊断日志", action: #selector(openLog), keyEquivalent: ""))
        menu.addItem(.separator())
        menu.addItem(NSMenuItem(title: "退出 VibeBoard Companion", action: #selector(quit), keyEquivalent: "q"))
        for item in menu.items where item.action != nil {
            item.target = self
        }
        statusItem.menu = menu
    }

    private func presentWelcome() {
        let alert = NSAlert()
        alert.alertStyle = .informational
        alert.messageText = "VibeBoard Companion 已就绪"
        alert.informativeText = "接下来会打开 Companion，完成 Codex 和 VibeBoard 的连接。"
        alert.addButton(withTitle: "开始设置")
        alert.showsSuppressionButton = true
        alert.suppressionButton?.title = "登录时自动启动"
        alert.suppressionButton?.state = .on
        alert.runModal()
        if alert.suppressionButton?.state == .on {
            setLoginEnabled(true, reportError: false)
        }
    }

    private func prepareLog() {
        try? logHandle?.close()
        let directory = logURL.deletingLastPathComponent()
        try? FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        if let size = (try? logURL.resourceValues(forKeys: [.fileSizeKey]).fileSize), size > 5 * 1024 * 1024 {
            let previous = directory.appendingPathComponent("companion.previous.log")
            try? FileManager.default.removeItem(at: previous)
            try? FileManager.default.moveItem(at: logURL, to: previous)
        }
        if !FileManager.default.fileExists(atPath: logURL.path) {
            FileManager.default.createFile(atPath: logURL.path, contents: nil)
        }
        logHandle = try? FileHandle(forWritingTo: logURL)
        _ = try? logHandle?.seekToEnd()
    }

    private func ensureCompanion() {
        guard companionProcess?.isRunning != true else { return }
        guard let selectedPort = selectCompanionPort() else {
            presentError("本地 Companion 没有可用端口（已尝试 8790-8899）。请关闭占用这些端口的程序后重试。")
            return
        }
        companionPort = selectedPort
        defaults.set(selectedPort, forKey: companionPortDefaultsKey)
        if companionResponding(at: selectedPort) { return }
        guard FileManager.default.isExecutableFile(atPath: agentURL.path),
              FileManager.default.isExecutableFile(atPath: helperURL.path),
              FileManager.default.fileExists(atPath: agentRootURL.path) else {
            presentError("Companion 安装不完整，请从官方安装包重新安装。")
            return
        }
        prepareLog()

        let process = Process()
        process.executableURL = agentURL
        process.currentDirectoryURL = FileManager.default.homeDirectoryForCurrentUser
        process.arguments = [
            "--mode", "monitor",
            "--workspace", FileManager.default.homeDirectoryForCurrentUser.path,
            "--parent-pid", String(ProcessInfo.processInfo.processIdentifier),
            "--approval-helper", helperURL.path,
            "--companion-port", String(companionPort),
            "--companion-no-open",
        ]
        var environment = ProcessInfo.processInfo.environment
        environment["VIBEBOARD_COMPANION_ROOT"] = agentRootURL.path
        environment["VIBEBOARD_COMPANION_AGENT"] = agentURL.path
        environment["NODE"] = resourcesURL.appendingPathComponent("Tools/node").path
        environment["CODEX_PET_SHARP"] = resourcesURL.appendingPathComponent("Tools/node_modules/sharp").path
        let sftool = resourcesURL.appendingPathComponent("Tools/sftool")
        if FileManager.default.isExecutableFile(atPath: sftool.path) {
            environment["VIBEBOARD_SFTOOL"] = sftool.path
        }
        let firmwareKey = agentRootURL.appendingPathComponent("keys/firmware-public.pem")
        if FileManager.default.fileExists(atPath: firmwareKey.path), let firmwareManifestURL {
            environment["VIBEBOARD_FIRMWARE_PUBLIC_KEY"] = firmwareKey.path
            environment["VIBEBOARD_FIRMWARE_MANIFEST_URL"] = firmwareManifestURL.absoluteString
        }
        if let origin = publicOrigin() {
            environment["VIBEBOARD_COMPANION_ORIGINS"] = origin
        }
        process.environment = environment
        process.standardOutput = logHandle
        process.standardError = logHandle
        process.terminationHandler = { [weak self] task in
            DispatchQueue.main.async {
                guard let self, self.companionProcess === task else { return }
                self.companionProcess = nil
                guard !self.isTerminating else { return }
                self.markServiceUnavailable()
                self.scheduleRestart()
            }
        }
        do {
            try process.run()
            companionProcess = process
        } catch {
            presentError("无法启动 VibeBoard Companion：\(error.localizedDescription)")
        }
    }

    private func companionResponding(at port: Int = 0) -> Bool {
        let targetPort = port == 0 ? companionPort : port
        var request = URLRequest(url: dashboardURL(for: targetPort).appendingPathComponent("v1/status"))
        request.timeoutInterval = 0.6
        let semaphore = DispatchSemaphore(value: 0)
        var available = false
        URLSession.shared.dataTask(with: request) { data, response, _ in
            available = self.isCompanionStatus(data: data, response: response)
            semaphore.signal()
        }.resume()
        _ = semaphore.wait(timeout: .now() + 0.7)
        return available
    }

    private func isCompanionStatus(data: Data?, response: URLResponse?) -> Bool {
        guard (response as? HTTPURLResponse)?.statusCode == 200,
              let data,
              let root = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
              let companion = root["companion"] as? [String: Any],
              companion["connected"] as? Bool == true,
              companion["version"] != nil else {
            return false
        }
        return true
    }

    private func rememberedCompanionPort() -> Int {
        let value = defaults.integer(forKey: companionPortDefaultsKey)
        return (defaultCompanionPort...maximumCompanionPort).contains(value) ? value : defaultCompanionPort
    }

    private func dashboardURL(for port: Int) -> URL {
        URL(string: "http://127.0.0.1:\(port)/")!
    }

    private func selectCompanionPort() -> Int? {
        let preferred: [Int] = [companionPort, defaults.integer(forKey: companionPortDefaultsKey), defaultCompanionPort]
        var seen = Set<Int>()
        for port in preferred where (defaultCompanionPort...maximumCompanionPort).contains(port) && seen.insert(port).inserted {
            if companionResponding(at: port) {
                return port
            }
        }
        for port in defaultCompanionPort...maximumCompanionPort where isPortAvailable(port) {
            return port
        }
        return nil
    }

    private func isPortAvailable(_ port: Int) -> Bool {
        let descriptor = socket(AF_INET, SOCK_STREAM, 0)
        guard descriptor >= 0 else { return false }
        defer { close(descriptor) }
        var address = sockaddr_in()
        address.sin_len = UInt8(MemoryLayout<sockaddr_in>.size)
        address.sin_family = sa_family_t(AF_INET)
        address.sin_port = in_port_t(port).bigEndian
        address.sin_addr = in_addr(s_addr: inet_addr("127.0.0.1"))
        return withUnsafePointer(to: &address) {
            $0.withMemoryRebound(to: sockaddr.self, capacity: 1) {
                Darwin.bind(descriptor, $0, socklen_t(MemoryLayout<sockaddr_in>.size)) == 0
            }
        }
    }

    private func scheduleRestart() {
        restartWorkItem?.cancel()
        let work = DispatchWorkItem { [weak self] in
            guard let self, !self.isTerminating else { return }
            self.ensureCompanion()
            self.refreshStatus()
        }
        restartWorkItem = work
        DispatchQueue.main.asyncAfter(deadline: .now() + 2, execute: work)
    }

    private func openWhenReady(_ url: URL) {
        pendingDashboardURL = url
        readinessAttempts = 0
        probeCompanion()
    }

    private func probeCompanion() {
        var request = URLRequest(url: dashboardBase.appendingPathComponent("v1/status"))
        request.timeoutInterval = 0.8
        URLSession.shared.dataTask(with: request) { [weak self] data, response, _ in
            DispatchQueue.main.async {
                guard let self else { return }
                if self.isCompanionStatus(data: data, response: response) {
                    let target = self.pendingDashboardURL ?? self.dashboardBase
                    self.pendingDashboardURL = nil
                    NSWorkspace.shared.open(target)
                    self.refreshStatus()
                    return
                }
                if self.readinessAttempts == 0 {
                    self.ensureCompanion()
                }
                self.readinessAttempts += 1
                if self.readinessAttempts < 80 {
                    DispatchQueue.main.asyncAfter(deadline: .now() + 0.25) { self.probeCompanion() }
                } else {
                    self.presentError("本地 Companion 服务未能在 20 秒内启动，请打开诊断日志。")
                }
            }
        }.resume()
    }

    private func refreshStatus() {
        var request = URLRequest(url: dashboardBase.appendingPathComponent("v1/status"))
        request.timeoutInterval = 1.5
        URLSession.shared.dataTask(with: request) { [weak self] data, response, _ in
            guard let self else { return }
            guard let data,
                  self.isCompanionStatus(data: data, response: response),
                  let root = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else {
                DispatchQueue.main.async {
                    self.markServiceUnavailable()
                    self.ensureCompanion()
                }
                return
            }
            let board = root["board"] as? [String: Any]
            let codex = root["codex"] as? [String: Any]
            let boardConnected = board?["connected"] as? Bool ?? false
            let codexBound = codex?["bound"] as? Bool ?? false
            let codexTrusted = codex?["trusted"] as? Bool ?? false
            DispatchQueue.main.async {
                self.boardStatusItem.title = boardConnected ? "板子：已连接" : "板子：等待连接"
                self.codexStatusItem.title = codexBound && codexTrusted ? "Codex：已连接" : (codexBound ? "Codex：等待信任" : "Codex：未绑定")
                self.statusItem.button?.image = NSImage(
                    systemSymbolName: boardConnected ? "pawprint.fill" : "pawprint",
                    accessibilityDescription: "VibeBoard Companion"
                )
            }
        }.resume()
    }

    private func markServiceUnavailable() {
        boardStatusItem.title = "板子：Companion 未运行"
        codexStatusItem.title = "Codex：Companion 未运行"
        statusItem.button?.image = NSImage(systemSymbolName: "exclamationmark.circle", accessibilityDescription: "Companion 未运行")
    }

    private func publicOrigin() -> String? {
        guard let url = publicSiteURL, var components = URLComponents(url: url, resolvingAgainstBaseURL: false) else { return nil }
        components.path = ""
        components.query = nil
        components.fragment = nil
        return components.string?.trimmingCharacters(in: CharacterSet(charactersIn: "/"))
    }

    private func localDashboardURL(for source: URL) -> URL? {
        if source.scheme == "vibeboard", source.host == "companion", source.path == "/open" {
            return dashboardBase
        }
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

    @objc private func openDashboard() {
        openWhenReady(dashboardBase)
    }

    @objc private func openPetGallery() {
        if let publicSiteURL {
            NSWorkspace.shared.open(publicSiteURL)
        } else {
            openDashboard()
        }
    }

    @objc private func openSetup() {
        var components = URLComponents(url: dashboardBase, resolvingAgainstBaseURL: false)!
        components.queryItems = [URLQueryItem(name: "setup", value: "1")]
        openWhenReady(components.url ?? dashboardBase)
    }

    @objc private func openLog() {
        if !FileManager.default.fileExists(atPath: logURL.path) {
            prepareLog()
        }
        NSWorkspace.shared.activateFileViewerSelecting([logURL])
    }

    @objc private func toggleLoginItem() {
        setLoginEnabled(SMAppService.mainApp.status != .enabled, reportError: true)
    }

    private func setLoginEnabled(_ enabled: Bool, reportError: Bool) {
        do {
            if enabled, SMAppService.mainApp.status != .enabled {
                try SMAppService.mainApp.register()
            } else if !enabled, SMAppService.mainApp.status == .enabled {
                try SMAppService.mainApp.unregister()
            }
        } catch {
            if reportError {
                presentError("无法更新登录启动设置：\(error.localizedDescription)")
            }
        }
        updateLoginItem()
    }

    private func updateLoginItem() {
        loginItem.state = SMAppService.mainApp.status == .enabled ? .on : .off
    }

    @objc private func checkForUpdatesFromMenu() {
        checkForUpdates(reportCurrent: true)
    }

    private func checkForUpdates(reportCurrent: Bool) {
        guard let updateManifestURL else { return }
        var request = URLRequest(url: updateManifestURL)
        request.timeoutInterval = 10
        URLSession.shared.dataTask(with: request) { data, response, error in
            var message: String?
            var downloadURL: URL?
            if error != nil || (response as? HTTPURLResponse)?.statusCode != 200 || data == nil {
                message = "无法检查更新，请稍后重试。"
            } else if let data,
                      let manifest = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
                      let version = manifest["version"] as? String,
                      let urlText = manifest["downloadURL"] as? String,
                      let url = URL(string: urlText), url.scheme == "https" {
                let current = Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "0"
                if current.compare(version, options: .numeric) == .orderedAscending {
                    message = "VibeBoard Companion \(version) 已发布。"
                    downloadURL = url
                } else if reportCurrent {
                    message = "当前已是最新版本。"
                }
            } else {
                message = "更新信息格式无效。"
            }
            guard let message else { return }
            DispatchQueue.main.async {
                let alert = NSAlert()
                alert.messageText = "VibeBoard Companion"
                alert.informativeText = message
                if downloadURL != nil {
                    alert.addButton(withTitle: "下载")
                    alert.addButton(withTitle: "稍后")
                } else {
                    alert.addButton(withTitle: "好")
                }
                if alert.runModal() == .alertFirstButtonReturn, let downloadURL {
                    NSWorkspace.shared.open(downloadURL)
                }
            }
        }.resume()
    }

    @objc private func quit() {
        NSApp.terminate(nil)
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
