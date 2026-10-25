#pragma once
// ═══════════════════════════════════════════════════════
//  Part 3d: Compact Records Tests
// ═══════════════════════════════════════════════════════

static void runCompactionTests() {
    std::cout << "========================================\n";
    std::cout << "  Part 3d: Compact Records Tests\n";
    std::cout << "========================================\n\n";

    SearchEngine engine;
    std::vector<FileRecord> records;
    for (int i = 0; i < 100; i++) {
        records.push_back({"compact_" + std::to_string(i) + ".txt", "/test", 1, (uint64_t)i, (time_t)i});
    }
    engine.loadRecords(std::move(records));

    check(engine.recordCount() == 100, "Compact: initial recordCount == 100");
    check(engine.liveRecordCount() == 100, "Compact: initial liveRecordCount == 100");

    // Remove half the records
    for (int i = 0; i < 50; i++) {
        engine.removeByPath("/test/compact_" + std::to_string(i) + ".txt");
    }
    check(engine.liveRecordCount() == 50, "Compact: after removing 50, liveRecordCount == 50");
    check(engine.recordCount() == 100, "Compact: after removing 50, recordCount still 100 (tombstones)");

    // Compact
    engine.compactRecords();
    check(engine.recordCount() == 50, "Compact: after compaction, recordCount == 50");
    check(engine.liveRecordCount() == 50, "Compact: after compaction, liveRecordCount == 50");

    // Verify remaining records are still queryable
    auto res = engine.query("compact_50");
    check(res.size() == 1, "Compact: compact_50.txt still queryable after compaction");
    check(engine.getRecord(res[0]).name == "compact_50.txt", "Compact: correct record data after compaction");

    // Verify removed records are gone
    res = engine.query("compact_0");
    check(res.empty(), "Compact: compact_0.txt not queryable after compaction");

    // Compact when nothing to compact
    engine.compactRecords();
    check(engine.recordCount() == 50, "Compact: no-op compaction preserves recordCount");

    std::cout << "\n";
}
