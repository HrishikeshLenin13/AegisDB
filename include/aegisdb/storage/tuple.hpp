#pragma once

#include "aegisdb/common/config.hpp"
#include "aegisdb/common/types.hpp"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace aegisdb {

// On-disk tuple:
//   xmin u64 | xmax u64 | prev_page u32 | prev_slot u16 | n_cols u16 | flags u16
//   null bitmap ceil(n/8)
//   for each non-null col: INTEGER i64 | DOUBLE f64 | BOOL u8 | VARCHAR u16+bytes
inline constexpr std::uint16_t kTupleFlagOverflow = 1;

inline std::vector<std::uint8_t> SerializeTuple(const Tuple& t) {
  std::vector<std::uint8_t> buf;
  auto push_u8 = [&](std::uint8_t x) { buf.push_back(x); };
  auto push_u16 = [&](std::uint16_t x) {
    buf.push_back(static_cast<std::uint8_t>(x & 0xff));
    buf.push_back(static_cast<std::uint8_t>((x >> 8) & 0xff));
  };
  auto push_u32 = [&](std::uint32_t x) {
    for (int i = 0; i < 4; ++i) buf.push_back(static_cast<std::uint8_t>((x >> (8 * i)) & 0xff));
  };
  auto push_u64 = [&](std::uint64_t x) {
    for (int i = 0; i < 8; ++i) buf.push_back(static_cast<std::uint8_t>((x >> (8 * i)) & 0xff));
  };
  push_u64(t.xmin);
  push_u64(t.xmax);
  push_u32(t.prev_page);
  push_u16(t.prev_slot);
  push_u16(static_cast<std::uint16_t>(t.values.size()));
  push_u16(0);  // flags
  const std::size_t n = t.values.size();
  const std::size_t nb = (n + 7) / 8;
  const std::size_t bitmap_off = buf.size();
  buf.resize(buf.size() + nb, 0);
  for (std::size_t i = 0; i < n; ++i) {
    if (t.values[i].is_null) {
      buf[bitmap_off + i / 8] |= static_cast<std::uint8_t>(1u << (i % 8));
      continue;
    }
    const Value& v = t.values[i];
    switch (v.type) {
      case TypeId::Integer:
        push_u64(static_cast<std::uint64_t>(v.i64));
        break;
      case TypeId::Double: {
        std::uint64_t u;
        std::memcpy(&u, &v.d, 8);
        push_u64(u);
        break;
      }
      case TypeId::Boolean:
        push_u8(v.i64 ? 1 : 0);
        break;
      case TypeId::Varchar: {
        if (v.str.size() > kMaxVarchar) {
          throw std::runtime_error("varchar exceeds max length");
        }
        push_u16(static_cast<std::uint16_t>(v.str.size()));
        buf.insert(buf.end(), v.str.begin(), v.str.end());
        break;
      }
    }
  }
  return buf;
}

inline Tuple DeserializeTuple(const std::uint8_t* p, std::size_t n, const Schema* schema = nullptr) {
  auto ru16 = [&](std::size_t off) -> std::uint16_t {
    return static_cast<std::uint16_t>(p[off] | (p[off + 1] << 8));
  };
  auto ru32 = [&](std::size_t off) -> std::uint32_t {
    std::uint32_t x = 0;
    for (int i = 0; i < 4; ++i) x |= static_cast<std::uint32_t>(p[off + i]) << (8 * i);
    return x;
  };
  auto ru64 = [&](std::size_t off) -> std::uint64_t {
    std::uint64_t x = 0;
    for (int i = 0; i < 8; ++i) x |= static_cast<std::uint64_t>(p[off + i]) << (8 * i);
    return x;
  };
  if (n < 26) throw std::runtime_error("tuple too small");
  Tuple t;
  t.xmin = ru64(0);
  t.xmax = ru64(8);
  t.prev_page = ru32(16);
  t.prev_slot = ru16(20);
  std::uint16_t ncols = ru16(22);
  std::size_t off = 26;
  const std::size_t nb = (ncols + 7) / 8;
  if (off + nb > n) throw std::runtime_error("tuple bitmap truncated");
  const std::uint8_t* bm = p + off;
  off += nb;
  t.values.resize(ncols);
  for (std::uint16_t i = 0; i < ncols; ++i) {
    TypeId ty = TypeId::Integer;
    if (schema && i < schema->columns.size()) ty = schema->columns[i].type;
    t.values[i].type = ty;
    if (bm[i / 8] & (1u << (i % 8))) {
      t.values[i].is_null = true;
      continue;
    }
    t.values[i].is_null = false;
    if (ty == TypeId::Varchar) {
      std::uint16_t len = ru16(off);
      off += 2;
      t.values[i].str.assign(reinterpret_cast<const char*>(p + off), len);
      off += len;
    } else if (ty == TypeId::Double) {
      std::uint64_t u = ru64(off);
      off += 8;
      std::memcpy(&t.values[i].d, &u, 8);
    } else if (ty == TypeId::Boolean) {
      t.values[i].i64 = p[off++];
    } else {
      t.values[i].i64 = static_cast<std::int64_t>(ru64(off));
      off += 8;
    }
  }
  return t;
}

}  // namespace aegisdb
