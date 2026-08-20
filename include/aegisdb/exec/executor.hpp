#pragma once

#include "aegisdb/catalog/catalog.hpp"
#include "aegisdb/plan/optimizer.hpp"
#include "aegisdb/storage/heap_file.hpp"
#include "aegisdb/txn/txn_manager.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <memory>
#include <queue>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace aegisdb {

class Executor {
 public:
  virtual ~Executor() = default;
  virtual void Init() = 0;
  virtual bool Next(Tuple* out) = 0;
  virtual const Schema& OutputSchema() const = 0;
  virtual void Close() {}
};

class SeqScanExec : public Executor {
 public:
  SeqScanExec(TableInfo* table, TxnManager* txn, std::shared_ptr<Transaction> tx, Schema out)
      : table_(table), txn_(txn), tx_(std::move(tx)), schema_(std::move(out)) {}

  void Init() override {
    rows_.clear();
    i_ = 0;
    table_->heap->Scan([&](RID, Tuple& t) {
      if (!txn_ || txn_->Visible(t, tx_->snap)) rows_.push_back(std::move(t));
      return true;
    });
  }
  bool Next(Tuple* out) override {
    if (i_ >= rows_.size()) return false;
    *out = rows_[i_++];
    return true;
  }
  const Schema& OutputSchema() const override { return schema_; }

 private:
  TableInfo* table_;
  TxnManager* txn_;
  std::shared_ptr<Transaction> tx_;
  Schema schema_;
  std::vector<Tuple> rows_;
  std::size_t i_ = 0;
};

class IndexScanExec : public Executor {
 public:
  IndexScanExec(TableInfo* table, IndexInfo* ix, PhysNode* node, TxnManager* txn, std::shared_ptr<Transaction> tx)
      : table_(table), ix_(ix), node_(node), txn_(txn), tx_(std::move(tx)), schema_(node->schema) {}

  void Init() override {
    rows_.clear();
    i_ = 0;
    std::vector<RID> rids;
    if (ix_->kind == IndexKind::BPlus && ix_->btree) {
      std::optional<Value> lo, hi;
      if (node_->has_lo) lo = node_->index_lo;
      if (node_->has_hi) {
        hi = node_->index_hi;
        if (node_->index_eq) {
          // inclusive point lookup: bump a copy for exclusive end via Search
          rids = ix_->btree->Search(node_->index_lo);
        } else {
          auto pairs = ix_->btree->Range(lo, hi);
          for (auto& p : pairs) rids.push_back(p.second);
        }
      } else {
        auto pairs = ix_->btree->Range(lo, hi);
        for (auto& p : pairs) rids.push_back(p.second);
      }
    } else if (ix_->hash && node_->index_eq) {
      rids = ix_->hash->Search(node_->index_lo);
    }
    for (RID rid : rids) {
      auto t = table_->heap->Get(rid);
      if (!t) continue;
      if (txn_ && tx_ && !txn_->Visible(*t, tx_->snap)) continue;
      rows_.push_back(*t);
    }
  }
  bool Next(Tuple* out) override {
    if (i_ >= rows_.size()) return false;
    *out = rows_[i_++];
    return true;
  }
  const Schema& OutputSchema() const override { return schema_; }

 private:
  TableInfo* table_;
  IndexInfo* ix_;
  PhysNode* node_;
  TxnManager* txn_;
  std::shared_ptr<Transaction> tx_;
  Schema schema_;
  std::vector<Tuple> rows_;
  std::size_t i_ = 0;
};

class FilterExec : public Executor {
 public:
  FilterExec(std::unique_ptr<Executor> child, std::unique_ptr<Expr> pred)
      : child_(std::move(child)), pred_(std::move(pred)) {}
  void Init() override { child_->Init(); }
  bool Next(Tuple* out) override {
    while (child_->Next(out)) {
      if (PredTrue(pred_.get(), *out, child_->OutputSchema())) return true;
    }
    return false;
  }
  const Schema& OutputSchema() const override { return child_->OutputSchema(); }

