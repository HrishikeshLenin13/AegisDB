#pragma once

#include "aegisdb/common/rid.hpp"
#include "aegisdb/common/types.hpp"
#include "aegisdb/index/bloom_filter.hpp"
#include "aegisdb/storage/buffer_pool.hpp"
#include "aegisdb/storage/page.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace aegisdb {

// Extendible hashing: a directory of 2^global_depth bucket pointers, each
// bucket carrying a local_depth. Overflow of a bucket either splits it
// (and doubles the directory when local == global) or chains an overflow page
// once global depth hits the single-page directory limit.
class ExtendibleHash {
 public:
  static constexpr int kMaxGlobalDepth = 9;  // 512 directory entries fit in 4 KiB

  ExtendibleHash(BufferPool* bpm, std::uint32_t dir_page) : bpm_(bpm), dir_(dir_page) {
    bloom_.Reset(4096, 0.01);
  }

  static std::uint32_t Create(BufferPool* bpm) {
    std::uint32_t bucket = 0;
    Page* b = bpm->NewPage(&bucket, PageType::HashBucket);
    b->Header()->extra = 0;   // nkeys
    b->Header()->extra2 = 0;  // local depth
    b->Header()->next_page = kInvalidPageId;
    b->SetDirty(true);
    bpm->UnpinPage(bucket, true);

    std::uint32_t dir = 0;
    Page* d = bpm->NewPage(&dir, PageType::HashDirectory);
    d->Header()->extra = 0;  // global depth
    WriteDirEntry(d, 0, bucket);
    d->SetDirty(true);
    bpm->UnpinPage(dir, true);
    return dir;
  }

  std::uint32_t DirPage() const { return dir_; }

  void Insert(const Value& key, RID rid) {
    std::string k = key.EncodeKey();
    bloom_.Add(k);
    auto h = Hash64(k);
    Page* d = bpm_->FetchPage(dir_);
    int g = d->Header()->extra;
    std::uint32_t idx = static_cast<std::uint32_t>(h & Mask(g));
    std::uint32_t bucket = ReadDirEntry(d, idx);
    bpm_->UnpinPage(dir_, false);
    if (!InsertIntoBucket(bucket, k, rid)) SplitOrOverflow(idx, bucket, k, rid);
  }

  std::vector<RID> Search(const Value& key) {
    std::string k = key.EncodeKey();
    if (!bloom_.MaybeContains(k)) return {};
    auto h = Hash64(k);
    Page* d = bpm_->FetchPage(dir_);
    int g = d->Header()->extra;
    std::uint32_t bucket = ReadDirEntry(d, static_cast<std::uint32_t>(h & Mask(g)));
    bpm_->UnpinPage(dir_, false);
    std::vector<RID> out;
    std::uint32_t pid = bucket;
    while (pid != kInvalidPageId) {
      Page* p = bpm_->FetchPage(pid);
      auto keys = ReadKeys(p);
      for (std::size_t i = 0; i < keys.size(); ++i) {
        if (keys[i] == k) out.push_back(ReadRid(p, static_cast<std::uint16_t>(i)));
      }
      std::uint32_t next = p->Header()->next_page;
      bpm_->UnpinPage(pid, false);
      pid = next;
    }
    return out;
  }

  bool MaybeContains(const Value& key) { return bloom_.MaybeContains(key.EncodeKey()); }

 private:
  static std::uint32_t Mask(int depth) { return depth == 0 ? 0 : (1u << depth) - 1u; }

