#pragma once

#include "aegisdb/common/config.hpp"
#include "aegisdb/common/latch.hpp"

#include <atomic>
#include <cstdint>
#include <cstring>

namespace aegisdb {

enum class PageType : std::uint8_t {
  Invalid = 0,
  Superblock = 1,
  Heap = 2,
  BPlusInternal = 3,
  BPlusLeaf = 4,
  HashDirectory = 5,
  HashBucket = 6,
  Fsm = 7,
  Catalog = 8,
};

// Page header lives in the first bytes of every 4 KiB page.
// Heap slotted layout (after header):
//   slots grow forward from kPageHeaderSize
//   tuples grow backward from PAGE_SIZE
struct PageHeader {
  std::uint32_t magic;
  std::uint32_t page_id;
  std::uint64_t lsn;
  PageType type;
  std::uint8_t flags;
  std::uint16_t slot_count;
  std::uint16_t free_start;  // first free byte after slot directory
  std::uint16_t free_end;    // first used tuple byte (from the end)
  std::uint32_t next_page;
  std::uint32_t prev_page;
  std::uint16_t extra;  // nkeys for index pages, local_depth for hash, etc.
  std::uint16_t extra2;
};

static_assert(sizeof(PageHeader) <= 48, "keep the header compact");
inline constexpr std::uint16_t kPageHeaderSize = 48;

class Page {
 public:
  Page() { Reset(); }

  char* Data() { return data_; }
  const char* Data() const { return data_; }

  PageHeader* Header() { return reinterpret_cast<PageHeader*>(data_); }
  const PageHeader* Header() const { return reinterpret_cast<const PageHeader*>(data_); }

  void Reset() {
    std::memset(data_, 0, kPageSize);
    auto* h = Header();
    h->magic = kMagic;
    h->page_id = kInvalidPageId;
    h->lsn = kInvalidLsn;
    h->type = PageType::Invalid;
    h->free_start = kPageHeaderSize;
    h->free_end = kPageSize;
    h->next_page = kInvalidPageId;
    h->prev_page = kInvalidPageId;
    pin_count_ = 0;
    dirty_ = false;
    ref_ = false;
  }

  void Init(std::uint32_t page_id, PageType type) {
    Reset();
    auto* h = Header();
    h->page_id = page_id;
    h->type = type;
    dirty_ = true;
  }

  std::uint32_t GetPageId() const { return Header()->page_id; }
  std::uint64_t GetLsn() const { return Header()->lsn; }
  void SetLsn(std::uint64_t lsn) { Header()->lsn = lsn; }

  int PinCount() const { return pin_count_.load(std::memory_order_relaxed); }
  void Pin() { pin_count_.fetch_add(1, std::memory_order_relaxed); }
  void Unpin() { pin_count_.fetch_sub(1, std::memory_order_relaxed); }

  bool IsDirty() const { return dirty_; }
  void SetDirty(bool d) { dirty_ = d; }

  bool Ref() const { return ref_; }
  void SetRef(bool r) { ref_ = r; }

  Latch& GetLatch() { return latch_; }

  std::uint16_t FreeSpace() const {
    const auto* h = Header();
    if (h->free_end < h->free_start) return 0;
    return static_cast<std::uint16_t>(h->free_end - h->free_start);
  }

 private:
  alignas(8) char data_[kPageSize];
  std::atomic<int> pin_count_{0};
  bool dirty_ = false;
  bool ref_ = false;
  Latch latch_;
};

}  // namespace aegisdb
