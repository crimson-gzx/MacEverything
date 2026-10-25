#include "InstanceLock.h"
#include "Logger.h"
#include <sys/file.h>
#include <unistd.h>
#include <fcntl.h>

InstanceLock::~InstanceLock() {
    unlock();
}

bool InstanceLock::tryLock(const std::string& lockFilePath) {
    if (fd_ >= 0) return true; // already locked

    fd_ = open(lockFilePath.c_str(), O_CREAT | O_RDWR, 0600);
    if (fd_ < 0) {
        LOG_ERROR("InstanceLock", "Failed to open lock file: " << lockFilePath);
        return false;
    }

    if (flock(fd_, LOCK_EX | LOCK_NB) != 0) {
        LOG_WARN("InstanceLock", "Another instance is already running (lock file: " << lockFilePath << ")");
        close(fd_);
        fd_ = -1;
        return false;
    }

    path_ = lockFilePath;
    LOG_INFO("InstanceLock", "Instance lock acquired: " << lockFilePath);
    return true;
}

void InstanceLock::unlock() {
    if (fd_ >= 0) {
        flock(fd_, LOCK_UN);
        close(fd_);
        fd_ = -1;
        LOG_INFO("InstanceLock", "Instance lock released: " << path_);
    }
}
