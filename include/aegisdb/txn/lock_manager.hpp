#pragma once

#include "aegisdb/common/rid.hpp"
#include "aegisdb/common/status.hpp"

#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace aegisdb {

enum class LockMode : std::uint8_t { Shared = 1, Exclusive = 2 };

enum class TxnState : std::uint8_t { Growing = 1, Shrinking = 2, Committed = 3, Aborted = 4 };

// Strict 2PL lock table. Wait-die: a younger waiter (higher xid) aborts
// rather than waiting for an older holder, which prevents deadlock cycles.
class LockManager {
 public:
  bool Lock(std::uint64_t xid, RID rid, LockMode mode) {
    std::unique_lock<std::mutex> g(mu_);
    auto& queue = table_[rid];
    while (true) {
      if (Conflicts(queue, xid, mode)) {
        std::uint64_t oldest_holder = ~0ULL;
        for (auto& gnt : queue.granted) {
          if (ConflictsMode(gnt.mode, mode) && gnt.xid != xid) oldest_holder = std::min(oldest_holder, gnt.xid);
        }
        if (xid > oldest_holder) {
          // Younger than holder: die.
          return false;
        }
        cv_.wait(g);
        continue;
      }
      // Upgrade or grant.
      for (auto& gnt : queue.granted) {
        if (gnt.xid == xid) {
          if (mode == LockMode::Exclusive) gnt.mode = LockMode::Exclusive;
          held_[xid].insert(rid);
          return true;
        }
      }
      queue.granted.push_back(Grant{xid, mode});
      held_[xid].insert(rid);
      return true;
    }
  }

  void UnlockAll(std::uint64_t xid) {
    std::lock_guard<std::mutex> g(mu_);
    auto it = held_.find(xid);
    if (it == held_.end()) return;
    for (RID rid : it->second) {
      auto& q = table_[rid];
      q.granted.erase(std::remove_if(q.granted.begin(), q.granted.end(),
                                     [&](const Grant& x) { return x.xid == xid; }),
                      q.granted.end());
      if (q.granted.empty()) table_.erase(rid);
    }
    held_.erase(it);
    cv_.notify_all();
  }

 private:
  struct Grant {
    std::uint64_t xid;
    LockMode mode;
  };
  struct Queue {
    std::vector<Grant> granted;
  };

  static bool ConflictsMode(LockMode a, LockMode b) {
    return a == LockMode::Exclusive || b == LockMode::Exclusive;
  }

  static bool Conflicts(const Queue& q, std::uint64_t xid, LockMode mode) {
    for (const auto& g : q.granted) {
      if (g.xid == xid) {
        if (mode == LockMode::Shared) return false;
        return q.granted.size() > 1;  // upgrade needs to be alone
      }
      if (ConflictsMode(g.mode, mode)) return true;
    }
    return false;
  }

  std::mutex mu_;
  std::condition_variable cv_;
  std::unordered_map<RID, Queue, RIDHash> table_;
  std::unordered_map<std::uint64_t, std::unordered_set<RID, RIDHash>> held_;
};

}  // namespace aegisdb
