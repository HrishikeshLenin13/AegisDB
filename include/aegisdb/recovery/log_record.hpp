#pragma once

#include "aegisdb/common/config.hpp"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace aegisdb {

enum class LogType : std::uint16_t {
  Invalid = 0,
  Begin = 1,
  Insert = 2,
  Update = 3,
  Delete = 4,
  Commit = 5,
  Abort = 6,
  Clr = 7,  // compensation log record (ARIES undo)
  Checkpoint = 8,
  NewPage = 9,
};

struct LogRecord {
  std::uint64_t lsn = kInvalidLsn;
  std::uint64_t prev_lsn = kInvalidLsn;
  std::uint64_t txn_id = kInvalidTxnId;
  LogType type = LogType::Invalid;
  std::uint32_t page_id = kInvalidPageId;
  std::uint16_t slot_id = 0;
  std::uint64_t undo_next = kInvalidLsn;  // CLR
  std::vector<std::uint8_t> before;
  std::vector<std::uint8_t> after;

  std::vector<std::uint8_t> Encode() const {
    std::vector<std::uint8_t> buf;
    auto pu16 = [&](std::uint16_t x) {
      buf.push_back(x & 0xff);
      buf.push_back((x >> 8) & 0xff);
    };
    auto pu32 = [&](std::uint32_t x) {
      for (int i = 0; i < 4; ++i) buf.push_back(static_cast<std::uint8_t>((x >> (8 * i)) & 0xff));
    };
    auto pu64 = [&](std::uint64_t x) {
      for (int i = 0; i < 8; ++i) buf.push_back(static_cast<std::uint8_t>((x >> (8 * i)) & 0xff));
    };
    pu64(lsn);
    pu64(prev_lsn);
    pu64(txn_id);
    pu16(static_cast<std::uint16_t>(type));
    pu32(page_id);
    pu16(slot_id);
    pu64(undo_next);
    pu32(static_cast<std::uint32_t>(before.size()));
    pu32(static_cast<std::uint32_t>(after.size()));
    buf.insert(buf.end(), before.begin(), before.end());
    buf.insert(buf.end(), after.begin(), after.end());
    return buf;
  }

  static LogRecord Decode(const std::uint8_t* p, std::size_t n, std::size_t* consumed) {
    auto ru16 = [&](std::size_t o) -> std::uint16_t { return static_cast<std::uint16_t>(p[o] | (p[o + 1] << 8)); };
    auto ru32 = [&](std::size_t o) -> std::uint32_t {
      std::uint32_t x = 0;
      for (int i = 0; i < 4; ++i) x |= static_cast<std::uint32_t>(p[o + i]) << (8 * i);
      return x;
    };
    auto ru64 = [&](std::size_t o) -> std::uint64_t {
      std::uint64_t x = 0;
      for (int i = 0; i < 8; ++i) x |= static_cast<std::uint64_t>(p[o + i]) << (8 * i);
      return x;
    };
    LogRecord r;
    if (n < 48) {
      *consumed = 0;
      return r;
    }
    r.lsn = ru64(0);
    r.prev_lsn = ru64(8);
    r.txn_id = ru64(16);
    r.type = static_cast<LogType>(ru16(24));
    r.page_id = ru32(26);
    r.slot_id = ru16(30);
    r.undo_next = ru64(32);
    std::uint32_t bl = ru32(40);
    std::uint32_t al = ru32(44);
    if (48 + bl + al > n) {
      *consumed = 0;
      return r;
    }
    r.before.assign(p + 48, p + 48 + bl);
    r.after.assign(p + 48 + bl, p + 48 + bl + al);
    *consumed = 48 + bl + al;
    return r;
  }
};

}  // namespace aegisdb
