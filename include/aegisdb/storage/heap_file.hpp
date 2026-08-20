#pragma once

#include "aegisdb/common/rid.hpp"
#include "aegisdb/common/types.hpp"
#include "aegisdb/storage/buffer_pool.hpp"
#include "aegisdb/storage/free_space_map.hpp"
#include "aegisdb/storage/slotted_page.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace aegisdb {

class HeapFile {
 public:
  using LogHook = std::function<std::uint64_t(std::uint32_t page_id, std::uint16_t slot, const char* kind,
                                              const std::vector<std::uint8_t>& before,
                                              const std::vector<std::uint8_t>& after)>;

  HeapFile(BufferPool* bpm, FreeSpaceMap* fsm, std::uint32_t first_page, Schema schema)
      : bpm_(bpm), fsm_(fsm), first_(first_page), schema_(std::move(schema)) {}

  static std::uint32_t Create(BufferPool* bpm, FreeSpaceMap* fsm) {
    std::uint32_t pid = 0;
    Page* p = bpm->NewPage(&pid, PageType::Heap);
    SlottedPage::Init(p, pid);
    p->SetDirty(true);
    if (fsm) fsm->Set(pid, p->FreeSpace());
    bpm->UnpinPage(pid, true);
    return pid;
  }

  RID Insert(const Tuple& t) {
    auto bytes = SerializeTuple(t);
    if (bytes.size() > kMaxTupleSize) throw AegisError("tuple too large for a page");
    const auto need = static_cast<std::uint16_t>(bytes.size() + kSlotSize);

    auto try_page = [&](std::uint32_t pid) -> std::optional<RID> {
      Page* page = bpm_->FetchPage(pid);
      auto slot = SlottedPage::Insert(page, bytes.data(), static_cast<std::uint16_t>(bytes.size()));
      if (!slot) {
        bpm_->UnpinPage(pid, false);
        return std::nullopt;
      }
      if (log_) {
        std::vector<std::uint8_t> empty;
        auto lsn = log_(pid, *slot, "INSERT", empty, bytes);
        page->SetLsn(lsn);
      }
      page->SetDirty(true);
      if (fsm_) fsm_->Set(pid, page->FreeSpace());
      bpm_->UnpinPage(pid, true);
      return RID{pid, *slot};
    };

    // Stay inside this heap's page chain. The FSM is a hint, never a cross-file allocator.
    if (fsm_) {
      std::uint32_t hint = fsm_->Find(need);
      if (hint != kInvalidPageId) {
        bool ours = false;
        for (std::uint32_t p = first_; p != kInvalidPageId && !ours;) {
          if (p == hint) ours = true;
          Page* pg = bpm_->FetchPage(p);
          std::uint32_t next = pg->Header()->next_page;
          bpm_->UnpinPage(p, false);
          p = next;
          if (p == hint) ours = true;
        }
        if (ours) {
          if (auto rid = try_page(hint)) {
            ++tuple_count_;
            return *rid;
          }
        }
      }
    }
    for (std::uint32_t pid = first_; pid != kInvalidPageId;) {
      Page* page = bpm_->FetchPage(pid);
      std::uint32_t next = page->Header()->next_page;
      std::uint16_t free_sp = page->FreeSpace();
      bpm_->UnpinPage(pid, false);
      if (free_sp >= need) {
        if (auto rid = try_page(pid)) {
          ++tuple_count_;
          return *rid;
        }
      }
      if (next == kInvalidPageId) break;
      pid = next;
    }
    Page* page = AppendPage();
    std::uint32_t pid = page->GetPageId();
    auto slot = SlottedPage::Insert(page, bytes.data(), static_cast<std::uint16_t>(bytes.size()));
    if (!slot) {
      bpm_->UnpinPage(pid, false);
      throw AegisError("tuple does not fit on a fresh heap page");
    }
    RID rid{pid, *slot};
    if (log_) {
      std::vector<std::uint8_t> empty;
      auto lsn = log_(pid, *slot, "INSERT", empty, bytes);
      page->SetLsn(lsn);
    }
    page->SetDirty(true);
    if (fsm_) fsm_->Set(pid, page->FreeSpace());
    bpm_->UnpinPage(pid, true);
    ++tuple_count_;
    return rid;
  }