  static std::uint64_t Hash64(const std::string& s) {
    std::uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : s) {
      h ^= c;
      h *= 1099511628211ULL;
    }
    return h;
  }

  static std::uint32_t ReadDirEntry(const Page* d, std::uint32_t i) {
    std::uint32_t id;
    std::memcpy(&id, d->Data() + kPageHeaderSize + i * 4, 4);
    return id;
  }
  static void WriteDirEntry(Page* d, std::uint32_t i, std::uint32_t id) {
    std::memcpy(d->Data() + kPageHeaderSize + i * 4, &id, 4);
    d->SetDirty(true);
  }

  static RID ReadRid(const Page* p, std::uint16_t i) {
    RID r;
    const char* base = p->Data() + kPageHeaderSize + 4 + static_cast<std::size_t>(i) * 8;
    std::memcpy(&r.page_id, base, 4);
    std::memcpy(&r.slot_id, base + 4, 2);
    return r;
  }
  static void WriteRid(Page* p, std::uint16_t i, RID r) {
    char* base = p->Data() + kPageHeaderSize + 4 + static_cast<std::size_t>(i) * 8;
    std::memcpy(base, &r.page_id, 4);
    std::memcpy(base + 4, &r.slot_id, 2);
  }

  static std::vector<std::string> ReadKeys(const Page* p) {
    std::vector<std::string> keys;
    std::uint16_t n = p->Header()->extra;
    keys.reserve(n);
    std::uint16_t off = static_cast<std::uint16_t>(kPageHeaderSize + 4 + n * 8);
    for (std::uint16_t i = 0; i < n; ++i) {
      std::uint16_t len;
      std::memcpy(&len, p->Data() + off, 2);
      off += 2;
      keys.emplace_back(p->Data() + off, len);
      off += len;
    }
    return keys;
  }

  static void WriteBucket(Page* p, const std::vector<std::string>& keys, const std::vector<RID>& rids,
                          int local_depth) {
    p->Header()->extra = static_cast<std::uint16_t>(keys.size());
    p->Header()->extra2 = static_cast<std::uint16_t>(local_depth);
    for (std::size_t i = 0; i < rids.size(); ++i) WriteRid(p, static_cast<std::uint16_t>(i), rids[i]);
    std::uint16_t off = static_cast<std::uint16_t>(kPageHeaderSize + 4 + keys.size() * 8);
    for (const auto& k : keys) {
      auto len = static_cast<std::uint16_t>(k.size());
      std::memcpy(p->Data() + off, &len, 2);
      off += 2;
      std::memcpy(p->Data() + off, k.data(), len);
      off += len;
    }
    p->SetDirty(true);
  }

  static std::uint16_t BucketBytes(const std::vector<std::string>& keys) {
    std::uint32_t n = kPageHeaderSize + 4 + static_cast<std::uint32_t>(keys.size()) * 8;
    for (const auto& k : keys) n += 2 + static_cast<std::uint32_t>(k.size());
    return static_cast<std::uint16_t>(std::min<std::uint32_t>(n, 0xffff));
  }

  bool InsertIntoBucket(std::uint32_t bucket, const std::string& k, RID rid) {
    std::uint32_t pid = bucket;
    Page* last = nullptr;
    std::uint32_t last_id = kInvalidPageId;
    while (pid != kInvalidPageId) {
      Page* p = bpm_->FetchPage(pid);
      auto keys = ReadKeys(p);
      std::vector<RID> rids;
      for (std::uint16_t i = 0; i < p->Header()->extra; ++i) rids.push_back(ReadRid(p, i));
      keys.push_back(k);
      rids.push_back(rid);
      if (BucketBytes(keys) <= kPageSize - 16) {
        WriteBucket(p, keys, rids, p->Header()->extra2);
        bpm_->UnpinPage(pid, true);
        if (last) bpm_->UnpinPage(last_id, false);
        return true;
      }
      keys.pop_back();
      rids.pop_back();
      if (last) bpm_->UnpinPage(last_id, false);
      last = p;
      last_id = pid;
      pid = p->Header()->next_page;
    }
    if (last) bpm_->UnpinPage(last_id, false);
    return false;
  }

  void SplitOrOverflow(std::uint32_t dir_idx, std::uint32_t bucket, const std::string& k, RID rid) {
    Page* d = bpm_->FetchPage(dir_);
    Page* b = bpm_->FetchPage(bucket);
    int local = b->Header()->extra2;
    int global = d->Header()->extra;
    auto keys = ReadKeys(b);
    std::vector<RID> rids;
    for (std::uint16_t i = 0; i < b->Header()->extra; ++i) rids.push_back(ReadRid(b, i));
    keys.push_back(k);
    rids.push_back(rid);

    if (local == global && global >= kMaxGlobalDepth) {
      // Overflow chain.
      std::uint32_t ov = 0;
      Page* op = bpm_->NewPage(&ov, PageType::HashBucket);
      op->Header()->next_page = kInvalidPageId;
      WriteBucket(op, {k}, {rid}, local);
      // restore original without the new key
      keys.pop_back();
      rids.pop_back();
      b->Header()->next_page = ov;
      WriteBucket(b, keys, rids, local);
      bpm_->UnpinPage(ov, true);
      bpm_->UnpinPage(bucket, true);
      bpm_->UnpinPage(dir_, false);
      return;
    }

    if (local == global) {
      // Double directory.
      int n = 1 << global;
      std::vector<std::uint32_t> entries(n);
      for (int i = 0; i < n; ++i) entries[static_cast<std::size_t>(i)] = ReadDirEntry(d, i);
      d->Header()->extra = static_cast<std::uint16_t>(global + 1);
      for (int i = 0; i < n; ++i) {
        WriteDirEntry(d, i, entries[static_cast<std::size_t>(i)]);
        WriteDirEntry(d, i + n, entries[static_cast<std::size_t>(i)]);
      }
      global++;
    }

    std::uint32_t nb = 0;
    Page* np = bpm_->NewPage(&nb, PageType::HashBucket);
    np->Header()->next_page = kInvalidPageId;
    int new_local = local + 1;
    std::vector<std::string> lkeys, rkeys;
    std::vector<RID> lrids, rrids;
    for (std::size_t i = 0; i < keys.size(); ++i) {
      auto h = Hash64(keys[i]);
      if (h & (1u << local)) {
        rkeys.push_back(keys[i]);
        rrids.push_back(rids[i]);
      } else {
        lkeys.push_back(keys[i]);
        lrids.push_back(rids[i]);
      }
    }
    WriteBucket(b, lkeys, lrids, new_local);
    WriteBucket(np, rkeys, rrids, new_local);

    const int n = 1 << global;
    for (int i = 0; i < n; ++i) {
      if (ReadDirEntry(d, i) != bucket) continue;
      if (i & (1 << local)) WriteDirEntry(d, i, nb);
    }
    bpm_->UnpinPage(nb, true);
    bpm_->UnpinPage(bucket, true);
    bpm_->UnpinPage(dir_, true);
    (void)dir_idx;
  }

  BufferPool* bpm_;
  std::uint32_t dir_;
  BloomFilter bloom_;
};

}  // namespace aegisdb