 private:
  std::unique_ptr<Executor> child_;
  std::unique_ptr<Expr> pred_;
};

class ProjectExec : public Executor {
 public:
  ProjectExec(std::unique_ptr<Executor> child, std::vector<std::unique_ptr<Expr>> proj, Schema out)
      : child_(std::move(child)), proj_(std::move(proj)), schema_(std::move(out)) {}
  void Init() override { child_->Init(); }
  bool Next(Tuple* out) override {
    Tuple in;
    if (!child_->Next(&in)) return false;
    if (proj_.empty()) {
      *out = in;
      return true;
    }
    out->values.clear();
    for (auto& e : proj_) out->values.push_back(EvalExpr(*e, in, child_->OutputSchema()));
    return true;
  }
  const Schema& OutputSchema() const override { return schema_; }

 private:
  std::unique_ptr<Executor> child_;
  std::vector<std::unique_ptr<Expr>> proj_;
  Schema schema_;
};

class HashJoinExec : public Executor {
 public:
  HashJoinExec(std::unique_ptr<Executor> left, std::unique_ptr<Executor> right, int lc, int rc, Schema out)
      : left_(std::move(left)), right_(std::move(right)), lc_(lc), rc_(rc), schema_(std::move(out)) {}
  void Init() override {
    build_.clear();
    left_->Init();
    Tuple t;
    while (left_->Next(&t)) {
      if (lc_ < 0 || lc_ >= static_cast<int>(t.values.size())) continue;
      build_[t.values[static_cast<std::size_t>(lc_)].Hash()].push_back(t);
    }
    right_->Init();
    probe_i_ = 0;
    have_probe_ = false;
  }
  bool Next(Tuple* out) override {
    while (true) {
      if (have_probe_ && probe_i_ < cand_.size()) {
        Concat(out, cand_[probe_i_++], probe_);
        return true;
      }
      if (!right_->Next(&probe_)) return false;
      have_probe_ = true;
      probe_i_ = 0;
      cand_.clear();
      if (rc_ < 0 || rc_ >= static_cast<int>(probe_.values.size())) continue;
      auto h = probe_.values[static_cast<std::size_t>(rc_)].Hash();
      auto it = build_.find(h);
      if (it == build_.end()) {
        have_probe_ = false;
        continue;
      }
      for (auto& l : it->second) {
        if (l.values[static_cast<std::size_t>(lc_)].Compare(probe_.values[static_cast<std::size_t>(rc_)]) == 0)
          cand_.push_back(l);
      }
    }
  }
  const Schema& OutputSchema() const override { return schema_; }

 private:
  static void Concat(Tuple* out, const Tuple& l, const Tuple& r) {
    out->values = l.values;
    out->values.insert(out->values.end(), r.values.begin(), r.values.end());
  }
  std::unique_ptr<Executor> left_, right_;
  int lc_, rc_;
  Schema schema_;
  std::unordered_map<std::size_t, std::vector<Tuple>> build_;
  Tuple probe_;
  std::vector<Tuple> cand_;
  std::size_t probe_i_ = 0;
  bool have_probe_ = false;
};

class NestedLoopJoinExec : public Executor {
 public:
  NestedLoopJoinExec(std::unique_ptr<Executor> left, std::unique_ptr<Executor> right, std::unique_ptr<Expr> pred,
                     Schema out)
      : left_(std::move(left)), right_(std::move(right)), pred_(std::move(pred)), schema_(std::move(out)) {}
  void Init() override {
    left_rows_.clear();
    left_->Init();
    Tuple t;
    while (left_->Next(&t)) left_rows_.push_back(t);
    right_->Init();
    li_ = 0;
    have_r_ = false;
  }
  bool Next(Tuple* out) override {
    while (true) {
      if (!have_r_) {
        if (!right_->Next(&r_)) return false;
        have_r_ = true;
        li_ = 0;
      }
      while (li_ < left_rows_.size()) {
        Tuple j;
        j.values = left_rows_[li_++].values;
        j.values.insert(j.values.end(), r_.values.begin(), r_.values.end());
        if (!pred_ || PredTrue(pred_.get(), j, schema_)) {
          *out = std::move(j);
          return true;
        }
      }
      have_r_ = false;
    }
  }
  const Schema& OutputSchema() const override { return schema_; }

