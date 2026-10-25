#pragma once
#include <unistd.h>
#include <iostream>

static void runInstanceLockTests() {
    std::cout << "========================================\n";
    std::cout << "  Part 26 — Instance Lock Tests\n";
    std::cout << "========================================\n\n";

    std::string lockPath = "/tmp/test_instance_lock_" + std::to_string(getpid()) + ".lock";

    // Test 1: Basic lock/unlock
    {
        InstanceLock lock;
        check(lock.tryLock(lockPath), "First lock succeeds");
        check(lock.isLocked(), "Lock reports locked");
        lock.unlock();
        check(!lock.isLocked(), "Lock reports unlocked after unlock");
    }

    // Test 2: Second instance fails while first holds lock
    {
        InstanceLock lock1;
        check(lock1.tryLock(lockPath), "First lock succeeds");

        InstanceLock lock2;
        check(!lock2.tryLock(lockPath), "Second lock fails (another instance)");

        lock1.unlock();
        check(lock2.tryLock(lockPath), "Second lock succeeds after first releases");
    }

    // Test 3: RAII cleanup — lock released when object goes out of scope
    {
        {
            InstanceLock lock;
            check(lock.tryLock(lockPath), "RAII lock succeeds");
        } // lock goes out of scope, should release

        InstanceLock lock2;
        check(lock2.tryLock(lockPath), "Lock available after RAII cleanup");
    }

    // Test 4: Double tryLock on same object returns true (idempotent)
    {
        InstanceLock lock;
        check(lock.tryLock(lockPath), "First tryLock succeeds");
        check(lock.tryLock(lockPath), "Second tryLock on same object returns true (already locked)");
        lock.unlock();
    }

    // Test 5: Unlock on never-locked object is a no-op
    {
        InstanceLock lock;
        check(!lock.isLocked(), "New lock is not locked");
        lock.unlock(); // should not crash
        check(!lock.isLocked(), "Still not locked after unlock on new lock");
    }

    // Clean up
    unlink(lockPath.c_str());

    std::cout << "\n";
}
