#include <windows.h>
#include <MinHook.h>
#include <atomic>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

extern "C" void CdtFilterThunk();
extern "C" void CdtTestSite();
extern "C" int CdtTestInvoke();
extern "C" { void* CdtFilterTrampoline = nullptr; }
std::atomic<uint32_t> calls{};
std::atomic<bool> valid{true};
extern "C" void CdtObserveCapture(uint64_t outer, uint64_t command)
{
    if (outer != 0xCC || command != 0x44) valid = false;
    ++calls;
}
int main()
{
    if (!CdtTestInvoke() || MH_Initialize()!=MH_OK ||
        MH_CreateHook(CdtTestSite,CdtFilterThunk,&CdtFilterTrampoline)!=MH_OK || MH_EnableHook(CdtTestSite)!=MH_OK) return 1;
    std::vector<std::thread> threads;
    for (unsigned n=0;n<8;++n)
        threads.emplace_back([] { for(unsigned i=0;i<10000;++i) if(!CdtTestInvoke()) valid=false; });
    for (auto& thread:threads) thread.join();
    const auto disabled=MH_DisableHook(CdtTestSite);
    if (!valid || calls!=80000 || disabled!=MH_OK || !CdtTestInvoke())
    { std::cerr<<"register/flags restoration, displaced instruction or capture count failed: "<<calls<<'\n'; return 1; }
    std::cout<<"8 threads / 80000 hooks: all 15 GPRs, RFLAGS, XMM0..15, stack and displaced mov preserved; every capture observed.\n";
}
