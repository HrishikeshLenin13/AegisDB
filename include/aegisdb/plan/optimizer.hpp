#pragma once

#include "aegisdb/catalog/catalog.hpp"
#include "aegisdb/parse/parser.hpp"

#include <cmath>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace aegisdb {

enum class PhysOp {
  SeqScan,
  IndexScan,
  Filter,
  Project,
  HashJoin,
  MergeJoin,
  NestedLoop,
  HashAgg,
  Sort,
  Limit,
};

struct PhysNode {
  PhysOp op = PhysOp::SeqScan;
  std::string table;
  std::string alias;
  std::string index;
  int index_col = -1;
  bool index_eq = false;
  Value index_lo;
  Value index_hi;
  bool has_lo = false;
  bool has_hi = false;
  std::unique_ptr<Expr> pred;
  std::vector<std::unique_ptr<Expr>> projects;
  std::vector<std::unique_ptr<Expr>> groups;
  std::vector<std::unique_ptr<Expr>> aggs;
  std::vector<std::string> out_names;
  std::vector<OrderBy> order;
  int limit = -1;
  int left_join_col = -1;
  int right_join_col = -1;
  double cost = 0;
  double rows = 0;
  Schema schema;
  std::unique_ptr<PhysNode> left;
  std::unique_ptr<PhysNode> right;
};

inline std::unique_ptr<Expr> CloneExpr(const Expr& e) {
  auto n = std::make_unique<Expr>();
  n->kind = e.kind;
  n->table = e.table;
  n->name = e.name;
  n->value = e.value;
  n->op = e.op;
  n->agg = e.agg;
  n->alias = e.alias;
  if (e.lhs) n->lhs = CloneExpr(*e.lhs);
  if (e.rhs) n->rhs = CloneExpr(*e.rhs);
  if (e.arg) n->arg = CloneExpr(*e.arg);
  return n;
}

inline Value EvalExpr(const Expr& e, const Tuple& t, const Schema& sch);

inline int ColIndex(const Schema& sch, const std::string& table, const std::string& name) {
  if (!table.empty()) {
    int i = sch.IndexOf(table + "." + name);
    if (i >= 0) return i;
    for (int c = 0; c < static_cast<int>(sch.columns.size()); ++c) {
      const auto& n = sch.columns[static_cast<std::size_t>(c)].name;
      auto dot = n.rfind('.');
      if (dot != std::string::npos && n.substr(0, dot) == table && n.substr(dot + 1) == name) return c;
    }
    return -1;
  }
  int i = sch.IndexOf(name);
  if (i >= 0) return i;
  int found = -1;
  for (int c = 0; c < static_cast<int>(sch.columns.size()); ++c) {
    const auto& n = sch.columns[static_cast<std::size_t>(c)].name;
    auto dot = n.rfind('.');
    if (dot != std::string::npos && n.substr(dot + 1) == name) {
      if (found >= 0) return -1;
      found = c;
    }
  }
  return found;
}

