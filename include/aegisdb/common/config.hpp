#pragma once

#include <cstddef>
#include <cstdint>

namespace aegisdb {

inline constexpr std::uint32_t kPageSize = 4096;
inline constexpr std::uint32_t kInvalidPageId = static_cast<std::uint32_t>(-1);
inline constexpr std::uint64_t kInvalidLsn = 0;
inline constexpr std::uint64_t kInvalidTxnId = 0;
inline constexpr std::uint32_t kDefaultPoolPages = 1024;
inline constexpr std::uint32_t kLogBufferPages = 64;
inline constexpr std::uint16_t kMaxVarchar = 4096;
inline constexpr std::uint32_t kMagic = 0x41454753;  // 'AEGS'
inline constexpr std::uint32_t kFileVersion = 1;
inline constexpr std::size_t kMaxTupleSize = kPageSize - 128;

}  // namespace aegisdb
