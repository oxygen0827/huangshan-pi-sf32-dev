import Foundation

public enum RuntimePackageError: Error, Equatable {
    case unsafeAppId(String)
    case unsafePath(String)
    case missingMainLua
    case missingManifest
    case emptyDirectory(URL)
}

public struct RuntimePackage: Sendable {
    public let appId: String
    public let files: [String: Data]

    public init(appId: String, files: [String: Data]) throws {
        guard Self.isSafeAppId(appId) else {
            throw RuntimePackageError.unsafeAppId(appId)
        }
        for path in files.keys {
            guard Self.isSafePath(path) else {
                throw RuntimePackageError.unsafePath(path)
            }
        }
        guard files["main.lua"] != nil else {
            throw RuntimePackageError.missingMainLua
        }
        guard files["manifest.json"] != nil || files["app.info"] != nil else {
            throw RuntimePackageError.missingManifest
        }
        self.appId = appId
        self.files = files
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
            files[normalizedPath] = try Data(contentsOf: fileURL)
        }

        guard !files.isEmpty else {
            throw RuntimePackageError.emptyDirectory(directory)
        }

        let manifestAppId = manifestId(from: files["manifest.json"])
        let packageId = overrideAppId ?? manifestAppId ?? root.lastPathComponent
        return try RuntimePackage(appId: packageId, files: files)
    }

    public func installCommands(chunkBytes: Int = 48) -> [String] {
        let chunkSize = min(max(chunkBytes, 16), 240)
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
        let value = rawValue.replacingOccurrences(of: "\\", with: "/")
        if value.hasPrefix("/") || value.contains("..") || value.contains("//") {
            return false
        }
        let pattern = #"^(manifest\.json|app\.info|main\.lua|files\.txt|README\.md|(?:assets|images|fonts|lib)/[A-Za-z0-9_./-]+\.(?:json|txt|png|jpg|jpeg|bin|ttf|otf|lua))$"#
        return value.range(of: pattern, options: .regularExpression) != nil
    }

    private static func manifestId(from data: Data?) -> String? {
        guard let data else { return nil }
        guard let object = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
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
