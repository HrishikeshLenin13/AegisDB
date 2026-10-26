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
  // CASES
  std::cout << "Slotted-page regression checks passed.\n";
}