 private:
  std::unique_ptr<Executor> left_, right_;
  std::unique_ptr<Expr> pred_;
  Schema schema_;
  std::vector<Tuple> left_rows_;
  Tuple r_;
  std::size_t li_ = 0;
  bool have_r_ = false;
};

class ExternalSortExec : public Executor {
 public:
  ExternalSortExec(std::unique_ptr<Executor> child, std::vector<OrderBy> order, std::size_t mem_tuples = 8192)
      : child_(std::move(child)), order_(std::move(order)), mem_(mem_tuples) {}

  void Init() override {
    child_->Init();
    runs_.clear();
    std::vector<Tuple> buf;
    Tuple t;
    while (child_->Next(&t)) {
      buf.push_back(std::move(t));
      if (buf.size() >= mem_) {
        SortBuf(buf);
        Spill(buf);
        buf.clear();
      }
    }
    SortBuf(buf);
    if (runs_.empty()) {
      data_ = std::move(buf);
    } else {
      if (!buf.empty()) Spill(buf);
      Merge();
    }
    i_ = 0;
  }
  bool Next(Tuple* out) override {
    if (i_ >= data_.size()) return false;
    *out = data_[i_++];
    return true;
  }
  const Schema& OutputSchema() const override { return child_->OutputSchema(); }
  void Close() override {
    for (auto& f : run_files_) std::remove(f.c_str());
  }

 private:
  int Cmp(const Tuple& a, const Tuple& b) const {
    for (const auto& o : order_) {
      Value va = EvalExpr(*o.expr, a, child_->OutputSchema());
      Value vb = EvalExpr(*o.expr, b, child_->OutputSchema());
      int c = va.Compare(vb);
      if (c != 0) return o.desc ? -c : c;
    }
    return 0;
  }
  void SortBuf(std::vector<Tuple>& buf) {
    std::sort(buf.begin(), buf.end(), [&](const Tuple& a, const Tuple& b) { return Cmp(a, b) < 0; });
  }
  void Spill(const std::vector<Tuple>& buf) {
    std::string path = "/tmp/aegis-sort-" + std::to_string(reinterpret_cast<uintptr_t>(this)) + "-" +
                       std::to_string(runs_.size());
    std::ofstream out(path, std::ios::binary);
    auto pu32 = [&](std::uint32_t x) { out.write(reinterpret_cast<const char*>(&x), 4); };
    pu32(static_cast<std::uint32_t>(buf.size()));
    for (const auto& t : buf) {
      auto b = SerializeTuple(t);
      pu32(static_cast<std::uint32_t>(b.size()));
      out.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
    }
    run_files_.push_back(path);
    runs_.push_back(path);
  }
  void Merge() {
    struct Item {
      Tuple t;
      std::size_t run;
    };
    auto cmp = [&](const Item& a, const Item& b) { return Cmp(a.t, b.t) > 0; };
    std::priority_queue<Item, std::vector<Item>, decltype(cmp)> pq(cmp);
    std::vector<std::ifstream> ins;
    std::vector<std::uint32_t> left;
    ins.reserve(runs_.size());
    left.resize(runs_.size());
    for (std::size_t i = 0; i < runs_.size(); ++i) {
      ins.emplace_back(runs_[i], std::ios::binary);
      ins[i].read(reinterpret_cast<char*>(&left[i]), 4);
      if (left[i] > 0) {
        std::uint32_t sz;
        ins[i].read(reinterpret_cast<char*>(&sz), 4);
        std::vector<std::uint8_t> b(sz);
        ins[i].read(reinterpret_cast<char*>(b.data()), sz);
        pq.push(Item{DeserializeTuple(b.data(), b.size()), i});
        left[i]--;
      }
    }
    data_.clear();
    while (!pq.empty()) {
      auto it = pq.top();
      pq.pop();
      data_.push_back(it.t);
      auto r = it.run;
      if (left[r] > 0) {
        std::uint32_t sz;
        ins[r].read(reinterpret_cast<char*>(&sz), 4);
        std::vector<std::uint8_t> b(sz);
        ins[r].read(reinterpret_cast<char*>(b.data()), sz);
        pq.push(Item{DeserializeTuple(b.data(), b.size()), r});
        left[r]--;
      }
    }
  }

