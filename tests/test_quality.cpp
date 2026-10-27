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
  { // slotted-lifecycle-payload-15
Page p;P::Init(&p,77,11,88);std::vector<std::uint8_t> value(15,7),other(16,9);auto a=P::Insert(&p,value.data(),15);auto b=P::Insert(&p,other.data(),16);assert(a&&b);assert(P::Read(&p,*a).value()==value);assert(P::Update(&p,*a,other.data(),16));assert(P::Read(&p,*a).value()==other);assert(P::Delete(&p,*b));P::Compact(&p);assert(P::Read(&p,*a).value()==other);assert(!P::Read(&p,*b));assert(P::LiveTupleCount(&p)==1);assert(P::LiveBytes(&p)==16);assert(P::ReclaimableBytes(&p)==0);assert(p.Header()->prev_page==11&&p.Header()->next_page==88);
  }
  { // slotted-lifecycle-payload-16
Page p;P::Init(&p,77,11,88);std::vector<std::uint8_t> value(16,7),other(17,9);auto a=P::Insert(&p,value.data(),16);auto b=P::Insert(&p,other.data(),17);assert(a&&b);assert(P::Read(&p,*a).value()==value);assert(P::Update(&p,*a,other.data(),17));assert(P::Read(&p,*a).value()==other);assert(P::Delete(&p,*b));P::Compact(&p);assert(P::Read(&p,*a).value()==other);assert(!P::Read(&p,*b));assert(P::LiveTupleCount(&p)==1);assert(P::LiveBytes(&p)==17);assert(P::ReclaimableBytes(&p)==0);assert(p.Header()->prev_page==11&&p.Header()->next_page==88);
  }
  { // slotted-lifecycle-payload-31
Page p;P::Init(&p,77,11,88);std::vector<std::uint8_t> value(31,7),other(32,9);auto a=P::Insert(&p,value.data(),31);auto b=P::Insert(&p,other.data(),32);assert(a&&b);assert(P::Read(&p,*a).value()==value);assert(P::Update(&p,*a,other.data(),32));assert(P::Read(&p,*a).value()==other);assert(P::Delete(&p,*b));P::Compact(&p);assert(P::Read(&p,*a).value()==other);assert(!P::Read(&p,*b));assert(P::LiveTupleCount(&p)==1);assert(P::LiveBytes(&p)==32);assert(P::ReclaimableBytes(&p)==0);assert(p.Header()->prev_page==11&&p.Header()->next_page==88);
  }
  { // slotted-lifecycle-payload-32
Page p;P::Init(&p,77,11,88);std::vector<std::uint8_t> value(32,7),other(33,9);auto a=P::Insert(&p,value.data(),32);auto b=P::Insert(&p,other.data(),33);assert(a&&b);assert(P::Read(&p,*a).value()==value);assert(P::Update(&p,*a,other.data(),33));assert(P::Read(&p,*a).value()==other);assert(P::Delete(&p,*b));P::Compact(&p);assert(P::Read(&p,*a).value()==other);assert(!P::Read(&p,*b));assert(P::LiveTupleCount(&p)==1);assert(P::LiveBytes(&p)==33);assert(P::ReclaimableBytes(&p)==0);assert(p.Header()->prev_page==11&&p.Header()->next_page==88);
  }
  { // slotted-lifecycle-payload-63
Page p;P::Init(&p,77,11,88);std::vector<std::uint8_t> value(63,7),other(64,9);auto a=P::Insert(&p,value.data(),63);auto b=P::Insert(&p,other.data(),64);assert(a&&b);assert(P::Read(&p,*a).value()==value);assert(P::Update(&p,*a,other.data(),64));assert(P::Read(&p,*a).value()==other);assert(P::Delete(&p,*b));P::Compact(&p);assert(P::Read(&p,*a).value()==other);assert(!P::Read(&p,*b));assert(P::LiveTupleCount(&p)==1);assert(P::LiveBytes(&p)==64);assert(P::ReclaimableBytes(&p)==0);assert(p.Header()->prev_page==11&&p.Header()->next_page==88);
  }
  { // slotted-lifecycle-payload-64
Page p;P::Init(&p,77,11,88);std::vector<std::uint8_t> value(64,7),other(65,9);auto a=P::Insert(&p,value.data(),64);auto b=P::Insert(&p,other.data(),65);assert(a&&b);assert(P::Read(&p,*a).value()==value);assert(P::Update(&p,*a,other.data(),65));assert(P::Read(&p,*a).value()==other);assert(P::Delete(&p,*b));P::Compact(&p);assert(P::Read(&p,*a).value()==other);assert(!P::Read(&p,*b));assert(P::LiveTupleCount(&p)==1);assert(P::LiveBytes(&p)==65);assert(P::ReclaimableBytes(&p)==0);assert(p.Header()->prev_page==11&&p.Header()->next_page==88);
  }
  { // slotted-lifecycle-payload-127
Page p;P::Init(&p,77,11,88);std::vector<std::uint8_t> value(127,7),other(128,9);auto a=P::Insert(&p,value.data(),127);auto b=P::Insert(&p,other.data(),128);assert(a&&b);assert(P::Read(&p,*a).value()==value);assert(P::Update(&p,*a,other.data(),128));assert(P::Read(&p,*a).value()==other);assert(P::Delete(&p,*b));P::Compact(&p);assert(P::Read(&p,*a).value()==other);assert(!P::Read(&p,*b));assert(P::LiveTupleCount(&p)==1);assert(P::LiveBytes(&p)==128);assert(P::ReclaimableBytes(&p)==0);assert(p.Header()->prev_page==11&&p.Header()->next_page==88);
  }
  { // slotted-lifecycle-payload-255
Page p;P::Init(&p,77,11,88);std::vector<std::uint8_t> value(255,7),other(256,9);auto a=P::Insert(&p,value.data(),255);auto b=P::Insert(&p,other.data(),256);assert(a&&b);assert(P::Read(&p,*a).value()==value);assert(P::Update(&p,*a,other.data(),256));assert(P::Read(&p,*a).value()==other);assert(P::Delete(&p,*b));P::Compact(&p);assert(P::Read(&p,*a).value()==other);assert(!P::Read(&p,*b));assert(P::LiveTupleCount(&p)==1);assert(P::LiveBytes(&p)==256);assert(P::ReclaimableBytes(&p)==0);assert(p.Header()->prev_page==11&&p.Header()->next_page==88);
  }
  { // slotted-lifecycle-payload-512
Page p;P::Init(&p,77,11,88);std::vector<std::uint8_t> value(512,7),other(513,9);auto a=P::Insert(&p,value.data(),512);auto b=P::Insert(&p,other.data(),513);assert(a&&b);assert(P::Read(&p,*a).value()==value);assert(P::Update(&p,*a,other.data(),513));assert(P::Read(&p,*a).value()==other);assert(P::Delete(&p,*b));P::Compact(&p);assert(P::Read(&p,*a).value()==other);assert(!P::Read(&p,*b));assert(P::LiveTupleCount(&p)==1);assert(P::LiveBytes(&p)==513);assert(P::ReclaimableBytes(&p)==0);assert(p.Header()->prev_page==11&&p.Header()->next_page==88);
  }
  { // readinto-guarded-capacity-1
Page p;P::Init(&p,1);std::vector<std::uint8_t> source(1,42);auto id=P::Insert(&p,source.data(),1);std::vector<std::uint8_t> target(3,99);assert(!P::ReadInto(&p,*id,std::span<std::uint8_t>(target.data(),0)));for(auto b:target)assert(b==99);assert(P::ReadInto(&p,*id,std::span<std::uint8_t>(target.data(),2)).value()==1);for(std::size_t i=0;i<1;i++)assert(target[i]==42);assert(target[1]==99&&target[2]==99);
  }
  { // readinto-guarded-capacity-2
Page p;P::Init(&p,1);std::vector<std::uint8_t> source(2,42);auto id=P::Insert(&p,source.data(),2);std::vector<std::uint8_t> target(4,99);assert(!P::ReadInto(&p,*id,std::span<std::uint8_t>(target.data(),1)));for(auto b:target)assert(b==99);assert(P::ReadInto(&p,*id,std::span<std::uint8_t>(target.data(),3)).value()==2);for(std::size_t i=0;i<2;i++)assert(target[i]==42);assert(target[2]==99&&target[3]==99);
  }
  { // readinto-guarded-capacity-3
Page p;P::Init(&p,1);std::vector<std::uint8_t> source(3,42);auto id=P::Insert(&p,source.data(),3);std::vector<std::uint8_t> target(5,99);assert(!P::ReadInto(&p,*id,std::span<std::uint8_t>(target.data(),2)));for(auto b:target)assert(b==99);assert(P::ReadInto(&p,*id,std::span<std::uint8_t>(target.data(),4)).value()==3);for(std::size_t i=0;i<3;i++)assert(target[i]==42);assert(target[3]==99&&target[4]==99);
  }
  { // readinto-guarded-capacity-7
Page p;P::Init(&p,1);std::vector<std::uint8_t> source(7,42);auto id=P::Insert(&p,source.data(),7);std::vector<std::uint8_t> target(9,99);assert(!P::ReadInto(&p,*id,std::span<std::uint8_t>(target.data(),6)));for(auto b:target)assert(b==99);assert(P::ReadInto(&p,*id,std::span<std::uint8_t>(target.data(),8)).value()==7);for(std::size_t i=0;i<7;i++)assert(target[i]==42);assert(target[7]==99&&target[8]==99);
  }
  { // readinto-guarded-capacity-8
Page p;P::Init(&p,1);std::vector<std::uint8_t> source(8,42);auto id=P::Insert(&p,source.data(),8);std::vector<std::uint8_t> target(10,99);assert(!P::ReadInto(&p,*id,std::span<std::uint8_t>(target.data(),7)));for(auto b:target)assert(b==99);assert(P::ReadInto(&p,*id,std::span<std::uint8_t>(target.data(),9)).value()==8);for(std::size_t i=0;i<8;i++)assert(target[i]==42);assert(target[8]==99&&target[9]==99);
  }
  { // readinto-guarded-capacity-15
Page p;P::Init(&p,1);std::vector<std::uint8_t> source(15,42);auto id=P::Insert(&p,source.data(),15);std::vector<std::uint8_t> target(17,99);assert(!P::ReadInto(&p,*id,std::span<std::uint8_t>(target.data(),14)));for(auto b:target)assert(b==99);assert(P::ReadInto(&p,*id,std::span<std::uint8_t>(target.data(),16)).value()==15);for(std::size_t i=0;i<15;i++)assert(target[i]==42);assert(target[15]==99&&target[16]==99);
  }
  { // readinto-guarded-capacity-16
Page p;P::Init(&p,1);std::vector<std::uint8_t> source(16,42);auto id=P::Insert(&p,source.data(),16);std::vector<std::uint8_t> target(18,99);assert(!P::ReadInto(&p,*id,std::span<std::uint8_t>(target.data(),15)));for(auto b:target)assert(b==99);assert(P::ReadInto(&p,*id,std::span<std::uint8_t>(target.data(),17)).value()==16);for(std::size_t i=0;i<16;i++)assert(target[i]==42);assert(target[16]==99&&target[17]==99);
  }
  { // readinto-guarded-capacity-31
Page p;P::Init(&p,1);std::vector<std::uint8_t> source(31,42);auto id=P::Insert(&p,source.data(),31);std::vector<std::uint8_t> target(33,99);assert(!P::ReadInto(&p,*id,std::span<std::uint8_t>(target.data(),30)));for(auto b:target)assert(b==99);assert(P::ReadInto(&p,*id,std::span<std::uint8_t>(target.data(),32)).value()==31);for(std::size_t i=0;i<31;i++)assert(target[i]==42);assert(target[31]==99&&target[32]==99);
  }
  { // readinto-guarded-capacity-32
Page p;P::Init(&p,1);std::vector<std::uint8_t> source(32,42);auto id=P::Insert(&p,source.data(),32);std::vector<std::uint8_t> target(34,99);assert(!P::ReadInto(&p,*id,std::span<std::uint8_t>(target.data(),31)));for(auto b:target)assert(b==99);assert(P::ReadInto(&p,*id,std::span<std::uint8_t>(target.data(),33)).value()==32);for(std::size_t i=0;i<32;i++)assert(target[i]==42);assert(target[32]==99&&target[33]==99);
  }
  { // readinto-guarded-capacity-63
Page p;P::Init(&p,1);std::vector<std::uint8_t> source(63,42);auto id=P::Insert(&p,source.data(),63);std::vector<std::uint8_t> target(65,99);assert(!P::ReadInto(&p,*id,std::span<std::uint8_t>(target.data(),62)));for(auto b:target)assert(b==99);assert(P::ReadInto(&p,*id,std::span<std::uint8_t>(target.data(),64)).value()==63);for(std::size_t i=0;i<63;i++)assert(target[i]==42);assert(target[63]==99&&target[64]==99);
  }
  { // readinto-guarded-capacity-64
Page p;P::Init(&p,1);std::vector<std::uint8_t> source(64,42);auto id=P::Insert(&p,source.data(),64);std::vector<std::uint8_t> target(66,99);assert(!P::ReadInto(&p,*id,std::span<std::uint8_t>(target.data(),63)));for(auto b:target)assert(b==99);assert(P::ReadInto(&p,*id,std::span<std::uint8_t>(target.data(),65)).value()==64);for(std::size_t i=0;i<64;i++)assert(target[i]==42);assert(target[64]==99&&target[65]==99);
  }
  { // readinto-guarded-capacity-127
Page p;P::Init(&p,1);std::vector<std::uint8_t> source(127,42);auto id=P::Insert(&p,source.data(),127);std::vector<std::uint8_t> target(129,99);assert(!P::ReadInto(&p,*id,std::span<std::uint8_t>(target.data(),126)));for(auto b:target)assert(b==99);assert(P::ReadInto(&p,*id,std::span<std::uint8_t>(target.data(),128)).value()==127);for(std::size_t i=0;i<127;i++)assert(target[i]==42);assert(target[127]==99&&target[128]==99);
  }
  { // readinto-guarded-capacity-128
Page p;P::Init(&p,1);std::vector<std::uint8_t> source(128,42);auto id=P::Insert(&p,source.data(),128);std::vector<std::uint8_t> target(130,99);assert(!P::ReadInto(&p,*id,std::span<std::uint8_t>(target.data(),127)));for(auto b:target)assert(b==99);assert(P::ReadInto(&p,*id,std::span<std::uint8_t>(target.data(),129)).value()==128);for(std::size_t i=0;i<128;i++)assert(target[i]==42);assert(target[128]==99&&target[129]==99);
  }
  { // readinto-guarded-capacity-255
Page p;P::Init(&p,1);std::vector<std::uint8_t> source(255,42);auto id=P::Insert(&p,source.data(),255);std::vector<std::uint8_t> target(257,99);assert(!P::ReadInto(&p,*id,std::span<std::uint8_t>(target.data(),254)));for(auto b:target)assert(b==99);assert(P::ReadInto(&p,*id,std::span<std::uint8_t>(target.data(),256)).value()==255);for(std::size_t i=0;i<255;i++)assert(target[i]==42);assert(target[255]==99&&target[256]==99);
  }
  { // readinto-guarded-capacity-256
Page p;P::Init(&p,1);std::vector<std::uint8_t> source(256,42);auto id=P::Insert(&p,source.data(),256);std::vector<std::uint8_t> target(258,99);assert(!P::ReadInto(&p,*id,std::span<std::uint8_t>(target.data(),255)));for(auto b:target)assert(b==99);assert(P::ReadInto(&p,*id,std::span<std::uint8_t>(target.data(),257)).value()==256);for(std::size_t i=0;i<256;i++)assert(target[i]==42);assert(target[256]==99&&target[257]==99);
  }
  { // bloom-key-roundtrip-empty
Bloom a(100),b(100);auto key=std::string();std::vector<std::string> values{key,key+"suffix"};a.AddRange(values.begin(),values.end());assert(a.Count()==2);for(const auto& value:values)assert(a.MaybeContains(value));b.Add("other");auto source_count=b.Count();a.MergeFrom(b);for(const auto& value:values)assert(a.MaybeContains(value));assert(a.MaybeContains("other"));assert(b.Count()==source_count);auto bits=a.BitCount();a.Clear();assert(a.Empty()&&a.BitCount()==bits&&a.FillRatio()==0.0);
  }
  { // bloom-key-roundtrip-ascii
Bloom a(100),b(100);auto key=std::string("ordinary");std::vector<std::string> values{key,key+"suffix"};a.AddRange(values.begin(),values.end());assert(a.Count()==2);for(const auto& value:values)assert(a.MaybeContains(value));b.Add("other");auto source_count=b.Count();a.MergeFrom(b);for(const auto& value:values)assert(a.MaybeContains(value));assert(a.MaybeContains("other"));assert(b.Count()==source_count);auto bits=a.BitCount();a.Clear();assert(a.Empty()&&a.BitCount()==bits&&a.FillRatio()==0.0);
  }
  { // bloom-key-roundtrip-spaces
Bloom a(100),b(100);auto key=std::string("with spaces");std::vector<std::string> values{key,key+"suffix"};a.AddRange(values.begin(),values.end());assert(a.Count()==2);for(const auto& value:values)assert(a.MaybeContains(value));b.Add("other");auto source_count=b.Count();a.MergeFrom(b);for(const auto& value:values)assert(a.MaybeContains(value));assert(a.MaybeContains("other"));assert(b.Count()==source_count);auto bits=a.BitCount();a.Clear();assert(a.Empty()&&a.BitCount()==bits&&a.FillRatio()==0.0);
  }
  { // bloom-key-roundtrip-nul
Bloom a(100),b(100);auto key=std::string("a\0b",3);std::vector<std::string> values{key,key+"suffix"};a.AddRange(values.begin(),values.end());assert(a.Count()==2);for(const auto& value:values)assert(a.MaybeContains(value));b.Add("other");auto source_count=b.Count();a.MergeFrom(b);for(const auto& value:values)assert(a.MaybeContains(value));assert(a.MaybeContains("other"));assert(b.Count()==source_count);auto bits=a.BitCount();a.Clear();assert(a.Empty()&&a.BitCount()==bits&&a.FillRatio()==0.0);
  }
  { // bloom-key-roundtrip-all-nul
Bloom a(100),b(100);auto key=std::string(16,0);std::vector<std::string> values{key,key+"suffix"};a.AddRange(values.begin(),values.end());assert(a.Count()==2);for(const auto& value:values)assert(a.MaybeContains(value));b.Add("other");auto source_count=b.Count();a.MergeFrom(b);for(const auto& value:values)assert(a.MaybeContains(value));assert(a.MaybeContains("other"));assert(b.Count()==source_count);auto bits=a.BitCount();a.Clear();assert(a.Empty()&&a.BitCount()==bits&&a.FillRatio()==0.0);
  }
  { // bloom-key-roundtrip-high-byte
Bloom a(100),b(100);auto key=std::string(16,static_cast<char>(255));std::vector<std::string> values{key,key+"suffix"};a.AddRange(values.begin(),values.end());assert(a.Count()==2);for(const auto& value:values)assert(a.MaybeContains(value));b.Add("other");auto source_count=b.Count();a.MergeFrom(b);for(const auto& value:values)assert(a.MaybeContains(value));assert(a.MaybeContains("other"));assert(b.Count()==source_count);auto bits=a.BitCount();a.Clear();assert(a.Empty()&&a.BitCount()==bits&&a.FillRatio()==0.0);
  }
  { // bloom-key-roundtrip-unicode
Bloom a(100),b(100);auto key=std::string("café");std::vector<std::string> values{key,key+"suffix"};a.AddRange(values.begin(),values.end());assert(a.Count()==2);for(const auto& value:values)assert(a.MaybeContains(value));b.Add("other");auto source_count=b.Count();a.MergeFrom(b);for(const auto& value:values)assert(a.MaybeContains(value));assert(a.MaybeContains("other"));assert(b.Count()==source_count);auto bits=a.BitCount();a.Clear();assert(a.Empty()&&a.BitCount()==bits&&a.FillRatio()==0.0);
  }
  { // bloom-key-roundtrip-cjk
Bloom a(100),b(100);auto key=std::string("数学");std::vector<std::string> values{key,key+"suffix"};a.AddRange(values.begin(),values.end());assert(a.Count()==2);for(const auto& value:values)assert(a.MaybeContains(value));b.Add("other");auto source_count=b.Count();a.MergeFrom(b);for(const auto& value:values)assert(a.MaybeContains(value));assert(a.MaybeContains("other"));assert(b.Count()==source_count);auto bits=a.BitCount();a.Clear();assert(a.Empty()&&a.BitCount()==bits&&a.FillRatio()==0.0);
  }
}
