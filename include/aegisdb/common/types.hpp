#pragma once

#include "aegisdb/common/config.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace aegisdb {

enum class TypeId : std::uint8_t {
  Integer = 1,
  Double = 2,
  Varchar = 3,
  Boolean = 4,
};

inline const char* TypeName(TypeId t) {
  switch (t) {
    case TypeId::Integer:
      return "INTEGER";
    case TypeId::Double:
      return "DOUBLE";
    case TypeId::Varchar:
      return "VARCHAR";
    case TypeId::Boolean:
      return "BOOLEAN";
  }
  return "UNKNOWN";
}

inline TypeId TypeFromName(std::string name) {
  for (char& c : name) {
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
  }
  if (name == "INT" || name == "INTEGER" || name == "BIGINT") return TypeId::Integer;
  if (name == "DOUBLE" || name == "FLOAT" || name == "REAL") return TypeId::Double;
  if (name.rfind("VARCHAR", 0) == 0 || name.rfind("CHAR", 0) == 0 || name == "TEXT" ||
      name == "DATE") {
    return TypeId::Varchar;
  }
  if (name == "BOOL" || name == "BOOLEAN") return TypeId::Boolean;
  throw std::runtime_error("unknown type: " + name);
}

struct Column {
  std::string name;
  TypeId type = TypeId::Integer;
  std::uint16_t varchar_len = 255;
  bool nullable = true;
};

struct Schema {
  std::vector<Column> columns;

  std::size_t ColumnCount() const { return columns.size(); }

  int IndexOf(std::string_view name) const {
    for (int i = 0; i < static_cast<int>(columns.size()); ++i) {
      if (columns[static_cast<std::size_t>(i)].name == name) return i;
    }
    return -1;
  }
};

class Value {
 public:
  TypeId type = TypeId::Integer;
  bool is_null = true;
  std::int64_t i64 = 0;
  double d = 0;
  std::string str;

  static Value Null(TypeId t) {
    Value v;
    v.type = t;
    v.is_null = true;
    return v;
  }
  static Value Integer(std::int64_t x) {
    Value v;
    v.type = TypeId::Integer;
    v.is_null = false;
    v.i64 = x;
    return v;
  }
  static Value Double(double x) {
    Value v;
    v.type = TypeId::Double;
    v.is_null = false;
    v.d = x;
    return v;
  }
  static Value Varchar(std::string s) {
    Value v;
    v.type = TypeId::Varchar;
    v.is_null = false;
    v.str = std::move(s);
    return v;
  }
  static Value Boolean(bool x) {
    Value v;
    v.type = TypeId::Boolean;
    v.is_null = false;
    v.i64 = x ? 1 : 0;
    return v;
  }

  bool AsBool() const { return !is_null && i64 != 0; }

  std::string ToString() const {
    if (is_null) return "NULL";
    switch (type) {
      case TypeId::Integer:
        return std::to_string(i64);
      case TypeId::Double: {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.10g", d);
        return buf;
      }
      case TypeId::Varchar:
        return str;
      case TypeId::Boolean:
        return i64 ? "true" : "false";
    }
    return "?";
  }

  int Compare(const Value& o) const {
    if (is_null && o.is_null) return 0;
    if (is_null) return 1;  // NULL sorts last
    if (o.is_null) return -1;
    if (type == TypeId::Varchar || o.type == TypeId::Varchar) {
      std::string a = (type == TypeId::Varchar) ? str : ToString();
      std::string b = (o.type == TypeId::Varchar) ? o.str : o.ToString();
      if (a < b) return -1;
      if (a > b) return 1;
      return 0;
    }
    if (type == TypeId::Double || o.type == TypeId::Double) {
      double a = (type == TypeId::Double) ? d : static_cast<double>(i64);
      double b = (o.type == TypeId::Double) ? o.d : static_cast<double>(o.i64);
      if (a < b) return -1;
      if (a > b) return 1;
      return 0;
    }
    if (i64 < o.i64) return -1;
    if (i64 > o.i64) return 1;
    return 0;
  }