  std::unique_ptr<Executor> child_;
  std::vector<OrderBy> order_;
  std::size_t mem_;
  std::vector<Tuple> data_;
  std::vector<std::string> runs_;
  std::vector<std::string> run_files_;
  std::size_t i_ = 0;
};

class SortMergeJoinExec : public Executor {
 public:
  SortMergeJoinExec(std::unique_ptr<Executor> left, std::unique_ptr<Executor> right, int lc, int rc, Schema out)
      : lc_(lc), rc_(rc), schema_(std::move(out)) {
    OrderBy ol, orr;
    auto el = std::make_unique<Expr>();
    el->kind = ExprKind::Column;
    el->name = left->OutputSchema().columns[static_cast<std::size_t>(std::max(0, lc))].name;
    auto er = std::make_unique<Expr>();
    er->kind = ExprKind::Column;
    er->name = right->OutputSchema().columns[static_cast<std::size_t>(std::max(0, rc))].name;
    ol.expr = std::move(el);
    orr.expr = std::move(er);
    std::vector<OrderBy> lo, ro;
    lo.push_back(std::move(ol));
    ro.push_back(std::move(orr));
    left_ = std::make_unique<ExternalSortExec>(std::move(left), std::move(lo));
    right_ = std::make_unique<ExternalSortExec>(std::move(right), std::move(ro));
  }
  void Init() override {
    left_->Init();
    right_->Init();
    has_l_ = left_->Next(&l_);
    has_r_ = right_->Next(&r_);
    rbuf_.clear();
    ri_ = 0;
  }
  bool Next(Tuple* out) override {
    while (has_l_ && has_r_) {
      int c = l_.values[static_cast<std::size_t>(lc_)].Compare(r_.values[static_cast<std::size_t>(rc_)]);
      if (c < 0) {
        has_l_ = left_->Next(&l_);
        rbuf_.clear();
        continue;
      }
      if (c > 0) {
        has_r_ = right_->Next(&r_);
        rbuf_.clear();
        continue;
      }
      if (rbuf_.empty()) {
        Value key = r_.values[static_cast<std::size_t>(rc_)];
        rbuf_.push_back(r_);
        while (true) {
          Tuple n;
          if (!right_->Next(&n)) {
            has_r_ = false;
            break;
          }
          if (n.values[static_cast<std::size_t>(rc_)].Compare(key) != 0) {
            r_ = n;
            has_r_ = true;
            break;
          }
          rbuf_.push_back(n);
        }
        ri_ = 0;
      }
      if (ri_ < rbuf_.size()) {
        out->values = l_.values;
        out->values.insert(out->values.end(), rbuf_[ri_].values.begin(), rbuf_[ri_].values.end());
        ++ri_;
        if (ri_ == rbuf_.size()) {
          has_l_ = left_->Next(&l_);
          if (has_l_ && l_.values[static_cast<std::size_t>(lc_)].Compare(rbuf_[0].values[static_cast<std::size_t>(rc_)]) ==
                            0) {
            ri_ = 0;
          } else {
            rbuf_.clear();
          }
        }
        return true;
      }
    }
    return false;
  }
  const Schema& OutputSchema() const override { return schema_; }

 private:
  std::unique_ptr<Executor> left_, right_;
  int lc_, rc_;
  Schema schema_;
  Tuple l_, r_;
  bool has_l_ = false, has_r_ = false;
  std::vector<Tuple> rbuf_;
  std::size_t ri_ = 0;
};

