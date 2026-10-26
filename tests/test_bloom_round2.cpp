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
  { aegisdb::BloomFilter left(100),right(100); left.Add("left"); right.Add("right"); left.MergeFrom(right); assert(left.Count()==2 && left.MaybeContains("left") && left.MaybeContains("right")); auto count=left.Count(); bool rejected=false; try { left.MergeFrom(aegisdb::BloomFilter(10)); } catch(const std::invalid_argument&) {rejected=true;} assert(rejected && left.Count()==count); }
  // Bulk and merged insertion paths must have no false negatives.
  for (std::size_t size : {1u, 7u, 64u, 1000u}) {
    aegisdb::BloomFilter bulk(size), scalar(size), left(size), right(size);
    std::vector<std::string> keys;
    for (std::size_t i=0;i<size;++i) keys.push_back("key-"+std::to_string(i));
    bulk.AddRange(keys.begin(),keys.end());
    double previous = 0;
    for (std::size_t i=0;i<size;++i) {
      scalar.Add(keys[i]);
      (i%2 ? right : left).Add(keys[i]);
      assert(scalar.FillRatio()>=previous);previous=scalar.FillRatio();
    }
    left.MergeFrom(right);
    assert(bulk.Count()==size && left.Count()==size);
    assert(bulk.FillRatio()==scalar.FillRatio() && bulk.FillRatio()==left.FillRatio());
    for (const auto& key:keys) assert(bulk.MaybeContains(key) && scalar.MaybeContains(key) && left.MaybeContains(key));
    assert(bulk.FillRatio()<=1 && bulk.EstimatedFalsePositiveRate()<=1);
    auto bytes=bulk.SizeBytes();bulk.Clear();assert(bulk.Empty() && bulk.SizeBytes()==bytes);
  }
  std::cout << "Bloom observability checks passed.\n";
}
