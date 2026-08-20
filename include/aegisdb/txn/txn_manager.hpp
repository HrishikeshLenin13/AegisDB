#pragma once

#include "aegisdb/common/types.hpp"
#include "aegisdb/recovery/log_manager.hpp"
#include "aegisdb/txn/lock_manager.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace aegisdb {

struct Snapshot {
  std::uint64_t reader_xid = 0;
  std::uint64_t xmax = 0;  // next xid at Begin
  std::unordered_set<std::uint64_t> active;
};

struct Transaction {
  std::uint64_t xid = 0;
  std::uint64_t prev_lsn = kInvalidLsn;
  TxnState state = TxnState::Growing;
  Snapshot snap;
  std::vector<RID> writes;
};

class TxnManager {
 public:
  explicit TxnManager(LogManager* log) : log_(log) {}

  std::shared_ptr<Transaction> Begin() {
    std::lock_guard<std::mutex> g(mu_);
    auto t = std::make_shared<Transaction>();
    t->xid = next_xid_++;
    t->snap.reader_xid = t->xid;
    t->snap.xmax = next_xid_;
    t->snap.active = active_;
    active_.insert(t->xid);
    txns_[t->xid] = t;
    LogRecord rec;
    rec.type = LogType::Begin;
    rec.txn_id = t->xid;
    rec.prev_lsn = kInvalidLsn;
    t->prev_lsn = log_->Append(rec);
    return t;
  }

  void Commit(const std::shared_ptr<Transaction>& t) {
    std::lock_guard<std::mutex> g(mu_);
    LogRecord rec;
    rec.type = LogType::Commit;
    rec.txn_id = t->xid;
    rec.prev_lsn = t->prev_lsn;
    t->prev_lsn = log_->Append(rec);
    log_->Flush();  // commit is durable when the COMMIT record hits disk
    t->state = TxnState::Committed;
    committed_[t->xid] = t->prev_lsn;
    active_.erase(t->xid);
    locks_.UnlockAll(t->xid);
  }

  void Abort(const std::shared_ptr<Transaction>& t) {
    std::lock_guard<std::mutex> g(mu_);
    LogRecord rec;
    rec.type = LogType::Abort;
    rec.txn_id = t->xid;
    rec.prev_lsn = t->prev_lsn;
    t->prev_lsn = log_->Append(rec);
    t->state = TxnState::Aborted;
    aborted_.insert(t->xid);
    active_.erase(t->xid);
    locks_.UnlockAll(t->xid);
  }

  bool IsCommitted(std::uint64_t xid) const {
    std::lock_guard<std::mutex> g(mu_);
    return committed_.count(xid) != 0;
  }

  // Snapshot isolation visibility over version-chained tuples (xmin/xmax).
  bool Visible(const Tuple& tup, const Snapshot& snap) const {
    std::lock_guard<std::mutex> g(mu_);
    return VisibleLocked(tup, snap);
  }

  bool VisibleLocked(const Tuple& tup, const Snapshot& snap) const {
    auto created = tup.xmin;
    auto deleted = tup.xmax;
    if (created == snap.reader_xid) {
      return deleted == 0 || deleted != snap.reader_xid;
    }
    if (aborted_.count(created) || (!committed_.count(created) && created != snap.reader_xid)) {
      return false;
    }
    if (created >= snap.xmax || snap.active.count(created)) return false;
    if (deleted == 0) return true;
    if (deleted == snap.reader_xid) return false;
    if (aborted_.count(deleted) || !committed_.count(deleted)) return true;
    if (deleted >= snap.xmax || snap.active.count(deleted)) return true;
    return false;
  }

  bool Lock(std::uint64_t xid, RID rid, LockMode mode) { return locks_.Lock(xid, rid, mode); }
  void UnlockAll(std::uint64_t xid) { locks_.UnlockAll(xid); }

  std::uint64_t Log(const std::shared_ptr<Transaction>& t, LogType type, std::uint32_t page, std::uint16_t slot,
                    const std::vector<std::uint8_t>& before, const std::vector<std::uint8_t>& after) {
    LogRecord rec;
    rec.type = type;
    rec.txn_id = t->xid;
    rec.prev_lsn = t->prev_lsn;
    rec.page_id = page;
    rec.slot_id = slot;
    rec.before = before;
    rec.after = after;
    t->prev_lsn = log_->Append(rec);
    return t->prev_lsn;
  }

  void RecoverCommit(std::uint64_t xid) {
    std::lock_guard<std::mutex> g(mu_);
    committed_[xid] = 1;
    active_.erase(xid);
    next_xid_ = std::max(next_xid_, xid + 1);
  }
  void RecoverAbort(std::uint64_t xid) {
    std::lock_guard<std::mutex> g(mu_);
    aborted_.insert(xid);
    active_.erase(xid);
    next_xid_ = std::max(next_xid_, xid + 1);
  }
  void NoteXid(std::uint64_t xid) {
    std::lock_guard<std::mutex> g(mu_);
    next_xid_ = std::max(next_xid_, xid + 1);
  }

  LockManager& Locks() { return locks_; }

 private:
  LogManager* log_;
  mutable std::mutex mu_;
  std::uint64_t next_xid_ = 1;
  std::unordered_set<std::uint64_t> active_;
  std::unordered_map<std::uint64_t, std::uint64_t> committed_;
  std::unordered_set<std::uint64_t> aborted_;
  std::unordered_map<std::uint64_t, std::shared_ptr<Transaction>> txns_;
  LockManager locks_;
};

}  // namespace aegisdb
