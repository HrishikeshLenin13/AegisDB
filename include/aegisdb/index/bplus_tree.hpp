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
#include <utility>
#include <vector>

namespace aegisdb {
namespace bplus_detail {

inline constexpr std::uint16_t kRidStride = 8;  // u32 page, u16 slot, u16 pad

inline std::uint16_t NKeys(const Page* p) { return p->Header()->extra; }
inline void SetNKeys(Page* p, std::uint16_t n) { p->Header()->extra = n; }
inline bool IsLeaf(const Page* p) { return p->Header()->type == PageType::BPlusLeaf; }

inline RID ReadRid(const Page* p, std::uint16_t i) {
  const char* base = p->Data() + kPageHeaderSize + static_cast<std::size_t>(i) * kRidStride;
  RID r;
  std::memcpy(&r.page_id, base, 4);
  std::memcpy(&r.slot_id, base + 4, 2);
  return r;
}

inline void WriteRid(Page* p, std::uint16_t i, RID r) {
  char* base = p->Data() + kPageHeaderSize + static_cast<std::size_t>(i) * kRidStride;
  std::memcpy(base, &r.page_id, 4);
  std::memcpy(base + 4, &r.slot_id, 2);
  std::uint16_t pad = 0;
  std::memcpy(base + 6, &pad, 2);
}

inline std::uint32_t ReadChild(const Page* p, std::uint16_t i) {
  const char* base = p->Data() + kPageHeaderSize + static_cast<std::size_t>(i) * 4;
  std::uint32_t id;
  std::memcpy(&id, base, 4);
  return id;
}

inline void WriteChild(Page* p, std::uint16_t i, std::uint32_t id) {
  char* base = p->Data() + kPageHeaderSize + static_cast<std::size_t>(i) * 4;
  std::memcpy(base, &id, 4);
}

// Keys are packed after the RID/child array as (u16 len + bytes)*nkeys.
inline std::uint16_t KeyAreaOff(const Page* p) {
  if (IsLeaf(p)) return static_cast<std::uint16_t>(kPageHeaderSize + NKeys(p) * kRidStride);
  return static_cast<std::uint16_t>(kPageHeaderSize + (NKeys(p) + 1) * 4);
}

inline std::vector<std::string> ReadKeys(const Page* p) {
  std::vector<std::string> keys;
  keys.reserve(NKeys(p));
  std::uint16_t off = KeyAreaOff(p);
  for (std::uint16_t i = 0; i < NKeys(p); ++i) {
    std::uint16_t len;
    std::memcpy(&len, p->Data() + off, 2);
    off += 2;
    keys.emplace_back(p->Data() + off, len);
    off += len;
  }
  return keys;
}

inline void WritePayload(Page* p, const std::vector<std::string>& keys, const std::vector<RID>& rids,
                         const std::vector<std::uint32_t>& children) {
  const bool leaf = IsLeaf(p);
  SetNKeys(p, static_cast<std::uint16_t>(keys.size()));
  if (leaf) {
    for (std::size_t i = 0; i < rids.size(); ++i) WriteRid(p, static_cast<std::uint16_t>(i), rids[i]);
  } else {
    for (std::size_t i = 0; i < children.size(); ++i)
      WriteChild(p, static_cast<std::uint16_t>(i), children[i]);
  }
  std::uint16_t off = KeyAreaOff(p);
  for (const auto& k : keys) {
    auto len = static_cast<std::uint16_t>(k.size());
    std::memcpy(p->Data() + off, &len, 2);
    off += 2;
    std::memcpy(p->Data() + off, k.data(), len);
    off += len;
  }
  p->Header()->free_start = off;
  p->SetDirty(true);
}

inline std::uint16_t PayloadBytes(const std::vector<std::string>& keys, bool leaf) {
  std::uint32_t n = kPageHeaderSize;
  if (leaf) n += static_cast<std::uint32_t>(keys.size()) * kRidStride;
  else n += static_cast<std::uint32_t>(keys.size() + 1) * 4;
  for (const auto& k : keys) n += 2 + static_cast<std::uint32_t>(k.size());
  return static_cast<std::uint16_t>(std::min<std::uint32_t>(n, 0xffff));
}

inline bool Fits(const std::vector<std::string>& keys, bool leaf) {
  return PayloadBytes(keys, leaf) <= kPageSize - 8;
}

inline int LowerBound(const std::vector<std::string>& keys, const std::string& key) {
  return static_cast<int>(std::lower_bound(keys.begin(), keys.end(), key) - keys.begin());
}

}  // namespace bplus_detail

class BPlusTree {
 public:
  BPlusTree(BufferPool* bpm, std::uint32_t root, TypeId /*key_type*/)
      : bpm_(bpm), root_(root) {
    bloom_.Reset(4096, 0.01);
  }