inline Value EvalExpr(const Expr& e, const Tuple& t, const Schema& sch) {
  switch (e.kind) {
    case ExprKind::Literal:
      return e.value;
    case ExprKind::Star:
      return Value::Integer(1);
    case ExprKind::Column: {
      int i = ColIndex(sch, e.table, e.name);
      if (i < 0 || i >= static_cast<int>(t.values.size())) return Value::Null(TypeId::Integer);
      return t.values[static_cast<std::size_t>(i)];
    }
    case ExprKind::Agg:
      return e.arg ? EvalExpr(*e.arg, t, sch) : Value::Integer(1);
    case ExprKind::Alias:
      return e.lhs ? EvalExpr(*e.lhs, t, sch) : Value::Null(TypeId::Integer);
    case ExprKind::BinOp: {
      Value l = EvalExpr(*e.lhs, t, sch);
      Value r = EvalExpr(*e.rhs, t, sch);
      auto num = [](const Value& v) -> double {
        if (v.is_null) return 0;
        return v.type == TypeId::Double ? v.d : static_cast<double>(v.i64);
      };
      switch (e.op) {
        case BinOp::Add: {
          if (l.type == TypeId::Double || r.type == TypeId::Double) return Value::Double(num(l) + num(r));
          return Value::Integer(l.i64 + r.i64);
        }
        case BinOp::Sub: {
          if (l.type == TypeId::Double || r.type == TypeId::Double) return Value::Double(num(l) - num(r));
          return Value::Integer(l.i64 - r.i64);
        }
        case BinOp::Mul: {
          if (l.type == TypeId::Double || r.type == TypeId::Double) return Value::Double(num(l) * num(r));
          return Value::Integer(l.i64 * r.i64);
        }
        case BinOp::Div: {
          double d = num(r);
          if (d == 0) return Value::Null(TypeId::Double);
          return Value::Double(num(l) / d);
        }
        case BinOp::Eq:
          return Value::Boolean(l.Compare(r) == 0 && !l.is_null && !r.is_null);
        case BinOp::Ne:
          return Value::Boolean(l.Compare(r) != 0);
        case BinOp::Lt:
          return Value::Boolean(l.Compare(r) < 0);
        case BinOp::Gt:
          return Value::Boolean(l.Compare(r) > 0);
        case BinOp::Le:
          return Value::Boolean(l.Compare(r) <= 0);
        case BinOp::Ge:
          return Value::Boolean(l.Compare(r) >= 0);
        case BinOp::And:
          return Value::Boolean(l.AsBool() && r.AsBool());
        case BinOp::Or:
          return Value::Boolean(l.AsBool() || r.AsBool());
      }
    }
  }
  return Value::Null(TypeId::Integer);
}

inline bool PredTrue(const Expr* e, const Tuple& t, const Schema& sch) {
  if (!e) return true;
  return EvalExpr(*e, t, sch).AsBool();
}

struct RelSlot {
  std::string table;
  std::string alias;
  TableInfo* info = nullptr;
  std::unique_ptr<Expr> filter;
};

inline double Selectivity(const Expr* e, TableInfo* t) {
  if (!e || !t) return 1.0;
  if (e->kind != ExprKind::BinOp) return 0.5;
  if (e->op == BinOp::And) return Selectivity(e->lhs.get(), t) * Selectivity(e->rhs.get(), t);
  if (e->op == BinOp::Or) {
    double a = Selectivity(e->lhs.get(), t);
    double b = Selectivity(e->rhs.get(), t);
    return a + b - a * b;
  }
  const Expr* col = nullptr;
  const Expr* lit = nullptr;
  if (e->lhs && e->lhs->kind == ExprKind::Column) col = e->lhs.get();
  if (e->rhs && e->rhs->kind == ExprKind::Literal) lit = e->rhs.get();
  if (e->rhs && e->rhs->kind == ExprKind::Column) col = e->rhs.get();
  if (e->lhs && e->lhs->kind == ExprKind::Literal) lit = e->lhs.get();
  if (!col) return 0.33;
  int idx = t->schema.IndexOf(col->name);
  ColumnStats st;
  if (idx >= 0 && idx < static_cast<int>(t->stats.cols.size())) st = t->stats.cols[static_cast<std::size_t>(idx)];
  double ndv = std::max<double>(1, static_cast<double>(st.ndv));
  if (e->op == BinOp::Eq) return 1.0 / ndv;
  if (e->op == BinOp::Ne) return 1.0 - 1.0 / ndv;
  if (!lit || st.min_v.is_null || st.max_v.is_null) return 0.33;
  double min = st.min_v.type == TypeId::Double ? st.min_v.d : static_cast<double>(st.min_v.i64);
  double max = st.max_v.type == TypeId::Double ? st.max_v.d : static_cast<double>(st.max_v.i64);
  double span = std::max(1.0, max - min);
  double v = lit->value.type == TypeId::Double ? lit->value.d : static_cast<double>(lit->value.i64);
  if (e->op == BinOp::Lt || e->op == BinOp::Le) return std::clamp((v - min) / span, 0.01, 1.0);
  if (e->op == BinOp::Gt || e->op == BinOp::Ge) return std::clamp((max - v) / span, 0.01, 1.0);
  return 0.33;
}

