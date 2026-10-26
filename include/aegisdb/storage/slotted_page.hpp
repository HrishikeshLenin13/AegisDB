#pragma once

#include "aegisdb/common/rid.hpp"
#include "aegisdb/storage/page.hpp"
#include "aegisdb/storage/tuple.hpp"

#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

namespace aegisdb {

// Slot directory entry: offset of the tuple from the start of the page, size,
// and flags (0 = occupied, 1 = empty/reusable).
struct Slot {
  std::uint16_t offset;
  std::uint16_t size;
  std::uint16_t flags;
};

inline constexpr std::uint16_t kSlotEmpty = 1;
inline constexpr std::uint16_t kSlotSize = 6;

inline Slot ReadSlot(const Page* page, std::uint16_t i) {
  const char* p = page->Data() + kPageHeaderSize + static_cast<std::size_t>(i) * kSlotSize;
  Slot s;
  std::memcpy(&s.offset, p, 2);
  std::memcpy(&s.size, p + 2, 2);
  std::memcpy(&s.flags, p + 4, 2);
  return s;
}

inline void WriteSlot(Page* page, std::uint16_t i, Slot s) {
  char* p = page->Data() + kPageHeaderSize + static_cast<std::size_t>(i) * kSlotSize;
  std::memcpy(p, &s.offset, 2);
  std::memcpy(p + 2, &s.size, 2);
  std::memcpy(p + 4, &s.flags, 2);
}

class SlottedPage {
 public:
  static void Init(Page* page, std::uint32_t page_id, std::uint32_t prev = kInvalidPageId,
                   std::uint32_t next = kInvalidPageId) {
    page->Init(page_id, PageType::Heap);
    auto* h = page->Header();
    h->prev_page = prev;
    h->next_page = next;
    h->slot_count = 0;
    h->free_start = kPageHeaderSize;
    h->free_end = kPageSize;
  }

  static std::optional<std::uint16_t> Insert(Page* page, const std::uint8_t* data, std::uint16_t size) {
    auto* h = page->Header();
    // Reuse an empty slot if the tuple fits in the current free region.
    std::int32_t reuse = -1;
    for (std::uint16_t i = 0; i < h->slot_count; ++i) {
      Slot s = ReadSlot(page, i);
      if (s.flags & kSlotEmpty) {
        reuse = i;
        break;
      }
    }
    const std::uint16_t dir_grow = (reuse < 0) ? kSlotSize : 0;
    if (page->FreeSpace() < size + dir_grow) return std::nullopt;
    h->free_end = static_cast<std::uint16_t>(h->free_end - size);
    std::memcpy(page->Data() + h->free_end, data, size);
    Slot s{h->free_end, size, 0};
    std::uint16_t slot_id;
    if (reuse >= 0) {
      slot_id = static_cast<std::uint16_t>(reuse);
      WriteSlot(page, slot_id, s);
    } else {
      slot_id = h->slot_count;
      WriteSlot(page, slot_id, s);
      h->slot_count++;
      h->free_start = static_cast<std::uint16_t>(kPageHeaderSize + h->slot_count * kSlotSize);
    }
    page->SetDirty(true);
    return slot_id;
  }

  static bool Delete(Page* page, std::uint16_t slot_id) {
    auto* h = page->Header();
    if (slot_id >= h->slot_count) return false;
    Slot s = ReadSlot(page, slot_id);
    if (s.flags & kSlotEmpty) return false;
    s.flags |= kSlotEmpty;
    WriteSlot(page, slot_id, s);
    page->SetDirty(true);
    MaybeCompact(page);
    return true;
  }

  static bool Update(Page* page, std::uint16_t slot_id, const std::uint8_t* data, std::uint16_t size) {
    auto* h = page->Header();
    if (slot_id >= h->slot_count) return false;
    Slot s = ReadSlot(page, slot_id);
    if (s.flags & kSlotEmpty) return false;
    if (size <= s.size) {
      std::memcpy(page->Data() + s.offset, data, size);
      s.size = size;
      WriteSlot(page, slot_id, s);
      page->SetDirty(true);
      return true;
    }
    // Delete logically then insert at a new offset, keeping the same slot id.
    s.flags |= kSlotEmpty;
    WriteSlot(page, slot_id, s);
    if (page->FreeSpace() < size) {
      s.flags = 0;
      WriteSlot(page, slot_id, s);
      return false;
    }
    h->free_end = static_cast<std::uint16_t>(h->free_end - size);
    std::memcpy(page->Data() + h->free_end, data, size);
    WriteSlot(page, slot_id, Slot{h->free_end, size, 0});
    page->SetDirty(true);
    return true;
  }

  static std::optional<std::vector<std::uint8_t>> Read(const Page* page, std::uint16_t slot_id) {
    const auto* h = page->Header();
    if (slot_id >= h->slot_count) return std::nullopt;
    Slot s = ReadSlot(page, slot_id);
    if (s.flags & kSlotEmpty) return std::nullopt;
    std::vector<std::uint8_t> out(s.size);
    std::memcpy(out.data(), page->Data() + s.offset, s.size);
    return out;
  }

  static std::size_t LiveTupleCount(const Page* page) {
    std::size_t count = 0;
    for (std::uint16_t i = 0; i < page->Header()->slot_count; ++i)
      if (!(ReadSlot(page, i).flags & kSlotEmpty)) ++count;
    return count;
  }

  static std::size_t LiveBytes(const Page* page) {
    std::size_t bytes = 0;
    for (std::uint16_t i = 0; i < page->Header()->slot_count; ++i) {
      auto slot = ReadSlot(page, i);
      if (!(slot.flags & kSlotEmpty)) bytes += slot.size;
    }
    return bytes;
  }

  static void MaybeCompact(Page* page) {
    auto* h = page->Header();
    // Compact if free space is fragmented (more than 25% wasted vs live tuples).
    std::uint32_t live = 0;
    for (std::uint16_t i = 0; i < h->slot_count; ++i) {
      Slot s = ReadSlot(page, i);
      if (!(s.flags & kSlotEmpty)) live += s.size;
    }
    const std::uint32_t occupied = kPageSize - h->free_end + (h->free_start - kPageHeaderSize);
    if (occupied < live + 256) return;
    std::vector<std::vector<std::uint8_t>> tuples(h->slot_count);
    std::vector<bool> empty(h->slot_count);
    for (std::uint16_t i = 0; i < h->slot_count; ++i) {
      Slot s = ReadSlot(page, i);
      empty[i] = s.flags & kSlotEmpty;
      if (!empty[i]) {
        tuples[i].resize(s.size);
        std::memcpy(tuples[i].data(), page->Data() + s.offset, s.size);
      }
    }
    std::uint16_t end = kPageSize;
    for (std::uint16_t i = 0; i < h->slot_count; ++i) {
      if (empty[i]) {
        WriteSlot(page, i, Slot{0, 0, kSlotEmpty});
        continue;
      }
      end = static_cast<std::uint16_t>(end - tuples[i].size());
      std::memcpy(page->Data() + end, tuples[i].data(), tuples[i].size());
      WriteSlot(page, i, Slot{end, static_cast<std::uint16_t>(tuples[i].size()), 0});
    }
    h->free_end = end;
    page->SetDirty(true);
  }
};

}  // namespace aegisdb