  static std::uint32_t Create(BufferPool* bpm) {
    std::uint32_t pid = 0;
    Page* p = bpm->NewPage(&pid, PageType::BPlusLeaf);
    p->Header()->next_page = kInvalidPageId;
    p->Header()->prev_page = kInvalidPageId;
    p->Header()->extra = 0;
    p->Header()->extra2 = 0;  // level
    p->SetDirty(true);
    bpm->UnpinPage(pid, true);
    return pid;
  }

  std::uint32_t Root() const { return root_; }
  BloomFilter& Bloom() { return bloom_; }

  bool Insert(const Value& key, RID rid) {
    std::string k = key.EncodeKey();
    bloom_.Add(k);
    if (root_ == kInvalidPageId) {
      root_ = Create(bpm_);
    }
    std::vector<std::uint32_t> path;
    std::uint32_t leaf = FindLeaf(k, &path);
    Page* lp = bpm_->FetchPage(leaf);
    auto keys = bplus_detail::ReadKeys(lp);
    std::vector<RID> rids;
    rids.reserve(keys.size());
    for (std::uint16_t i = 0; i < bplus_detail::NKeys(lp); ++i) rids.push_back(bplus_detail::ReadRid(lp, i));
    int pos = bplus_detail::LowerBound(keys, k);
    keys.insert(keys.begin() + pos, k);
    rids.insert(rids.begin() + pos, rid);
    if (bplus_detail::Fits(keys, true)) {
      bplus_detail::WritePayload(lp, keys, rids, {});
      bpm_->UnpinPage(leaf, true);
      return true;
    }
    // Split leaf.
    std::size_t mid = keys.size() / 2;
    std::vector<std::string> right_keys(keys.begin() + static_cast<std::ptrdiff_t>(mid), keys.end());
    std::vector<RID> right_rids(rids.begin() + static_cast<std::ptrdiff_t>(mid), rids.end());
    keys.resize(mid);
    rids.resize(mid);
    std::string sep = right_keys.front();

    std::uint32_t right_id = 0;
    Page* rp = bpm_->NewPage(&right_id, PageType::BPlusLeaf);
    rp->Header()->extra2 = 0;
    rp->Header()->next_page = lp->Header()->next_page;
    rp->Header()->prev_page = leaf;
    if (lp->Header()->next_page != kInvalidPageId) {
      Page* nxt = bpm_->FetchPage(lp->Header()->next_page);
      nxt->Header()->prev_page = right_id;
      nxt->SetDirty(true);
      bpm_->UnpinPage(lp->Header()->next_page, true);
    }
    lp->Header()->next_page = right_id;
    bplus_detail::WritePayload(lp, keys, rids, {});
    bplus_detail::WritePayload(rp, right_keys, right_rids, {});
    bpm_->UnpinPage(leaf, true);
    bpm_->UnpinPage(right_id, true);
    InsertInParent(path, leaf, sep, right_id);
    return true;
  }

  std::vector<RID> Search(const Value& key) {
    std::string k = key.EncodeKey();
    if (!bloom_.MaybeContains(k)) return {};
    std::uint32_t leaf = FindLeaf(k, nullptr);
    Page* p = bpm_->FetchPage(leaf);
    auto keys = bplus_detail::ReadKeys(p);
    std::vector<RID> out;
    for (std::size_t i = 0; i < keys.size(); ++i) {
      if (keys[i] == k) out.push_back(bplus_detail::ReadRid(p, static_cast<std::uint16_t>(i)));
    }
    bpm_->UnpinPage(leaf, false);
    return out;
  }