class HashAggExec : public Executor {
 public:
  HashAggExec(std::unique_ptr<Executor> child, std::vector<std::unique_ptr<Expr>> groups,
              std::vector<std::unique_ptr<Expr>> aggs, Schema out)
      : child_(std::move(child)), groups_(std::move(groups)), aggs_(std::move(aggs)), schema_(std::move(out)) {}
  void Init() override {
    child_->Init();
    struct Acc {
      std::vector<Value> keys;
      std::vector<double> sum;
      std::vector<std::uint64_t> cnt;
      std::vector<Value> minv;
      std::vector<Value> maxv;
    };
    std::unordered_map<std::string, Acc> m;
    Tuple t;
    const Schema& in = child_->OutputSchema();
    while (child_->Next(&t)) {
      std::string key;
      std::vector<Value> kv;
      for (auto& g : groups_) {
        auto v = EvalExpr(*g, t, in);
        kv.push_back(v);
        key += v.ToString();
        key.push_back(0);
      }
      auto& a = m[key];
      if (a.cnt.empty()) {
        a.keys = kv;
        a.sum.assign(aggs_.size(), 0);
        a.cnt.assign(aggs_.size(), 0);
        a.minv.resize(aggs_.size(), Value::Null(TypeId::Double));
        a.maxv.resize(aggs_.size(), Value::Null(TypeId::Double));
      }
      for (std::size_t i = 0; i < aggs_.size(); ++i) {
        Value v = EvalExpr(*aggs_[i], t, in);
        double d = v.is_null ? 0 : (v.type == TypeId::Double ? v.d : static_cast<double>(v.i64));
        a.sum[i] += d;
        a.cnt[i]++;
        if (a.minv[i].is_null || v.Compare(a.minv[i]) < 0) a.minv[i] = v;
        if (a.maxv[i].is_null || v.Compare(a.maxv[i]) > 0) a.maxv[i] = v;
      }
    }
    rows_.clear();
    for (auto& [k, a] : m) {
      Tuple o;
      o.values = a.keys;
      for (std::size_t i = 0; i < aggs_.size(); ++i) {
        AggOp op = aggs_[i]->kind == ExprKind::Agg ? aggs_[i]->agg : AggOp::Sum;
        if (aggs_[i]->kind == ExprKind::BinOp) op = AggOp::Sum;  // SUM(expr)
        // Detect agg wrapper.
        const Expr* e = aggs_[i].get();
        if (e->kind == ExprKind::Agg) op = e->agg;
        switch (op) {
          case AggOp::Count:
            o.values.push_back(Value::Integer(static_cast<std::int64_t>(a.cnt[i])));
            break;
          case AggOp::Sum:
            o.values.push_back(Value::Double(a.sum[i]));
            break;
          case AggOp::Avg:
            o.values.push_back(Value::Double(a.cnt[i] ? a.sum[i] / static_cast<double>(a.cnt[i]) : 0));
            break;
          case AggOp::Min:
            o.values.push_back(a.minv[i]);
            break;
          case AggOp::Max:
            o.values.push_back(a.maxv[i]);
            break;
        }
      }
      rows_.push_back(std::move(o));
    }
    i_ = 0;
  }
  bool Next(Tuple* out) override {
    if (i_ >= rows_.size()) return false;
    *out = rows_[i_++];
    return true;
  }
  const Schema& OutputSchema() const override { return schema_; }

 private:
  std::unique_ptr<Executor> child_;
  std::vector<std::unique_ptr<Expr>> groups_;
  std::vector<std::unique_ptr<Expr>> aggs_;
  Schema schema_;
  std::vector<Tuple> rows_;
  std::size_t i_ = 0;
};

class LimitExec : public Executor {
 public:
  LimitExec(std::unique_ptr<Executor> child, int n) : child_(std::move(child)), n_(n) {}
  void Init() override {
    child_->Init();
    seen_ = 0;
  }
  bool Next(Tuple* out) override {
    if (seen_ >= n_) return false;
    if (!child_->Next(out)) return false;
    ++seen_;
    return true;
  }
  const Schema& OutputSchema() const override { return child_->OutputSchema(); }

