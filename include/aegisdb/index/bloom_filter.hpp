#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace aegisdb {

// Classic Bloom filter for existence checks before probing an index.
class BloomFilter {
 public:
  BloomFilter() = default;
  BloomFilter(std::size_t expected, double fpr = 0.01) { Reset(expected, fpr); }

  void Reset(std::size_t expected, double fpr = 0.01) {
    if (expected == 0) expected = 64;
    // m = -n ln(p) / (ln 2)^2
    double m = -static_cast<double>(expected) * std::log(fpr) / (0.693147 * 0.693147);
    bits_.assign(static_cast<std::size_t>(m / 8) + 1, 0);
    k_ = static_cast<int>(std::max(1.0, (static_cast<double>(bits_.size() * 8) / expected) * 0.693147));
    n_ = 0;
  }

  void Add(std::string_view key) {
    auto [h1, h2] = Hash(key);
    const std::size_t m = bits_.size() * 8;
    if (m == 0) return;
    for (int i = 0; i < k_; ++i) {
      std::size_t bit = static_cast<std::size_t>((h1 + static_cast<std::uint64_t>(i) * h2) % m);
      bits_[bit / 8] |= static_cast<std::uint8_t>(1u << (bit % 8));
    }
    ++n_;
  }

  bool MaybeContains(std::string_view key) const {
    if (bits_.empty()) return true;
    auto [h1, h2] = Hash(key);
    const std::size_t m = bits_.size() * 8;
    for (int i = 0; i < k_; ++i) {
      std::size_t bit = static_cast<std::size_t>((h1 + static_cast<std::uint64_t>(i) * h2) % m);
      if ((bits_[bit / 8] & (1u << (bit % 8))) == 0) return false;
    }
    return true;
  }

  std::size_t SizeBytes() const { return bits_.size(); }
  std::size_t Count() const { return n_; }

 private:
  static std::pair<std::uint64_t, std::uint64_t> Hash(std::string_view s) {
    // FNV-1a + a second mix. Not cryptographic; fine for a filter.
    std::uint64_t h1 = 14695981039346656037ULL;
    for (unsigned char c : s) {
      h1 ^= c;
      h1 *= 1099511628211ULL;
    }
    std::uint64_t h2 = h1 ^ 0x9E3779B97F4A7C15ULL;
    h2 *= 0xBF58476D1CE4E5B9ULL;
    return {h1, h2 | 1};
  }

  std::vector<std::uint8_t> bits_;
  int k_ = 4;
  std::size_t n_ = 0;
};

}  // namespace aegisdb
