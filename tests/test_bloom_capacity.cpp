#include "aegisdb/index/bloom_filter.hpp"
#include <cassert>
#include <limits>
int main() {
  aegisdb::BloomFilter filter(10);
  filter.Add("kept");
  auto bytes = filter.SizeBytes();
  bool rejected = false;
  try { filter.Reset(std::numeric_limits<std::size_t>::max(), 1e-200); }
  catch (const std::length_error&) { rejected = true; }
  assert(rejected && filter.Count() == 1 && filter.SizeBytes() == bytes);
  assert(filter.MaybeContains("kept"));
  filter.Reset(20);
  assert(filter.Count() == 0 && !filter.MaybeContains("kept"));
}
