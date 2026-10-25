import Foundation

enum MCPClient: String, CaseIterable {
    case claudeCode
    case cursor
    case claudeDesktop

    var displayName: String {
        switch self {
        case .claudeCode: return "Claude Code"
        case .cursor: return "Cursor"
        case .claudeDesktop: return "Claude Desktop"
        }
    }

    var configFileURL: URL {
        let home = FileManager.default.homeDirectoryForCurrentUser
        switch self {
        case .claudeCode:
            return home.appendingPathComponent(".claude/settings.json")
        case .cursor:
            return home.appendingPathComponent(".cursor/mcp.json")
        case .claudeDesktop:
            return home
                .appendingPathComponent("Library/Application Support/Claude/claude_desktop_config.json")
        }
    }
}

struct MCPConfigManager {
    static var mcpBinaryPath: String {
        guard let execURL = Bundle.main.executableURL else { return "" }
        return execURL.deletingLastPathComponent()
            .appendingPathComponent("MacEverythingMCP").path
    }

    static func isEnabled(for client: MCPClient) -> Bool {
        guard let root = readJSON(at: client.configFileURL) else { return false }
        guard let servers = root["mcpServers"] as? [String: Any] else { return false }
        return servers["maceverything"] != nil
    }

    static func setEnabled(_ enabled: Bool, for client: MCPClient) {
        if enabled {
            enable(for: client)
        } else {
            disable(for: client)
        }
    }

    // MARK: - Private

    private static func enable(for client: MCPClient) {
        let url = client.configFileURL
        var root = readJSON(at: url) ?? [:]
        var servers = root["mcpServers"] as? [String: Any] ?? [:]
        servers["maceverything"] = [
            "command": mcpBinaryPath,
            "args": [String]()
        ] as [String: Any]
        root["mcpServers"] = servers
        writeJSON(root, to: url)
    }

    private static func disable(for client: MCPClient) {
        let url = client.configFileURL
        guard var root = readJSON(at: url) else { return }
        guard var servers = root["mcpServers"] as? [String: Any] else { return }
        servers.removeValue(forKey: "maceverything")
        if servers.isEmpty {
            root.removeValue(forKey: "mcpServers")
        } else {
            root["mcpServers"] = servers
        }
        writeJSON(root, to: url)
    }

    private static func readJSON(at url: URL) -> [String: Any]? {
        guard let data = try? Data(contentsOf: url) else { return nil }
        return try? JSONSerialization.jsonObject(with: data) as? [String: Any]
    }

    private static func writeJSON(_ dict: [String: Any], to url: URL) {
        let dir = url.deletingLastPathComponent()
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        guard let data = try? JSONSerialization.data(
            withJSONObject: dict,
            options: [.prettyPrinted, .sortedKeys]
        ) else { return }
        try? data.write(to: url, options: .atomic)
    }
}
