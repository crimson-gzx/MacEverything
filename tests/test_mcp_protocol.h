#pragma once
// Part 49: MCP Protocol Tests
// Tests the MCP server binary via subprocess stdio communication.

#include <cstdio>
#include <string>
#include <sstream>
#include <vector>

/// Send JSON-RPC messages to the MCP binary via pipe and capture all responses.
static std::string mcpExec(const std::string& input) {
    // Use the built binary path (relative to build dir)
    std::string cmd = "echo '" + input + "' | ./build/Release/MacEverythingMCP 2>/dev/null";
    FILE* fp = popen(cmd.c_str(), "r");
    if (!fp) return "";

    std::string output;
    char buf[4096];
    while (fgets(buf, sizeof(buf), fp)) {
        output += buf;
    }
    pclose(fp);
    return output;
}

/// Split newline-delimited responses into individual JSON strings.
static std::vector<std::string> splitLines(const std::string& s) {
    std::vector<std::string> lines;
    std::istringstream stream(s);
    std::string line;
    while (std::getline(stream, line)) {
        // Trim trailing \r
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.pop_back();
        if (!line.empty()) lines.push_back(line);
    }
    return lines;
}

/// Check if a JSON string contains a given substring.
static bool jsonContains(const std::string& json, const std::string& needle) {
    return json.find(needle) != std::string::npos;
}

static void runMcpProtocolTests() {
    std::cout << "========================================\n";
    std::cout << "  Part 49: MCP Protocol Tests\n";
    std::cout << "========================================\n\n";

    // Skip if MCP binary not built
    if (std::system("test -x ./build/Release/MacEverythingMCP") != 0) {
        std::cout << "  [SKIP] MCP binary not found at ./build/Release/MacEverythingMCP\n";
        return;
    }

    // -- Test 1: Initialize --
    std::cout << "  --- Test 1: Initialize ---\n";
    {
        std::string input = R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"test","version":"1.0"}}})";
        auto output = mcpExec(input);
        auto lines = splitLines(output);
        check(lines.size() == 1, "initialize: exactly 1 response line");
        check(jsonContains(lines[0], "\"protocolVersion\":\"2025-03-26\""), "initialize: has protocol version");
        check(jsonContains(lines[0], "\"name\":\"MacEverything\""), "initialize: has server name");
        check(jsonContains(lines[0], "\"tools\":{}"), "initialize: has tools capability");
        check(jsonContains(lines[0], "\"id\":1"), "initialize: id matches");
    }

    // -- Test 2: tools/list --
    std::cout << "\n  --- Test 2: tools/list ---\n";
    {
        std::string input =
            R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"test","version":"1.0"}}})"
            "\n"
            R"({"jsonrpc":"2.0","method":"notifications/initialized"})"
            "\n"
            R"({"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}})";
        auto output = mcpExec(input);
        auto lines = splitLines(output);
        check(lines.size() == 2, "tools/list: 2 response lines (init + list)");
        auto& toolsResp = lines[1];
        check(jsonContains(toolsResp, "\"id\":2"), "tools/list: id matches");
        check(jsonContains(toolsResp, "search_files"), "tools/list: has search_files");
        check(jsonContains(toolsResp, "search_content"), "tools/list: has search_content");
        check(jsonContains(toolsResp, "recent_files"), "tools/list: has recent_files");
        check(jsonContains(toolsResp, "index_status"), "tools/list: has index_status");
        check(jsonContains(toolsResp, "inputSchema"), "tools/list: tools have inputSchema");
    }

    // -- Test 3: ping --
    std::cout << "\n  --- Test 3: ping ---\n";
    {
        std::string input =
            R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"test","version":"1.0"}}})"
            "\n"
            R"({"jsonrpc":"2.0","id":99,"method":"ping"})";
        auto output = mcpExec(input);
        auto lines = splitLines(output);
        check(lines.size() == 2, "ping: 2 response lines");
        check(jsonContains(lines[1], "\"id\":99"), "ping: id matches");
        check(jsonContains(lines[1], "\"result\":{}"), "ping: result is empty object");
    }

    // -- Test 4: Unknown method returns error --
    std::cout << "\n  --- Test 4: Unknown method ---\n";
    {
        std::string input =
            R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"test","version":"1.0"}}})"
            "\n"
            R"({"jsonrpc":"2.0","id":5,"method":"nonexistent/method"})";
        auto output = mcpExec(input);
        auto lines = splitLines(output);
        check(lines.size() == 2, "unknown method: 2 response lines");
        check(jsonContains(lines[1], "\"error\""), "unknown method: has error field");
        check(jsonContains(lines[1], "-32601"), "unknown method: error code is -32601");
    }

    // -- Test 5: notifications/initialized is silent --
    std::cout << "\n  --- Test 5: Notification is silent ---\n";
    {
        std::string input =
            R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"test","version":"1.0"}}})"
            "\n"
            R"({"jsonrpc":"2.0","method":"notifications/initialized"})";
        auto output = mcpExec(input);
        auto lines = splitLines(output);
        // Notification has no "id" so should produce no response
        check(lines.size() == 1, "notification: only 1 response (initialize), notification is silent");
    }

    // -- Test 6: tools/call with unknown tool --
    std::cout << "\n  --- Test 6: Unknown tool call ---\n";
    {
        std::string input =
            R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"test","version":"1.0"}}})"
            "\n"
            R"({"jsonrpc":"2.0","id":10,"method":"tools/call","params":{"name":"nonexistent_tool","arguments":{}}})";
        auto output = mcpExec(input);
        auto lines = splitLines(output);
        check(lines.size() == 2, "unknown tool: 2 response lines");
        check(jsonContains(lines[1], "\"isError\":true"), "unknown tool: isError is true");
        check(jsonContains(lines[1], "Unknown tool"), "unknown tool: error message mentions unknown tool");
    }

    // -- Test 7: tools/call search_files without query --
    std::cout << "\n  --- Test 7: search_files missing query ---\n";
    {
        std::string input =
            R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"test","version":"1.0"}}})"
            "\n"
            R"({"jsonrpc":"2.0","id":11,"method":"tools/call","params":{"name":"search_files","arguments":{}}})";
        auto output = mcpExec(input);
        auto lines = splitLines(output);
        check(lines.size() == 2, "missing query: 2 response lines");
        check(jsonContains(lines[1], "missing required parameter"), "missing query: error message mentions missing parameter");
    }

    // -- Test 8: Empty and whitespace lines are skipped --
    std::cout << "\n  --- Test 8: Empty lines skipped ---\n";
    {
        std::string input =
            "\n\n"
            R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"test","version":"1.0"}}})"
            "\n\n\n"
            R"({"jsonrpc":"2.0","id":2,"method":"ping"})"
            "\n\n";
        auto output = mcpExec(input);
        auto lines = splitLines(output);
        check(lines.size() == 2, "empty lines: still 2 responses");
        check(jsonContains(lines[0], "\"id\":1"), "empty lines: first response has id 1");
        check(jsonContains(lines[1], "\"id\":2"), "empty lines: second response has id 2");
    }

    // -- Test 9: Invalid JSON returns parse error --
    std::cout << "\n  --- Test 9: Invalid JSON ---\n";
    {
        std::string input = "this is not json at all";
        auto output = mcpExec(input);
        auto lines = splitLines(output);
        check(lines.size() == 1, "invalid JSON: 1 response");
        check(jsonContains(lines[0], "-32700"), "invalid JSON: parse error code -32700");
    }

    std::cout << "\n  Part 49 complete.\n\n";
}
