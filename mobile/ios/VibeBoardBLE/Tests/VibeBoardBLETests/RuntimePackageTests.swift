import Foundation
import Testing
@testable import VibeBoardBLE

@Test func exposesRuntimeTransportDefaults() {
    #expect(VibeBoardRuntimeDefaults.bleAppPageLimit == 1)
    #expect(VibeBoardRuntimeDefaults.dataChunkBytes == 160)
    #expect(VibeBoardRuntimeDefaults.voiceChunkBytes == 200)
    #expect(VibeBoardRuntimeDefaults.installChunkBytes == 48)
    #expect(VibeBoardRuntimeDefaults.maxInstallChunkBytes == 240)
}

private func runtimeManifest(_ appId: String, components: String = "[]") -> Data {
    Data("""
    {"schemaVersion":1,"kind":"huangshan-runtime-app-manifest","id":"\(appId)","entry":"main.lua","components":\(components)}
    """.utf8)
}

@Test func buildsInstallCommandsInStableOrder() throws {
    let package = try RuntimePackage(
        appId: "clock_test",
        files: [
            "main.lua": Data("print('hello')".utf8),
            "manifest.json": runtimeManifest("clock_test")
        ]
    )

    let commands = package.installCommands(chunkBytes: 16)

    #expect(commands.first == "vb_runtime_install_begin clock_test")
    #expect(commands.last == "vb_runtime_install_end clock_test")
    #expect(commands.contains("vb_runtime_install_file clock_test main.lua 0 7072696e74282768656c6c6f2729"))
    #expect(commands.contains { $0.hasPrefix("vb_runtime_install_file clock_test manifest.json 0 ") })
    #expect(commands.contains { $0.hasPrefix("vb_runtime_install_file clock_test manifest.json 16 ") })
}

@Test func injectsManifestIntegrityBeforeInstall() throws {
    let mainLua = Data("print('ok')\n".utf8)
    let asset = Data("asset".utf8)
    let package = try RuntimePackage(
        appId: "hash_test",
        files: [
            "main.lua": mainLua,
            "manifest.json": runtimeManifest("hash_test"),
            "assets/note.txt": asset
        ]
    )

    guard let manifestData = package.files["manifest.json"],
          let object = try JSONSerialization.jsonObject(with: manifestData) as? [String: Any],
          let entries = object["files"] as? [[String: Any]],
          let integrity = object["integrity"] as? [String: Any] else {
        #expect(Bool(false), "Expected generated manifest integrity fields")
        return
    }

    let byPath = Dictionary(uniqueKeysWithValues: entries.compactMap { entry -> (String, [String: Any])? in
        guard let path = entry["path"] as? String else { return nil }
        return (path, entry)
    })

    #expect(byPath["manifest.json"] == nil)
    #expect(byPath["main.lua"]?["size"] as? Int == mainLua.count)
    #expect(byPath["assets/note.txt"]?["size"] as? Int == asset.count)
    #expect((byPath["main.lua"]?["sha256"] as? String)?.count == 64)
    #expect(integrity["algorithm"] as? String == "sha256")
    #expect((integrity["filesDigest"] as? String)?.count == 64)
}

