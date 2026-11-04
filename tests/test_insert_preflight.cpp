#include "aegisdb/storage/slotted_page.hpp"
#include <cassert>
#include <vector>
int main(){using P=aegisdb::SlottedPage;aegisdb::Page page;P::Init(&page,1);std::vector<std::uint8_t> data(100,7);std::vector<std::uint16_t> ids;
while(auto id=P::Insert(&page,data.data(),100))ids.push_back(*id);
assert(!P::CanInsert(&page,100));assert(P::Delete(&page,ids.front()));page.SetDirty(false);auto free=page.FreeSpace();assert(P::CanInsert(&page,100));assert(!page.IsDirty()&&page.FreeSpace()==free);assert(!P::CanInsert(&page,4096));assert(P::Insert(&page,data.data(),100)==ids.front());
}
