#pragma once

#include "aegisdb/common/config.hpp"
#include "aegisdb/common/status.hpp"
#include "aegisdb/storage/disk_manager.hpp"
#include "aegisdb/storage/page.hpp"

#include <cstdint>
#include <functional>
#include <list>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace aegisdb {

// Clock (second-chance) replacement, with an LRU fallback used when the clock
// hand has swept the whole pool without finding a victim (every frame pinned).
// Steal is allowed: dirty victims are written back. WAL is enforced by the
// caller: FlushLogTo(page.lsn) before WritePage.
class BufferPool {
 public:
  using FlushHook = std::function<void(std::uint64_t lsn)>;

  BufferPool(DiskManager* disk, std::size_t nframes, FlushHook flush_log = {})
      : disk_(disk), frames_(nframes), flush_log_(std::move(flush_log)) {}

  ~BufferPool() { FlushAll(); }

  Page* FetchPage(std::uint32_t page_id) {
    std::unique_lock<std::mutex> g(mu_);
    auto it = page_table_.find(page_id);
    if (it != page_table_.end()) {
      Page* p = &frames_[it->second];
      p->Pin();
      p->SetRef(true);
      TouchLru(it->second);
      return p;
    }
    std::size_t victim = FindVictimLocked();
    EvictLocked(victim);
    disk_->ReadPage(page_id, frames_[victim].Data());
    frames_[victim].Pin();
    frames_[victim].SetDirty(false);
    frames_[victim].SetRef(true);
    page_table_[page_id] = victim;
    TouchLru(victim);
    return &frames_[victim];
  }

  Page* NewPage(std::uint32_t* page_id, PageType type) {
    std::unique_lock<std::mutex> g(mu_);
    std::size_t victim = FindVictimLocked();
    EvictLocked(victim);
    *page_id = disk_->AllocatePage();
    frames_[victim].Init(*page_id, type);
    frames_[victim].Pin();
    frames_[victim].SetRef(true);
    page_table_[*page_id] = victim;
    TouchLru(victim);
    return &frames_[victim];
  }

  void UnpinPage(std::uint32_t page_id, bool dirty) {
    std::lock_guard<std::mutex> g(mu_);
    auto it = page_table_.find(page_id);
    if (it == page_table_.end()) return;
    Page* p = &frames_[it->second];
    if (dirty) p->SetDirty(true);
    p->Unpin();
  }

  void FlushPage(std::uint32_t page_id) {
    std::lock_guard<std::mutex> g(mu_);
    auto it = page_table_.find(page_id);
    if (it == page_table_.end()) return;
    WriteBackLocked(it->second);
  }

  void FlushAll() {
    std::lock_guard<std::mutex> g(mu_);
    for (std::size_t i = 0; i < frames_.size(); ++i) {
      if (frames_[i].GetPageId() != kInvalidPageId && frames_[i].IsDirty()) {
        WriteBackLocked(i);
      }
    }
  }

  std::size_t Size() const { return frames_.size(); }

  // Testing helpers.
  bool InPool(std::uint32_t page_id) {
    std::lock_guard<std::mutex> g(mu_);
    return page_table_.count(page_id) != 0;
  }

 private:
  void TouchLru(std::size_t idx) {
    auto it = lru_pos_.find(idx);
    if (it != lru_pos_.end()) lru_.erase(it->second);
    lru_.push_front(idx);
    lru_pos_[idx] = lru_.begin();
  }

  std::size_t FindVictimLocked() {
    // Prefer a free frame.
    for (std::size_t i = 0; i < frames_.size(); ++i) {
      if (frames_[i].GetPageId() == kInvalidPageId && frames_[i].PinCount() == 0) return i;
    }
    // Clock: skip pinned; if referenced, give a second chance.
    const std::size_t n = frames_.size();
    for (std::size_t sweep = 0; sweep < n * 2; ++sweep) {
      std::size_t i = clock_hand_;
      clock_hand_ = (clock_hand_ + 1) % n;
      if (frames_[i].PinCount() > 0) continue;
      if (frames_[i].Ref()) {
        frames_[i].SetRef(false);
        continue;
      }
      return i;
    }
    // LRU fallback among unpinned frames.
    for (auto idx : lru_) {
      if (idx < frames_.size() && frames_[idx].PinCount() == 0) return idx;
    }
    throw AegisError("buffer pool exhausted: all frames pinned");
  }

  void EvictLocked(std::size_t idx) {
    Page& p = frames_[idx];
    if (p.GetPageId() != kInvalidPageId) {
      if (p.IsDirty()) WriteBackLocked(idx);
      page_table_.erase(p.GetPageId());
    }
    p.Reset();
  }

  void WriteBackLocked(std::size_t idx) {
    Page& p = frames_[idx];
    if (p.GetPageId() == kInvalidPageId) return;
    if (flush_log_ && p.GetLsn() != kInvalidLsn) flush_log_(p.GetLsn());
    disk_->WritePage(p.GetPageId(), p.Data());
    p.SetDirty(false);
  }

  DiskManager* disk_;
  std::vector<Page> frames_;
  std::unordered_map<std::uint32_t, std::size_t> page_table_;
  std::size_t clock_hand_ = 0;
  std::list<std::size_t> lru_;
  std::unordered_map<std::size_t, std::list<std::size_t>::iterator> lru_pos_;
  std::mutex mu_;
  FlushHook flush_log_;
};

}  // namespace aegisdb
