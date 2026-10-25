// tests/test_wal_crc.h — Part 15: WAL CRC32 Integrity & Size Limit Tests

#pragma once

inline void runWalCrcTests() {
    std::cout << "╔══════════════════════════════════════════╗\n";
    std::cout << "║  Part 15: WAL CRC32 & Size Limit Tests  ║\n";
    std::cout << "╚══════════════════════════════════════════╝\n\n";

    auto tmpDir = fs::temp_directory_path() / "mac_everything_test_walcrc";
    fs::create_directories(tmpDir);

    // --- Test 15.1: CRC32 known value ---
    {
        // CRC32 of "123456789" should be 0xCBF43926 (ISO 3309)
        const char* testStr = "123456789";
        uint32_t crc = IndexWAL::crc32(testStr, 9);
        check(crc == 0xCBF43926u, "15.1 CRC32 known value (0xCBF43926)");
    }

    // --- Test 15.2: CRC32 empty data ---
    {
        uint32_t crc = IndexWAL::crc32("", 0);
        check(crc == 0x00000000u, "15.2 CRC32 empty data = 0");
    }

    // --- Test 15.3: IndexWAL write + read with CRC roundtrip ---
    {
        auto walPath = (tmpDir / "test_crc.wal").string();
        fs::remove(walPath);

        IndexWAL wal;
        wal.setSyncInterval(0);
        bool opened = wal.open(walPath);
        check(opened, "15.3a WAL open");

        FileRecord r1{"file1.txt", "/tmp", 1, 100, 1000, 12345, 1};
        FileRecord r2{"file2.txt", "/tmp/sub", 2, 200, 2000, 67890, 1};
        check(wal.append(WALOp::Add, "/tmp/file1.txt", r1), "15.3b append Add");
        check(wal.append(WALOp::Update, "/tmp/sub/file2.txt", r2), "15.3c append Update");
        check(wal.append(WALOp::Remove, "/tmp/file1.txt"), "15.3d append Remove");
        wal.close();

        auto entries = IndexWAL::readAll(walPath);
        check(entries.size() == 3u, "15.3e readAll returns 3 entries");
        if (entries.size() == 3) {
            check(entries[0].op == WALOp::Add && entries[0].fullPath == "/tmp/file1.txt",
                  "15.3f entry[0] is Add /tmp/file1.txt");
            check(entries[0].record.name == "file1.txt", "15.3g entry[0] record name");
            check(entries[1].op == WALOp::Update && entries[1].record.name == "file2.txt",
                  "15.3h entry[1] is Update file2.txt");
            check(entries[2].op == WALOp::Remove && entries[2].fullPath == "/tmp/file1.txt",
                  "15.3i entry[2] is Remove");
        }

        fs::remove(walPath);
    }

    // --- Test 15.4: CRC corruption stops reading ---
    {
        auto walPath = (tmpDir / "test_corrupt.wal").string();
        fs::remove(walPath);

        IndexWAL wal;
        wal.setSyncInterval(0);
        wal.open(walPath);
        FileRecord r{"f.txt", "/", 1, 10, 100, 111, 1};
        wal.append(WALOp::Add, "/f.txt", r);
        wal.append(WALOp::Add, "/g.txt", r);
        wal.append(WALOp::Add, "/h.txt", r);
        wal.close();

        // Verify all 3 read back
        auto entries = IndexWAL::readAll(walPath);
        check(entries.size() == 3u, "15.4a all 3 entries valid before corruption");

        // Corrupt a byte roughly in the middle of the file
        {
            FILE* f = fopen(walPath.c_str(), "r+b");
            fseek(f, 0, SEEK_END);
            long sz = ftell(f);
            long corruptPos = sz / 2;
            fseek(f, corruptPos, SEEK_SET);
            uint8_t byte;
            size_t nr = fread(&byte, 1, 1, f);
            (void)nr;
            byte ^= 0xFF;
            fseek(f, corruptPos, SEEK_SET);
            fwrite(&byte, 1, 1, f);
            fclose(f);
        }

        entries = IndexWAL::readAll(walPath);
        check(entries.size() < 3u, "15.4b corruption detected, recovered partial entries");

        fs::remove(walPath);
    }

    // --- Test 15.5: Truncated WAL recovers gracefully ---
    {
        auto walPath = (tmpDir / "test_truncate.wal").string();
        fs::remove(walPath);

        IndexWAL wal;
        wal.setSyncInterval(0);
        wal.open(walPath);
        FileRecord r{"x.txt", "/a", 1, 50, 500, 999, 1};
        wal.append(WALOp::Add, "/a/x.txt", r);
        wal.append(WALOp::Add, "/a/y.txt", r);
        wal.close();

        // Truncate to ~60% — should keep first entry, break second
        {
            FILE* f = fopen(walPath.c_str(), "r+b");
            fseek(f, 0, SEEK_END);
            long sz = ftell(f);
            long newSize = sz * 6 / 10;
            ftruncate(fileno(f), newSize);
            fclose(f);
        }

        auto entries = IndexWAL::readAll(walPath);
        check(entries.size() == 1u, "15.5 truncated WAL recovers first entry");
        if (!entries.empty()) {
            check(entries[0].fullPath == "/a/x.txt", "15.5b first entry correct");
        }

        fs::remove(walPath);
    }

    // --- Test 15.6: ContentIndexWAL CRC roundtrip ---
    {
        auto walPath = (tmpDir / "content_crc.wal").string();
        fs::remove(walPath);

        ContentIndexWAL wal;
        wal.setSyncInterval(0);
        wal.open(walPath);

        std::vector<Trigram> tris = {0x616263, 0x626364, 0x636465};
        wal.appendAdd(42, 0xDEADBEEF, tris);
        wal.appendRemove(99);
        wal.appendAdd(7, 0xCAFE, {});
        wal.close();

        auto entries = ContentIndexWAL::readAll(walPath);
        check(entries.size() == 3u, "15.6a ContentIndexWAL 3 entries roundtrip");
        if (entries.size() == 3) {
            check(entries[0].op == ContentIndexWAL::Entry::Add && entries[0].fileIndex == 42,
                  "15.6b entry[0] Add fileIndex=42");
            check(entries[0].contentHash == 0xDEADBEEFu, "15.6c entry[0] contentHash");
            check(entries[0].trigrams.size() == 3u, "15.6d entry[0] 3 trigrams");
            check(entries[1].op == ContentIndexWAL::Entry::Remove && entries[1].fileIndex == 99,
                  "15.6e entry[1] Remove fileIndex=99");
            check(entries[2].op == ContentIndexWAL::Entry::Add && entries[2].trigrams.empty(),
                  "15.6f entry[2] Add with 0 trigrams");
        }

        fs::remove(walPath);
    }

    // --- Test 15.7: ContentIndexWAL CRC corruption detection ---
    {
        auto walPath = (tmpDir / "content_corrupt.wal").string();
        fs::remove(walPath);

        ContentIndexWAL wal;
        wal.setSyncInterval(0);
        wal.open(walPath);

        std::vector<Trigram> tris = {0x010203};
        wal.appendAdd(1, 0x1111, tris);
        wal.appendAdd(2, 0x2222, tris);
        wal.close();

        // Corrupt last 2 bytes (part of CRC of second entry)
        {
            FILE* f = fopen(walPath.c_str(), "r+b");
            fseek(f, 0, SEEK_END);
            long sz = ftell(f);
            fseek(f, sz - 2, SEEK_SET);
            uint8_t garbage = 0xFF;
            fwrite(&garbage, 1, 1, f);
            fclose(f);
        }

        auto entries = ContentIndexWAL::readAll(walPath);
        check(entries.size() == 1u, "15.7 ContentIndexWAL corruption stops at entry 2");

        fs::remove(walPath);
    }

    // --- Test 15.8: WAL size limit enforcement ---
    {
        auto walPath = (tmpDir / "test_sizelimit.wal").string();
        fs::remove(walPath);

        // Create a file that's already at the size limit
        {
            FILE* f = fopen(walPath.c_str(), "wb");
            ftruncate(fileno(f), IndexWAL::kMaxWALSize);
            fclose(f);
        }

        IndexWAL wal;
        wal.setSyncInterval(0);
        wal.open(walPath);
        FileRecord r{"test.txt", "/tmp", 1, 10, 100, 1, 1};
        bool result = wal.append(WALOp::Add, "/tmp/test.txt", r);
        check(!result, "15.8a append rejected when WAL at size limit");
        wal.close();

        // Verify small WAL still accepts writes
        fs::remove(walPath);
        IndexWAL wal2;
        wal2.setSyncInterval(0);
        wal2.open(walPath);
        result = wal2.append(WALOp::Add, "/tmp/test.txt", r);
        check(result, "15.8b append succeeds on fresh WAL");
        wal2.close();

        fs::remove(walPath);
    }

    // Cleanup
    fs::remove_all(tmpDir);

    std::cout << "\n  Part 15 complete.\n\n";
}
