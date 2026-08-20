#pragma once

#include "aegisdb/catalog/catalog.hpp"
#include "aegisdb/common/status.hpp"
#include "aegisdb/common/types.hpp"

#include <cctype>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace aegisdb {

enum class TokenKind {
  End,
  Ident,
  Number,
  String,
  Star,
  Comma,
  LParen,
  RParen,
  Eq,
  Ne,
  Lt,
  Gt,
  Le,
  Ge,
  Plus,
  Minus,
  Slash,
  Dot,
  Semicolon,
  KwSelect,
  KwFrom,
  KwWhere,
  KwJoin,
  KwInner,
  KwOn,
  KwGroup,
  KwBy,
  KwOrder,
  KwAsc,
  KwDesc,
  KwLimit,
  KwCreate,
  KwTable,
  KwIndex,
  KwInsert,
  KwInto,
  KwValues,
  KwAnd,
  KwOr,
  KwNot,
  KwNull,
  KwTrue,
  KwFalse,
  KwExplain,
  KwBegin,
  KwCommit,
  KwRollback,
  KwAnalyze,
  KwAs,
  KwCount,
  KwSum,
  KwAvg,
  KwMin,
  KwMax,
  KwDistinct,
  KwHash,
  KwUsing,
  KwBetween,
};

struct Token {
  TokenKind kind;
  std::string text;
};

class Lexer {
 public:
  explicit Lexer(std::string sql) : s_(std::move(sql)) {}

  Token Next() {
    Skip();
    if (i_ >= s_.size()) return {TokenKind::End, ""};
    char c = s_[i_];
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') return IdentOrKw();
    if (std::isdigit(static_cast<unsigned char>(c))) return Number();
    if (c == '\'') return String();
    ++i_;
    switch (c) {
      case '*':
        return {TokenKind::Star, "*"};
      case ',':
        return {TokenKind::Comma, ","};
      case '(':
        return {TokenKind::LParen, "("};
      case ')':
        return {TokenKind::RParen, ")"};
      case '.':
        return {TokenKind::Dot, "."};
      case ';':
        return {TokenKind::Semicolon, ";"};
      case '+':
        return {TokenKind::Plus, "+"};
      case '-':
        return {TokenKind::Minus, "-"};
      case '/':
        return {TokenKind::Slash, "/"};
      case '=':
        return {TokenKind::Eq, "="};
      case '<':
        if (i_ < s_.size() && s_[i_] == '=') {
          ++i_;
          return {TokenKind::Le, "<="};
        }
        if (i_ < s_.size() && s_[i_] == '>') {
          ++i_;
          return {TokenKind::Ne, "<>"};
        }
        return {TokenKind::Lt, "<"};
      case '>':
        if (i_ < s_.size() && s_[i_] == '=') {
          ++i_;
          return {TokenKind::Ge, ">="};
        }
        return {TokenKind::Gt, ">"};
      case '!':
        if (i_ < s_.size() && s_[i_] == '=') {
          ++i_;
          return {TokenKind::Ne, "!="};
        }
        break;
    }
    throw AegisError(std::string("unexpected character: ") + c);
  }

  Token Peek() {
    auto saved = i_;
    auto t = Next();
    i_ = saved;
    return t;
  }

 private:
  void Skip() {
    while (i_ < s_.size()) {
      if (std::isspace(static_cast<unsigned char>(s_[i_]))) {
        ++i_;
        continue;
      }
      if (s_[i_] == '-' && i_ + 1 < s_.size() && s_[i_ + 1] == '-') {
        while (i_ < s_.size() && s_[i_] != '\n') ++i_;
        continue;
      }
      break;
    }
  }

