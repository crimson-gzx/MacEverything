#pragma once
// Part 41: CLI daemon startup test
// Launches the daemon binary on a tmpdir, verifies /api/health returns 200,
// sends SIGTERM and verifies graceful exit.

#include <fstream>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <cstring>
#include <sstream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static std::string daemonHttpGet(uint16_t port, const std::string& path) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return "";

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
        return "";
    }

    std::string request = "GET " + path + " HTTP/1.0\r\nHost: localhost\r\n\r\n";
    send(sock, request.c_str(), request.size(), 0);

    std::string response;
    char buf[4096];
    ssize_t n;
    while ((n = recv(sock, buf, sizeof(buf), 0)) > 0) {
        response.append(buf, n);
    }
    close(sock);
    return response;
}

static bool runPart41() {
    std::cout << "\n=== Part 41: CLI daemon startup ===\n";


    // Check that the daemon binary exists
    if (access("./maceverything-daemon", X_OK) != 0) {
        std::cout << "  SKIP: maceverything-daemon binary not found (build with 'make daemon')\n";
        return true;
    }

    // Create a tmpdir with test files
    auto tmpBase = fs::temp_directory_path() / "maceverything_test_daemon";
    fs::remove_all(tmpBase);
    fs::create_directories(tmpBase / "subdir");
    { std::ofstream f(tmpBase / "hello.txt"); f << "hello world"; }
    { std::ofstream f(tmpBase / "subdir" / "test.log"); f << "log entry"; }

    std::string cachePath = (tmpBase / "cache").string();
    std::string logPath = (tmpBase / "logs").string();
    fs::create_directories(cachePath);
    fs::create_directories(logPath);

    // Pick a test port that's unlikely to conflict
    uint16_t testPort = 19870;

    // Fork and exec the daemon
    pid_t pid = fork();
    if (pid < 0) {
        std::cout << "  FAIL: fork() failed\n";
        return false;
    }

    if (pid == 0) {
        // Child: exec daemon
        std::string portStr = std::to_string(testPort);
        execl("./maceverything-daemon", "maceverything-daemon",
              "--port", portStr.c_str(),
              "--root", tmpBase.c_str(),
              "--cache-dir", cachePath.c_str(),
              "--log-dir", logPath.c_str(),
              nullptr);
        _exit(127); // exec failed
    }

    // Parent: wait for daemon to start, then test HTTP
    // Note: on systems with EDR/antivirus software, newly compiled binaries
    // may be suspended for 5-10 seconds during initial cloud scanning.
    bool serverReady = false;
    for (int attempt = 0; attempt < 200; attempt++) { // up to 20 seconds
        usleep(100000); // 100ms
        auto response = daemonHttpGet(testPort, "/api/health");
        if (response.find("200 OK") != std::string::npos) {
            serverReady = true;
            break;
        }
    }

    check(serverReady, "Daemon /api/health returns 200");

    if (serverReady) {
        // Verify health response contains expected JSON
        auto healthResp = daemonHttpGet(testPort, "/api/health");
        check(healthResp.find("\"status\"") != std::string::npos, "Health response contains status field");

        // Verify status endpoint works
        auto statusResp = daemonHttpGet(testPort, "/api/status");
        check(statusResp.find("200 OK") != std::string::npos, "Status endpoint returns 200");
    }

    // Send SIGTERM for graceful shutdown
    kill(pid, SIGTERM);

    int status = 0;
    int waitResult = 0;
    for (int i = 0; i < 100; i++) { // up to 10 seconds
        waitResult = waitpid(pid, &status, WNOHANG);
        if (waitResult > 0) break;
        usleep(100000);
    }

    if (waitResult == 0) {
        // Still running — force kill
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
        check(false, "Daemon exited gracefully on SIGTERM");
    } else {
        check(WIFEXITED(status), "Daemon exited gracefully on SIGTERM");
    }

    // Clean up
    fs::remove_all(tmpBase);

    return true;
}