inline bool ExtractIndexPred(const Expr* e, const std::string& col, Value* lo, Value* hi, bool* has_lo, bool* has_hi,
                             bool* eq) {
  if (!e || e->kind != ExprKind::BinOp) return false;
  if (e->op == BinOp::And) {
    bool a = ExtractIndexPred(e->lhs.get(), col, lo, hi, has_lo, has_hi, eq);
    bool b = ExtractIndexPred(e->rhs.get(), col, lo, hi, has_lo, has_hi, eq);
    return a || b;
  }
  const Expr* c = nullptr;
  const Expr* lit = nullptr;
  bool flip = false;
  if (e->lhs && e->lhs->kind == ExprKind::Column && e->lhs->name == col && e->rhs && e->rhs->kind == ExprKind::Literal) {
    c = e->lhs.get();
    lit = e->rhs.get();
  } else if (e->rhs && e->rhs->kind == ExprKind::Column && e->rhs->name == col && e->lhs &&
             e->lhs->kind == ExprKind::Literal) {
    c = e->rhs.get();
    lit = e->lhs.get();
    flip = true;
  }
  if (!c || !lit) return false;
  BinOp op = e->op;
  if (flip) {
    if (op == BinOp::Lt) op = BinOp::Gt;
    else if (op == BinOp::Gt) op = BinOp::Lt;
    else if (op == BinOp::Le) op = BinOp::Ge;
    else if (op == BinOp::Ge) op = BinOp::Le;
  }
  if (op == BinOp::Eq) {
    *lo = lit->value;
    *hi = lit->value;
    *has_lo = *has_hi = true;
    *eq = true;
    return true;
  }
  if (op == BinOp::Gt || op == BinOp::Ge) {
    *lo = lit->value;
    *has_lo = true;
    return true;
  }
  if (op == BinOp::Lt || op == BinOp::Le) {
    *hi = lit->value;
    *has_hi = true;
    return true;
  }
  return false;
}

inline std::unique_ptr<PhysNode> MakeScan(RelSlot& r) {
  auto n = std::make_unique<PhysNode>();
  n->table = r.table;
  n->alias = r.alias.empty() ? r.table : r.alias;
  n->schema = r.info->schema;
  for (auto& c : n->schema.columns) {
    if (c.name.find('.') == std::string::npos) c.name = n->alias + "." + c.name;
  }
  double rows = std::max<double>(1, static_cast<double>(r.info->stats.rows));
  double pages = std::max<double>(1, static_cast<double>(r.info->stats.pages));
  double sel = Selectivity(r.filter.get(), r.info);
  n->rows = std::max(1.0, rows * sel);
  n->cost = pages * 1.0 + rows * 0.01;
  n->op = PhysOp::SeqScan;
  if (r.filter) {
    // Index path if a btree exists on a sargable column.
    for (auto& ix : r.info->indexes) {
      Value lo, hi;
      bool has_lo = false, has_hi = false, eq = false;
      if (!ExtractIndexPred(r.filter.get(), ix->column, &lo, &hi, &has_lo, &has_hi, &eq)) continue;
      if (ix->kind == IndexKind::Hash && !eq) continue;
      auto idx = std::make_unique<PhysNode>();
      idx->op = PhysOp::IndexScan;
      idx->table = n->table;
      idx->alias = n->alias;
      idx->schema = n->schema;
      idx->index = ix->name;
      idx->index_col = ix->col_idx;
      idx->index_lo = lo;
      idx->index_hi = hi;
      idx->has_lo = has_lo;
      idx->has_hi = has_hi;
      idx->index_eq = eq;
      idx->rows = n->rows;
      idx->cost = (ix->kind == IndexKind::Hash ? 4.0 : 12.0) + n->rows * 0.02;
      if (idx->cost < n->cost) n = std::move(idx);
      break;
    }
    if (r.filter) {
      auto f = std::make_unique<PhysNode>();
      f->op = PhysOp::Filter;
      f->pred = CloneExpr(*r.filter);
      f->schema = n->schema;
      f->rows = n->rows;
      f->cost = n->cost + n->rows * 0.0025;
      f->left = std::move(n);
      n = std::move(f);
    }
  }
  return n;
}

