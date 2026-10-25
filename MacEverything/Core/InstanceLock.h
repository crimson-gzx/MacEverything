#pragma once
#include <string>

/// RAII file lock for single-instance enforcement.
/// Uses POSIX flock() for advisory locking to prevent multiple
/// app instances from corrupting shared WAL and index files.
class InstanceLock {
public:
    InstanceLock() = default;
    ~InstanceLock();

    InstanceLock(const InstanceLock&) = delete;
    InstanceLock& operator=(const InstanceLock&) = delete;

    /// Try to acquire the lock. Returns true if this is the only instance.
    bool tryLock(const std::string& lockFilePath);

    /// Release the lock.
    void unlock();

    /// Whether the lock is currently held.
    bool isLocked() const { return fd_ >= 0; }

private:
    int fd_ = -1;
    std::string path_;
};