  // Inclusive start, exclusive end. nullopt end = +inf; nullopt start = -inf.
  std::vector<std::pair<Value, RID>> Range(const std::optional<Value>& start, const std::optional<Value>& end,
                                           int limit = -1) {
    std::string sk = start ? start->EncodeKey() : std::string();
    std::string ek = end ? end->EncodeKey() : std::string();
    std::uint32_t leaf = start ? FindLeaf(sk, nullptr) : LeftmostLeaf();
    std::vector<std::pair<Value, RID>> out;
    std::uint32_t pid = leaf;
    while (pid != kInvalidPageId) {
      Page* p = bpm_->FetchPage(pid);
      auto keys = bplus_detail::ReadKeys(p);
      std::uint32_t next = p->Header()->next_page;
      for (std::size_t i = 0; i < keys.size(); ++i) {
        if (start && keys[i] < sk) continue;
        if (end && keys[i] >= ek) {
          bpm_->UnpinPage(pid, false);
          return out;
        }
        out.emplace_back(Value::DecodeKey(keys[i]), bplus_detail::ReadRid(p, static_cast<std::uint16_t>(i)));
        if (limit >= 0 && static_cast<int>(out.size()) >= limit) {
          bpm_->UnpinPage(pid, false);
          return out;
        }
      }
      bpm_->UnpinPage(pid, false);
      pid = next;
    }
    return out;
  }

  bool Remove(const Value& key, RID rid) {
    std::string k = key.EncodeKey();
    std::vector<std::uint32_t> path;
    std::uint32_t leaf = FindLeaf(k, &path);
    Page* p = bpm_->FetchPage(leaf);
    auto keys = bplus_detail::ReadKeys(p);
    std::vector<RID> rids;
    for (std::uint16_t i = 0; i < bplus_detail::NKeys(p); ++i) rids.push_back(bplus_detail::ReadRid(p, i));
    int found = -1;
    for (int i = 0; i < static_cast<int>(keys.size()); ++i) {
      if (keys[static_cast<std::size_t>(i)] == k && rids[static_cast<std::size_t>(i)] == rid) {
        found = i;
        break;
      }
    }
    if (found < 0) {
      bpm_->UnpinPage(leaf, false);
      return false;
    }
    keys.erase(keys.begin() + found);
    rids.erase(rids.begin() + found);
    bplus_detail::WritePayload(p, keys, rids, {});
    const bool underflow = keys.size() < 2 && leaf != root_;
    bpm_->UnpinPage(leaf, true);
    if (underflow) HandleUnderflow(path, leaf);
    return true;
  }

 private:
  std::uint32_t LeftmostLeaf() {
    std::uint32_t pid = root_;
    while (true) {
      Page* p = bpm_->FetchPage(pid);
      if (bplus_detail::IsLeaf(p)) {
        bpm_->UnpinPage(pid, false);
        return pid;
      }
      std::uint32_t child = bplus_detail::ReadChild(p, 0);
      bpm_->UnpinPage(pid, false);
      pid = child;
    }
  }

  std::uint32_t FindLeaf(const std::string& key, std::vector<std::uint32_t>* path) {
    std::uint32_t pid = root_;
    while (true) {
      Page* p = bpm_->FetchPage(pid);
      if (bplus_detail::IsLeaf(p)) {
        bpm_->UnpinPage(pid, false);
        return pid;
      }
      if (path) path->push_back(pid);
      auto keys = bplus_detail::ReadKeys(p);
      int i = bplus_detail::LowerBound(keys, key);
      // lower_bound: child i is the first key >= search key's left subtree...
      // Internal: child[0] | k0 | child[1] | k1 | ...
      // keys < k0 -> child0; k0 <= key < k1 -> child1
      if (i < static_cast<int>(keys.size()) && keys[static_cast<std::size_t>(i)] == key) i++;
      std::uint32_t child = bplus_detail::ReadChild(p, static_cast<std::uint16_t>(i));
      bpm_->UnpinPage(pid, false);
      pid = child;
    }
  }

