#pragma once

#include "aegisdb/recovery/log_manager.hpp"
#include "aegisdb/storage/buffer_pool.hpp"
#include "aegisdb/storage/slotted_page.hpp"
#include "aegisdb/txn/txn_manager.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace aegisdb {

struct TxnTableEntry {
  std::uint64_t last_lsn = kInvalidLsn;
  TxnState state = TxnState::Growing;
};

struct DirtyPageEntry {
  std::uint64_t rec_lsn = kInvalidLsn;
};

// ARIES: Analysis, Redo, Undo. Steal / no-force: dirty pages may be written
// before commit (after their log records), and commit only forces the WAL.
class RecoveryManager {
 public:
  RecoveryManager(BufferPool* bpm, LogManager* log, TxnManager* txn) : bpm_(bpm), log_(log), txn_(txn) {}

  void Recover() {
    auto recs = log_->ReadAll();
    if (recs.empty()) return;

    // --- Analysis ---
    std::unordered_map<std::uint64_t, TxnTableEntry> att;
    std::unordered_map<std::uint32_t, DirtyPageEntry> dpt;
    std::size_t start = 0;
    for (std::size_t i = 0; i < recs.size(); ++i) {
      if (recs[i].type == LogType::Checkpoint) start = i;
    }
    for (std::size_t i = start; i < recs.size(); ++i) {
      const auto& r = recs[i];
      if (r.page_id != kInvalidPageId && dpt.find(r.page_id) == dpt.end()) {
        dpt[r.page_id].rec_lsn = r.lsn;
      }
      if (r.txn_id != kInvalidTxnId) {
        auto& e = att[r.txn_id];
        e.last_lsn = r.lsn;
        if (r.type == LogType::Commit) {
          e.state = TxnState::Committed;
          txn_->RecoverCommit(r.txn_id);
        } else if (r.type == LogType::Abort || r.type == LogType::Clr) {
          if (r.type == LogType::Abort) {
            e.state = TxnState::Aborted;
            txn_->RecoverAbort(r.txn_id);
          }
        } else {
          e.state = TxnState::Growing;
          txn_->NoteXid(r.txn_id);
        }
      }
    }

    // --- Redo (repeating history) ---
    std::uint64_t redo_lsn = ~0ULL;
    for (auto& [pid, e] : dpt) redo_lsn = std::min(redo_lsn, e.rec_lsn);
    if (dpt.empty()) redo_lsn = 0;
    for (const auto& r : recs) {
      if (r.lsn < redo_lsn) continue;
      Redo(r);
    }

    // --- Undo losers ---
    std::vector<LogRecord> losers;
    for (const auto& r : recs) {
      auto it = att.find(r.txn_id);
      if (it == att.end()) continue;
      if (it->second.state == TxnState::Committed || it->second.state == TxnState::Aborted) continue;
      losers.push_back(r);
    }
    std::sort(losers.begin(), losers.end(), [](const LogRecord& a, const LogRecord& b) { return a.lsn > b.lsn; });
    std::unordered_map<std::uint64_t, std::uint64_t> undo_next;  // skip undone via CLR
    for (const auto& r : losers) {
      auto it = undo_next.find(r.txn_id);
      if (it != undo_next.end() && r.lsn > it->second) continue;
      if (r.type == LogType::Clr) {
        undo_next[r.txn_id] = r.undo_next;
        continue;
      }
      Undo(r);
      LogRecord clr;
      clr.type = LogType::Clr;
      clr.txn_id = r.txn_id;
      clr.page_id = r.page_id;
      clr.slot_id = r.slot_id;
      clr.undo_next = r.prev_lsn;
      clr.after = r.before;  // compensation installs the before-image
      log_->Append(clr);
      undo_next[r.txn_id] = r.prev_lsn;
    }
    for (auto& [xid, e] : att) {
      if (e.state == TxnState::Growing) txn_->RecoverAbort(xid);
    }
    log_->Flush();
    bpm_->FlushAll();
  }

  void Checkpoint() {
    LogRecord rec;
    rec.type = LogType::Checkpoint;
    log_->Append(rec);
    log_->Flush();
    bpm_->FlushAll();
  }

 private:
  void Redo(const LogRecord& r) {
    if (r.page_id == kInvalidPageId) return;
    if (r.type != LogType::Insert && r.type != LogType::Update && r.type != LogType::Delete &&
        r.type != LogType::Clr && r.type != LogType::NewPage) {
      return;
    }
    Page* p = bpm_->FetchPage(r.page_id);
    if (p->GetLsn() >= r.lsn) {
      bpm_->UnpinPage(r.page_id, false);
      return;
    }
    Apply(p, r, /*redo=*/true);
    p->SetLsn(r.lsn);
    p->SetDirty(true);
    bpm_->UnpinPage(r.page_id, true);
  }

  void Undo(const LogRecord& r) {
    if (r.page_id == kInvalidPageId) return;
    if (r.type != LogType::Insert && r.type != LogType::Update && r.type != LogType::Delete) return;
    Page* p = bpm_->FetchPage(r.page_id);
    Apply(p, r, /*redo=*/false);
    p->SetDirty(true);
    bpm_->UnpinPage(r.page_id, true);
  }

  static void Apply(Page* p, const LogRecord& r, bool redo) {
    if (p->Header()->type == PageType::Invalid || p->Header()->magic != kMagic) {
      p->Init(r.page_id, PageType::Heap);
    }
    const std::vector<std::uint8_t>* img = nullptr;
    if (redo) {
      if (r.type == LogType::Delete) {
        SlottedPage::Delete(p, r.slot_id);
        return;
      }
      img = r.after.empty() ? &r.before : &r.after;
      if (r.type == LogType::Clr) img = &r.after;
    } else {
      if (r.type == LogType::Insert) {
        SlottedPage::Delete(p, r.slot_id);
        return;
      }
      img = &r.before;
    }
    if (!img || img->empty()) return;
    if (!SlottedPage::Update(p, r.slot_id, img->data(), static_cast<std::uint16_t>(img->size()))) {
      // Slot missing (never allocated): grow the directory and place the tuple.
      auto* h = p->Header();
      while (h->slot_count <= r.slot_id) {
        WriteSlot(p, h->slot_count, Slot{0, 0, kSlotEmpty});
        h->slot_count++;
        h->free_start = static_cast<std::uint16_t>(kPageHeaderSize + h->slot_count * kSlotSize);
      }
      if (p->FreeSpace() < img->size()) SlottedPage::MaybeCompact(p);
      if (p->FreeSpace() >= img->size()) {
        h->free_end = static_cast<std::uint16_t>(h->free_end - img->size());
        std::memcpy(p->Data() + h->free_end, img->data(), img->size());
        WriteSlot(p, r.slot_id, Slot{h->free_end, static_cast<std::uint16_t>(img->size()), 0});
        p->SetDirty(true);
      }
    }
  }

  BufferPool* bpm_;
  LogManager* log_;
  TxnManager* txn_;
};

}  // namespace aegisdb
