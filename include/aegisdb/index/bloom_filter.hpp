#pragma once

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aegisdb {

// Classic Bloom filter for existence checks before probing an index.
class BloomFilter {
 public:
  BloomFilter() = default;
  BloomFilter(std::size_t expected, double fpr = 0.01) { Reset(expected, fpr); }

  void Reset(std::size_t expected, double fpr = 0.01) {
    // Validate before changing an existing filter or converting log results
    // to allocation sizes. NaN/infinity and endpoints are not probabilities.
    if (!std::isfinite(fpr) || fpr <= 0.0 || fpr >= 1.0) {
      throw std::invalid_argument("Bloom filter false-positive rate must be finite and between 0 and 1");
    }
    if (expected == 0) expected = 64;
    // m = -n ln(p) / (ln 2)^2
    double m = -static_cast<double>(expected) * std::log(fpr) / (0.693147 * 0.693147);
    // Bound bits as well as bytes: Add/MaybeContains multiply the size by 8.
    const auto max_bytes = std::min(bits_.max_size(), std::numeric_limits<std::size_t>::max() / 8);
    if (!std::isfinite(m) || m / 8 >= static_cast<double>(max_bytes)) {
      throw std::length_error("Bloom filter capacity is too large");
    }
    std::vector<std::uint8_t> next(static_cast<std::size_t>(m / 8) + 1, 0);
    const int next_k = static_cast<int>(std::max(1.0, (static_cast<double>(next.size() * 8) / expected) * 0.693147));
    bits_.swap(next);
    k_ = next_k;
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

  template <typename InputIterator>
  void AddRange(InputIterator first, InputIterator last) {
    for (; first != last; ++first) Add(*first);
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

  // Reuse the current capacity and hash count without reallocating.
  void Clear() {
    std::fill(bits_.begin(), bits_.end(), 0);
    n_ = 0;
  }

  // Model estimate based on insertion count, not a measurement of unique keys.
  double EstimatedFalsePositiveRate() const {
    if (bits_.empty()) return 1.0;
    const double occupancy = -std::expm1(-static_cast<double>(k_) * static_cast<double>(n_) / static_cast<double>(BitCount()));
    return std::clamp(std::pow(occupancy, k_), 0.0, 1.0);
  }

  double FillRatio() const {
    if (bits_.empty()) return 0.0;
    std::size_t set = 0;
    for (auto byte : bits_) set += std::popcount(static_cast<unsigned int>(byte));
    return static_cast<double>(set) / static_cast<double>(BitCount());
  }

  bool Empty() const { return n_ == 0; }
  std::size_t BitCount() const { return bits_.size() * 8; }
  int HashCount() const { return k_; }

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
