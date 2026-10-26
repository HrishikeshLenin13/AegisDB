#include "aegisdb/index/bloom_filter.hpp"
#include <cassert>
#include <iostream>
#include <vector>
#include <string>
int main() {
  { aegisdb::BloomFilter filter(100); assert(filter.Empty()); assert(filter.BitCount() == filter.SizeBytes()*8); assert(filter.HashCount() > 0); filter.Add("a"); assert(!filter.Empty()); }
  { aegisdb::BloomFilter filter(100); assert(filter.EstimatedFalsePositiveRate()==0); for(int i=0;i<100;i++) filter.Add(std::to_string(i)); assert(filter.EstimatedFalsePositiveRate()>0 && filter.EstimatedFalsePositiveRate()<0.05); aegisdb::BloomFilter unknown; assert(unknown.EstimatedFalsePositiveRate()==1); }
  { aegisdb::BloomFilter filter(100); assert(filter.FillRatio()==0); filter.Add("one"); auto before=filter.FillRatio(); assert(before>0 && before<=1); filter.Add("one"); assert(filter.FillRatio()==before); filter.Clear(); assert(filter.FillRatio()==0); }
  { aegisdb::BloomFilter filter(100); std::vector<std::string> keys={"one","two","three"}; filter.AddRange(keys.begin(),keys.end()); assert(filter.Count()==3); for(auto& key:keys) assert(filter.MaybeContains(key)); }
  // CASES
  std::cout << "Bloom observability checks passed.\n";
}
