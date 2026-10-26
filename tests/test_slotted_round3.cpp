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
  // CASES
  std::cout << "Slotted-page regression checks passed.\n";
}
