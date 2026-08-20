#pragma once

#include "aegisdb/storage/buffer_pool.hpp"
#include "aegisdb/storage/page.hpp"

#include <algorithm>
#include <cstdint>

namespace aegisdb {

// Page-level free-space map. Each heap page is represented by one byte:
// free_space / 16, so 0 means full and 255 means ~4 KiB free.
// FSM pages are chained from the superblock; each holds (kPageSize - header)
// entries, covering that many heap pages.
class FreeSpaceMap {
 public:
  FreeSpaceMap(BufferPool* bpm, std::uint32_t first_page) : bpm_(bpm), first_(first_page) {}

  static std::uint32_t Create(BufferPool* bpm) {
    std::uint32_t pid = 0;
    Page* p = bpm->NewPage(&pid, PageType::Fsm);
    p->Header()->next_page = kInvalidPageId;
    p->SetDirty(true);
    bpm->UnpinPage(pid, true);
    return pid;
  }

  void Set(std::uint32_t heap_page, std::uint16_t free_bytes) {
    const std::uint8_t cat = static_cast<std::uint8_t>(std::min<std::uint16_t>(free_bytes / 16, 255));
    auto loc = Locate(heap_page, true);
    Page* p = bpm_->FetchPage(loc.fsm_page);
    p->Data()[kPageHeaderSize + loc.offset] = static_cast<char>(cat);
    p->SetDirty(true);
    bpm_->UnpinPage(loc.fsm_page, true);
  }

  // First heap page with at least `need` free bytes, or kInvalidPageId.
  std::uint32_t Find(std::uint16_t need) {
    const std::uint8_t want = static_cast<std::uint8_t>(std::min<std::uint16_t>((need + 15) / 16, 255));
    std::uint32_t pid = first_;
    std::uint32_t base = 0;
    const std::uint32_t cap = kPageSize - kPageHeaderSize;
    while (pid != kInvalidPageId) {
      Page* p = bpm_->FetchPage(pid);
      for (std::uint32_t i = 0; i < cap; ++i) {
        auto cat = static_cast<std::uint8_t>(p->Data()[kPageHeaderSize + i]);
        if (cat >= want && cat > 0) {
          std::uint32_t heap = base + i;
          bpm_->UnpinPage(pid, false);
          return heap;
        }
      }
      std::uint32_t next = p->Header()->next_page;
      bpm_->UnpinPage(pid, false);
      pid = next;
      base += cap;
    }
    return kInvalidPageId;
  }

  std::uint32_t FirstPage() const { return first_; }

 private:
  struct Loc {
    std::uint32_t fsm_page;
    std::uint32_t offset;
  };

  Loc Locate(std::uint32_t heap_page, bool grow) {
    const std::uint32_t cap = kPageSize - kPageHeaderSize;
    std::uint32_t idx = heap_page;
    std::uint32_t pid = first_;
    while (true) {
      Page* p = bpm_->FetchPage(pid);
      if (idx < cap) {
        Loc loc{pid, idx};
        bpm_->UnpinPage(pid, false);
        return loc;
      }
      idx -= cap;
      std::uint32_t next = p->Header()->next_page;
      if (next == kInvalidPageId) {
        if (!grow) {
          bpm_->UnpinPage(pid, false);
          return Loc{pid, 0};
        }
        Page* np = bpm_->NewPage(&next, PageType::Fsm);
        np->Header()->next_page = kInvalidPageId;
        np->SetDirty(true);
        p->Header()->next_page = next;
        p->SetDirty(true);
        bpm_->UnpinPage(next, true);
      }
      bpm_->UnpinPage(pid, p->IsDirty());
      pid = next;
    }
  }

  BufferPool* bpm_;
  std::uint32_t first_;
};

}  // namespace aegisdb