  std::optional<Tuple> Get(RID rid) {
    Page* page = bpm_->FetchPage(rid.page_id);
    auto raw = SlottedPage::Read(page, rid.slot_id);
    bpm_->UnpinPage(rid.page_id, false);
    if (!raw) return std::nullopt;
    return DeserializeTuple(raw->data(), raw->size(), &schema_);
  }

  bool Update(RID rid, const Tuple& t) {
    auto bytes = SerializeTuple(t);
    Page* page = bpm_->FetchPage(rid.page_id);
    auto before = SlottedPage::Read(page, rid.slot_id);
    bool ok = SlottedPage::Update(page, rid.slot_id, bytes.data(), static_cast<std::uint16_t>(bytes.size()));
    if (ok && log_ && before) {
      auto lsn = log_(rid.page_id, rid.slot_id, "UPDATE", *before, bytes);
      page->SetLsn(lsn);
    }
    if (fsm_) fsm_->Set(rid.page_id, page->FreeSpace());
    bpm_->UnpinPage(rid.page_id, ok);
    return ok;
  }

  bool Delete(RID rid) {
    Page* page = bpm_->FetchPage(rid.page_id);
    auto before = SlottedPage::Read(page, rid.slot_id);
    bool ok = SlottedPage::Delete(page, rid.slot_id);
    if (ok && log_ && before) {
      std::vector<std::uint8_t> empty;
      auto lsn = log_(rid.page_id, rid.slot_id, "DELETE", *before, empty);
      page->SetLsn(lsn);
    }
    if (fsm_) fsm_->Set(rid.page_id, page->FreeSpace());
    bpm_->UnpinPage(rid.page_id, ok);
    if (ok && tuple_count_ > 0) --tuple_count_;
    return ok;
  }

  template <typename Fn>
  void Scan(Fn&& fn) {
    std::uint32_t pid = first_;
    while (pid != kInvalidPageId) {
      Page* page = bpm_->FetchPage(pid);
      auto* h = page->Header();
      const std::uint16_t n = h->slot_count;
      const std::uint32_t next = h->next_page;
      for (std::uint16_t i = 0; i < n; ++i) {
        auto raw = SlottedPage::Read(page, i);
        if (!raw) continue;
        Tuple t = DeserializeTuple(raw->data(), raw->size(), &schema_);
        if (!fn(RID{pid, i}, t)) {
          bpm_->UnpinPage(pid, false);
          return;
        }
      }
      bpm_->UnpinPage(pid, false);
      pid = next;
    }
  }

  std::uint32_t FirstPage() const { return first_; }
  const Schema& GetSchema() const { return schema_; }
  std::uint64_t TupleCount() const { return tuple_count_; }
  void SetTupleCount(std::uint64_t n) { tuple_count_ = n; }
  void SetLogHook(LogHook h) { log_ = std::move(h); }

 private:
  Page* AppendPage() {
    std::uint32_t new_id = 0;
    Page* np = bpm_->NewPage(&new_id, PageType::Heap);
    // Link at the tail.
    std::uint32_t pid = first_;
    Page* prev = nullptr;
    std::uint32_t prev_id = kInvalidPageId;
    while (pid != kInvalidPageId) {
      Page* p = bpm_->FetchPage(pid);
      if (p->Header()->next_page == kInvalidPageId) {
        prev = p;
        prev_id = pid;
        break;
      }
      std::uint32_t next = p->Header()->next_page;
      bpm_->UnpinPage(pid, false);
      pid = next;
    }
    SlottedPage::Init(np, new_id, prev_id, kInvalidPageId);
    if (prev) {
      prev->Header()->next_page = new_id;
      prev->SetDirty(true);
      bpm_->UnpinPage(prev_id, true);
    } else {
      first_ = new_id;
    }
    if (fsm_) fsm_->Set(new_id, np->FreeSpace());
    return np;
  }

  BufferPool* bpm_;
  FreeSpaceMap* fsm_;
  std::uint32_t first_;
  Schema schema_;
  LogHook log_;
  std::uint64_t tuple_count_ = 0;
};

}  // namespace aegisdb
