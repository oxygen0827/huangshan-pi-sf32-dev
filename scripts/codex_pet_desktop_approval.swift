import AppKit
import ApplicationServices
import Foundation

private let codexBundleIdentifier = "com.openai.codex"
private let allowLabels: Set<String> = [
    "allow", "approve", "run", "yes", "允许", "批准", "运行", "确认"
]
private let denyLabels: Set<String> = [
    "deny", "reject", "no", "拒绝", "拒绝请求"
]

struct Options {
    var decision: String?
    var session: String?
    var dryRun = false
    var inspect = false
    var requestAccessibility = false
    var selfTest = false
}

struct Candidate {
    let element: AXUIElement
    let parent: AXUIElement?
    let label: String
}

private func normalized(_ value: String) -> String {
    value.trimmingCharacters(in: .whitespacesAndNewlines).lowercased()
}

private func classification(_ value: String) -> String? {
    let label = normalized(value)
    if allowLabels.contains(label) { return "allow" }
    if denyLabels.contains(label) { return "deny" }
    return nil
}

private func emit(_ value: [String: Any], code: Int32) -> Never {
    let data = try! JSONSerialization.data(withJSONObject: value, options: [.sortedKeys])
    FileHandle.standardOutput.write(data)
    FileHandle.standardOutput.write(Data([0x0a]))
    exit(code)
}

private func parseOptions() -> Options {
    var result = Options()
    var index = 1
    let arguments = CommandLine.arguments
    while index < arguments.count {
        switch arguments[index] {
        case "--approve": result.decision = "allow"
        case "--deny": result.decision = "deny"
        case "--session":
            index += 1
            if index < arguments.count { result.session = arguments[index] }
        case "--dry-run": result.dryRun = true
        case "--inspect": result.inspect = true
        case "--request-accessibility": result.requestAccessibility = true
        case "--self-test": result.selfTest = true
        default:
            emit(["ok": false, "reason": "invalid arguments"], code: 2)
        }
        index += 1
    }
    return result
}

private func attribute(_ element: AXUIElement, _ name: CFString) -> CFTypeRef? {
    var value: CFTypeRef?
    guard AXUIElementCopyAttributeValue(element, name, &value) == .success else { return nil }
    return value
}

private func stringAttribute(_ element: AXUIElement, _ name: CFString) -> String? {
    attribute(element, name) as? String
}

private func children(_ element: AXUIElement) -> [AXUIElement] {
    attribute(element, kAXChildrenAttribute as CFString) as? [AXUIElement] ?? []
}

private func buttonLabel(_ element: AXUIElement) -> String? {
    for name in [kAXTitleAttribute, kAXDescriptionAttribute, kAXHelpAttribute, kAXValueAttribute] {
        if let value = stringAttribute(element, name as CFString), !normalized(value).isEmpty {
            return value
        }
    }
    return nil
}

private func collectButtons(_ root: AXUIElement) -> [Candidate] {
    var result: [Candidate] = []
    var queue: [(AXUIElement, AXUIElement?)] = [(root, nil)]
    var visited = 0
    while !queue.isEmpty && visited < 2_000 {
        let (element, parent) = queue.removeFirst()
        visited += 1
        if stringAttribute(element, kAXRoleAttribute as CFString) == (kAXButtonRole as String),
           let label = buttonLabel(element), classification(label) != nil {
            result.append(Candidate(element: element, parent: parent, label: label))
        }
        for child in children(element) {
            queue.append((child, element))
        }
    }
    return result
}

private func sameParent(_ lhs: AXUIElement?, _ rhs: AXUIElement?) -> Bool {
    guard let lhs, let rhs else { return false }
    return CFEqual(lhs, rhs)
}

private func trusted(prompt: Bool) -> Bool {
    if !prompt { return AXIsProcessTrusted() }
    let options = [kAXTrustedCheckOptionPrompt.takeUnretainedValue() as String: true] as CFDictionary
    return AXIsProcessTrustedWithOptions(options)
}

private func validSession(_ value: String) -> Bool {
    value.range(of: "^[A-Za-z0-9][A-Za-z0-9_.:-]{0,39}$", options: .regularExpression) != nil
}

private func selfTest() -> Never {
    precondition(classification("Allow") == "allow")
    precondition(classification("拒绝") == "deny")
    precondition(classification("Cancel") == nil)
    precondition(classification("Run command now") == nil)
    precondition(validSession("019f6e2c-1234-5678-9abc-def012345678"))
    precondition(!validSession("../../unsafe"))
    emit(["ok": true, "selfTest": true], code: 0)
}

let options = parseOptions()
if options.selfTest { selfTest() }

guard trusted(prompt: options.requestAccessibility) else {
    emit(["ok": false, "reason": "Accessibility permission is required"], code: 3)
}

if options.decision != nil {
    guard let session = options.session, validSession(session),
          let url = URL(string: "codex://threads/\(session)") else {
        emit(["ok": false, "reason": "invalid task id"], code: 2)
    }
    guard NSWorkspace.shared.open(url) else {
        emit(["ok": false, "reason": "Codex task could not be opened"], code: 4)
    }
}

let applications = NSRunningApplication.runningApplications(withBundleIdentifier: codexBundleIdentifier)
guard applications.count == 1, let application = applications.first else {
    emit(["ok": false, "reason": "exactly one Codex app must be running"], code: 4)
}

let appElement = AXUIElementCreateApplication(application.processIdentifier)
var candidates: [Candidate] = []
let deadline = Date().addingTimeInterval(options.inspect ? 0.2 : 6.0)
repeat {
    candidates = collectButtons(appElement)
    let allow = candidates.filter { classification($0.label) == "allow" }
    let deny = candidates.filter { classification($0.label) == "deny" }
    if allow.count == 1, deny.count == 1, sameParent(allow[0].parent, deny[0].parent) { break }
    if options.inspect { break }
    Thread.sleep(forTimeInterval: 0.25)
} while Date() < deadline

let allow = candidates.filter { classification($0.label) == "allow" }
let deny = candidates.filter { classification($0.label) == "deny" }
let safePair = allow.count == 1 && deny.count == 1 && sameParent(allow[0].parent, deny[0].parent)

if options.inspect {
    emit([
        "ok": true,
        "allowCount": allow.count,
        "denyCount": deny.count,
        "safePair": safePair,
    ], code: 0)
}

guard let decision = options.decision, safePair else {
    emit(["ok": false, "reason": "a unique approval control pair was not found"], code: 5)
}
let target = decision == "allow" ? allow[0].element : deny[0].element
if options.dryRun {
    emit(["ok": true, "decision": decision, "dryRun": true], code: 0)
}
guard AXUIElementPerformAction(target, kAXPressAction as CFString) == .success else {
    emit(["ok": false, "reason": "approval control could not be pressed"], code: 6)
}
emit(["ok": true, "decision": decision], code: 0)
