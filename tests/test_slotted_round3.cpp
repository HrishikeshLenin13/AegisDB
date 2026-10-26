#include "aegisdb/storage/slotted_page.hpp"
#include <cassert>
#include <iostream>
#include <vector>
int main() {
  { aegisdb::Page page; aegisdb::SlottedPage::Init(&page, 1);
    std::uint8_t value[] = {1,2,3}; auto id = aegisdb::SlottedPage::Insert(&page,value,3);
    assert(id && aegisdb::SlottedPage::LiveTupleCount(&page)==1 && aegisdb::SlottedPage::LiveBytes(&page)==3);
    aegisdb::SlottedPage::Delete(&page,*id);assert(aegisdb::SlottedPage::LiveTupleCount(&page)==0);
  }
  { aegisdb::Page page; aegisdb::SlottedPage::Init(&page,1);std::vector<std::uint8_t> data(100,7);
    auto a=aegisdb::SlottedPage::Insert(&page,data.data(),100);auto b=aegisdb::SlottedPage::Insert(&page,data.data(),100);
    aegisdb::SlottedPage::Delete(&page,*a);assert(aegisdb::SlottedPage::ReclaimableBytes(&page)==100);
    auto before=page.FreeSpace();aegisdb::SlottedPage::Compact(&page);
    assert(page.FreeSpace()==before+100 && aegisdb::SlottedPage::ReclaimableBytes(&page)==0);
    assert(aegisdb::SlottedPage::Read(&page,*b).value()==data);
  }
  { aegisdb::Page page;aegisdb::SlottedPage::Init(&page,1);std::vector<std::uint8_t> data(100,8);
    std::vector<std::uint16_t> ids;while(auto id=aegisdb::SlottedPage::Insert(&page,data.data(),100)) ids.push_back(*id);
    aegisdb::SlottedPage::Delete(&page,ids.front());assert(aegisdb::SlottedPage::ReclaimableBytes(&page)==100);
    auto inserted=aegisdb::SlottedPage::Insert(&page,data.data(),100);assert(inserted==ids.front());
    for(auto id:ids) assert(aegisdb::SlottedPage::Read(&page,id).value()==data);
  }
  { aegisdb::Page page;aegisdb::SlottedPage::Init(&page,1);std::vector<std::uint8_t> data(100,3);
    std::vector<std::uint16_t> ids;while(auto id=aegisdb::SlottedPage::Insert(&page,data.data(),100)) ids.push_back(*id);
    auto old=aegisdb::SlottedPage::Read(&page,ids[0]);std::vector<std::uint8_t> huge(4096,4);
    assert(!aegisdb::SlottedPage::Update(&page,ids[0],huge.data(),4096));assert(aegisdb::SlottedPage::Read(&page,ids[0])==old);
    aegisdb::SlottedPage::Delete(&page,ids.back());std::vector<std::uint8_t> larger(180,9);
    assert(aegisdb::SlottedPage::Update(&page,ids[0],larger.data(),180));assert(aegisdb::SlottedPage::Read(&page,ids[0]).value()==larger);
    for(std::size_t i=1;i+1<ids.size();i++) assert(aegisdb::SlottedPage::Read(&page,ids[i]).value()==data);
  }
  { aegisdb::Page page;aegisdb::SlottedPage::Init(&page,1);std::uint8_t bytes[]={1,2,3};auto id=aegisdb::SlottedPage::Insert(&page,bytes,3);
    std::uint8_t small[]={9,9};assert(!aegisdb::SlottedPage::ReadInto(&page,*id,small));assert(small[0]==9 && small[1]==9);
    std::uint8_t out[]={0,0,0,7};assert(aegisdb::SlottedPage::ReadInto(&page,*id,out)==3);assert(out[0]==1 && out[2]==3 && out[3]==7);
    assert(!aegisdb::SlottedPage::ReadInto(&page,99,out));
  }
  // CASES
  std::cout << "Slotted-page regression checks passed.\n";
}
