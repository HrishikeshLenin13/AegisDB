#pragma once

#include "aegisdb/catalog/catalog.hpp"
#include "aegisdb/exec/executor.hpp"
#include "aegisdb/parse/parser.hpp"
#include "aegisdb/plan/optimizer.hpp"
#include "aegisdb/recovery/log_manager.hpp"
#include "aegisdb/recovery/recovery.hpp"
#include "aegisdb/storage/buffer_pool.hpp"
#include "aegisdb/storage/disk_manager.hpp"
#include "aegisdb/storage/free_space_map.hpp"
#include "aegisdb/txn/txn_manager.hpp"

#include <chrono>
#include <algorithm>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace aegisdb {

struct QueryResult {
  Schema schema;
  std::vector<Tuple> rows;
  std::string plan;
  double elapsed_ms = 0;
  std::string message;
};

class Database {
 public:
  explicit Database(std::string path, std::size_t pool_pages = kDefaultPoolPages)
      : base_(std::move(path)),
        disk_(base_ + ".aegis"),
        log_(base_ + ".wal"),
        bpm_(&disk_, pool_pages, [this](std::uint64_t lsn) { log_.FlushTo(lsn); }),
        txn_(&log_),
        recovery_(&bpm_, &log_, &txn_) {
    OpenOrCreate();
    recovery_.Recover();
    LoadCatalog();
    implicit_ = txn_.Begin();
  }

  ~Database() {
    if (skip_shutdown_flush_) return;
    try {
      PersistCatalog();
      recovery_.Checkpoint();
      bpm_.FlushAll();
      disk_.Sync();
    } catch (...) {
    }
  }

  QueryResult Execute(const std::string& sql) {
    auto t0 = std::chrono::steady_clock::now();
    Parser p(sql);
    Statement s = p.Parse();
    QueryResult r;
    if (s.explain) {
      Optimizer opt(&catalog_);
      auto plan = opt.PlanSelect(s);
      r.plan = Optimizer::Explain(*plan);
      r.message = r.plan;
    } else {
      switch (s.kind) {
        case StmtKind::CreateTable:
          CreateTable(s);
          r.message = "CREATE TABLE";
          break;
        case StmtKind::CreateIndex:
          CreateIndex(s);
          r.message = "CREATE INDEX";
          break;
        case StmtKind::Insert:
          Insert(s);
          r.message = "INSERT " + std::to_string(s.rows.size());
          break;
        case StmtKind::Select:
          r = RunSelect(s);
          break;
        case StmtKind::Begin:
          implicit_ = txn_.Begin();
          r.message = "BEGIN";
          break;
        case StmtKind::Commit:
          txn_.Commit(implicit_);
          implicit_ = txn_.Begin();
          r.message = "COMMIT";
          break;
        case StmtKind::Rollback:
          txn_.Abort(implicit_);
          implicit_ = txn_.Begin();
          r.message = "ROLLBACK";
          break;
        case StmtKind::Analyze:
          Analyze(s.table);
          r.message = "ANALYZE";
          break;
        case StmtKind::Explain:
          break;
      }
    }
    auto t1 = std::chrono::steady_clock::now();
    r.elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return r;
  }

  std::string Explain(const std::string& sql) {
    Parser p(sql);
    Statement s = p.Parse();
    Optimizer opt(&catalog_);
    auto plan = opt.PlanSelect(s);
    return Optimizer::Explain(*plan);
  }

  Catalog& GetCatalog() { return catalog_; }
  BufferPool& Pool() { return bpm_; }
  TxnManager& Txns() { return txn_; }
  void Checkpoint() { recovery_.Checkpoint(); }
  void Flush() {
    PersistCatalog();
    bpm_.FlushAll();
    log_.Flush();
  }

  // Crash without flushing data pages (log is already flushed on commit).
  void CrashSim() {
    PersistCatalog();
    bpm_.FlushPage(0);
    bpm_.FlushPage(cat_page_);
    log_.Flush();
    skip_shutdown_flush_ = true;
  }

 private:
  struct Superblock {
    std::uint32_t magic = kMagic;
    std::uint32_t version = kFileVersion;
    std::uint32_t fsm_page = kInvalidPageId;
    std::uint32_t catalog_page = kInvalidPageId;
  };