  bool operator==(const Value& o) const { return Compare(o) == 0 && is_null == o.is_null; }
  bool operator<(const Value& o) const { return Compare(o) < 0; }

  std::size_t Hash() const {
    if (is_null) return 0;
    if (type == TypeId::Varchar) {
      return std::hash<std::string>{}(str);
    }
    if (type == TypeId::Double) {
      return std::hash<double>{}(d);
    }
    return std::hash<std::int64_t>{}(i64);
  }

  // Memcomparable encoding so B+ tree / hash keys can be compared with memcmp.
  std::string EncodeKey() const {
    std::string out;
    out.push_back(static_cast<char>(type));
    out.push_back(is_null ? 1 : 0);
    if (is_null) return out;
    if (type == TypeId::Varchar) {
      std::uint32_t n = static_cast<std::uint32_t>(str.size());
      char buf[4];
      buf[0] = static_cast<char>((n >> 24) & 0xff);
      buf[1] = static_cast<char>((n >> 16) & 0xff);
      buf[2] = static_cast<char>((n >> 8) & 0xff);
      buf[3] = static_cast<char>(n & 0xff);
      out.append(buf, 4);
      out.append(str);
      return out;
    }
    if (type == TypeId::Double) {
      std::uint64_t u;
      static_assert(sizeof(u) == sizeof(d));
      std::memcpy(&u, &d, sizeof(u));
      if (u & (1ULL << 63)) {
        u = ~u;
      } else {
        u ^= (1ULL << 63);
      }
      for (int i = 7; i >= 0; --i) out.push_back(static_cast<char>((u >> (i * 8)) & 0xff));
      return out;
    }
    // signed int64 -> big-endian with sign bit flipped
    std::uint64_t u = static_cast<std::uint64_t>(i64) ^ (1ULL << 63);
    for (int i = 7; i >= 0; --i) out.push_back(static_cast<char>((u >> (i * 8)) & 0xff));
    return out;
  }

  static Value DecodeKey(std::string_view k) {
    if (k.size() < 2) return Null(TypeId::Integer);
    auto type = static_cast<TypeId>(k[0]);
    if (k[1]) return Null(type);
    if (type == TypeId::Varchar) {
      if (k.size() < 6) return Null(type);
      std::uint32_t n = (static_cast<std::uint8_t>(k[2]) << 24) | (static_cast<std::uint8_t>(k[3]) << 16) |
                        (static_cast<std::uint8_t>(k[4]) << 8) | static_cast<std::uint8_t>(k[5]);
      return Varchar(std::string(k.substr(6, n)));
    }
    if (type == TypeId::Double) {
      std::uint64_t u = 0;
      for (int i = 0; i < 8; ++i) u = (u << 8) | static_cast<std::uint8_t>(k[2 + i]);
      if (u & (1ULL << 63)) {
        u ^= (1ULL << 63);
      } else {
        u = ~u;
      }
      double d;
      std::memcpy(&d, &u, sizeof(d));
      return Double(d);
    }
    std::uint64_t u = 0;
    for (int i = 0; i < 8; ++i) u = (u << 8) | static_cast<std::uint8_t>(k[2 + i]);
    u ^= (1ULL << 63);
    auto v = Integer(static_cast<std::int64_t>(u));
    v.type = type;
    return v;
  }
};

struct Tuple {
  std::vector<Value> values;
  std::uint64_t xmin = 0;
  std::uint64_t xmax = 0;
  std::uint32_t prev_page = kInvalidPageId;
  std::uint16_t prev_slot = 0;

  Value& operator[](std::size_t i) { return values[i]; }
  const Value& operator[](std::size_t i) const { return values[i]; }
  std::size_t Size() const { return values.size(); }
};

}  // namespace aegisdb
