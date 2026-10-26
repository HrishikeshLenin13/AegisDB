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
  { // readinto-does-not-overwrite-destination-tail
Page p;P::Init(&p,7);std::uint8_t b=1;auto id=P::Insert(&p,&b,1);std::uint8_t dest[]={9,8};assert(P::ReadInto(&p,*id,dest)==1);assert(dest[0]==1&&dest[1]==8);
  }
  { // readinto-rejects-deleted-tuples
Page p;P::Init(&p,7);std::uint8_t b=1;auto id=P::Insert(&p,&b,1);P::Delete(&p,*id);assert(!P::ReadInto(&p,*id,std::span<std::uint8_t>(&b,1)));
  }
  { // compacting-empty-pages-reclaims-no-live-bytes
Page p;P::Init(&p,7);auto free=p.FreeSpace();P::Compact(&p);assert(P::LiveBytes(&p)==0&&p.FreeSpace()==free);
  }
  { // uninitialized-bloom-filters-answer-conservatively
Bloom b;assert(b.MaybeContains("anything"));assert(b.EstimatedFalsePositiveRate()==1);
  }
  { // initialized-empty-bloom-filters-have-no-occupied-bits
Bloom b(100);assert(b.FillRatio()==0);assert(!b.MaybeContains("anything"));
  }
  { // bloom-additions-have-no-false-negatives
Bloom b(100);for(int i=0;i<100;i++)b.Add(std::to_string(i));for(int i=0;i<100;i++)assert(b.MaybeContains(std::to_string(i)));
  }
  { // duplicate-bloom-inserts-do-not-increase-fill-ratio
Bloom b(100);b.Add("same");auto fill=b.FillRatio();b.Add("same");assert(b.FillRatio()==fill);assert(b.Count()==2);
  }
  { // bloom-clear-retains-capacity-and-hash-count
Bloom b(100);auto bytes=b.SizeBytes();auto hashes=b.HashCount();b.Add("x");b.Clear();assert(b.SizeBytes()==bytes&&b.HashCount()==hashes&&b.Empty());
  }
  { // bloom-reset-replaces-old-insertion-state
Bloom b(100);b.Add("old");b.Reset(200);assert(b.Empty()&&b.FillRatio()==0);
  }
  { // invalid-bloom-probabilities-preserve-existing-state
Bloom b(100);b.Add("old");for(double rate:{0.0,1.0,-0.1}){bool fail=false;try{b.Reset(100,rate);}catch(const std::invalid_argument&){fail=true;}assert(fail&&b.MaybeContains("old")&&b.Count()==1);}
  }
  { // bulk-bloom-insertion-matches-scalar-occupancy
std::vector<std::string> keys={"a","b","c"};Bloom x(100),y(100);x.AddRange(keys.begin(),keys.end());for(auto&key:keys)y.Add(key);assert(x.FillRatio()==y.FillRatio()&&x.Count()==y.Count());
  }
  { // merging-compatible-bloom-filters-preserves-both-key-sets
Bloom a(100),b(100);a.Add("a");b.Add("b");a.MergeFrom(b);assert(a.MaybeContains("a")&&a.MaybeContains("b")&&a.Count()==2);
  }
  { // incompatible-bloom-merges-do-not-change-the-receiver
Bloom a(100),b(1000);a.Add("a");auto fill=a.FillRatio();bool fail=false;try{a.MergeFrom(b);}catch(const std::invalid_argument&){fail=true;}assert(fail&&a.FillRatio()==fill&&a.Count()==1);
  }
  { // bloom-copies-have-independent-bit-storage
Bloom a(100);a.Add("a");Bloom b=a;b.Clear();assert(a.MaybeContains("a")&&!a.Empty());assert(b.Empty());
  }
  { // bloom-fill-and-model-estimates-stay-bounded
Bloom b(10);for(int i=0;i<1000;i++)b.Add(std::to_string(i));assert(b.FillRatio()>=0&&b.FillRatio()<=1);assert(b.EstimatedFalsePositiveRate()>=0&&b.EstimatedFalsePositiveRate()<=1);
  }
  { // empty-bulk-insertion-does-not-change-filter-state
Bloom b(10);std::vector<std::string> keys;b.AddRange(keys.begin(),keys.end());assert(b.Empty()&&b.FillRatio()==0);
  }
  { // bloom-merge-order-does-not-change-bit-occupancy
Bloom a(100),b(100);a.Add("a");b.Add("b");Bloom x=a,y=b;x.MergeFrom(b);y.MergeFrom(a);assert(x.FillRatio()==y.FillRatio());
  }
  { // bloom-capacity-diagnostics-use-bit-units
Bloom b(100);assert(b.BitCount()==b.SizeBytes()*8);assert(b.HashCount()>0);
  }
  { // default-record-identifiers-are-invalid
aegisdb::RID id;assert(!id.Valid());assert(id.page_id==aegisdb::kInvalidPageId);
  }
  { // record-identifiers-compare-page-before-slot
aegisdb::RID a{1,9},b{2,0};assert(a<b);assert(!(b<a));
  }
  { // record-identifiers-compare-slots-within-pages
aegisdb::RID a{1,2},b{1,3};assert(a<b&&a!=b);
  }
  { // record-identifier-equality-uses-both-fields
aegisdb::RID a{1,2},b{1,2};assert(a==b);assert(!(a!=b));
  }
  { // record-identifiers-render-both-address-components
aegisdb::RID a{12,34};assert(a.ToString()=="(12,34)");
  }
  { // page-initialization-keeps-explicit-page-types
Page p;p.Init(9,aegisdb::PageType::Heap);assert(p.GetPageId()==9&&p.Header()->type==aegisdb::PageType::Heap);
  }
  { // page-reset-clears-dirty-and-reference-flags
Page p;p.Init(9,aegisdb::PageType::Heap);p.SetRef(true);p.Reset();assert(!p.IsDirty()&&!p.Ref());
  }
  { // page-pin-accounting-is-balanced
Page p;p.Pin();p.Pin();assert(p.PinCount()==2);p.Unpin();p.Unpin();assert(p.PinCount()==0);
  }
  { // page-lsn-updates-are-observable
Page p;p.SetLsn(123);assert(p.GetLsn()==123);
  }
  { // page-free-space-never-underflows-corrupt-boundaries
Page p;p.Header()->free_start=100;p.Header()->free_end=50;assert(p.FreeSpace()==0);
  }
}
