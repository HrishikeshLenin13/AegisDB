#pragma once

#include <mutex>
#include <shared_mutex>

namespace aegisdb {

class Latch {
 public:
  void Lock() { mu_.lock(); }
  void Unlock() { mu_.unlock(); }
  void LockShared() { mu_.lock_shared(); }
  void UnlockShared() { mu_.unlock_shared(); }

 private:
  std::shared_mutex mu_;
};

class UniqueLatchGuard {
 public:
  explicit UniqueLatchGuard(Latch& l) : l_(&l) { l_->Lock(); }
  ~UniqueLatchGuard() {
    if (l_) l_->Unlock();
  }
  UniqueLatchGuard(const UniqueLatchGuard&) = delete;
  UniqueLatchGuard& operator=(const UniqueLatchGuard&) = delete;

 private:
  Latch* l_;
};

class SharedLatchGuard {
 public:
  explicit SharedLatchGuard(Latch& l) : l_(&l) { l_->LockShared(); }
  ~SharedLatchGuard() {
    if (l_) l_->UnlockShared();
  }
  SharedLatchGuard(const SharedLatchGuard&) = delete;
  SharedLatchGuard& operator=(const SharedLatchGuard&) = delete;

 private:
  Latch* l_;
};

}  // namespace aegisdb