  void InsertInParent(const std::vector<std::uint32_t>& path, std::uint32_t left, const std::string& sep,
                      std::uint32_t right) {
    if (path.empty()) {
      std::uint32_t new_root = 0;
      Page* rp = bpm_->NewPage(&new_root, PageType::BPlusInternal);
      rp->Header()->extra2 = 1;
      std::vector<std::string> keys{sep};
      std::vector<std::uint32_t> children{left, right};
      bplus_detail::WritePayload(rp, keys, {}, children);
      bpm_->UnpinPage(new_root, true);
      root_ = new_root;
      return;
    }
    std::uint32_t parent = path.back();
    std::vector<std::uint32_t> up(path.begin(), path.end() - 1);
    Page* pp = bpm_->FetchPage(parent);
    auto keys = bplus_detail::ReadKeys(pp);
    std::vector<std::uint32_t> children;
    for (std::uint16_t i = 0; i <= bplus_detail::NKeys(pp); ++i)
      children.push_back(bplus_detail::ReadChild(pp, i));
    int idx = 0;
    for (int i = 0; i < static_cast<int>(children.size()); ++i) {
      if (children[static_cast<std::size_t>(i)] == left) {
        idx = i;
        break;
      }
    }
    keys.insert(keys.begin() + idx, sep);
    children.insert(children.begin() + idx + 1, right);
    if (bplus_detail::Fits(keys, false)) {
      bplus_detail::WritePayload(pp, keys, {}, children);
      bpm_->UnpinPage(parent, true);
      return;
    }
    std::size_t mid = keys.size() / 2;
    std::string up_key = keys[mid];
    std::vector<std::string> rkeys(keys.begin() + static_cast<std::ptrdiff_t>(mid) + 1, keys.end());
    std::vector<std::uint32_t> rch(children.begin() + static_cast<std::ptrdiff_t>(mid) + 1, children.end());
    keys.resize(mid);
    children.resize(mid + 1);
    std::uint32_t right_id = 0;
    Page* rp = bpm_->NewPage(&right_id, PageType::BPlusInternal);
    rp->Header()->extra2 = pp->Header()->extra2;
    bplus_detail::WritePayload(pp, keys, {}, children);
    bplus_detail::WritePayload(rp, rkeys, {}, rch);
    bpm_->UnpinPage(parent, true);
    bpm_->UnpinPage(right_id, true);
    InsertInParent(up, parent, up_key, right_id);
  }

