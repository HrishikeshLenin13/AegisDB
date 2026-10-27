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
  { // slotted-lifecycle-payload-2
Page p;P::Init(&p,77,11,88);std::vector<std::uint8_t> value(2,7),other(3,9);auto a=P::Insert(&p,value.data(),2);auto b=P::Insert(&p,other.data(),3);assert(a&&b);assert(P::Read(&p,*a).value()==value);assert(P::Update(&p,*a,other.data(),3));assert(P::Read(&p,*a).value()==other);assert(P::Delete(&p,*b));P::Compact(&p);assert(P::Read(&p,*a).value()==other);assert(!P::Read(&p,*b));assert(P::LiveTupleCount(&p)==1);assert(P::LiveBytes(&p)==3);assert(P::ReclaimableBytes(&p)==0);assert(p.Header()->prev_page==11&&p.Header()->next_page==88);
  }
  { // slotted-lifecycle-payload-3
Page p;P::Init(&p,77,11,88);std::vector<std::uint8_t> value(3,7),other(4,9);auto a=P::Insert(&p,value.data(),3);auto b=P::Insert(&p,other.data(),4);assert(a&&b);assert(P::Read(&p,*a).value()==value);assert(P::Update(&p,*a,other.data(),4));assert(P::Read(&p,*a).value()==other);assert(P::Delete(&p,*b));P::Compact(&p);assert(P::Read(&p,*a).value()==other);assert(!P::Read(&p,*b));assert(P::LiveTupleCount(&p)==1);assert(P::LiveBytes(&p)==4);assert(P::ReclaimableBytes(&p)==0);assert(p.Header()->prev_page==11&&p.Header()->next_page==88);
  }
  { // slotted-lifecycle-payload-7
Page p;P::Init(&p,77,11,88);std::vector<std::uint8_t> value(7,7),other(8,9);auto a=P::Insert(&p,value.data(),7);auto b=P::Insert(&p,other.data(),8);assert(a&&b);assert(P::Read(&p,*a).value()==value);assert(P::Update(&p,*a,other.data(),8));assert(P::Read(&p,*a).value()==other);assert(P::Delete(&p,*b));P::Compact(&p);assert(P::Read(&p,*a).value()==other);assert(!P::Read(&p,*b));assert(P::LiveTupleCount(&p)==1);assert(P::LiveBytes(&p)==8);assert(P::ReclaimableBytes(&p)==0);assert(p.Header()->prev_page==11&&p.Header()->next_page==88);
  }
  { // slotted-lifecycle-payload-8
Page p;P::Init(&p,77,11,88);std::vector<std::uint8_t> value(8,7),other(9,9);auto a=P::Insert(&p,value.data(),8);auto b=P::Insert(&p,other.data(),9);assert(a&&b);assert(P::Read(&p,*a).value()==value);assert(P::Update(&p,*a,other.data(),9));assert(P::Read(&p,*a).value()==other);assert(P::Delete(&p,*b));P::Compact(&p);assert(P::Read(&p,*a).value()==other);assert(!P::Read(&p,*b));assert(P::LiveTupleCount(&p)==1);assert(P::LiveBytes(&p)==9);assert(P::ReclaimableBytes(&p)==0);assert(p.Header()->prev_page==11&&p.Header()->next_page==88);
  }
}