  Token IdentOrKw() {
    std::size_t a = i_;
    while (i_ < s_.size() && (std::isalnum(static_cast<unsigned char>(s_[i_])) || s_[i_] == '_')) ++i_;
    std::string t = s_.substr(a, i_ - a);
    std::string u = t;
    for (char& ch : u) {
      if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
    }
    static const std::pair<const char*, TokenKind> kws[] = {
        {"SELECT", TokenKind::KwSelect},   {"FROM", TokenKind::KwFrom},     {"WHERE", TokenKind::KwWhere},
        {"JOIN", TokenKind::KwJoin},       {"INNER", TokenKind::KwInner},   {"ON", TokenKind::KwOn},
        {"GROUP", TokenKind::KwGroup},     {"BY", TokenKind::KwBy},         {"ORDER", TokenKind::KwOrder},
        {"ASC", TokenKind::KwAsc},         {"DESC", TokenKind::KwDesc},     {"LIMIT", TokenKind::KwLimit},
        {"CREATE", TokenKind::KwCreate},   {"TABLE", TokenKind::KwTable},   {"INDEX", TokenKind::KwIndex},
        {"INSERT", TokenKind::KwInsert},   {"INTO", TokenKind::KwInto},     {"VALUES", TokenKind::KwValues},
        {"AND", TokenKind::KwAnd},         {"OR", TokenKind::KwOr},         {"NOT", TokenKind::KwNot},
        {"NULL", TokenKind::KwNull},       {"TRUE", TokenKind::KwTrue},     {"FALSE", TokenKind::KwFalse},
        {"EXPLAIN", TokenKind::KwExplain}, {"BEGIN", TokenKind::KwBegin},   {"COMMIT", TokenKind::KwCommit},
        {"ROLLBACK", TokenKind::KwRollback}, {"ANALYZE", TokenKind::KwAnalyze}, {"AS", TokenKind::KwAs},
        {"COUNT", TokenKind::KwCount},     {"SUM", TokenKind::KwSum},       {"AVG", TokenKind::KwAvg},
        {"MIN", TokenKind::KwMin},         {"MAX", TokenKind::KwMax},       {"DISTINCT", TokenKind::KwDistinct},
        {"HASH", TokenKind::KwHash},       {"USING", TokenKind::KwUsing},   {"BETWEEN", TokenKind::KwBetween},
    };
    for (auto [kw, k] : kws) {
      if (u == kw) return {k, t};
    }
    return {TokenKind::Ident, t};
  }

  Token Number() {
    std::size_t a = i_;
    while (i_ < s_.size() && (std::isdigit(static_cast<unsigned char>(s_[i_])) || s_[i_] == '.')) ++i_;
    return {TokenKind::Number, s_.substr(a, i_ - a)};
  }

  Token String() {
    ++i_;
    std::string out;
    while (i_ < s_.size() && s_[i_] != '\'') {
      if (s_[i_] == '\\' && i_ + 1 < s_.size()) {
        ++i_;
        out.push_back(s_[i_++]);
      } else {
        out.push_back(s_[i_++]);
      }
    }
    if (i_ >= s_.size()) throw AegisError("unterminated string");
    ++i_;
    return {TokenKind::String, out};
  }

  std::string s_;
  std::size_t i_ = 0;
};

enum class ExprKind { Column, Literal, Star, BinOp, Agg, Alias };
enum class BinOp { Eq, Ne, Lt, Gt, Le, Ge, And, Or, Add, Sub, Mul, Div };
enum class AggOp { Count, Sum, Avg, Min, Max };

struct Expr {
  ExprKind kind = ExprKind::Literal;
  std::string table;
  std::string name;
  Value value;
  BinOp op = BinOp::Eq;
  AggOp agg = AggOp::Count;
  std::unique_ptr<Expr> lhs;
  std::unique_ptr<Expr> rhs;
  std::unique_ptr<Expr> arg;
  std::string alias;
};

struct OrderBy {
  std::unique_ptr<Expr> expr;
  bool desc = false;
};

struct JoinClause {
  std::string table;
  std::string alias;
  std::unique_ptr<Expr> on;
};