inline std::unique_ptr<PhysNode> MakeJoin(std::unique_ptr<PhysNode> L, std::unique_ptr<PhysNode> R, const Expr& on) {
  int lc = -1, rc = -1;
  if (on.kind == ExprKind::BinOp && on.op == BinOp::Eq && on.lhs && on.rhs && on.lhs->kind == ExprKind::Column &&
      on.rhs->kind == ExprKind::Column) {
    lc = ColIndex(L->schema, on.lhs->table, on.lhs->name);
    rc = ColIndex(R->schema, on.rhs->table, on.rhs->name);
    if (lc < 0) {
      lc = ColIndex(L->schema, on.rhs->table, on.rhs->name);
      rc = ColIndex(R->schema, on.lhs->table, on.lhs->name);
    }
  }
  Schema sch = L->schema;
  sch.columns.insert(sch.columns.end(), R->schema.columns.begin(), R->schema.columns.end());
  double jr = std::max(1.0, L->rows * R->rows / std::max(10.0, std::min(L->rows, R->rows)));
  double hash_cost = L->cost + R->cost + L->rows * 0.02 + R->rows * 0.015;
  double merge_cost = L->cost + R->cost + L->rows * std::log2(std::max(2.0, L->rows)) * 0.01 +
                      R->rows * std::log2(std::max(2.0, R->rows)) * 0.01 + (L->rows + R->rows) * 0.01;
  auto n = std::make_unique<PhysNode>();
  n->schema = std::move(sch);
  n->rows = jr;
  n->left_join_col = lc;
  n->right_join_col = rc;
  n->pred = CloneExpr(on);
  if (lc >= 0 && rc >= 0 && merge_cost < hash_cost * 0.9) {
    n->op = PhysOp::MergeJoin;
    n->cost = merge_cost;
  } else if (lc >= 0 && rc >= 0) {
    n->op = PhysOp::HashJoin;
    n->cost = hash_cost;
  } else {
    n->op = PhysOp::NestedLoop;
    n->cost = L->cost + L->rows * R->cost;
  }
  n->left = std::move(L);
  n->right = std::move(R);
  return n;
}

class Optimizer {
 public:
  explicit Optimizer(Catalog* cat) : cat_(cat) {}

