#include "aegisdb/storage/slotted_page.hpp"
#include "aegisdb/index/bloom_filter.hpp"
#include "aegisdb/common/rid.hpp"
#include <cassert>
#include <vector>
#include <string>
using Page=aegisdb::Page;
using P=aegisdb::SlottedPage;
using Bloom=aegisdb::BloomFilter;

int main() {
  { // initialized-pages-have-no-live-tuples
Page p;P::Init(&p,7);assert(P::LiveTupleCount(&p)==0);assert(P::LiveBytes(&p)==0);
  }
}