  void OpenOrCreate() {
    if (disk_.NextPageId() == 0) {
      std::uint32_t pid = 0;
      Page* p = bpm_.NewPage(&pid, PageType::Superblock);
      Superblock sb;
      sb.fsm_page = 0;
      sb.catalog_page = 0;
      std::memcpy(p->Data() + kPageHeaderSize, &sb, sizeof(sb));
      p->SetDirty(true);
      bpm_.UnpinPage(pid, true);
      fsm_page_ = FreeSpaceMap::Create(&bpm_);
      cat_page_ = 0;
      Page* cp = bpm_.NewPage(&cat_page_, PageType::Catalog);
      cp->SetDirty(true);
      bpm_.UnpinPage(cat_page_, true);
      fsm_ = std::make_unique<FreeSpaceMap>(&bpm_, fsm_page_);
      WriteSuper();
      bpm_.FlushAll();
    } else {
      Page* p = bpm_.FetchPage(0);
      Superblock sb;
      std::memcpy(&sb, p->Data() + kPageHeaderSize, sizeof(sb));
      bpm_.UnpinPage(0, false);
      fsm_page_ = sb.fsm_page;
      cat_page_ = sb.catalog_page;
      fsm_ = std::make_unique<FreeSpaceMap>(&bpm_, fsm_page_);
    }
  }

  void WriteSuper() {
    Page* p = bpm_.FetchPage(0);
    Superblock sb;
    sb.fsm_page = fsm_page_;
    sb.catalog_page = cat_page_;
    std::memcpy(p->Data() + kPageHeaderSize, &sb, sizeof(sb));
    p->SetDirty(true);
    bpm_.UnpinPage(0, true);
  }

  void LoadCatalog() {
    Page* p = bpm_.FetchPage(cat_page_);
    std::uint32_t len = 0;
    std::memcpy(&len, p->Data() + kPageHeaderSize, 4);
    if (len > 0 && len < kPageSize - kPageHeaderSize) {
      catalog_.Deserialize(reinterpret_cast<const std::uint8_t*>(p->Data() + kPageHeaderSize + 4), len);
    }
    bpm_.UnpinPage(cat_page_, false);
    for (const auto& name : catalog_.Tables()) {
      auto* t = catalog_.Get(name);
      t->heap = std::make_unique<HeapFile>(&bpm_, fsm_.get(), t->heap_page, t->schema);
      t->heap->SetTupleCount(t->stats.rows);
      t->heap->SetLogHook([this](std::uint32_t page, std::uint16_t slot, const char* kind,
                                 const std::vector<std::uint8_t>& before, const std::vector<std::uint8_t>& after) {
        LogType lt = LogType::Insert;
        if (std::strcmp(kind, "DELETE") == 0) lt = LogType::Delete;
        else if (std::strcmp(kind, "UPDATE") == 0) lt = LogType::Update;
        return txn_.Log(implicit_, lt, page, slot, before, after);
      });
      for (auto& ix : t->indexes) {
        if (ix->kind == IndexKind::BPlus) ix->btree = std::make_unique<BPlusTree>(&bpm_, ix->root_page, TypeId::Integer);
        else ix->hash = std::make_unique<ExtendibleHash>(&bpm_, ix->root_page);
      }
    }
  }

  void PersistCatalog() {
    auto bytes = catalog_.Serialize();
    Page* p = bpm_.FetchPage(cat_page_);
    auto len = static_cast<std::uint32_t>(bytes.size());
    std::memcpy(p->Data() + kPageHeaderSize, &len, 4);
    std::size_t n = std::min(bytes.size(), static_cast<std::size_t>(kPageSize - kPageHeaderSize - 4));
    std::memcpy(p->Data() + kPageHeaderSize + 4, bytes.data(), n);
    p->SetDirty(true);
    bpm_.UnpinPage(cat_page_, true);
    WriteSuper();
  }

  void CreateTable(const Statement& s) {
    Schema sch;
    for (auto& c : s.columns) sch.columns.push_back(Column{c.name, c.type, c.varchar_len, true});
    auto* t = catalog_.CreateTable(s.table, sch);
    t->heap_page = HeapFile::Create(&bpm_, fsm_.get());
    t->heap = std::make_unique<HeapFile>(&bpm_, fsm_.get(), t->heap_page, t->schema);
    t->heap->SetLogHook([this](std::uint32_t page, std::uint16_t slot, const char* kind,
                               const std::vector<std::uint8_t>& before, const std::vector<std::uint8_t>& after) {
      LogType lt = LogType::Insert;
      if (std::strcmp(kind, "DELETE") == 0) lt = LogType::Delete;
      else if (std::strcmp(kind, "UPDATE") == 0) lt = LogType::Update;
      return txn_.Log(implicit_, lt, page, slot, before, after);
    });
    PersistCatalog();
  }

