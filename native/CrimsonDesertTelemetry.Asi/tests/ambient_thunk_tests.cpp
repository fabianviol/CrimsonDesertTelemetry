#include <windows.h>
#include <MinHook.h>
#include <atomic>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>
extern "C" void CdtAmbientThunkA();
extern "C" void CdtAmbientThunkB();
extern "C" void CdtTestSite();
extern "C" int CdtTestInvoke();
extern "C" { void* CdtAmbientTrampolineA = nullptr; void* CdtAmbientTrampolineB = nullptr; }
std::atomic<uint32_t> calls{};
std::atomic<bool> valid{true};
unsigned expectedPath{};
extern "C" void CdtObserveCapture(uint64_t sky, uint64_t command, uint64_t path)
{
    if (path != expectedPath || sky != (path ? 0x55u : 0x77u) || command != (path ? 0x77u : 0x44u)) valid = false;
    ++calls;
}
int main()
{
    if (!CdtTestInvoke() || MH_Initialize() != MH_OK) return 1;
    void* thunks[]{reinterpret_cast<void*>(CdtAmbientThunkA), reinterpret_cast<void*>(CdtAmbientThunkB)};
    void** trampolines[]{&CdtAmbientTrampolineA, &CdtAmbientTrampolineB};
    for (expectedPath = 0; expectedPath < 2; ++expectedPath)
    {
        calls = 0;
        if (MH_CreateHook(CdtTestSite, thunks[expectedPath], trampolines[expectedPath]) != MH_OK || MH_EnableHook(CdtTestSite) != MH_OK) return 1;
        std::vector<std::thread> threads;
        for (unsigned n = 0; n < 8; ++n)
            threads.emplace_back([] { for (unsigned i = 0; i < 10000; ++i) if (!CdtTestInvoke()) valid = false; });
        for (auto& thread : threads) thread.join();
        if (MH_DisableHook(CdtTestSite) != MH_OK || MH_RemoveHook(CdtTestSite) != MH_OK || !valid || calls != 80000 || !CdtTestInvoke()) return 1;
    }
    std::cout << "Both ambient thunks: 160000 multithreaded hooks preserve GPR/XMM/flags/stack and extract the correct sky/command/path.\n";
}