  std::unique_ptr<PhysNode> PlanSelect(const Statement& s) {
    std::vector<RelSlot> rels;
    auto add = [&](const std::string& table, const std::string& alias) {
      RelSlot r;
      r.table = table;
      r.alias = alias.empty() ? table : alias;
      r.info = cat_->Get(table);
      if (!r.info) throw AegisError("unknown table: " + table);
      rels.push_back(std::move(r));
    };
    add(s.from, s.from_alias);
    for (const auto& j : s.joins) add(j.table, j.alias);

    // Attach filters that mention only one relation.
    if (s.where) DistributeFilters(*s.where, rels);

    const int n = static_cast<int>(rels.size());
    std::vector<std::unique_ptr<PhysNode>> best(1u << n);
    std::vector<double> best_cost(1u << n, 1e100);
    for (int i = 0; i < n; ++i) {
      auto p = MakeScan(rels[static_cast<std::size_t>(i)]);
      best_cost[1 << i] = p->cost;
      best[1 << i] = std::move(p);
    }
    // Selinger-style left-deep DP join ordering.
    for (int mask = 1; mask < (1 << n); ++mask) {
      if (!best[mask]) continue;
      for (int i = 0; i < n; ++i) {
        if (mask & (1 << i)) continue;
        int nmask = mask | (1 << i);
        const Expr* on = JoinPred(s, rels, mask, i);
        if (!on && n > 1) continue;  // connected join graph only
        Expr dummy;
        dummy.kind = ExprKind::Literal;
        dummy.value = Value::Boolean(true);
        auto joined = MakeJoin(ClonePlan(*best[mask]), MakeScan(rels[static_cast<std::size_t>(i)]), on ? *on : dummy);
        if (joined->cost < best_cost[nmask]) {
          best_cost[nmask] = joined->cost;
          best[nmask] = std::move(joined);
        }
      }
    }
    auto plan = std::move(best[(1 << n) - 1]);
    if (!plan) throw AegisError("optimizer failed to produce a join plan");

    bool has_agg = false;
    for (const auto& e : s.select_list) {
      if (ContainsAgg(*e)) has_agg = true;
    }
    if (has_agg || !s.group_by.empty()) {
      auto a = std::make_unique<PhysNode>();
      a->op = PhysOp::HashAgg;
      for (const auto& g : s.group_by) a->groups.push_back(CloneExpr(*g));
      for (const auto& e : s.select_list) {
        if (ContainsAgg(*e)) a->aggs.push_back(CloneExpr(*e));
      }
      a->left = std::move(plan);
      a->rows = std::max(1.0, a->left->rows / 10.0);
      a->cost = a->left->cost + a->left->rows * 0.02;
      Schema out;
      for (auto& g : a->groups) {
        Column c;
        c.name = g->alias.empty() ? (g->name.empty() ? "grp" : g->name) : g->alias;
        c.type = TypeId::Integer;
        out.columns.push_back(c);
      }
      for (auto& ag : a->aggs) {
        Column c;
        c.name = ag->alias.empty() ? AggName(*ag) : ag->alias;
        c.type = (ag->kind == ExprKind::Agg && ag->agg == AggOp::Count) ? TypeId::Integer : TypeId::Double;
        out.columns.push_back(c);
      }
      a->schema = out;
      a->out_names.clear();
      for (auto& c : out.columns) a->out_names.push_back(c.name);
      plan = std::move(a);
    } else {
      auto p = std::make_unique<PhysNode>();
      p->op = PhysOp::Project;
      bool star = s.select_list.size() == 1 && s.select_list[0]->kind == ExprKind::Star;
      if (star) {
        p->schema = plan->schema;
      } else {
        for (const auto& e : s.select_list) {
          p->projects.push_back(CloneExpr(*e));
          Column c;
          c.name = e->alias.empty() ? (e->name.empty() ? "col" : e->name) : e->alias;
          c.type = TypeId::Integer;
          p->schema.columns.push_back(c);
        }
      }
      p->rows = plan->rows;
      p->cost = plan->cost + plan->rows * 0.002;
      p->left = std::move(plan);
      plan = std::move(p);
    }

    if (!s.order_by.empty()) {
      auto o = std::make_unique<PhysNode>();
      o->op = PhysOp::Sort;
      for (const auto& ob : s.order_by) {
        OrderBy x;
        x.expr = CloneExpr(*ob.expr);
        x.desc = ob.desc;
        o->order.push_back(std::move(x));
      }
      o->schema = plan->schema;
      o->rows = plan->rows;
      o->cost = plan->cost + plan->rows * std::log2(std::max(2.0, plan->rows)) * 0.01;
      o->left = std::move(plan);
      plan = std::move(o);
    }
    if (s.limit >= 0) {
      auto l = std::make_unique<PhysNode>();
      l->op = PhysOp::Limit;
      l->limit = s.limit;
      l->schema = plan->schema;
      l->rows = std::min(plan->rows, static_cast<double>(s.limit));
      l->cost = plan->cost;
      l->left = std::move(plan);
      plan = std::move(l);
    }
    return plan;
  }

