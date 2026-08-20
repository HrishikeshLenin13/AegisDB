#pragma once

#include "aegisdb/common/config.hpp"

#include <cstdint>
#include <functional>
#include <string>

namespace aegisdb {

struct RID {
  std::uint32_t page_id = kInvalidPageId;
  std::uint16_t slot_id = 0;

  bool Valid() const { return page_id != kInvalidPageId; }

  bool operator==(const RID& o) const { return page_id == o.page_id && slot_id == o.slot_id; }
  bool operator!=(const RID& o) const { return !(*this == o); }
  bool operator<(const RID& o) const {
    return page_id < o.page_id || (page_id == o.page_id && slot_id < o.slot_id);
  }

  std::string ToString() const {
    return "(" + std::to_string(page_id) + "," + std::to_string(slot_id) + ")";
  }
};

struct RIDHash {
  std::size_t operator()(const RID& r) const {
    return (static_cast<std::size_t>(r.page_id) << 16) ^ r.slot_id;
  }
};

}  // namespace aegisdb