enum class StmtKind { Select, CreateTable, CreateIndex, Insert, Begin, Commit, Rollback, Analyze, Explain };

struct ColumnDef {
  std::string name;
  TypeId type;
  std::uint16_t varchar_len = 255;
};

struct Statement {
  StmtKind kind = StmtKind::Select;
  bool explain = false;
  // select
  std::vector<std::unique_ptr<Expr>> select_list;
  std::string from;
  std::string from_alias;
  std::vector<JoinClause> joins;
  std::unique_ptr<Expr> where;
  std::vector<std::unique_ptr<Expr>> group_by;
  std::vector<OrderBy> order_by;
  int limit = -1;
  // ddl
  std::string table;
  std::vector<ColumnDef> columns;
  std::string index_name;
  std::string index_column;
  IndexKind index_kind = IndexKind::BPlus;
  // insert
  std::vector<std::vector<Value>> rows;
};

class Parser {
 public:
  explicit Parser(std::string sql) : lex_(std::move(sql)) { tok_ = lex_.Next(); }

  Statement Parse() {
    Statement s;
    if (tok_.kind == TokenKind::KwExplain) {
      s.explain = true;
      Eat(TokenKind::KwExplain);
    }
    switch (tok_.kind) {
      case TokenKind::KwSelect:
        ParseSelect(s);
        break;
      case TokenKind::KwCreate:
        Eat(TokenKind::KwCreate);
        if (tok_.kind == TokenKind::KwTable) ParseCreateTable(s);
        else if (tok_.kind == TokenKind::KwIndex || tok_.kind == TokenKind::KwHash) ParseCreateIndex(s);
        else throw AegisError("expected TABLE or INDEX");
        break;
      case TokenKind::KwInsert:
        ParseInsert(s);
        break;
      case TokenKind::KwBegin:
        s.kind = StmtKind::Begin;
        Eat(TokenKind::KwBegin);
        break;
      case TokenKind::KwCommit:
        s.kind = StmtKind::Commit;
        Eat(TokenKind::KwCommit);
        break;
      case TokenKind::KwRollback:
        s.kind = StmtKind::Rollback;
        Eat(TokenKind::KwRollback);
        break;
      case TokenKind::KwAnalyze:
        s.kind = StmtKind::Analyze;
        Eat(TokenKind::KwAnalyze);
        if (tok_.kind == TokenKind::Ident) s.table = EatIdent();
        break;
      default:
        throw AegisError("unexpected statement");
    }
    if (tok_.kind == TokenKind::Semicolon) Eat(TokenKind::Semicolon);
    return s;
  }

 private:
  void Eat(TokenKind k) {
    if (tok_.kind != k) throw AegisError("unexpected token '" + tok_.text + "'");
    tok_ = lex_.Next();
  }
  std::string EatIdent() {
    if (tok_.kind != TokenKind::Ident) throw AegisError("expected identifier");
    auto t = tok_.text;
    tok_ = lex_.Next();
    return t;
  }

