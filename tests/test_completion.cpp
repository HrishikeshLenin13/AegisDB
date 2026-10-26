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
  { // insertion-preserves-binary-payloads
Page p;P::Init(&p,7);std::uint8_t bytes[]={0,255,0,7};auto id=P::Insert(&p,bytes,4);assert(id);assert(P::Read(&p,*id).value()==std::vector<std::uint8_t>(bytes,bytes+4));
  }
  { // multiple-inserts-keep-stable-distinct-slot-ids
Page p;P::Init(&p,7);std::uint8_t b=1;auto x=P::Insert(&p,&b,1),y=P::Insert(&p,&b,1);assert(x&&y&&*x!=*y);assert(P::LiveTupleCount(&p)==2);
  }
  { // oversized-inserts-leave-live-data-intact
Page p;P::Init(&p,7);std::uint8_t b=7;auto id=P::Insert(&p,&b,1);std::vector<std::uint8_t> large(4096,1);assert(!P::Insert(&p,large.data(),4096));assert(P::Read(&p,*id)->at(0)==7);
  }
  { // deleting-an-unknown-slot-does-not-change-occupancy
Page p;P::Init(&p,7);assert(!P::Delete(&p,99));assert(P::LiveTupleCount(&p)==0);
  }
  { // deleting-the-same-tuple-twice-is-not-success
Page p;P::Init(&p,7);std::uint8_t b=1;auto id=P::Insert(&p,&b,1);assert(P::Delete(&p,*id));assert(!P::Delete(&p,*id));
  }
  { // deleted-slots-are-not-readable
Page p;P::Init(&p,7);std::uint8_t b=1;auto id=P::Insert(&p,&b,1);P::Delete(&p,*id);assert(!P::Read(&p,*id));
  }
  { // new-inserts-reuse-empty-slot-identifiers
Page p;P::Init(&p,7);std::uint8_t b=1;auto old=P::Insert(&p,&b,1);P::Delete(&p,*old);auto next=P::Insert(&p,&b,1);assert(next==old);
  }
  { // shrinking-updates-retain-the-original-slot
Page p;P::Init(&p,7);std::uint8_t data[]={1,2,3};auto id=P::Insert(&p,data,3);assert(P::Update(&p,*id,data,1));assert(P::Read(&p,*id)->size()==1);assert(P::LiveBytes(&p)==1);
  }
  { // growing-updates-keep-neighboring-tuples-unchanged
Page p;P::Init(&p,7);std::uint8_t b=2;auto x=P::Insert(&p,&b,1),y=P::Insert(&p,&b,1);std::vector<std::uint8_t> data(80,9);assert(P::Update(&p,*x,data.data(),80));assert(P::Read(&p,*y)->at(0)==2);
  }
  { // failed-growth-retains-the-original-tuple
Page p;P::Init(&p,7);std::uint8_t b=7;auto id=P::Insert(&p,&b,1);std::vector<std::uint8_t> data(4096,9);assert(!P::Update(&p,*id,data.data(),4096));assert(P::Read(&p,*id)->at(0)==7);
  }
  { // unknown-update-targets-do-not-create-tuples
Page p;P::Init(&p,7);std::uint8_t b=1;assert(!P::Update(&p,99,&b,1));assert(P::LiveTupleCount(&p)==0);
  }
  { // deleted-update-targets-stay-deleted
Page p;P::Init(&p,7);std::uint8_t b=1;auto id=P::Insert(&p,&b,1);P::Delete(&p,*id);assert(!P::Update(&p,*id,&b,1));
  }
  { // compaction-preserves-page-and-tuple-identities
Page p;P::Init(&p,7);std::uint8_t b=3;auto id=P::Insert(&p,&b,1);P::Compact(&p);assert(p.GetPageId()==7);assert(P::Read(&p,*id)->at(0)==3);
  }
  { // compaction-is-idempotent-for-free-space
Page p;P::Init(&p,7);std::uint8_t b=3;auto id=P::Insert(&p,&b,1);P::Delete(&p,*id);P::Compact(&p);auto free=p.FreeSpace();P::Compact(&p);assert(p.FreeSpace()==free);assert(P::ReclaimableBytes(&p)==0);
  }
  { // reclaimable-bytes-exclude-live-payloads
Page p;P::Init(&p,7);std::vector<std::uint8_t> bytes(50,3);auto id=P::Insert(&p,bytes.data(),50);assert(P::ReclaimableBytes(&p)==0);P::Update(&p,*id,bytes.data(),20);assert(P::ReclaimableBytes(&p)==30);
  }
  { // readinto-leaves-short-destinations-untouched
Page p;P::Init(&p,7);std::uint8_t b[]={1,2,3};auto id=P::Insert(&p,b,3);std::uint8_t dest[]={9,9};assert(!P::ReadInto(&p,*id,dest));assert(dest[0]==9&&dest[1]==9);
  }
}
