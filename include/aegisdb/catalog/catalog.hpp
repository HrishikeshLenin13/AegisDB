#pragma once

#include "aegisdb/common/status.hpp"
#include "aegisdb/common/types.hpp"
#include "aegisdb/index/bplus_tree.hpp"
#include "aegisdb/index/extendible_hash.hpp"
#include "aegisdb/storage/heap_file.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace aegisdb {

enum class IndexKind { BPlus, Hash };

struct ColumnStats {
  std::uint64_t ndv = 1;
  Value min_v = Value::Null(TypeId::Integer);
  Value max_v = Value::Null(TypeId::Integer);
  double null_frac = 0;
};

struct TableStats {
  std::uint64_t rows = 0;
  std::uint32_t pages = 1;
  std::vector<ColumnStats> cols;
};

struct IndexInfo {
  std::string name;
  std::string table;
  std::string column;
  int col_idx = 0;
  IndexKind kind = IndexKind::BPlus;
  std::uint32_t root_page = kInvalidPageId;
  std::unique_ptr<BPlusTree> btree;
  std::unique_ptr<ExtendibleHash> hash;
};

struct TableInfo {
  std::string name;
  std::uint32_t heap_page = kInvalidPageId;
  Schema schema;
  TableStats stats;
  std::unique_ptr<HeapFile> heap;
  std::vector<std::unique_ptr<IndexInfo>> indexes;
};

class Catalog {
 public:
  TableInfo* CreateTable(const std::string& name, Schema schema) {
    if (tables_.count(name)) throw AegisError("table already exists: " + name);
    auto t = std::make_unique<TableInfo>();
    t->name = name;
    t->schema = std::move(schema);
    t->stats.cols.resize(t->schema.ColumnCount());
    auto* ptr = t.get();
    tables_[name] = std::move(t);
    order_.push_back(name);
    return ptr;
  }

  TableInfo* Get(const std::string& name) {
    auto it = tables_.find(name);
    if (it == tables_.end()) return nullptr;
    return it->second.get();
  }

  IndexInfo* FindIndex(const std::string& table, const std::string& column) {
    auto* t = Get(table);
    if (!t) return nullptr;
    for (auto& ix : t->indexes) {
      if (ix->column == column) return ix.get();
    }
    return nullptr;
  }

  const std::vector<std::string>& Tables() const { return order_; }

  std::vector<std::uint8_t> Serialize() const {
    std::vector<std::uint8_t> buf;
    auto pu8 = [&](std::uint8_t x) { buf.push_back(x); };
    auto pu32 = [&](std::uint32_t x) {
      for (int i = 0; i < 4; ++i) buf.push_back(static_cast<std::uint8_t>((x >> (8 * i)) & 0xff));
    };
    auto pu64 = [&](std::uint64_t x) {
      for (int i = 0; i < 8; ++i) buf.push_back(static_cast<std::uint8_t>((x >> (8 * i)) & 0xff));
    };
    auto pstr = [&](const std::string& s) {
      pu32(static_cast<std::uint32_t>(s.size()));
      buf.insert(buf.end(), s.begin(), s.end());
    };
    pu32(static_cast<std::uint32_t>(order_.size()));
    for (const auto& name : order_) {
      auto* t = tables_.at(name).get();
      pstr(t->name);
      pu32(t->heap_page);
      pu32(static_cast<std::uint32_t>(t->schema.columns.size()));
      for (const auto& c : t->schema.columns) {
        pstr(c.name);
        pu8(static_cast<std::uint8_t>(c.type));
        buf.push_back(static_cast<std::uint8_t>(c.varchar_len >> 8));
        buf.push_back(static_cast<std::uint8_t>(c.varchar_len & 0xff));
      }
      pu64(t->stats.rows);
      pu32(static_cast<std::uint32_t>(t->indexes.size()));
      for (const auto& ix : t->indexes) {
        pstr(ix->name);
        pstr(ix->column);
        pu8(static_cast<std::uint8_t>(ix->kind));
        pu32(ix->root_page);
      }
    }
    return buf;
  }

  void Deserialize(const std::uint8_t* p, std::size_t n) {
    tables_.clear();
    order_.clear();
    std::size_t off = 0;
    auto ru32 = [&]() {
      std::uint32_t x = 0;
      for (int i = 0; i < 4; ++i) x |= static_cast<std::uint32_t>(p[off++]) << (8 * i);
      return x;
    };
    auto ru64 = [&]() {
      std::uint64_t x = 0;
      for (int i = 0; i < 8; ++i) x |= static_cast<std::uint64_t>(p[off++]) << (8 * i);
      return x;
    };
    auto rstr = [&]() {
      auto len = ru32();
      std::string s(reinterpret_cast<const char*>(p + off), len);
      off += len;
      return s;
    };
    if (n < 4) return;
    auto nt = ru32();
    for (std::uint32_t i = 0; i < nt; ++i) {
      auto t = std::make_unique<TableInfo>();
      t->name = rstr();
      t->heap_page = ru32();
      auto nc = ru32();
      for (std::uint32_t c = 0; c < nc; ++c) {
        Column col;
        col.name = rstr();
        col.type = static_cast<TypeId>(p[off++]);
        col.varchar_len = static_cast<std::uint16_t>((p[off] << 8) | p[off + 1]);
        off += 2;
        t->schema.columns.push_back(col);
      }
      t->stats.rows = ru64();
      t->stats.cols.resize(t->schema.ColumnCount());
      auto ni = ru32();
      for (std::uint32_t j = 0; j < ni; ++j) {
        auto ix = std::make_unique<IndexInfo>();
        ix->name = rstr();
        ix->table = t->name;
        ix->column = rstr();
        ix->kind = static_cast<IndexKind>(p[off++]);
        ix->root_page = ru32();
        ix->col_idx = t->schema.IndexOf(ix->column);
        t->indexes.push_back(std::move(ix));
      }
      order_.push_back(t->name);
      tables_[t->name] = std::move(t);
    }
  }

 private:
  std::unordered_map<std::string, std::unique_ptr<TableInfo>> tables_;
  std::vector<std::string> order_;
};

}  // namespace aegisdb