  void ParseSelect(Statement& s) {
    s.kind = StmtKind::Select;
    Eat(TokenKind::KwSelect);
    if (tok_.kind == TokenKind::KwDistinct) Eat(TokenKind::KwDistinct);
    s.select_list.push_back(ParseSelectItem());
    while (tok_.kind == TokenKind::Comma) {
      Eat(TokenKind::Comma);
      s.select_list.push_back(ParseSelectItem());
    }
    Eat(TokenKind::KwFrom);
    s.from = EatIdent();
    if (tok_.kind == TokenKind::KwAs) Eat(TokenKind::KwAs);
    if (tok_.kind == TokenKind::Ident) s.from_alias = EatIdent();
    while (tok_.kind == TokenKind::KwJoin || tok_.kind == TokenKind::KwInner) {
      if (tok_.kind == TokenKind::KwInner) Eat(TokenKind::KwInner);
      Eat(TokenKind::KwJoin);
      JoinClause j;
      j.table = EatIdent();
      if (tok_.kind == TokenKind::KwAs) Eat(TokenKind::KwAs);
      if (tok_.kind == TokenKind::Ident) j.alias = EatIdent();
      Eat(TokenKind::KwOn);
      j.on = ParseOr();
      s.joins.push_back(std::move(j));
    }
    if (tok_.kind == TokenKind::KwWhere) {
      Eat(TokenKind::KwWhere);
      s.where = ParseOr();
    }
    if (tok_.kind == TokenKind::KwGroup) {
      Eat(TokenKind::KwGroup);
      Eat(TokenKind::KwBy);
      s.group_by.push_back(ParseExpr());
      while (tok_.kind == TokenKind::Comma) {
        Eat(TokenKind::Comma);
        s.group_by.push_back(ParseExpr());
      }
    }
    if (tok_.kind == TokenKind::KwOrder) {
      Eat(TokenKind::KwOrder);
      Eat(TokenKind::KwBy);
      do {
        if (tok_.kind == TokenKind::Comma) Eat(TokenKind::Comma);
        OrderBy o;
        o.expr = ParseExpr();
        if (tok_.kind == TokenKind::KwDesc) {
          o.desc = true;
          Eat(TokenKind::KwDesc);
        } else if (tok_.kind == TokenKind::KwAsc) {
          Eat(TokenKind::KwAsc);
        }
        s.order_by.push_back(std::move(o));
      } while (tok_.kind == TokenKind::Comma);
    }
    if (tok_.kind == TokenKind::KwLimit) {
      Eat(TokenKind::KwLimit);
      s.limit = std::stoi(tok_.text);
      Eat(TokenKind::Number);
    }
  }

  std::unique_ptr<Expr> ParseSelectItem() {
    auto e = ParseExpr();
    if (tok_.kind == TokenKind::KwAs) {
      Eat(TokenKind::KwAs);
      e->alias = EatIdent();
    } else if (tok_.kind == TokenKind::Ident) {
      e->alias = EatIdent();
    }
    return e;
  }

  void ParseCreateTable(Statement& s) {
    s.kind = StmtKind::CreateTable;
    Eat(TokenKind::KwTable);
    s.table = EatIdent();
    Eat(TokenKind::LParen);
    while (true) {
      ColumnDef c;
      c.name = EatIdent();
      std::string ty = tok_.text;
      tok_ = lex_.Next();
      std::uint16_t vlen = 255;
      if (tok_.kind == TokenKind::LParen) {
        Eat(TokenKind::LParen);
        vlen = static_cast<std::uint16_t>(std::stoi(tok_.text));
        Eat(TokenKind::Number);
        Eat(TokenKind::RParen);
      }
      c.type = TypeFromName(ty);
      c.varchar_len = vlen;
      s.columns.push_back(c);
      if (tok_.kind == TokenKind::Comma) {
        Eat(TokenKind::Comma);
        continue;
      }
      break;
    }
    Eat(TokenKind::RParen);
  }

  void ParseCreateIndex(Statement& s) {
    s.kind = StmtKind::CreateIndex;
    if (tok_.kind == TokenKind::KwHash) {
      s.index_kind = IndexKind::Hash;
      Eat(TokenKind::KwHash);
    }
    Eat(TokenKind::KwIndex);
    s.index_name = EatIdent();
    Eat(TokenKind::KwOn);
    s.table = EatIdent();
    Eat(TokenKind::LParen);
    s.index_column = EatIdent();
    Eat(TokenKind::RParen);
    if (tok_.kind == TokenKind::KwUsing) {
      Eat(TokenKind::KwUsing);
      if (tok_.kind == TokenKind::KwHash) {
        s.index_kind = IndexKind::Hash;
        Eat(TokenKind::KwHash);
      } else {
        EatIdent();
      }
    }
  }