  static std::string Explain(const PhysNode& n, int indent = 0) {
    std::ostringstream os;
    os << std::string(static_cast<std::size_t>(indent), ' ') << OpName(n.op);
    os << "  cost=" << n.cost << " rows=" << n.rows;
    if (!n.table.empty()) os << " table=" << n.table;
    if (!n.index.empty()) os << " index=" << n.index;
    if (n.left_join_col >= 0) os << " join=(" << n.left_join_col << "=" << n.right_join_col << ")";
    os << "\n";
    if (n.left) os << Explain(*n.left, indent + 2);
    if (n.right) os << Explain(*n.right, indent + 2);
    return os.str();
  }

 private:
  static const char* OpName(PhysOp o) {
    switch (o) {
      case PhysOp::SeqScan:
        return "SeqScan";
      case PhysOp::IndexScan:
        return "IndexScan";
      case PhysOp::Filter:
        return "Filter";
      case PhysOp::Project:
        return "Project";
      case PhysOp::HashJoin:
        return "HashJoin";
      case PhysOp::MergeJoin:
        return "SortMergeJoin";
      case PhysOp::NestedLoop:
        return "NestedLoopJoin";
      case PhysOp::HashAgg:
        return "HashAggregate";
      case PhysOp::Sort:
        return "ExternalSort";
      case PhysOp::Limit:
        return "Limit";
    }
    return "?";
  }

  static bool ContainsAgg(const Expr& e) {
    if (e.kind == ExprKind::Agg) return true;
    if (e.lhs && ContainsAgg(*e.lhs)) return true;
    if (e.rhs && ContainsAgg(*e.rhs)) return true;
    if (e.arg && ContainsAgg(*e.arg)) return true;
    return false;
  }

  static std::string AggName(const Expr& e) {
    if (e.kind != ExprKind::Agg) return "agg";
    switch (e.agg) {
      case AggOp::Count:
        return "count";
      case AggOp::Sum:
        return "sum";
      case AggOp::Avg:
        return "avg";
      case AggOp::Min:
        return "min";
      case AggOp::Max:
        return "max";
    }
    return "agg";
  }

  static std::unique_ptr<PhysNode> ClonePlan(const PhysNode& n) {
    auto p = std::make_unique<PhysNode>();
    p->op = n.op;
    p->table = n.table;
    p->alias = n.alias;
    p->index = n.index;
    p->index_col = n.index_col;
    p->index_eq = n.index_eq;
    p->index_lo = n.index_lo;
    p->index_hi = n.index_hi;
    p->has_lo = n.has_lo;
    p->has_hi = n.has_hi;
    if (n.pred) p->pred = CloneExpr(*n.pred);
    p->left_join_col = n.left_join_col;
    p->right_join_col = n.right_join_col;
    p->cost = n.cost;
    p->rows = n.rows;
    p->schema = n.schema;
    p->limit = n.limit;
    if (n.left) p->left = ClonePlan(*n.left);
    if (n.right) p->right = ClonePlan(*n.right);
    return p;
  }

  static bool Mentions(const Expr& e, const std::string& alias, const std::string& table) {
    if (e.kind == ExprKind::Column) {
      if (!e.table.empty()) return e.table == alias || e.table == table;
      return true;
    }
    bool ok = false;
    if (e.lhs) ok = ok || Mentions(*e.lhs, alias, table);
    if (e.rhs) ok = ok || Mentions(*e.rhs, alias, table);
    if (e.arg) ok = ok || Mentions(*e.arg, alias, table);
    return ok;
  }