  void HandleUnderflow(const std::vector<std::uint32_t>& path, std::uint32_t node) {
    if (path.empty() || node == root_) return;
    std::uint32_t parent = path.back();
    Page* pp = bpm_->FetchPage(parent);
    auto pkeys = bplus_detail::ReadKeys(pp);
    std::vector<std::uint32_t> children;
    for (std::uint16_t i = 0; i <= bplus_detail::NKeys(pp); ++i)
      children.push_back(bplus_detail::ReadChild(pp, i));
    int idx = 0;
    for (int i = 0; i < static_cast<int>(children.size()); ++i) {
      if (children[static_cast<std::size_t>(i)] == node) {
        idx = i;
        break;
      }
    }
    // Try borrow from left sibling.
    if (idx > 0) {
      std::uint32_t sib = children[static_cast<std::size_t>(idx - 1)];
      Page* sp = bpm_->FetchPage(sib);
      Page* np = bpm_->FetchPage(node);
      auto skeys = bplus_detail::ReadKeys(sp);
      auto nkeys = bplus_detail::ReadKeys(np);
      if (skeys.size() > 2) {
        if (bplus_detail::IsLeaf(np)) {
          std::vector<RID> srids, nrids;
          for (std::uint16_t i = 0; i < bplus_detail::NKeys(sp); ++i) srids.push_back(bplus_detail::ReadRid(sp, i));
          for (std::uint16_t i = 0; i < bplus_detail::NKeys(np); ++i) nrids.push_back(bplus_detail::ReadRid(np, i));
          nkeys.insert(nkeys.begin(), skeys.back());
          nrids.insert(nrids.begin(), srids.back());
          skeys.pop_back();
          srids.pop_back();
          pkeys[static_cast<std::size_t>(idx - 1)] = nkeys.front();
          bplus_detail::WritePayload(sp, skeys, srids, {});
          bplus_detail::WritePayload(np, nkeys, nrids, {});
          bplus_detail::WritePayload(pp, pkeys, {}, children);
        } else {
          std::vector<std::uint32_t> sch, nch;
          for (std::uint16_t i = 0; i <= bplus_detail::NKeys(sp); ++i) sch.push_back(bplus_detail::ReadChild(sp, i));
          for (std::uint16_t i = 0; i <= bplus_detail::NKeys(np); ++i) nch.push_back(bplus_detail::ReadChild(np, i));
          nkeys.insert(nkeys.begin(), pkeys[static_cast<std::size_t>(idx - 1)]);
          nch.insert(nch.begin(), sch.back());
          pkeys[static_cast<std::size_t>(idx - 1)] = skeys.back();
          skeys.pop_back();
          sch.pop_back();
          bplus_detail::WritePayload(sp, skeys, {}, sch);
          bplus_detail::WritePayload(np, nkeys, {}, nch);
          bplus_detail::WritePayload(pp, pkeys, {}, children);
        }
        bpm_->UnpinPage(sib, true);
        bpm_->UnpinPage(node, true);
        bpm_->UnpinPage(parent, true);
        return;
      }
      bpm_->UnpinPage(sib, false);
      bpm_->UnpinPage(node, false);
    }
    // Merge with left sibling if possible, else right.
    int merge_idx = (idx > 0) ? idx : idx + 1;
    if (merge_idx <= 0 || merge_idx >= static_cast<int>(children.size())) {
      bpm_->UnpinPage(parent, false);
      return;
    }
    std::uint32_t left = children[static_cast<std::size_t>(merge_idx - 1)];
    std::uint32_t right = children[static_cast<std::size_t>(merge_idx)];
    Page* lp = bpm_->FetchPage(left);
    Page* rp = bpm_->FetchPage(right);
    auto lkeys = bplus_detail::ReadKeys(lp);
    auto rkeys = bplus_detail::ReadKeys(rp);
    if (bplus_detail::IsLeaf(lp)) {
      std::vector<RID> lrids, rrids;
      for (std::uint16_t i = 0; i < bplus_detail::NKeys(lp); ++i) lrids.push_back(bplus_detail::ReadRid(lp, i));
      for (std::uint16_t i = 0; i < bplus_detail::NKeys(rp); ++i) rrids.push_back(bplus_detail::ReadRid(rp, i));
      lkeys.insert(lkeys.end(), rkeys.begin(), rkeys.end());
      lrids.insert(lrids.end(), rrids.begin(), rrids.end());
      lp->Header()->next_page = rp->Header()->next_page;
      if (rp->Header()->next_page != kInvalidPageId) {
        Page* nxt = bpm_->FetchPage(rp->Header()->next_page);
        nxt->Header()->prev_page = left;
        nxt->SetDirty(true);
        bpm_->UnpinPage(rp->Header()->next_page, true);
      }
      bplus_detail::WritePayload(lp, lkeys, lrids, {});
    } else {
      std::vector<std::uint32_t> lch, rch;
      for (std::uint16_t i = 0; i <= bplus_detail::NKeys(lp); ++i) lch.push_back(bplus_detail::ReadChild(lp, i));
      for (std::uint16_t i = 0; i <= bplus_detail::NKeys(rp); ++i) rch.push_back(bplus_detail::ReadChild(rp, i));
      lkeys.push_back(pkeys[static_cast<std::size_t>(merge_idx - 1)]);
      lkeys.insert(lkeys.end(), rkeys.begin(), rkeys.end());
      lch.insert(lch.end(), rch.begin(), rch.end());
      bplus_detail::WritePayload(lp, lkeys, {}, lch);
    }
    pkeys.erase(pkeys.begin() + (merge_idx - 1));
    children.erase(children.begin() + merge_idx);
    bplus_detail::WritePayload(pp, pkeys, {}, children);
    bpm_->UnpinPage(left, true);
    bpm_->UnpinPage(right, false);
    bool parent_under = pkeys.size() < 1 && parent != root_;
    if (parent == root_ && pkeys.empty()) {
      root_ = left;
    }
    bpm_->UnpinPage(parent, true);
    if (parent_under) {
      std::vector<std::uint32_t> up(path.begin(), path.end() - 1);
      HandleUnderflow(up, parent);
    }
  }

  BufferPool* bpm_;
  std::uint32_t root_;
  BloomFilter bloom_;
};

}  // namespace aegisdb
