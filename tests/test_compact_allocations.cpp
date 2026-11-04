#include "aegisdb/storage/slotted_page.hpp"
#include <cstdlib>
#include <new>
#include <cassert>
#include <array>
#include <iostream>
static bool tracking=false;static std::size_t allocations=0;
void* operator new(std::size_t n){if(tracking)++allocations;if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept{std::free(p);}
void operator delete(void* p,std::size_t) noexcept{std::free(p);}
int main(){using P=aegisdb::SlottedPage;aegisdb::Page page;P::Init(&page,7);std::uint8_t data[]={1,2,3,4};std::array<std::uint16_t,50> ids{};
for(auto& id:ids){auto next=P::Insert(&page,data,4);assert(next);id=*next;}for(std::size_t i=0;i<ids.size();i+=2)assert(P::Delete(&page,ids[i]));
tracking=true;P::Compact(&page);tracking=false;
std::cout<<"compact allocations="<<allocations<<"\n";assert(allocations==0);
for(std::size_t i=1;i<ids.size();i+=2)assert(P::Read(&page,ids[i]).value()==std::vector<std::uint8_t>({1,2,3,4}));assert(P::ReclaimableBytes(&page)==0);
}