@Test func validatesAppIdAndPaths() throws {
    #expect(RuntimePackage.isSafeAppId("status_test"))
    #expect(!RuntimePackage.isSafeAppId("StatusTest"))
    #expect(!RuntimePackage.isSafeAppId("a_very_long_app_id"))

    #expect(RuntimePackage.isSafePath("assets/icon.png"))
    #expect(RuntimePackage.isSafePath("assets/rocky/idle0.rle"))
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

@Test func rejectsInvalidRuntimeManifestsBeforeInstall() throws {
    do {
        _ = try RuntimePackage(
            appId: "bad",
            files: [
                "main.lua": Data("print('bad')".utf8),
                "manifest.json": Data("{\"id\":\"bad\"}".utf8)
            ]
        )
        #expect(Bool(false), "Expected missing schema/kind to throw")
    } catch RuntimePackageError.invalidManifest(let message) {
        #expect(message.contains("schemaVersion"))
    } catch {
        #expect(Bool(false), "Expected invalidManifest, got \(error)")
    }

    do {
        _ = try RuntimePackage(
            appId: "bad",
            files: [
                "main.lua": Data("print('bad')".utf8),
                "manifest.json": runtimeManifest("other_app")
            ]
        )
        #expect(Bool(false), "Expected mismatched manifest id to throw")
    } catch RuntimePackageError.invalidManifest(let message) {
        #expect(message.contains("id"))
    } catch {
        #expect(Bool(false), "Expected invalidManifest, got \(error)")
    }

    do {
        _ = try RuntimePackage(
            appId: "bad",
            files: [
                "main.lua": Data("print('bad')".utf8),
                "manifest.json": runtimeManifest("bad", components: #"[{"type":"action","capability":"reload"}]"#)
            ]
        )
        #expect(Bool(false), "Expected incomplete action component to throw")
    } catch RuntimePackageError.invalidManifest(let message) {
        #expect(message.contains("label"))
    } catch {
        #expect(Bool(false), "Expected invalidManifest, got \(error)")
    }
}

@Test func validatesHuangshanRuntimeProfileBeforeInstall() throws {
    let manifest = Data(#"{"schemaVersion":1,"kind":"huangshan-runtime-app-manifest","id":"phone_app","entry":"main.lua","runtimeProfile":"huangshan-pi","capabilities":["display","ble","flow.latest"],"components":[]}"#.utf8)
    let package = try RuntimePackage(appId: "phone_app", files: [
        "main.lua": Data("print('ok')".utf8),
        "manifest.json": manifest
    ])

    #expect(package.appId == "phone_app")
}

@Test func acceptsRuntimeAppLibraryMetadataBeforeInstall() throws {
    let manifest = Data(#"{"schemaVersion":1,"kind":"huangshan-runtime-app-manifest","id":"gallery_app","entry":"main.lua","category":"Games","icon":"gamepad-2","author":"Huangshan Runtime Team","screenshot":"assets/preview.png","requirements":["Runtime","Touch gestures"],"components":[]}"#.utf8)
    let package = try RuntimePackage(appId: "gallery_app", files: [
        "main.lua": Data("print('ok')".utf8),
        "manifest.json": manifest,
        "assets/preview.png": Data([0x89, 0x50, 0x4e, 0x47])
    ])

    #expect(package.appId == "gallery_app")
    #expect(package.files.keys.contains("assets/preview.png"))
}

@Test func rejectsMalformedRuntimeAppLibraryMetadataBeforeInstall() throws {
    let manifests = [
        (#"{"schemaVersion":1,"kind":"huangshan-runtime-app-manifest","id":"bad","entry":"main.lua","category":7,"components":[]}"#, "category"),
        (#"{"schemaVersion":1,"kind":"huangshan-runtime-app-manifest","id":"bad","entry":"main.lua","screenshot":"../preview.png","components":[]}"#, "screenshot"),
        (#"{"schemaVersion":1,"kind":"huangshan-runtime-app-manifest","id":"bad","entry":"main.lua","requirements":"Runtime","components":[]}"#, "requirements"),
        (#"{"schemaVersion":1,"kind":"huangshan-runtime-app-manifest","id":"bad","entry":"main.lua","requirements":["wifi"],"components":[]}"#, "BLE/serial"),
    ]

    for (manifest, expected) in manifests {
        do {
            _ = try RuntimePackage(appId: "bad", files: [
                "main.lua": Data("print('bad')".utf8),
                "manifest.json": Data(manifest.utf8)
            ])
            #expect(Bool(false), "Expected metadata manifest to throw")
        } catch RuntimePackageError.invalidManifest(let message) {
            #expect(message.contains(expected))
        } catch {
            #expect(Bool(false), "Expected invalidManifest, got \(error)")
        }
    }
}

@Test func rejectsNonIntegerManifestVersionBeforeInstall() throws {
    do {
        _ = try RuntimePackage(appId: "bad", files: [
            "main.lua": Data("print('bad')".utf8),
            "manifest.json": Data(#"{"schemaVersion":1.5,"kind":"huangshan-runtime-app-manifest","id":"bad","entry":"main.lua","components":[]}"#.utf8)
        ])
        #expect(Bool(false), "Expected non-integer schemaVersion to throw")
    } catch RuntimePackageError.invalidManifest(let message) {
        #expect(message.contains("schemaVersion"))
    } catch {
        #expect(Bool(false), "Expected invalidManifest, got \(error)")
    }
}

@Test func rejectsESP32NativeCapabilitiesBeforeInstall() throws {
    let cases = [
        (#"{"schemaVersion":1,"kind":"huangshan-runtime-app-manifest","id":"bad","entry":"main.lua","runtimeProfile":"esp32","components":[]}"#, "not compatible"),
        (#"{"schemaVersion":1,"kind":"huangshan-runtime-app-manifest","id":"bad","entry":"main.lua","capabilities":["wifi"],"components":[]}"#, "BLE/serial"),
        (#"{"schemaVersion":1,"kind":"huangshan-runtime-app-manifest","id":"bad","entry":"main.lua","requires":["http.client"],"components":[]}"#, "BLE/serial"),
        (#"{"schemaVersion":1,"kind":"huangshan-runtime-app-manifest","id":"bad","entry":"main.lua","permissions":["camera"],"components":[]}"#, "BLE/serial"),
        (#"{"schemaVersion":1,"kind":"huangshan-runtime-app-manifest","id":"bad","entry":"main.lua","capabilities":["native"],"components":[]}"#, "BLE/serial"),
        (#"{"schemaVersion":1,"kind":"huangshan-runtime-app-manifest","id":"bad","entry":"main.lua","requires":["nes"],"components":[]}"#, "BLE/serial"),
        (#"{"schemaVersion":1,"kind":"huangshan-runtime-app-manifest","id":"bad","entry":"main.lua","permissions":["gamepad"],"components":[]}"#, "BLE/serial"),
        (#"{"schemaVersion":1,"kind":"huangshan-runtime-app-manifest","id":"bad","entry":"main.lua","capabilities":["i2s.audio"],"components":[]}"#, "BLE/serial"),
        (#"{"schemaVersion":1,"kind":"huangshan-runtime-app-manifest","id":"bad","entry":"main.lua","capabilities":["esp32.psram"],"components":[]}"#, "not supported by Huangshan Runtime profile"),
    ]

    for (manifest, expected) in cases {
        do {
            _ = try RuntimePackage(appId: "bad", files: [
                "main.lua": Data("print('bad')".utf8),
                "manifest.json": Data(manifest.utf8)
            ])
            #expect(Bool(false), "Expected manifest to throw")
        } catch RuntimePackageError.invalidManifest(let message) {
            #expect(message.contains(expected))
        } catch {
            #expect(Bool(false), "Expected invalidManifest, got \(error)")
        }
    }
}

@Test func rejectsMalformedCapabilityListsBeforeInstall() throws {
    let manifests = [
        (#"{"schemaVersion":1,"kind":"huangshan-runtime-app-manifest","id":"bad","entry":"main.lua","capabilities":"display","components":[]}"#, "must be a list"),
        (#"{"schemaVersion":1,"kind":"huangshan-runtime-app-manifest","id":"bad","entry":"main.lua","capabilities":[7],"components":[]}"#, "must be a capability string"),
    ]

    for (manifest, expected) in manifests {
        do {
            _ = try RuntimePackage(appId: "bad", files: [
                "main.lua": Data("print('bad')".utf8),
                "manifest.json": Data(manifest.utf8)
            ])
            #expect(Bool(false), "Expected manifest to throw")
        } catch RuntimePackageError.invalidManifest(let message) {
            #expect(message.contains(expected))
        } catch {
            #expect(Bool(false), "Expected invalidManifest, got \(error)")
        }
    }
}

@Test func rejectsUnsupportedManifestComponentsBeforeInstall() throws {
    do {
        _ = try RuntimePackage(appId: "bad", files: [
            "main.lua": Data("print('bad')".utf8),
            "manifest.json": runtimeManifest("bad", components: #"[{"type":"status","capability":"wifi.rssi","label":"Wi-Fi"}]"#)
        ])
        #expect(Bool(false), "Expected unsupported capability to throw")
    } catch RuntimePackageError.invalidManifest(let message) {
        #expect(message.contains("capability"))
        #expect(message.contains("wifi.rssi"))
    } catch {
        #expect(Bool(false), "Expected invalidManifest, got \(error)")
    }

    do {
        _ = try RuntimePackage(appId: "bad", files: [
            "main.lua": Data("print('bad')".utf8),
            "manifest.json": runtimeManifest("bad", components: #"[{"type":"label","text":"Bad","color":"blue"}]"#)
        ])
        #expect(Bool(false), "Expected bad label color to throw")
    } catch RuntimePackageError.invalidManifest(let message) {
        #expect(message.contains("#RRGGBB"))
    } catch {
        #expect(Bool(false), "Expected invalidManifest, got \(error)")
    }

    do {
        _ = try RuntimePackage(appId: "bad", files: [
            "main.lua": Data("print('bad')".utf8),
            "manifest.json": runtimeManifest("bad", components: #"[{"type":"chart","text":"Bad"}]"#)
        ])
        #expect(Bool(false), "Expected unsupported component type to throw")
    } catch RuntimePackageError.invalidManifest(let message) {
        #expect(message.contains("type"))
        #expect(message.contains("chart"))
    } catch {
        #expect(Bool(false), "Expected invalidManifest, got \(error)")
    }


    do {
        _ = try RuntimePackage(appId: "bad", files: [
            "main.lua": Data("print('bad')".utf8),
            "manifest.json": runtimeManifest("bad", components: #"[{"type":7,"capability":"status","label":"Status"}]"#)
        ])
        #expect(Bool(false), "Expected non-string component type to throw")
    } catch RuntimePackageError.invalidManifest(let message) {
        #expect(message.contains("type"))
        #expect(message.contains("string"))
    } catch {
        #expect(Bool(false), "Expected invalidManifest, got \(error)")
    }

    do {
        _ = try RuntimePackage(appId: "bad", files: [
            "main.lua": Data("print('bad')".utf8),
            "manifest.json": runtimeManifest("bad", components: #"[{"type":"label","text":"Bad","x":1.5}]"#)
        ])
        #expect(Bool(false), "Expected non-integer label x to throw")
    } catch RuntimePackageError.invalidManifest(let message) {
        #expect(message.contains("x"))
        #expect(message.contains("integer"))
    } catch {
        #expect(Bool(false), "Expected invalidManifest, got \(error)")
    }
}

@Test func acceptsPowerManifestAndLuaHelperBeforeInstall() throws {
    let package = try RuntimePackage(appId: "pwr_app", files: [
        "main.lua": Data("local l = lv_label_create(lv_scr_act())\nvibe_power_label(l, 'charger')\n".utf8),
        "manifest.json": runtimeManifest(
            "pwr_app",
            components: #"[{"type":"status","capability":"power.charger","label":"Charger"}]"#
        )
    ])

    #expect(package.appId == "pwr_app")
}


@Test func acceptsDisplayManifestAndLuaHelperBeforeInstall() throws {
    let package = try RuntimePackage(appId: "disp_app", files: [
        "main.lua": Data("local l = lv_label_create(lv_scr_act())\nvibe_display_brightness('70')\nvibe_display_label(l, 'brightness')\n".utf8),
        "manifest.json": runtimeManifest(
            "disp_app",
            components: #"[{"type":"status","capability":"display.brightness","label":"Brightness"}]"#
        )
    ])

    #expect(package.appId == "disp_app")
}

@Test func acceptsVoiceManifestAndLuaHelperBeforeInstall() throws {
    let package = try RuntimePackage(appId: "voice_app", files: [
        "main.lua": Data("local l = lv_label_create(lv_scr_act())\nvibe_voice_start('600')\nvibe_voice_clear()\nvibe_voice_label(l, 'ready')\n".utf8),
        "manifest.json": runtimeManifest(
            "voice_app",
            components: #"[{"type":"status","capability":"voice.ready","label":"Voice"},{"type":"action","capability":"voice.start","label":"Record","value":"600"},{"type":"action","capability":"voice.clear","label":"Clear"}]"#
        )
    ])

    #expect(package.appId == "voice_app")
}

@Test func acceptsFlowManifestAndLuaHelperBeforeInstall() throws {
    let package = try RuntimePackage(appId: "flow_app", files: [
        "main.lua": Data("local l = lv_label_create(lv_scr_act())\nvibe_flow_label(l, 'latest')\n".utf8),
        "manifest.json": runtimeManifest(
            "flow_app",
            components: #"[{"type":"status","capability":"flow.latest","label":"Flow"}]"#
        )
    ])

    #expect(package.appId == "flow_app")
}

@Test func acceptsPeerPagerManifestAndLuaHelpersBeforeInstall() throws {
    let package = try RuntimePackage(appId: "pager", files: [
        "main.lua": Data("vibe_peer_pager()\n".utf8),
        "manifest.json": Data(#"{"schemaVersion":1,"kind":"huangshan-runtime-app-manifest","id":"pager","entry":"main.lua","runtimeProfile":"huangshan-pi","capabilities":["peer","peer.status","peer.messages","peer.send","peer.pair"],"components":[]}"#.utf8)
    ])

    #expect(package.appId == "pager")
}

@Test func acceptsFullLuaAndRejectsDisabledCapabilitiesBeforeInstall() throws {
    let package = try RuntimePackage(appId: "full_lua", files: [
        "main.lua": Data("local total=0\nfor i=1,3 do total=total+i end\nlocal m=require('lib.demo')\nprint(total,m.value)\n".utf8),
        "lib/demo.lua": Data("return {value=42}\n".utf8),
        "manifest.json": Data(#"{"schemaVersion":1,"kind":"huangshan-runtime-app-manifest","id":"full_lua","entry":"main.lua","runtimeProfile":"huangshan-pi","capabilities":["lua.full"],"components":[]}"#.utf8)
    ])
    #expect(package.appId == "full_lua")

    do {
        _ = try RuntimePackage(appId: "bad", files: [
            "main.lua": Data("wifi_start()\n".utf8),
            "app.info": Data("bad".utf8)
        ])
        #expect(Bool(false), "Expected forbidden Runtime call to throw")
    } catch RuntimePackageError.invalidManifest(let message) {
        #expect(message.contains("forbidden Runtime Lua function"))
    } catch {
        #expect(Bool(false), "Expected invalidManifest, got \(error)")
    }

    do {
        _ = try RuntimePackage(appId: "bad", files: [
            "main.lua": Data("os.execute('bad')\n".utf8),
            "app.info": Data("bad".utf8)
        ])
        #expect(Bool(false), "Expected disabled standard library to throw")
    } catch RuntimePackageError.invalidManifest(let message) {
        #expect(message.contains("standard library disabled"))
    } catch {
        #expect(Bool(false), "Expected invalidManifest, got \(error)")
    }
}

@Test func describesRuntimePackageErrorsForImportUI() throws {
    #expect(RuntimePackageError.missingMainLua.localizedDescription == "Runtime package must include main.lua")
    #expect(RuntimePackageError.invalidManifest("manifest.json id must match app id demo").localizedDescription == "manifest.json id must match app id demo")
    #expect(RuntimePackageError.unsafePath("../main.lua").localizedDescription.contains("../main.lua"))
}

@Test func loadsPackageFromDirectoryUsingManifestId() throws {
    let root = URL(fileURLWithPath: NSTemporaryDirectory())
        .appendingPathComponent("vibeboard-package-\(UUID().uuidString)", isDirectory: true)
    try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
    defer { try? FileManager.default.removeItem(at: root) }

    let assets = root.appendingPathComponent("assets", isDirectory: true)
    try FileManager.default.createDirectory(at: assets, withIntermediateDirectories: true)
    try runtimeManifest("phone_app").write(to: root.appendingPathComponent("manifest.json"))
    try Data("print('phone')".utf8).write(to: root.appendingPathComponent("main.lua"))
    try Data("asset".utf8).write(to: assets.appendingPathComponent("note.txt"))

    let package = try RuntimePackage.fromDirectory(root)

    #expect(package.appId == "phone_app")
    #expect(package.files.keys.sorted() == ["assets/note.txt", "main.lua", "manifest.json"])
}

@Test func rejectsDuplicateDirectoryPathsAfterNormalization() throws {
    let root = URL(fileURLWithPath: NSTemporaryDirectory())
        .appendingPathComponent("vibeboard-duplicate-package-\(UUID().uuidString)", isDirectory: true)
    try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
    defer { try? FileManager.default.removeItem(at: root) }

    let assets = root.appendingPathComponent("assets", isDirectory: true)
    try FileManager.default.createDirectory(at: assets, withIntermediateDirectories: true)
    try runtimeManifest("phone_app").write(to: root.appendingPathComponent("manifest.json"))
    try Data("print('phone')".utf8).write(to: root.appendingPathComponent("main.lua"))
    try Data("one".utf8).write(to: assets.appendingPathComponent("icon.png"))
    try Data("two".utf8).write(to: root.appendingPathComponent("assets\\icon.png"))

    do {
        _ = try RuntimePackage.fromDirectory(root)
        #expect(Bool(false), "Expected duplicate normalized path to throw")
    } catch RuntimePackageError.unsafePath(let message) {
        #expect(message.contains("duplicate package path"))
        #expect(message.contains("assets/icon.png"))
    } catch {
        #expect(Bool(false), "Expected unsafePath, got \(error)")
    }
}

@Test func buildsInfoFlowCommands() throws {
    let frame = try VibeBoardInfoFlowFrame(channel: "phone.feed", sequence: 42, payload: "hello")

    #expect(frame.command == "flow_send phone.feed 42 68656c6c6f")
}

@Test func buildsInfoFlowCommandsWithUTF8Payload() throws {
    let frame = try VibeBoardInfoFlowFrame(channel: "phone", sequence: 7, payload: "你好")

    #expect(frame.command == "flow_send phone 7 e4bda0e5a5bd")
}

@Test func buildsEmptyInfoFlowPayloadCommand() throws {
    let frame = try VibeBoardInfoFlowFrame(channel: "phone", sequence: 1, payload: "")

    #expect(frame.command == "flow_send phone 1 -")
}

@Test func validatesInfoFlowChannels() throws {
    #expect(VibeBoardInfoFlowFrame.isSafeChannel("phone"))
    #expect(VibeBoardInfoFlowFrame.isSafeChannel("phone.feed-1"))
    #expect(!VibeBoardInfoFlowFrame.isSafeChannel("phone feed"))
    #expect(!VibeBoardInfoFlowFrame.isSafeChannel("../phone"))
}

@Test func rejectsOversizedInfoFlowPayloads() throws {
    do {
        _ = try VibeBoardInfoFlowFrame(channel: "phone", sequence: 1, payload: String(repeating: "a", count: 193))
        #expect(Bool(false), "Expected oversized payload to throw")
    } catch VibeBoardBLEError.commandFailed(let message) {
        #expect(message == "info flow payload exceeds 192 bytes")
    } catch {
        #expect(Bool(false), "Expected commandFailed, got \(error)")
    }
}

@Test func decodesRuntimeCapabilities() throws {
    let json = """
    {"api":"vibeboard-huangshan-capabilities/v1","rt":"vibeboard-huangshan-runtime/v1","ble":"vibeboard-huangshan-ble-install/v1","sens":"vibeboard-huangshan-sensors/v1","touch":"vibeboard-huangshan-touch/v1","flow":"vibeboard-huangshan-info-flow/v1","voice":"vibeboard-huangshan-voice-bridge/v1","pwr":"vibeboard-huangshan-power/v1","disp":"vibeboard-huangshan-display/v1","gpio":"vibeboard-huangshan-gpio/v1","rgb":"vibeboard-huangshan-rgb/v1","fs":1,"ins":{"ser":1,"ble":1,"max":240},"app":{"lua":"script-subset","comp":8},"hw":{"disp":1,"touch":1,"sens":1,"voice":1,"flow":1,"batt":1,"chg":1,"gpio":1,"rgb":1}}
    """

    let capabilities = try JSONDecoder().decode(VibeBoardRuntimeCapabilities.self, from: Data(json.utf8))

    #expect(capabilities.api == "vibeboard-huangshan-capabilities/v1")
    #expect(capabilities.ins.max == 240)
    #expect(capabilities.ins.stage == 0)
    #expect(capabilities.app.lua == "script-subset")
    #expect(capabilities.app.manifest == 1)
    #expect(capabilities.app.assets == 1)
    #expect(capabilities.hw.voice == 1)
    #expect(capabilities.hw.batt == 1)
    #expect(capabilities.hw.chg == 1)
    #expect(capabilities.hw.rgb == 1)
    #expect(capabilities.hw.gpio == 1)
    #expect(capabilities.pwr == "vibeboard-huangshan-power/v1")
    #expect(capabilities.disp == "vibeboard-huangshan-display/v1")
    #expect(capabilities.gpio == "vibeboard-huangshan-gpio/v1")
    #expect(capabilities.touch == "vibeboard-huangshan-touch/v1")
    #expect(capabilities.rgb == "vibeboard-huangshan-rgb/v1")
    #expect(capabilities.summary.contains("lua=script-subset"))
    #expect(capabilities.summary.contains("chg:1"))
    #expect(capabilities.summary.contains("display_api=vibeboard-huangshan-display/v1"))
    #expect(capabilities.summary.contains("gpio:1"))
    #expect(capabilities.summary.contains("rgb:1"))
}

@Test func decodesRuntimePowerSnapshot() throws {
    let json = #"""
    {"api":"vibeboard-huangshan-power/v1","available":1,"ready":1,"battery":{"ok":1,"mv":4350,"raw":43504,"dev":"bat1","ch":7},"charger":{"ok":1,"state":2,"status":"charging","det":1,"en":1,"sys":24,"fault":0}}
    """#
    let snapshot = try JSONDecoder().decode(VibeBoardPowerSnapshot.self, from: Data(json.utf8))

    #expect(snapshot.api == "vibeboard-huangshan-power/v1")
    #expect(snapshot.available == 1)
    #expect(snapshot.ready == 1)
    #expect(snapshot.battery.ok == 1)
    #expect(snapshot.battery.mv == 4350)
    #expect(snapshot.battery.raw == 43504)
    #expect(snapshot.battery.dev == "bat1")
    #expect(snapshot.battery.ch == 7)
    #expect(snapshot.charger.status == "charging")
    #expect(snapshot.charger.state == 2)
    #expect(snapshot.charger.det == 1)
    #expect(snapshot.charger.en == 1)
    #expect(snapshot.charger.sys == 24)
    #expect(snapshot.charger.fault == 0)
    #expect(snapshot.summary.contains("battery=4350mV"))
    #expect(snapshot.summary.contains("charger=charging"))
}



@Test func decodesRuntimeDisplaySnapshot() throws {
    let json = #"""
    {"api":"vibeboard-huangshan-display/v1","available":1,"ready":1,"ok":1,"dev":"lcd","width":390,"height":450,"bpp":16,"format":1,"align":4,"brightness":70,"state":4,"state_name":"on"}
    """#
    let snapshot = try JSONDecoder().decode(VibeBoardDisplaySnapshot.self, from: Data(json.utf8))

    #expect(snapshot.api == "vibeboard-huangshan-display/v1")
    #expect(snapshot.available == 1)
    #expect(snapshot.ready == 1)
    #expect(snapshot.ok == 1)
    #expect(snapshot.dev == "lcd")
    #expect(snapshot.width == 390)
    #expect(snapshot.height == 450)
    #expect(snapshot.bpp == 16)
    #expect(snapshot.brightness == 70)
    #expect(snapshot.stateName == "on")
    #expect(snapshot.summary.contains("display=390x450 70% on"))
}

@Test func decodesRuntimeAppManagerStatusAndList() throws {
    let statusJSON = #"{"api":"vibeboard-huangshan-app-manager/v1","active":"flow_stage","state":"idle","running":0,"failed":0,"last_status":0,"last_error":"stopped","launches":2,"stops":1,"pending_reload":0,"pending_stop":0,"launcher_page":0,"launcher_total":17,"launcher_count":5,"pending_delete":"","lua":"script-subset"}"#
    let status = try JSONDecoder().decode(VibeBoardRuntimeAppStatus.self, from: Data(statusJSON.utf8))

    #expect(status.api == "vibeboard-huangshan-app-manager/v1")
    #expect(status.active == "flow_stage")
    #expect(status.state == "idle")
    #expect(status.launcherTotal == 17)
    #expect(status.summary.contains("active=flow_stage"))

    let listJSON = #"{"api":"vibeboard-huangshan-app-manager/v1","active":"flow_stage","state":"idle","apps":[{"id":"flow_stage","name":"Flow Stage","description":"Runtime info flow status stage.","category":"Bridge","icon":"radio","author":"Huangshan Runtime Team","screenshot":"generated:flow_stage","requirements":["Runtime","Flow API"],"active":1,"compatible":1,"manifest":1,"app_info":0,"main_lua":1},{"id":"bad_app","name":"Bad","active":0,"compatible":0,"manifest":0,"app_info":1,"main_lua":0}],"count":2,"included":2,"truncated":0}"#
    let list = try JSONDecoder().decode(VibeBoardRuntimeAppList.self, from: Data(listJSON.utf8))

    #expect(list.api == "vibeboard-huangshan-app-manager/v1")
    #expect(list.apps.count == 2)
    #expect(list.apps[0].id == "flow_stage")
    #expect(list.apps[0].category == "Bridge")
    #expect(list.apps[0].icon == "radio")
    #expect(list.apps[0].author == "Huangshan Runtime Team")
    #expect(list.apps[0].screenshot == "generated:flow_stage")
    #expect(list.apps[0].requirements == ["Runtime", "Flow API"])
    #expect(list.apps[0].appInfo == 0)
    #expect(list.apps[1].compatible == 0)
    #expect(list.summary.contains("*flow_stage(ok)"))
}

@Test func runtimeJSONExtractorRejectsTruncatedAppList() throws {
    let truncated = #"{"api":"vibeboard-huangshan-app-manager/v1","error":"truncated","count":17}"#

    #expect(vibeBoardExtractJSONLine(truncated, expectedAPI: "vibeboard-huangshan-app-manager/v1") == nil)
    #expect(vibeBoardExtractJSONLine(truncated, expectedAPI: "vibeboard-huangshan-app-manager/v1", allowTruncated: true) == truncated)
    #expect(vibeBoardContainsTruncatedJSONLine(truncated, expectedAPI: "vibeboard-huangshan-app-manager/v1"))
}

@Test func runtimeJSONExtractorFindsEmbeddedAndPrettyJSON() throws {
    let pretty = """
    noise before {"api":"other/v1"}
    [vb_runtime] payload {
      "api" : "vibeboard-huangshan-app-manager/v1",
      "active" : "flow_stage",
      "apps" : [],
      "count" : 0
    } trailing log
    """
    let extracted = vibeBoardExtractJSONLine(pretty, expectedAPI: "vibeboard-huangshan-app-manager/v1")
    #expect(extracted?.contains("vibeboard-huangshan-app-manager/v1") == true)
    #expect(extracted?.contains("flow_stage") == true)

    let afterBrokenPrefix = "noise {broken { still broken " +
        #"{"api":"vibeboard-huangshan-app-manager/v1","active":"flow_stage"}"#
    #expect(vibeBoardExtractJSONLine(afterBrokenPrefix, expectedAPI: "vibeboard-huangshan-app-manager/v1")?.contains("flow_stage") == true)
}

@Test func combinesRuntimeAppPages() throws {
    let page0JSON = #"{"api":"vibeboard-huangshan-app-manager/v1","active":"flow_stage","state":"idle","offset":0,"limit":1,"apps":[{"id":"flow_stage","name":"Flow Stage","active":1,"compatible":1,"manifest":1,"app_info":0,"main_lua":1}],"count":2,"included":1,"truncated":0}"#
    let page1JSON = #"{"api":"vibeboard-huangshan-app-manager/v1","active":"flow_stage","state":"idle","offset":1,"limit":1,"apps":[{"id":"touch_stage","name":"Touch Stage","active":0,"compatible":1,"manifest":1,"app_info":0,"main_lua":1}],"count":2,"included":1,"truncated":0}"#
    let pages = try [page0JSON, page1JSON].map { text in
        try JSONDecoder().decode(VibeBoardRuntimeAppList.self, from: Data(text.utf8))
    }

    let list = try vibeBoardCombineAppPages(pages)

    #expect(list.apps.map(\.id) == ["flow_stage", "touch_stage"])
    #expect(list.count == 2)
    #expect(list.included == 2)
    #expect(list.truncated == 0)
    #expect(pages[0].offset == 0)
    #expect(pages[1].limit == 1)
}

@Test func rejectsNonContiguousRuntimeAppPages() throws {
    let page0JSON = #"{"api":"vibeboard-huangshan-app-manager/v1","active":"flow_stage","state":"idle","offset":0,"limit":1,"apps":[{"id":"flow_stage","name":"Flow Stage","active":1,"compatible":1,"manifest":1,"app_info":0,"main_lua":1}],"count":2,"included":1,"truncated":0}"#
    let repeatedPage0JSON = #"{"api":"vibeboard-huangshan-app-manager/v1","active":"flow_stage","state":"idle","offset":0,"limit":1,"apps":[{"id":"touch_stage","name":"Touch Stage","active":0,"compatible":1,"manifest":1,"app_info":0,"main_lua":1}],"count":2,"included":1,"truncated":0}"#
    let pages = try [page0JSON, repeatedPage0JSON].map { text in
        try JSONDecoder().decode(VibeBoardRuntimeAppList.self, from: Data(text.utf8))
    }

    #expect(throws: VibeBoardBLEError.self) {
        _ = try vibeBoardCombineAppPages(pages)
    }
}

@Test func rejectsDuplicateRuntimeAppPages() throws {
    let page0JSON = #"{"api":"vibeboard-huangshan-app-manager/v1","active":"flow_stage","state":"idle","offset":0,"limit":1,"apps":[{"id":"flow_stage","name":"Flow Stage","active":1,"compatible":1,"manifest":1,"app_info":0,"main_lua":1}],"count":2,"included":1,"truncated":0}"#
    let duplicatePageJSON = #"{"api":"vibeboard-huangshan-app-manager/v1","active":"flow_stage","state":"idle","offset":1,"limit":1,"apps":[{"id":"flow_stage","name":"Flow Stage","active":1,"compatible":1,"manifest":1,"app_info":0,"main_lua":1}],"count":2,"included":1,"truncated":0}"#
    let pages = try [page0JSON, duplicatePageJSON].map { text in
        try JSONDecoder().decode(VibeBoardRuntimeAppList.self, from: Data(text.utf8))
    }

    #expect(throws: VibeBoardBLEError.self) {
        _ = try vibeBoardCombineAppPages(pages)
    }
}

@Test func rejectsIncompleteRuntimeAppPages() throws {
    let page0JSON = #"{"api":"vibeboard-huangshan-app-manager/v1","active":"flow_stage","state":"idle","offset":0,"limit":1,"apps":[{"id":"flow_stage","name":"Flow Stage","active":1,"compatible":1,"manifest":1,"app_info":0,"main_lua":1}],"count":2,"included":1,"truncated":0}"#
    let emptyPageJSON = #"{"api":"vibeboard-huangshan-app-manager/v1","active":"flow_stage","state":"idle","offset":1,"limit":1,"apps":[],"count":2,"included":0,"truncated":1}"#
    let pages = try [page0JSON, emptyPageJSON].map { text in
        try JSONDecoder().decode(VibeBoardRuntimeAppList.self, from: Data(text.utf8))
    }

    #expect(throws: VibeBoardBLEError.self) {
        _ = try vibeBoardCombineAppPages(pages)
    }
}

@Test func matchesRuntimeAppManagerAckFormats() throws {
    #expect(VibeBoardBLEStatusMatcher.appLaunchMatches("ok launch app=flow_stage rc=0", appId: "flow_stage"))
    #expect(VibeBoardBLEStatusMatcher.appLaunchMatches("err launch app=flow_stage rc=-1", appId: "flow_stage"))
    #expect(VibeBoardBLEStatusMatcher.appLaunchMatches("[vb_runtime][launcher] launch app=flow_stage rc=0", appId: "flow_stage"))
    #expect(VibeBoardBLEStatusMatcher.appStopMatches("ok stop rc=0"))
    #expect(VibeBoardBLEStatusMatcher.appStopMatches("err stop rc=-1"))
    #expect(VibeBoardBLEStatusMatcher.appStopMatches("[vb_runtime][launcher] stop rc=0"))
    #expect(VibeBoardBLEStatusMatcher.appDeleteMatches("ok delete app=old_app rc=0", appId: "old_app"))
    #expect(VibeBoardBLEStatusMatcher.appDeleteMatches("noise\n[vb_runtime][launcher] delete app=old_app rc=0", appId: "old_app"))

    let launch = try VibeBoardRuntimeAppCommandAck(statusText: "ok launch app=flow_stage rc=0", command: "launch", appId: "flow_stage")
    #expect(launch.isOK)
    #expect(launch.resultCode == 0)
    #expect(launch.summary.contains("app=flow_stage"))

    let failed = try VibeBoardRuntimeAppCommandAck(statusText: "err delete app=flow_stage rc=-16", command: "delete", appId: "flow_stage")
    #expect(!failed.isOK)
    #expect(failed.resultCode == -16)

    let prefixed = try VibeBoardRuntimeAppCommandAck(
        statusText: "noise\n[vb_runtime][launcher] launch app=flow_stage rc=0",
        command: "launch",
        appId: "flow_stage"
    )
    #expect(prefixed.isOK)
    #expect(prefixed.rawStatus == "launch app=flow_stage rc=0")
}

@Test func decodesRuntimeRGBSnapshot() throws {
    let json = #"""
    {"api":"vibeboard-huangshan-rgb/v1","available":1,"ready":1,"ok":1,"dev":"rgbled","count":1,"color":"3366ff","name":"custom"}
    """#
    let snapshot = try JSONDecoder().decode(VibeBoardRGBSnapshot.self, from: Data(json.utf8))

    #expect(snapshot.api == "vibeboard-huangshan-rgb/v1")
    #expect(snapshot.available == 1)
    #expect(snapshot.ready == 1)
    #expect(snapshot.ok == 1)
    #expect(snapshot.dev == "rgbled")
    #expect(snapshot.count == 1)
    #expect(snapshot.color == "3366ff")
    #expect(snapshot.summary.contains("rgb=#3366ff"))
}

@Test func decodesRuntimeVoiceSnapshot() throws {
    let json = #"""
    {"api":"vibeboard-huangshan-voice-bridge/v1","available":1,"built":1,"ready":0,"recording":0,"seq":3,"requested_ms":800,"bytes":0,"rate":16000,"bits":16,"channels":1,"dropped":0,"err":0}
    """#
    let snapshot = try JSONDecoder().decode(VibeBoardVoiceSnapshot.self, from: Data(json.utf8))

    #expect(snapshot.api == "vibeboard-huangshan-voice-bridge/v1")
    #expect(snapshot.available == 1)
    #expect(snapshot.built == 1)
    #expect(snapshot.ready == 0)
    #expect(snapshot.recording == 0)
    #expect(snapshot.seq == 3)
    #expect(snapshot.requestedMs == 800)
    #expect(snapshot.rate == 16000)
    #expect(snapshot.summary.contains("voice ready=0 recording=0"))
}

@Test func decodesRuntimeAudioSnapshot() throws {
    let json = #"""
    {"api":"vibeboard-huangshan-audio-playback/v1","available":1,"playing":1,"ready":0,"suspended":0,"seq":2,"rate":16000,"channels":1,"bits":16,"bytes":1024,"total":23040,"volume":8,"err":1,"path":"/sdcard/apps/audio_stage/assets/chime.wav"}
    """#
    let snapshot = try JSONDecoder().decode(VibeBoardAudioSnapshot.self, from: Data(json.utf8))

    #expect(snapshot.api == "vibeboard-huangshan-audio-playback/v1")
    #expect(snapshot.playing == 1)
    #expect(snapshot.rate == 16000)
    #expect(snapshot.channels == 1)
    #expect(snapshot.volume == 8)
    #expect(snapshot.summary.contains("bytes=1024/23040"))
}


@Test func matchesInstallAckFormats() throws {
    #expect(VibeBoardBLEStatusMatcher.installAckMatches(
        "ok install_begin app=clock_test rc=0",
        command: "vb_runtime_install_begin clock_test"
    ))
    #expect(VibeBoardBLEStatusMatcher.installAckMatches(
        "ok install_file app=clock_test path=main.lua offset=16 rc=0",
        command: "vb_runtime_install_file clock_test main.lua 16 74657374"
    ))
    #expect(VibeBoardBLEStatusMatcher.installAckMatches(
        "ok install_end app=clock_test active=clock_test rc=0",
        command: "vb_runtime_install_end clock_test"
    ))
    #expect(VibeBoardBLEStatusMatcher.installAckMatches(
        "ok install_file clock_test/main.lua rc=0",
        command: "vb_runtime_install_file clock_test main.lua 16 74657374"
    ))
    #expect(VibeBoardBLEStatusMatcher.installAckMatches(
        "ok install_abort app=clock_test rc=0",
        command: "vb_runtime_install_abort clock_test"
    ))
}

@Test func matchesInfoFlowAckFormats() throws {
    #expect(VibeBoardBLEStatusMatcher.infoFlowAckMatches(
        "ok flow_send channel=phone seq=7 bytes=6 total=3",
        channel: "phone",
        sequence: 7
    ))
    #expect(VibeBoardBLEStatusMatcher.infoFlowAckMatches(
        "noise\nok flow_send channel=phone seq=7 bytes=6 total=3",
        channel: "phone",
        sequence: 7
    ))
    #expect(VibeBoardBLEStatusMatcher.infoFlowAckMatches(
        "err flow_send channel=phone seq=7 bytes=0 total=3",
        channel: "phone",
        sequence: 7
    ))
    #expect(VibeBoardBLEStatusMatcher.infoFlowAckMatches(
        "[vb_runtime][flow] recv total=3 seq=7 channel=phone bytes=6 text=hello!",
        channel: "phone",
        sequence: 7
    ))
    #expect(!VibeBoardBLEStatusMatcher.infoFlowAckMatches(
        "flow_send phone 7 68656c6c6f",
        channel: "phone",
        sequence: 7
    ))
    #expect(!VibeBoardBLEStatusMatcher.infoFlowAckMatches(
        "ok flow_send channel=phone seq=8 bytes=6 total=3",
        channel: "phone",
        sequence: 7
    ))
    #expect(VibeBoardBLEStatusMatcher.infoFlowStatusMatches(
        "ok flow api=vibeboard-huangshan-info-flow/v1 total=1 retained=1 seq=7 channel=phone bytes=6"
    ))
    #expect(VibeBoardBLEStatusMatcher.infoFlowStatusMatches("[vb_runtime][flow] total=1 retained=1"))
    #expect(!VibeBoardBLEStatusMatcher.infoFlowStatusMatches("flow_status"))
    #expect(VibeBoardBLEStatusMatcher.infoFlowClearMatches("ok flow_clear total=0"))
    #expect(VibeBoardBLEStatusMatcher.infoFlowClearMatches("err flow_clear rc=-1"))
    #expect(VibeBoardBLEStatusMatcher.infoFlowClearMatches("noise\n[vb_runtime][flow] err flow_clear rc=-5"))
    #expect(VibeBoardBLEStatusMatcher.infoFlowClearMatches("[vb_runtime][flow] cleared"))
    #expect(!VibeBoardBLEStatusMatcher.infoFlowClearMatches("voice_clear"))
}

@Test func parsesVoiceStatusAndDataFormats() throws {
    let status = try VibeBoardVoiceStatus(statusText: "ok voice api=vibeboard-huangshan-voice-bridge/v1 built=1 ready=1 recording=0 seq=2 bytes=48000 rate=16000 bits=16 channels=1 dropped=0 err=0")
    #expect(status.api == "vibeboard-huangshan-voice-bridge/v1")
    #expect(status.sequence == 2)
    #expect(status.bytes == 48000)
    #expect(status.rate == 16000)
    #expect(status.summary.contains("seq=2"))

    let ack = try VibeBoardVoiceStartAck(statusText: "ok voice_start seq=3 bytes=0 ms=1800 rc=0 built=1")
    #expect(ack.sequence == 3)
    #expect(ack.durationMs == 1800)
    #expect(ack.resultCode == 0)

    let chunk = try VibeBoardVoiceChunk(statusText: "ok voice_data seq=3 offset=160 bytes=4 hex=01020304")
    #expect(chunk.sequence == 3)
    #expect(chunk.offset == 160)
    #expect(chunk.payload == Data([0x01, 0x02, 0x03, 0x04]))
}

@Test func matchesVoiceAckFormats() throws {
    #expect(VibeBoardBLEStatusMatcher.voiceStatusMatches(
        "ok voice api=vibeboard-huangshan-voice-bridge/v1 built=1 ready=0 recording=1 seq=4 bytes=1600 rate=16000 bits=16 channels=1 dropped=0 err=0"
    ))
    #expect(VibeBoardBLEStatusMatcher.voiceStatusMatches(
        "ok voice api=vibeboard-huangshan-voice-bridge/v1 built=1 ready=1 recording=0 seq=4 bytes=48000 rate=16000 bits=16 channels=1 dropped=0 err=0",
        expectedSequence: 4
    ))
    #expect(VibeBoardBLEStatusMatcher.voiceStatusMatches(
        "noise\n[vb_runtime] ok voice api=vibeboard-huangshan-voice-bridge/v1 built=1 ready=1 recording=0 seq=4 bytes=48000 rate=16000 bits=16 channels=1 dropped=0 err=0",
        expectedSequence: 4
    ))
    #expect(VibeBoardBLEStatusMatcher.voiceStatusMatches(
        "[vb_runtime][voice] ok voice api=vibeboard-huangshan-voice-bridge/v1 built=1 ready=0 recording=0 seq=4 bytes=0 rate=16000 bits=\u{0}msh />msh />[vb_runtime][lua] timer Runtime Tick 2",
        expectedSequence: 4
    ))
    #expect(VibeBoardBLEStatusMatcher.voiceStartMatches(
        "ok voice_start seq=4 bytes=0 ms=1500 rc=0 built=1",
        expectedSequence: 4,
        durationMs: 1500
    ))
    #expect(VibeBoardBLEStatusMatcher.voiceStartMatches(
        "noise\n[vb_runtime][voice] err voice_start seq=4 bytes=0 ms=1500 rc=-5 built=1",
        expectedSequence: 4,
        durationMs: 1500
    ))
    #expect(VibeBoardBLEStatusMatcher.voiceStartMatches(
        "[vb_runtime] ok voice api=vibeboard-huangshan-voice-bridge/v1 built=1 ready=0 recording=1 seq=4 requested_ms=1500 bytes=0 rate=16000 bits=16 channels=1 dropped=0 err=1",
        expectedSequence: 4,
        durationMs: 1500
    ))
    #expect(VibeBoardBLEStatusMatcher.voiceReadMatches(
        "noise\n[vb_runtime] ok voice_data seq=4 offset=320 bytes=8 hex=0102030405060708",
        expectedSequence: 4,
        offset: 320
    ))
    #expect(VibeBoardBLEStatusMatcher.voiceStopMatches("ok voice_stop seq=4 bytes=16000 rc=0"))
    #expect(VibeBoardBLEStatusMatcher.voiceStopMatches("noise\nerr voice_stop seq=4 bytes=0 rc=-22"))
    #expect(!VibeBoardBLEStatusMatcher.voiceStopMatches("voice_stop"))
    #expect(VibeBoardBLEStatusMatcher.voiceClearMatches("ok voice_clear"))
    #expect(VibeBoardBLEStatusMatcher.voiceClearMatches("err voice_clear rc=-1"))
    #expect(VibeBoardBLEStatusMatcher.voiceClearMatches("noise\n[vb_runtime][voice] err voice_clear rc=-5"))
    #expect(VibeBoardBLEStatusMatcher.voiceClearMatches("[vb_runtime][voice] cleared"))
    #expect(!VibeBoardBLEStatusMatcher.voiceClearMatches("voice_clear"))

    let prefixedStatus = try VibeBoardVoiceStatus(statusText: "noise\n[vb_runtime] ok voice api=vibeboard-huangshan-voice-bridge/v1 built=1 ready=1 recording=0 seq=4 bytes=48000 rate=16000 bits=16 channels=1 dropped=0 err=0")
    #expect(prefixedStatus.sequence == 4)

    let statusFallbackAck = try VibeBoardVoiceStartAck(statusText: "[vb_runtime] ok voice api=vibeboard-huangshan-voice-bridge/v1 built=1 ready=0 recording=1 seq=5 requested_ms=1500 bytes=0 rate=16000 bits=16 channels=1 dropped=0 err=1")
    #expect(statusFallbackAck.sequence == 5)
    #expect(statusFallbackAck.durationMs == 1500)

    let prefixedChunk = try VibeBoardVoiceChunk(statusText: "noise\n[vb_runtime] ok voice_data seq=4 offset=320 bytes=4 hex=01020304")
    #expect(prefixedChunk.payload == Data([0x01, 0x02, 0x03, 0x04]))

    let emptyChunk = try VibeBoardVoiceChunk(statusText: "[vb_runtime][voice] ok voice_data seq=4 offset=320 bytes=0 hex=")
    #expect(emptyChunk.bytes == 0)
    #expect(emptyChunk.payload.isEmpty)
}


@Test func decodesRuntimeTouchSnapshot() throws {
    let json = #"""
    {"api":"vibeboard-huangshan-touch/v1","available":1,"ready":1,"active":0,"count":3,"x":122,"y":308,"event":"released","gesture":"left","dx":-84,"dy":6,"duration_ms":412,"tick":4567}
    """#
    let snapshot = try JSONDecoder().decode(VibeBoardTouchSnapshot.self, from: Data(json.utf8))

    #expect(snapshot.api == "vibeboard-huangshan-touch/v1")
    #expect(snapshot.available == 1)
    #expect(snapshot.ready == 1)
    #expect(snapshot.active == 0)
    #expect(snapshot.count == 3)
    #expect(snapshot.x == 122)
    #expect(snapshot.y == 308)
    #expect(snapshot.event == "released")
    #expect(snapshot.gesture == "left")
    #expect(snapshot.dx == -84)
    #expect(snapshot.dy == 6)
    #expect(snapshot.durationMs == 412)
    #expect(snapshot.tick == 4567)
    #expect(snapshot.summary.contains("gesture=left"))
}

@Test func decodesRuntimeGPIOSnapshot() throws {
    let json = #"""
    {"api":"vibeboard-huangshan-gpio/v1","available":1,"ready":1,"count":2,"inputs_only":1,"key1":{"ok":1,"pin":34,"active_high":1,"level":0,"pressed":0},"key2":{"ok":1,"pin":43,"active_high":1,"level":1,"pressed":1}}
    """#
    let snapshot = try JSONDecoder().decode(VibeBoardGPIOSnapshot.self, from: Data(json.utf8))

    #expect(snapshot.api == "vibeboard-huangshan-gpio/v1")
    #expect(snapshot.available == 1)
    #expect(snapshot.ready == 1)
    #expect(snapshot.inputsOnly == 1)
    #expect(snapshot.key1.pin == 34)
    #expect(snapshot.key2.pin == 43)
    #expect(snapshot.key2.pressed == 1)
    #expect(snapshot.summary.contains("key2=pressed/1"))
}