  void ParseInsert(Statement& s) {
    s.kind = StmtKind::Insert;
    Eat(TokenKind::KwInsert);
    Eat(TokenKind::KwInto);
    s.table = EatIdent();
    Eat(TokenKind::KwValues);
    while (true) {
      Eat(TokenKind::LParen);
      std::vector<Value> row;
      row.push_back(ParseLiteralValue());
      while (tok_.kind == TokenKind::Comma) {
        Eat(TokenKind::Comma);
        row.push_back(ParseLiteralValue());
      }
      Eat(TokenKind::RParen);
      s.rows.push_back(std::move(row));
      if (tok_.kind == TokenKind::Comma) {
        Eat(TokenKind::Comma);
        continue;
      }
      break;
    }
  }

  Value ParseLiteralValue() {
    if (tok_.kind == TokenKind::KwNull) {
      Eat(TokenKind::KwNull);
      return Value::Null(TypeId::Integer);
    }
    if (tok_.kind == TokenKind::KwTrue) {
      Eat(TokenKind::KwTrue);
      return Value::Boolean(true);
    }
    if (tok_.kind == TokenKind::KwFalse) {
      Eat(TokenKind::KwFalse);
      return Value::Boolean(false);
    }
    if (tok_.kind == TokenKind::String) {
      auto v = Value::Varchar(tok_.text);
      Eat(TokenKind::String);
      return v;
    }
    if (tok_.kind == TokenKind::Minus) {
      Eat(TokenKind::Minus);
      auto t = tok_.text;
      Eat(TokenKind::Number);
      if (t.find('.') != std::string::npos) return Value::Double(-std::stod(t));
      return Value::Integer(-std::stoll(t));
    }
    if (tok_.kind != TokenKind::Number) throw AegisError("expected literal");
    auto t = tok_.text;
    Eat(TokenKind::Number);
    if (t.find('.') != std::string::npos) return Value::Double(std::stod(t));
    return Value::Integer(std::stoll(t));
  }

  std::unique_ptr<Expr> ParseOr() {
    auto e = ParseAnd();
    while (tok_.kind == TokenKind::KwOr) {
      Eat(TokenKind::KwOr);
      auto n = std::make_unique<Expr>();
      n->kind = ExprKind::BinOp;
      n->op = BinOp::Or;
      n->lhs = std::move(e);
      n->rhs = ParseAnd();
      e = std::move(n);
    }
    return e;
  }
  std::unique_ptr<Expr> ParseAnd() {
    auto e = ParseCmp();
    while (tok_.kind == TokenKind::KwAnd) {
      Eat(TokenKind::KwAnd);
      auto n = std::make_unique<Expr>();
      n->kind = ExprKind::BinOp;
      n->op = BinOp::And;
      n->lhs = std::move(e);
      n->rhs = ParseCmp();
      e = std::move(n);
    }
    return e;
  }
  std::unique_ptr<Expr> ParseCmp() {
    auto e = ParseExpr();
    BinOp op;
    bool ok = true;
    switch (tok_.kind) {
      case TokenKind::Eq:
        op = BinOp::Eq;
        break;
      case TokenKind::Ne:
        op = BinOp::Ne;
        break;
      case TokenKind::Lt:
        op = BinOp::Lt;
        break;
      case TokenKind::Gt:
        op = BinOp::Gt;
        break;
      case TokenKind::Le:
        op = BinOp::Le;
        break;
      case TokenKind::Ge:
        op = BinOp::Ge;
        break;
      case TokenKind::KwBetween: {
        Eat(TokenKind::KwBetween);
        auto lo = ParseExpr();
        Eat(TokenKind::KwAnd);
        auto hi = ParseExpr();
        auto ge = std::make_unique<Expr>();
        ge->kind = ExprKind::BinOp;
        ge->op = BinOp::Ge;
        ge->lhs = CloneExpr(*e);
        ge->rhs = std::move(lo);
        auto le = std::make_unique<Expr>();
        le->kind = ExprKind::BinOp;
        le->op = BinOp::Le;
        le->lhs = std::move(e);
        le->rhs = std::move(hi);
        auto n = std::make_unique<Expr>();
        n->kind = ExprKind::BinOp;
        n->op = BinOp::And;
        n->lhs = std::move(ge);
        n->rhs = std::move(le);
        return n;
      }
      default:
        ok = false;
        break;
    }
    if (!ok) return e;
    tok_ = lex_.Next();
    auto n = std::make_unique<Expr>();
    n->kind = ExprKind::BinOp;
    n->op = op;
    n->lhs = std::move(e);
    n->rhs = ParseExpr();
    return n;
  }

