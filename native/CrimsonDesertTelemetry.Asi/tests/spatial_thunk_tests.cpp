#include <cstdint>
#include <atomic>
#include <thread>
#include <vector>
#include <iostream>
extern "C" int CdtSpatialTestInvoke();
extern "C" char CdtSpatialTestReturn;
std::atomic<unsigned> calls{};
std::atomic<bool> valid{true};
extern "C" void CdtSpatialDispatch(uint64_t c,uint32_t x,uint32_t y,uint32_t z,uint64_t owner,uint64_t caller)
{
    if(c!=0x11||x!=2||y!=1||z!=1||owner!=0x12345678||caller!=reinterpret_cast<uint64_t>(&CdtSpatialTestReturn)) valid=false;
    ++calls;
}
int main()
{
    std::vector<std::thread> threads;
    for(unsigned n=0;n<8;++n)threads.emplace_back([]{for(unsigned i=0;i<10000;++i)if(!CdtSpatialTestInvoke())valid=false;});
    for(auto& t:threads)t.join();
    if(!valid||calls!=80000)return 1;
    std::cout<<"PASS 80000 multithreaded spatial ABI calls: four arguments, owner, original return address, R13 and stack.\n";
}