 private:
  std::unique_ptr<Executor> child_;
  int n_;
  int seen_ = 0;
};

inline std::unique_ptr<Executor> BuildExecutor(PhysNode* n, Catalog* cat, TxnManager* txn,
                                               std::shared_ptr<Transaction> tx) {
  switch (n->op) {
    case PhysOp::SeqScan: {
      auto* t = cat->Get(n->table);
      return std::make_unique<SeqScanExec>(t, txn, tx, n->schema);
    }
    case PhysOp::IndexScan: {
      auto* t = cat->Get(n->table);
      auto* ix = cat->FindIndex(n->table, t->indexes.empty() ? "" : [&] {
        for (auto& i : t->indexes)
          if (i->name == n->index) return i->column;
        return t->indexes[0]->column;
      }());
      if (!ix) {
        for (auto& i : t->indexes)
          if (i->name == n->index) ix = i.get();
      }
      return std::make_unique<IndexScanExec>(t, ix, n, txn, tx);
    }
    case PhysOp::Filter: {
      auto c = BuildExecutor(n->left.get(), cat, txn, tx);
      return std::make_unique<FilterExec>(std::move(c), n->pred ? CloneExpr(*n->pred) : nullptr);
    }
    case PhysOp::Project: {
      auto c = BuildExecutor(n->left.get(), cat, txn, tx);
      std::vector<std::unique_ptr<Expr>> p;
      for (auto& e : n->projects) p.push_back(CloneExpr(*e));
      return std::make_unique<ProjectExec>(std::move(c), std::move(p), n->schema);
    }
    case PhysOp::HashJoin: {
      auto l = BuildExecutor(n->left.get(), cat, txn, tx);
      auto r = BuildExecutor(n->right.get(), cat, txn, tx);
      return std::make_unique<HashJoinExec>(std::move(l), std::move(r), n->left_join_col, n->right_join_col, n->schema);
    }
    case PhysOp::MergeJoin: {
      auto l = BuildExecutor(n->left.get(), cat, txn, tx);
      auto r = BuildExecutor(n->right.get(), cat, txn, tx);
      return std::make_unique<SortMergeJoinExec>(std::move(l), std::move(r), n->left_join_col, n->right_join_col,
                                                 n->schema);
    }
    case PhysOp::NestedLoop: {
      auto l = BuildExecutor(n->left.get(), cat, txn, tx);
      auto r = BuildExecutor(n->right.get(), cat, txn, tx);
      return std::make_unique<NestedLoopJoinExec>(std::move(l), std::move(r), n->pred ? CloneExpr(*n->pred) : nullptr,
                                                  n->schema);
    }
    case PhysOp::HashAgg: {
      auto c = BuildExecutor(n->left.get(), cat, txn, tx);
      std::vector<std::unique_ptr<Expr>> g, a;
      for (auto& e : n->groups) g.push_back(CloneExpr(*e));
      for (auto& e : n->aggs) a.push_back(CloneExpr(*e));
      return std::make_unique<HashAggExec>(std::move(c), std::move(g), std::move(a), n->schema);
    }
    case PhysOp::Sort: {
      auto c = BuildExecutor(n->left.get(), cat, txn, tx);
      std::vector<OrderBy> o;
      for (auto& e : n->order) {
        OrderBy x;
        x.expr = CloneExpr(*e.expr);
        x.desc = e.desc;
        o.push_back(std::move(x));
      }
      return std::make_unique<ExternalSortExec>(std::move(c), std::move(o));
    }
    case PhysOp::Limit: {
      auto c = BuildExecutor(n->left.get(), cat, txn, tx);
      return std::make_unique<LimitExec>(std::move(c), n->limit);
    }
  }
  throw AegisError("unknown physical op");
}

}  // namespace aegisdb