  std::unique_ptr<Expr> ParseExpr() { return ParseAdd(); }
  std::unique_ptr<Expr> ParseAdd() {
    auto e = ParseMul();
    while (tok_.kind == TokenKind::Plus || tok_.kind == TokenKind::Minus) {
      BinOp op = tok_.kind == TokenKind::Plus ? BinOp::Add : BinOp::Sub;
      tok_ = lex_.Next();
      auto n = std::make_unique<Expr>();
      n->kind = ExprKind::BinOp;
      n->op = op;
      n->lhs = std::move(e);
      n->rhs = ParseMul();
      e = std::move(n);
    }
    return e;
  }
  std::unique_ptr<Expr> ParseMul() {
    auto e = ParsePrimary();
    while (tok_.kind == TokenKind::Star || tok_.kind == TokenKind::Slash) {
      BinOp op = tok_.kind == TokenKind::Star ? BinOp::Mul : BinOp::Div;
      tok_ = lex_.Next();
      auto n = std::make_unique<Expr>();
      n->kind = ExprKind::BinOp;
      n->op = op;
      n->lhs = std::move(e);
      n->rhs = ParsePrimary();
      e = std::move(n);
    }
    return e;
  }
  std::unique_ptr<Expr> ParsePrimary() {
    if (tok_.kind == TokenKind::Star) {
      Eat(TokenKind::Star);
      auto e = std::make_unique<Expr>();
      e->kind = ExprKind::Star;
      return e;
    }
    if (tok_.kind == TokenKind::KwCount || tok_.kind == TokenKind::KwSum || tok_.kind == TokenKind::KwAvg ||
        tok_.kind == TokenKind::KwMin || tok_.kind == TokenKind::KwMax) {
      auto e = std::make_unique<Expr>();
      e->kind = ExprKind::Agg;
      if (tok_.kind == TokenKind::KwCount) e->agg = AggOp::Count;
      else if (tok_.kind == TokenKind::KwSum) e->agg = AggOp::Sum;
      else if (tok_.kind == TokenKind::KwAvg) e->agg = AggOp::Avg;
      else if (tok_.kind == TokenKind::KwMin) e->agg = AggOp::Min;
      else e->agg = AggOp::Max;
      tok_ = lex_.Next();
      Eat(TokenKind::LParen);
      e->arg = ParseExpr();
      Eat(TokenKind::RParen);
      return e;
    }
    if (tok_.kind == TokenKind::Ident) {
      auto e = std::make_unique<Expr>();
      e->kind = ExprKind::Column;
      e->name = EatIdent();
      if (tok_.kind == TokenKind::Dot) {
        Eat(TokenKind::Dot);
        e->table = e->name;
        e->name = EatIdent();
      }
      return e;
    }
    if (tok_.kind == TokenKind::LParen) {
      Eat(TokenKind::LParen);
      auto e = ParseOr();
      Eat(TokenKind::RParen);
      return e;
    }
    auto e = std::make_unique<Expr>();
    e->kind = ExprKind::Literal;
    e->value = ParseLiteralValue();
    return e;
  }

  static std::unique_ptr<Expr> CloneExpr(const Expr& e) {
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

  Lexer lex_;
  Token tok_;
};

}  // namespace aegisdb
