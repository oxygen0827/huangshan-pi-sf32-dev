import Foundation
import Testing
@testable import VibeBoardBLE

@Test func buildsInstallCommandsInStableOrder() throws {
    let package = try RuntimePackage(
        appId: "clock_test",
        files: [
            "main.lua": Data("print('hello')".utf8),
            "manifest.json": Data("{\"id\":\"clock_test\"}".utf8)
        ]
    )

    let commands = package.installCommands(chunkBytes: 16)

    #expect(commands.first == "vb_runtime_install_begin clock_test")
    #expect(commands.last == "vb_runtime_install_end clock_test")
    #expect(commands.contains("vb_runtime_install_file clock_test main.lua 0 7072696e74282768656c6c6f2729"))
    #expect(commands.contains("vb_runtime_install_file clock_test manifest.json 0 7b226964223a22636c6f636b5f746573"))
    #expect(commands.contains("vb_runtime_install_file clock_test manifest.json 16 74227d"))
}

@Test func validatesAppIdAndPaths() throws {
    #expect(RuntimePackage.isSafeAppId("status_test"))
    #expect(!RuntimePackage.isSafeAppId("StatusTest"))
    #expect(!RuntimePackage.isSafeAppId("a_very_long_app_id"))

    #expect(RuntimePackage.isSafePath("assets/icon.png"))
    #expect(RuntimePackage.isSafePath("main.lua"))
    #expect(!RuntimePackage.isSafePath("../main.lua"))
    #expect(!RuntimePackage.isSafePath("/main.lua"))
}

@Test func rejectsIncompletePackages() throws {
    #expect(throws: RuntimePackageError.missingMainLua) {
        _ = try RuntimePackage(appId: "bad", files: ["manifest.json": Data()])
    }

    #expect(throws: RuntimePackageError.missingManifest) {
        _ = try RuntimePackage(appId: "bad", files: ["main.lua": Data()])
    }
}

@Test func loadsPackageFromDirectoryUsingManifestId() throws {
    let root = URL(fileURLWithPath: NSTemporaryDirectory())
        .appendingPathComponent("vibeboard-package-\(UUID().uuidString)", isDirectory: true)
    try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
    defer { try? FileManager.default.removeItem(at: root) }

    let assets = root.appendingPathComponent("assets", isDirectory: true)
    try FileManager.default.createDirectory(at: assets, withIntermediateDirectories: true)
    try Data("{\"id\":\"phone_app\"}".utf8).write(to: root.appendingPathComponent("manifest.json"))
    try Data("print('phone')".utf8).write(to: root.appendingPathComponent("main.lua"))
    try Data("asset".utf8).write(to: assets.appendingPathComponent("note.txt"))

    let package = try RuntimePackage.fromDirectory(root)

    #expect(package.appId == "phone_app")
    #expect(package.files.keys.sorted() == ["assets/note.txt", "main.lua", "manifest.json"])
}