  static int RelOfColumn(const Expr& e, const std::vector<RelSlot>& rels) {
    if (e.kind != ExprKind::Column) return -1;
    if (!e.table.empty()) {
      for (int i = 0; i < static_cast<int>(rels.size()); ++i) {
        if (rels[static_cast<std::size_t>(i)].alias == e.table || rels[static_cast<std::size_t>(i)].table == e.table)
          return i;
      }
    }
    int found = -1;
    for (int i = 0; i < static_cast<int>(rels.size()); ++i) {
      if (rels[static_cast<std::size_t>(i)].info->schema.IndexOf(e.name) >= 0) {
        if (found >= 0) return -1;
        found = i;
      }
    }
    return found;
  }

  void DistributeFilters(const Expr& e, std::vector<RelSlot>& rels) {
    if (e.kind == ExprKind::BinOp && e.op == BinOp::And) {
      DistributeFilters(*e.lhs, rels);
      DistributeFilters(*e.rhs, rels);
      return;
    }
    // Join predicate vs local filter: if both sides are columns of different rels, skip.
    if (e.kind == ExprKind::BinOp && e.lhs && e.rhs && e.lhs->kind == ExprKind::Column &&
        e.rhs->kind == ExprKind::Column) {
      int a = RelOfColumn(*e.lhs, rels);
      int b = RelOfColumn(*e.rhs, rels);
      if (a >= 0 && b >= 0 && a != b) return;
    }
    int owner = -1;
    std::function<void(const Expr&)> walk = [&](const Expr& x) {
      if (x.kind == ExprKind::Column) {
        int r = RelOfColumn(x, rels);
        if (r >= 0) owner = (owner == -1 || owner == r) ? r : -2;
      }
      if (x.lhs) walk(*x.lhs);
      if (x.rhs) walk(*x.rhs);
      if (x.arg) walk(*x.arg);
    };
    walk(e);
    if (owner >= 0) {
      auto& slot = rels[static_cast<std::size_t>(owner)];
      if (!slot.filter) slot.filter = CloneExpr(e);
      else {
        auto n = std::make_unique<Expr>();
        n->kind = ExprKind::BinOp;
        n->op = BinOp::And;
        n->lhs = std::move(slot.filter);
        n->rhs = CloneExpr(e);
        slot.filter = std::move(n);
      }
    }
  }

  const Expr* JoinPred(const Statement& s, const std::vector<RelSlot>& rels, int mask, int newi) {
    for (const auto& j : s.joins) {
      if (j.on && j.on->kind == ExprKind::BinOp) {
        int a = RelOfColumn(*j.on->lhs, rels);
        int b = RelOfColumn(*j.on->rhs, rels);
        if (a < 0 || b < 0) continue;
        bool has_new = (a == newi || b == newi);
        bool has_old = ((mask & (1 << a)) && a != newi) || ((mask & (1 << b)) && b != newi);
        if (has_new && has_old) return j.on.get();
      }
    }
    if (s.where && s.where->kind == ExprKind::BinOp) return FindJoin(s.where.get(), rels, mask, newi);
    return nullptr;
  }

  const Expr* FindJoin(const Expr* e, const std::vector<RelSlot>& rels, int mask, int newi) {
    if (!e) return nullptr;
    if (e->kind == ExprKind::BinOp && e->op == BinOp::And) {
      auto a = FindJoin(e->lhs.get(), rels, mask, newi);
      if (a) return a;
      return FindJoin(e->rhs.get(), rels, mask, newi);
    }
    if (e->kind == ExprKind::BinOp && e->op == BinOp::Eq && e->lhs && e->rhs) {
      int a = RelOfColumn(*e->lhs, rels);
      int b = RelOfColumn(*e->rhs, rels);
      if (a < 0 || b < 0) return nullptr;
      bool has_new = (a == newi || b == newi);
      bool has_old = ((mask & (1 << a)) && a != newi) || ((mask & (1 << b)) && b != newi);
      if (has_new && has_old) return e;
    }
    return nullptr;
  }

  Catalog* cat_;
};

}  // namespace aegisdb
