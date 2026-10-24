#include "aegisdb/index/bloom_filter.hpp"
#include <cassert>
int main() {
  aegisdb::BloomFilter filter(100);
  auto capacity = filter.SizeBytes();
  filter.Add("first");
  assert(filter.MaybeContains("first"));
  filter.Clear();
  assert(filter.Count() == 0 && filter.SizeBytes() == capacity);
  assert(!filter.MaybeContains("first"));
  filter.Add("second");
  assert(filter.Count() == 1 && filter.MaybeContains("second"));
  aegisdb::BloomFilter empty;
  empty.Clear();
  assert(empty.SizeBytes() == 0 && empty.MaybeContains("anything"));
}
