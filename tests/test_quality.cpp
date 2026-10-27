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
  { // slotted-lifecycle-payload-0
Page p;P::Init(&p,77,11,88);std::vector<std::uint8_t> value(0,7),other(1,9);auto a=P::Insert(&p,value.data(),0);auto b=P::Insert(&p,other.data(),1);assert(a&&b);assert(P::Read(&p,*a).value()==value);assert(P::Update(&p,*a,other.data(),1));assert(P::Read(&p,*a).value()==other);assert(P::Delete(&p,*b));P::Compact(&p);assert(P::Read(&p,*a).value()==other);assert(!P::Read(&p,*b));assert(P::LiveTupleCount(&p)==1);assert(P::LiveBytes(&p)==1);assert(P::ReclaimableBytes(&p)==0);assert(p.Header()->prev_page==11&&p.Header()->next_page==88);
  }
  { // slotted-lifecycle-payload-1
Page p;P::Init(&p,77,11,88);std::vector<std::uint8_t> value(1,7),other(2,9);auto a=P::Insert(&p,value.data(),1);auto b=P::Insert(&p,other.data(),2);assert(a&&b);assert(P::Read(&p,*a).value()==value);assert(P::Update(&p,*a,other.data(),2));assert(P::Read(&p,*a).value()==other);assert(P::Delete(&p,*b));P::Compact(&p);assert(P::Read(&p,*a).value()==other);assert(!P::Read(&p,*b));assert(P::LiveTupleCount(&p)==1);assert(P::LiveBytes(&p)==2);assert(P::ReclaimableBytes(&p)==0);assert(p.Header()->prev_page==11&&p.Header()->next_page==88);
  }
}
