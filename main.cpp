// MacEverything CLI Test Harness
// Compile: clang++ -std=c++20 -O2 MacEverything/Core/*.cpp main.cpp -o mac_scanner

#include "MacEverything/Core/DirectoryScanner.h"
#include "MacEverything/Core/SearchEngine.h"
#include <fcntl.h>
#include <unistd.h>
#include <chrono>
#include <iostream>
#include <cstring>

int main(int argc, char* argv[]) {
    std::string rootPath = (argc > 1) ? argv[1] : "/";

    // Validate root path
    int testfd = open(rootPath.c_str(), O_RDONLY | O_DIRECTORY);
    if (testfd < 0) {
        std::cerr << "Error: cannot open directory '" << rootPath << "': " << strerror(errno) << "\n";
        return 1;
    }
    close(testfd);

    auto startTime = std::chrono::steady_clock::now();

    DirectoryScanner scanner;
    scanner.scan(rootPath);

    auto scanEnd = std::chrono::steady_clock::now();
    double scanElapsed = std::chrono::duration<double>(scanEnd - startTime).count();

    const auto& stats = scanner.getStats();
    uint64_t files = stats.fileCount.load();
    uint64_t dirs = stats.dirCount.load();
    uint64_t symlinks = stats.symlinkCount.load();
    uint64_t others = stats.otherCount.load();
    uint64_t errors = stats.errorCount.load();
    uint64_t total = files + dirs + symlinks + others;

    std::cout << "\n=== Scan Complete ===\n"
              << "Files:       " << files << "\n"
              << "Directories: " << dirs << "\n"
              << "Symlinks:    " << symlinks << "\n"
              << "Other:       " << others << "\n"
              << "Errors:      " << errors << " (directories skipped)\n"
              << "Total:       " << total << "\n"
              << "Scan time:   " << scanElapsed << "s\n";

    // Take results and show sample entries with size/modTime
    auto results = scanner.takeResults();
    std::cout << "\nTotal records: " << results.size() << "\n";

    // Load into SearchEngine
    SearchEngine engine;
    auto loadStart = std::chrono::steady_clock::now();
    engine.loadRecords(std::move(results));
    auto loadEnd = std::chrono::steady_clock::now();
    double loadElapsed = std::chrono::duration<double>(loadEnd - loadStart).count();
    std::cout << "Records loaded: " << engine.recordCount()
              << " (index build: " << loadElapsed << "s)\n";

    // Interactive query loop
    std::cout << "\n=== Search Mode (type query, empty to quit) ===\n";
    std::string query;
    for (;;) {
        std::cout << "> ";
        if (!std::getline(std::cin, query) || query.empty()) break;

        auto qStart = std::chrono::steady_clock::now();
        auto indices = engine.query(query, 50000);
        auto qEnd = std::chrono::steady_clock::now();
        double qElapsed = std::chrono::duration<double>(qEnd - qStart).count();

        std::cout << "Found " << indices.size() << " matches in "
                  << (qElapsed * 1000) << "ms\n";

        size_t show = std::min<size_t>(indices.size(), 20);
        for (size_t i = 0; i < show; i++) {
            const auto& r = engine.getRecord(indices[i]);
            const char* typeStr = "???";
            switch (r.type) {
                case 1: typeStr = "FILE"; break;
                case 2: typeStr = "DIR "; break;
                case 3: typeStr = "LINK"; break;
                case 4: typeStr = "OTHR"; break;
            }
            std::cout << "  [" << typeStr << "] "
                      << r.path << "/" << r.name
                      << "  size=" << r.size << "\n";
        }
        if (indices.size() > show) {
            std::cout << "  ... and " << (indices.size() - show) << " more\n";
        }
    }

    return 0;
}
