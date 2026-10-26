#include "aegisdb/index/bloom_filter.hpp"
#include <cassert>
#include <iostream>
#include <vector>
#include <string>
int main() {
  { aegisdb::BloomFilter filter(100); assert(filter.Empty()); assert(filter.BitCount() == filter.SizeBytes()*8); assert(filter.HashCount() > 0); filter.Add("a"); assert(!filter.Empty()); }
  // CASES
  std::cout << "Bloom observability checks passed.\n";
}
