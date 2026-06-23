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
                defaults.removeObject(forKey: cacheKey)
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

        defaults.set(candidate.identifier.uuidString, forKey: cacheKey)
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

    public func sensors() async throws -> VibeBoardSensorSnapshot {
        try await send("sensors")
        let status = try await readStatus()
        guard let data = status.data(using: .utf8) else {
            throw VibeBoardBLEError.commandFailed(status)
        }
        let snapshot = try JSONDecoder().decode(VibeBoardSensorSnapshot.self, from: data)
        trace("runtime sensors \(snapshot.summary)")
        return snapshot
    }

    public func install(_ package: RuntimePackage, chunkBytes: Int = 48) async throws {
        trace("install begin app=\(package.appId) files=\(package.files.count)")
        for command in package.installCommands(chunkBytes: chunkBytes) {
            try await send(command)
            let status = try await readStatus()
            trace("install ack \(status)")
            if status.hasPrefix("err ") || status.contains(" rc=-") {
                throw VibeBoardBLEError.commandFailed(status)
            }
        }
        try await send("status")
        let finalStatus = try await readStatus()
        guard finalStatus.contains("active=\(package.appId)") else {
            throw VibeBoardBLEError.commandFailed(finalStatus)
        }
        trace("install complete app=\(package.appId) status=\(finalStatus)")
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
        guard let value = defaults.string(forKey: cacheKey),
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