  void CreateIndex(const Statement& s) {
    auto* t = catalog_.Get(s.table);
    if (!t) throw AegisError("unknown table: " + s.table);
    auto ix = std::make_unique<IndexInfo>();
    ix->name = s.index_name;
    ix->table = s.table;
    ix->column = s.index_column;
    ix->col_idx = t->schema.IndexOf(s.index_column);
    if (ix->col_idx < 0) throw AegisError("unknown column: " + s.index_column);
    ix->kind = s.index_kind;
    if (ix->kind == IndexKind::BPlus) {
      ix->root_page = BPlusTree::Create(&bpm_);
      ix->btree = std::make_unique<BPlusTree>(&bpm_, ix->root_page, t->schema.columns[static_cast<std::size_t>(ix->col_idx)].type);
    } else {
      ix->root_page = ExtendibleHash::Create(&bpm_);
      ix->hash = std::make_unique<ExtendibleHash>(&bpm_, ix->root_page);
    }
    t->heap->Scan([&](RID rid, Tuple& tup) {
      const Value& k = tup.values[static_cast<std::size_t>(ix->col_idx)];
      if (ix->btree) ix->btree->Insert(k, rid);
      else ix->hash->Insert(k, rid);
      return true;
    });
    t->indexes.push_back(std::move(ix));
    PersistCatalog();
  }

  void Insert(const Statement& s) {
    auto* t = catalog_.Get(s.table);
    if (!t) throw AegisError("unknown table: " + s.table);
    for (auto& row : s.rows) {
      Tuple tup;
      tup.xmin = implicit_->xid;
      tup.xmax = 0;
      tup.values.resize(t->schema.ColumnCount());
      for (std::size_t i = 0; i < tup.values.size(); ++i) {
        if (i < row.size()) {
          tup.values[i] = row[i];
          tup.values[i].type = t->schema.columns[i].type;
        } else {
          tup.values[i] = Value::Null(t->schema.columns[i].type);
        }
      }
      RID rid = t->heap->Insert(tup);
      implicit_->writes.push_back(rid);
      for (auto& ix : t->indexes) {
        const Value& k = tup.values[static_cast<std::size_t>(ix->col_idx)];
        if (ix->btree) ix->btree->Insert(k, rid);
        else if (ix->hash) ix->hash->Insert(k, rid);
      }
      t->stats.rows++;
    }
  }

  QueryResult RunSelect(const Statement& s) {
    Optimizer opt(&catalog_);
    auto plan = opt.PlanSelect(s);
    QueryResult r;
    r.plan = Optimizer::Explain(*plan);
    r.schema = plan->schema;
    auto exec = BuildExecutor(plan.get(), &catalog_, &txn_, implicit_);
    exec->Init();
    Tuple t;
    while (exec->Next(&t)) r.rows.push_back(std::move(t));
    exec->Close();
    return r;
  }

  void Analyze(const std::string& only) {
    auto run = [&](TableInfo* t) {
      t->stats.rows = 0;
      t->stats.pages = 1;
      t->stats.cols.assign(t->schema.ColumnCount(), {});
      std::vector<std::unordered_set<std::string>> ndv(t->schema.ColumnCount());
      t->heap->Scan([&](RID, Tuple& tup) {
        t->stats.rows++;
        for (std::size_t i = 0; i < tup.values.size(); ++i) {
          auto& st = t->stats.cols[i];
          if (tup.values[i].is_null) {
            st.null_frac += 1;
            continue;
          }
          ndv[i].insert(tup.values[i].ToString());
          if (st.min_v.is_null || tup.values[i].Compare(st.min_v) < 0) st.min_v = tup.values[i];
          if (st.max_v.is_null || tup.values[i].Compare(st.max_v) > 0) st.max_v = tup.values[i];
        }
        return true;
      });
      for (std::size_t i = 0; i < ndv.size(); ++i) {
        t->stats.cols[i].ndv = std::max<std::uint64_t>(1, ndv[i].size());
        if (t->stats.rows) t->stats.cols[i].null_frac /= static_cast<double>(t->stats.rows);
      }
      t->stats.pages = static_cast<std::uint32_t>(std::max<std::uint64_t>(1, t->stats.rows / 20 + 1));
      t->heap->SetTupleCount(t->stats.rows);
    };
    if (!only.empty()) {
      auto* t = catalog_.Get(only);
      if (!t) throw AegisError("unknown table: " + only);
      run(t);
    } else {
      for (const auto& name : catalog_.Tables()) run(catalog_.Get(name));
    }
    PersistCatalog();
  }

  std::string base_;
  DiskManager disk_;
  LogManager log_;
  BufferPool bpm_;
  TxnManager txn_;
  RecoveryManager recovery_;
  Catalog catalog_;
  std::unique_ptr<FreeSpaceMap> fsm_;
  std::uint32_t fsm_page_ = kInvalidPageId;
  std::uint32_t cat_page_ = kInvalidPageId;
  std::shared_ptr<Transaction> implicit_;
  bool skip_shutdown_flush_ = false;
};

}  // namespace aegisdb
