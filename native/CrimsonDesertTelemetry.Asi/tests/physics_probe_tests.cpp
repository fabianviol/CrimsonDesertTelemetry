// Synthetic native observation tests; these do NOT validate game collision semantics.
#include "../src/physics_probe.cpp"
#include <iostream>
#include <limits>
#include <thread>
#include <vector>

namespace ch { void Log(const char*, ...) {} }
namespace ch::mem
{
bool SafeRead(const void* src, void* dst, size_t n)
{
    __try { std::memcpy(dst, src, n); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
}
using namespace cdt::physics;
namespace
{
unsigned checks{};
void Check(bool ok, const char* name)
{
    ++checks;
    if (!ok) { std::cerr << "FAIL " << name << '\n'; std::exit(1); }
}
template<class T, std::size_t N> void Put(std::array<std::uint8_t, N>& b, size_t offset, T value)
{ std::memcpy(b.data() + offset, &value, sizeof value); }
std::array<std::uint8_t, 0x1000> image{}, worldBytes{};
QueryCopy query;
std::atomic<unsigned> calls{}, badArgs{};
std::uint64_t __fastcall Original(void* w, void* q, void* xf, void* c, void* e)
{
    ++calls;
    if (w != worldBytes.data() || q != query.query.data() || xf != query.transform.data() ||
        c != query.collector.data() || e != query.collector.data()) ++badArgs;
    // Do not mutate shared synthetic arguments in concurrent pass-through test.
    return 0x123456789ABCDEF0ULL;
}
void Setup()
{
    work = {}; query = {}; calls = 0; badArgs = 0;
    base = reinterpret_cast<std::uint64_t>(image.data());
    Put(image, 0x1F8, base + 0x100);
    Put(image, 0x100, std::uint32_t{1});
    Put(image, 0x10C, std::uint32_t{0x300});
    Put(image, 0x114, std::uint32_t{0x100});
    constexpr char name[] = ".?AVhknpSphereShape@@";
    std::memcpy(image.data() + 0x310, name, sizeof name);
    Put(query.shape, 0, base + 0x200);
    Put(query.query, 0x28, reinterpret_cast<std::uint64_t>(query.shape.data()));
    Put(query.query, 0x30, Vec3{-529.755f, 611.292f, -420.300f});
    Put(query.collector, 0, base + 0x5D13528);
    Put(query.collector, 0x20, reinterpret_cast<std::uint64_t>(query.collector.data()) + 0x30);
    Put(query.collector, 0xC, std::uint32_t{1});
    Put(query.collector, 0x10, .25);
    Put(query.collector, 0x80, Vec3{0, 1, 0});
    Put(worldBytes, 0xB70, base + 0x800);
    work.phase = Phase::Waiting;
    work.player = {-10529.755f, 611.292f, -4420.300f};
    work.issued = GetTickCount64();
    originalShape = Original; armed = true;
}
std::uint64_t Invoke()
{ return ShapeHook(worldBytes.data(), query.query.data(), query.transform.data(), query.collector.data(), query.collector.data()); }
}
int main()
{
    Check(NearPlayer({-529.75f, 611, -420.3f}, {-10529.75f, 611, -4420.3f}), "negative tile truncation");
    Check(NearPlayer({529.75f, 611, 420.3f}, {10529.75f, 611, 4420.3f}), "positive tile");
    Check(!NearPlayer({470.25f, 611, 579.7f}, {-10529.75f, 611, -4420.3f}), "floor convention rejected");
    Check(NearPlayer({3, 6, 0}, {1000, 3, 1000}), "independent horizontal and vertical bounds");
    Check(!NearPlayer({3.01f, 6, 0}, {1000, 3, 1000}), "horizontal bound");
    Check(!NearPlayer({0, 6.01f, 0}, {1000, 3, 1000}), "vertical bound");
    Check(!NearPlayer({0, 1000, 0}, {0, 1000, 0}), "loading placeholder");
    Check(!Finite({std::numeric_limits<float>::quiet_NaN(), 0, 0}), "NaN rejected");
    Check(!Finite({0, std::numeric_limits<float>::infinity(), 0}), "infinity rejected");
    Check(!Finite({1000001.f, 0, 0}), "coordinate bound");
    std::array<std::uint8_t, 8> bytes{1,2,3,4,5,6,7,8};
    Check(Read<std::uint32_t>(bytes, 4) == 0x08070605, "bounded unaligned read");
    Check(Read<std::uint64_t>(bytes, 1) == 0 && Read<std::uint64_t>(bytes, SIZE_MAX) == 0, "out of bounds read");
    Setup();
    Check(Invoke() == 0x123456789ABCDEF0ULL, "original return preserved");
    Check(calls == 1 && badArgs == 0, "exactly one original call, five arguments preserved");
    Check(work.phase == Phase::Done && work.copied && work.afterCopied, "capture completed");
    Check(!armed && work.attempts == 1 && work.thread == GetCurrentThreadId(), "one-shot original thread");
    Check(work.snapshot.query == query.query && work.collectorAfter == query.collector, "owned exact snapshots");
    Check(Read<double>(work.collectorAfter, 0x10) == .25, "raw fraction preserved");
    Setup();
    Put(query.collector, 0, std::uint64_t{0});
    Invoke();
    Check(work.phase == Phase::Waiting && !work.copied && calls == 1, "foreign collector passed through");
    Setup();
    Put(query.query, 0x30, Vec3{0, 0, 0});
    Invoke();
    Check(!work.copied && calls == 1, "far query passed through");
    Setup();
    work.issued = GetTickCount64() - 3000;
    Invoke();
    Check(!work.copied && calls == 1, "expired request passed through");
    Setup();
    std::vector<std::thread> threads;
    for (unsigned t = 0; t < 8; ++t) threads.emplace_back([] { for (unsigned n = 0; n < 64; ++n) Invoke(); });
    for (auto& thread : threads) thread.join();
    Check(calls == 512 && badArgs == 0, "concurrent original calls preserved");
    Check(work.phase == Phase::Done && work.attempts == 1 && work.copied && work.afterCopied, "concurrent one-shot claim");
    Check(!Copy(0, bytes.data(), bytes.size()), "null read rejected");
    Check(!Start(base, L"."), "synthetic module cannot pass instruction guard");
    std::cout << checks << " physics observer synthetic checks passed; no live-game claim.\n";
}
