// Query layout / native execution approach informed by World Builder, MIT:
// Copyright (c) 2026 Moon-yungg. See licenses/WorldBuilder-MIT.txt.
// Pinned provenance and exact-build evidence: docs/PHYSICS_QUERY_RESEARCH.md.
#include "physics_probe.h"
#include "physics_query.h"
#include "console/common.h"
#include "console/mem.h"
#include "native_contract.generated.h"
#include <MinHook.h>
#include <nlohmann/json.hpp>
#include <intrin.h>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>

namespace cdt::physics
{
namespace
{
using Json = nlohmann::json;
// Five pointer/integer parameters verified in the current-build wrapper; preserve RAX.
using ShapeFn = std::uint64_t(__fastcall*)(void*, void*, void*, void*, void*);
// Current-build TtWorldCastRay wrapper forwards world, query, collector only.
// Optional private replay uses the observed layout and same-call lifetime only;
// this is not an optical classifier or a generally supported physics API.
using RayFn = std::uint64_t(__fastcall*)(void*, void*, void*);
ShapeFn originalShape{};
RayFn originalRay{};
void* shapeTarget{};
void* rayTarget{};
std::atomic<bool> enabled{}, armed{};
std::atomic<bool> rayEnabled{};
SRWLOCK mutex = SRWLOCK_INIT;
std::filesystem::path folder;
std::filesystem::file_time_type lastRequestWrite{};
std::uint64_t base{}, processStart{}, lastPoll{};
std::string lastId;
bool replayFaulted{};
unsigned replayTransactions{};
enum class Phase { Idle, Waiting, Capturing, Done };
struct Work
{
    Phase phase{Phase::Idle};
    std::string id, mode, reason;
    Vec3 player{};
    std::uint64_t issued{}, captured{}, world{}, worldInner{}, worldFilter{}, stackLow{}, stackHigh{};
    std::array<std::uint64_t, 4> addresses{};
    std::uint64_t extra{};
    DWORD thread{};
    QueryCopy snapshot{};
    std::array<std::uint8_t, 0x200> collectorAfter{};
    std::uint64_t caller{}, returnValue{};
    unsigned attempts{}, spheres{}, collectors{}, nearPlayer{};
    unsigned contextRejected{};
    bool copied{}, afterCopied{};
    bool rayQueryAfterCopied{};
    std::array<std::uint8_t, 0x100> rayQueryAfter{};
    bool controlCalled{}, controlMatched{}, segmentCalled{}, guardsIntact{}, originalsPreserved{};
    DWORD callException{};
    const char* controlStatus{}; // string literals only: no allocation in callback.
    Vec3 segmentStart{}, segmentEnd{};
    std::array<std::uint8_t, 0x300> controlAfter{}, segmentAfter{};
    std::array<std::uint8_t, 0x200> segmentQuery{};
};
Work work;
bool RayMode(const std::string& mode)
{ return mode == "rayobserve" || mode == "rayreplay" || mode == "raysegment"; }
bool Copy(std::uint64_t address, void* destination, std::size_t size)
{
    return address >= 0x10000 && address <= 0x00007FFFFFFFFFFFULL - size &&
        ch::mem::SafeRead(reinterpret_cast<const void*>(address), destination, size);
}
template<class T> bool Get(std::uint64_t address, T& value) { return Copy(address, &value, sizeof value); }
bool Sphere(std::uint64_t object)
{
    std::uint64_t vt{}, locator{};
    std::array<std::uint32_t, 6> col{};
    std::array<char, 96> name{};
    if (!Get(object, vt) || vt < base + 8 || !Get(vt - 8, locator) ||
        locator < base || !Copy(locator, col.data(), sizeof col) || col[0] != 1 ||
        locator - base != col[5] || !Copy(base + col[3] + 16, name.data(), name.size())) return false;
    name.back() = 0;
    return std::strcmp(name.data(), ".?AVhknpSphereShape@@") == 0;
}
bool Capture(void* world, void* query, void* transform, void* collector, void* extra)
{
    const auto w = reinterpret_cast<std::uint64_t>(world);
    const auto q = reinterpret_cast<std::uint64_t>(query);
    const auto x = reinterpret_cast<std::uint64_t>(transform);
    const auto c = reinterpret_cast<std::uint64_t>(collector);
    ++work.attempts;
    // The live refusals had a different fifth argument. Do not consume a replay
    // one-shot on that context; observation-only captures still retain it.
    if (work.mode != "observe" && extra != collector)
    { ++work.contextRejected; return false; }
    std::uint64_t shape{}, hits{}, table{};
    Vec3 start{};
    if (!Get(q + 0x28, shape) || !Sphere(shape)) return false;
    ++work.spheres;
    if (!Get(c + 0x20, hits) || hits != c + 0x30 || !Get(c, table) || table != base + 0x5D13528) return false;
    ++work.collectors;
    if (!Get(q + 0x30, start) || !NearPlayer(start, work.player)) return false;
    ++work.nearPlayer;
    auto& s = work.snapshot;
    if (!Copy(q, s.query.data(), s.query.size()) || !Copy(x, s.transform.data(), s.transform.size()) ||
        !Copy(c, s.collector.data(), 0x200) || !Copy(shape, s.shape.data(), s.shape.size()) ||
        !Get(w + 0xB70, work.worldInner) || !Get(w + 0xBC0, work.worldFilter) || !work.worldInner) return false;
    work.world = w;
    work.addresses = {q, x, c, shape};
    work.extra = reinterpret_cast<std::uint64_t>(extra);
    work.thread = GetCurrentThreadId();
    ULONG_PTR low{}, high{};
    GetCurrentThreadStackLimits(&low, &high);
    work.stackLow = low;
    work.stackHigh = high;
    work.captured = GetTickCount64();
    work.copied = true;
    return true;
}
bool CallCopy(NativeCopy& copy, DWORD& exception) noexcept
{
    __try
    {
        originalShape(reinterpret_cast<void*>(work.world), copy.query.data.data(), copy.transform.data.data(),
            copy.collector.data.data(), copy.collector.data.data());
        return true;
    }
    __except (exception = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void PrepareCopy(NativeCopy& copy)
{
    copy.query.data = work.snapshot.query;
    copy.transform.data = work.snapshot.transform;
    copy.shape.data = work.snapshot.shape;
    std::memcpy(copy.collector.data.data(), work.snapshot.collector.data(), work.snapshot.collector.size());
    // Only these two pointers have established ownership. No generic rebasing
    // of overlapping readback windows. Other references remain live until THIS
    // ShapeHook returns; copies are never queued or used asynchronously.
    Write(copy.query.data, 0x28, reinterpret_cast<std::uint64_t>(copy.shape.data.data()));
    Write(copy.collector.data, 0x20, reinterpret_cast<std::uint64_t>(copy.collector.data.data()) + 0x30);
}
bool OriginalsIntact()
{
    std::array<std::uint8_t, 0xA0> q{};
    std::array<std::uint8_t, 0x40> xf{};
    std::array<std::uint8_t, 0x70> shape{}; // includes confirmed radius and packed margin fields
    std::array<std::uint8_t, 0x140> collector{};
    return Copy(work.addresses[0], q.data(), q.size()) &&
        Copy(work.addresses[1], xf.data(), xf.size()) &&
        Copy(work.addresses[2], collector.data(), collector.size()) &&
        Copy(work.addresses[3], shape.data(), shape.size()) &&
        !std::memcmp(q.data(), work.snapshot.query.data(), q.size()) &&
        !std::memcmp(xf.data(), work.snapshot.transform.data(), xf.size()) &&
        !std::memcmp(collector.data(), work.collectorAfter.data(), collector.size()) &&
        !std::memcmp(shape.data(), work.snapshot.shape.data(), shape.size());
}
void RunControl()
{
    if (work.mode == "observe") return;
    if (replayFaulted || replayTransactions >= 12) { work.controlStatus = "unknown-replay-disabled-for-process"; return; }
    if (!work.afterCopied || work.caller != base + 0x32554FA || work.extra != work.addresses[2] ||
        work.thread != GetCurrentThreadId() || GetTickCount64() - work.captured > 100)
    { work.controlStatus = "unknown-call-context"; return; }
    for (auto address : work.addresses)
        if (address < work.stackLow || address > work.stackHigh - 0x200)
        { work.controlStatus = "unknown-object-lifetime"; return; }
    std::uint64_t inner{}, filter{};
    std::array<std::uint8_t, 0xA0> liveQuery{};
    std::array<std::uint8_t, 0x40> liveTransform{};
    std::array<std::uint8_t, 0x70> liveShape{};
    if (!Get(work.world + 0xB70, inner) || inner != work.worldInner ||
        !Get(work.world + 0xBC0, filter) || filter != work.worldFilter ||
        !Copy(work.addresses[0], liveQuery.data(), liveQuery.size()) ||
        !Copy(work.addresses[1], liveTransform.data(), liveTransform.size()) ||
        !Copy(work.addresses[3], liveShape.data(), liveShape.size()) ||
        std::memcmp(liveQuery.data(), work.snapshot.query.data(), liveQuery.size()) ||
        std::memcmp(liveTransform.data(), work.snapshot.transform.data(), liveTransform.size()) ||
        std::memcmp(liveShape.data(), work.snapshot.shape.data(), liveShape.size()) ||
        Read<std::uint32_t>(work.snapshot.collector, 0xC) != 0 || !PlausibleResult(work.collectorAfter))
    { work.controlStatus = "unknown-input-change-or-result"; return; }
    NativeCopy control{}, segment{};
    PrepareCopy(control);
    if (work.mode == "segment")
    {
        PrepareCopy(segment);
        if (!SetSegment(segment.query.data, work.segmentStart, work.segmentEnd, work.player))
        { work.controlStatus = "invalid-segment-or-unverified-zero-component"; return; }
        work.segmentQuery = segment.query.data;
    }
    ++replayTransactions;
    work.controlCalled = true;
    const bool returned = CallCopy(control, work.callException);
    work.guardsIntact = control.Intact();
    work.originalsPreserved = OriginalsIntact();
    work.controlAfter = control.collector.data;
    if (!returned || !work.guardsIntact || !work.originalsPreserved)
    { replayFaulted = true; work.controlStatus = "unknown-control-fault-restart-required"; return; }
    work.controlMatched = SameResult(work.collectorAfter, work.controlAfter);
    if (!work.controlMatched) { work.controlStatus = "unknown-control-disagrees"; return; }
    work.controlStatus = "control-matches-natural-query";
    if (work.mode != "segment") return;
    work.segmentCalled = true;
    const bool segmentReturned = CallCopy(segment, work.callException);
    work.guardsIntact = segment.Intact();
    work.originalsPreserved = OriginalsIntact();
    work.segmentAfter = segment.collector.data;
    if (!segmentReturned || !work.guardsIntact || !work.originalsPreserved)
    { replayFaulted = true; work.controlStatus = "unknown-segment-fault-restart-required"; return; }
    work.controlStatus = PlausibleResult(work.segmentAfter) ? "diagnostic-segment-completed" : "unknown-segment-result";
    // Neither status means optical visibility. Radius/filter come from the
    // natural sphere query, including any player/body collision exclusions.
}
std::uint64_t __fastcall ShapeHook(void* w, void* q, void* x, void* c, void* extra)
{
    bool claimed = false;
    if (armed.load(std::memory_order_relaxed) && TryAcquireSRWLockExclusive(&mutex))
    {
        if (!RayMode(work.mode) && work.phase == Phase::Waiting && GetTickCount64() - work.issued < 2000 && work.attempts < 512 && Capture(w, q, x, c, extra))
        {
            work.phase = Phase::Capturing;
            work.caller = reinterpret_cast<std::uint64_t>(_ReturnAddress());
            armed = false;
            claimed = true;
        }
        ReleaseSRWLockExclusive(&mutex);
    }
    // Exactly the original call on the original thread, with unchanged arguments.
    // Observe mode adds no call. Explicit control/segment mode uses own copies
    // only AFTER this original call returns, on this same native thread.
    const auto result = originalShape(w, q, x, c, extra);
    if (claimed)
    {
        AcquireSRWLockExclusive(&mutex);
        work.afterCopied = Copy(reinterpret_cast<std::uint64_t>(c), work.collectorAfter.data(), work.collectorAfter.size());
        work.returnValue = result;
        RunControl();
        work.phase = Phase::Done;
        ReleaseSRWLockExclusive(&mutex);
    }
    return result;
}
bool CaptureRay(void* world, void* query, void* collector)
{
    ++work.attempts;
    const auto w = reinterpret_cast<std::uint64_t>(world);
    const auto q = reinterpret_cast<std::uint64_t>(query);
    const auto c = reinterpret_cast<std::uint64_t>(collector);
    if (work.mode != "rayobserve")
    {
        std::uint64_t vt{}, hits{};
        std::array<double, 3> position{};
        if (!Get(c, vt) || vt != base + 0x5D13528 || !Get(c + 0x20, hits) || hits != c + 0x30)
        { ++work.contextRejected; return false; }
        ++work.collectors;
        if (!Get(q + 0x40, position)) return false;
        Vec3 start{};
        for (unsigned i = 0; i < 3; ++i)
        {
            if (!std::isfinite(position[i]) || std::abs(position[i]) > 1000000) return false;
            start[i] = static_cast<float>(position[i]);
        }
        // Observed native ray starts up to 4.44 gu above the player's feet.
        auto horizontalPlayer = work.player; horizontalPlayer[1] = start[1];
        if (!NearPlayer(start, horizontalPlayer) || std::abs(start[1] - work.player[1]) > 6.f) return false;
        ++work.nearPlayer;
    }
    // Bounded raw windows, NOT proven object extents. No collector callbacks,
    // hit-pointer traversal, assumed positions or radius writes in this mode.
    if (!Copy(q, work.snapshot.query.data(), 0x100) ||
        !Copy(c, work.snapshot.collector.data(), 0x140) ||
        !Get(w + 0xB70, work.worldInner) || !work.worldInner) return false;
    work.world = w; work.addresses = {q, 0, c, 0};
    work.thread = GetCurrentThreadId();
    ULONG_PTR low{}, high{};
    GetCurrentThreadStackLimits(&low, &high);
    work.stackLow = low; work.stackHigh = high;
    work.captured = GetTickCount64();
    work.copied = true;
    return true;
}
bool CallRayCopy(NativeRayCopy& copy, DWORD& exception) noexcept
{
    __try
    {
        originalRay(reinterpret_cast<void*>(work.world), copy.query.data.data(), copy.collector.data.data());
        return true;
    }
    __except (exception = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void PrepareRayCopy(NativeRayCopy& copy)
{
    // Observed collector begins at query+0xA0 in the known context. Do NOT copy
    // its overlapping bytes as part of the query or generically rebase qwords.
    std::memcpy(copy.query.data.data(), work.snapshot.query.data(), 0xA0);
    std::memcpy(copy.collector.data.data(), work.snapshot.collector.data(), 0x140);
    Write(copy.collector.data, 0x20, reinterpret_cast<std::uint64_t>(copy.collector.data.data()) + 0x30);
}
bool RayOriginalsIntact()
{
    std::array<std::uint8_t, 0xA0> query{};
    std::array<std::uint8_t, 0x140> collector{};
    std::uint64_t inner{};
    return Get(work.world + 0xB70, inner) && inner == work.worldInner &&
        Copy(work.addresses[0], query.data(), query.size()) &&
        Copy(work.addresses[2], collector.data(), collector.size()) &&
        !std::memcmp(query.data(), work.snapshot.query.data(), query.size()) &&
        !std::memcmp(collector.data(), work.collectorAfter.data(), collector.size());
}
void RunRayControl()
{
    if (work.mode == "rayobserve") return;
    if (replayFaulted || replayTransactions >= 12) { work.controlStatus = "unknown-replay-disabled-for-process"; return; }
    if (!work.afterCopied || !work.rayQueryAfterCopied || work.caller != base + 0x32551AF ||
        work.thread != GetCurrentThreadId() || GetTickCount64() - work.captured > 100)
    { work.controlStatus = "unknown-call-context"; return; }
    if (work.stackHigh < 0x140 || work.stackLow >= work.stackHigh)
    { work.controlStatus = "unknown-object-lifetime"; return; }
    for (auto address : {work.addresses[0], work.addresses[2]})
        if (address < work.stackLow || address > work.stackHigh - 0x140)
        { work.controlStatus = "unknown-object-lifetime"; return; }
    if (!RayOriginalsIntact() || std::memcmp(work.snapshot.query.data(), work.rayQueryAfter.data(), 0xA0) ||
        Read<std::uint64_t>(work.snapshot.collector, 0) != base + 0x5D13528 ||
        Read<std::uint64_t>(work.snapshot.collector, 0x20) != work.addresses[2] + 0x30 ||
        Read<std::uint32_t>(work.snapshot.collector, 0xC) != 0 || !PlausibleResult(work.collectorAfter))
    { work.controlStatus = "unknown-input-change-or-result"; return; }
    NativeRayCopy control{}, segment{};
    PrepareRayCopy(control);
    if (work.mode == "raysegment")
    {
        PrepareRayCopy(segment);
        if (!SetRaySegment(segment.query.data, work.segmentStart, work.segmentEnd, work.player))
        { work.controlStatus = "invalid-segment-or-unverified-zero-component"; return; }
        std::memcpy(work.segmentQuery.data(), segment.query.data.data(), segment.query.data.size());
    }
    ++replayTransactions;
    work.controlCalled = true;
    const bool returned = CallRayCopy(control, work.callException);
    work.guardsIntact = control.Intact(); work.originalsPreserved = RayOriginalsIntact();
    work.controlAfter = control.collector.data;
    if (!returned || !work.guardsIntact || !work.originalsPreserved)
    { replayFaulted = true; work.controlStatus = "unknown-control-fault-restart-required"; return; }
    work.controlMatched = SameResult(work.collectorAfter, work.controlAfter);
    if (!work.controlMatched) { work.controlStatus = "unknown-control-disagrees"; return; }
    work.controlStatus = "control-matches-natural-ray";
    if (work.mode != "raysegment") return;
    work.segmentCalled = true;
    const bool segmentReturned = CallRayCopy(segment, work.callException);
    work.guardsIntact = segment.Intact(); work.originalsPreserved = RayOriginalsIntact();
    work.segmentAfter = segment.collector.data;
    if (!segmentReturned || !work.guardsIntact || !work.originalsPreserved)
    { replayFaulted = true; work.controlStatus = "unknown-segment-fault-restart-required"; return; }
    work.controlStatus = PlausibleResult(work.segmentAfter) ? "diagnostic-ray-segment-completed" : "unknown-segment-result";
}
std::uint64_t __fastcall RayHook(void* world, void* query, void* collector)
{
    bool claimed = false;
    if (armed.load(std::memory_order_relaxed) && TryAcquireSRWLockExclusive(&mutex))
    {
        if (RayMode(work.mode) && work.phase == Phase::Waiting &&
            GetTickCount64() - work.issued < 2000 && work.attempts < 512 && CaptureRay(world, query, collector))
        {
            work.phase = Phase::Capturing;
            work.caller = reinterpret_cast<std::uint64_t>(_ReturnAddress());
            armed = false; claimed = true;
        }
        ReleaseSRWLockExclusive(&mutex);
    }
    const auto result = originalRay(world, query, collector); // exactly once, unmodified
    if (claimed)
    {
        AcquireSRWLockExclusive(&mutex);
        work.afterCopied = Copy(reinterpret_cast<std::uint64_t>(collector), work.collectorAfter.data(), 0x140);
        work.rayQueryAfterCopied = Copy(reinterpret_cast<std::uint64_t>(query), work.rayQueryAfter.data(), work.rayQueryAfter.size());
        work.returnValue = result;
        work.controlStatus = work.afterCopied ? "observed-game-ray" : "unknown-ray-result-unreadable";
        RunRayControl();
        work.phase = Phase::Done;
        ReleaseSRWLockExclusive(&mutex);
    }
    return result;
}
template<std::size_t N> bool Guard(std::uint64_t address, const std::array<std::uint8_t, N>& expected)
{
    std::array<std::uint8_t, N> actual{};
    return Copy(address, actual.data(), N) && actual == expected;
}
template<std::size_t N> std::string Hex(const std::array<std::uint8_t, N>& bytes)
{
    constexpr char digits[] = "0123456789abcdef";
    std::string text; text.reserve(N * 2);
    for (auto b : bytes) { text += digits[b >> 4]; text += digits[b & 15]; }
    return text;
}
void Save(const Work& done)
{
    const bool ray = RayMode(done.mode);
    const bool knownCollector = done.copied && Read<std::uint64_t>(done.snapshot.collector, 0) == base + 0x5D13528;
    const Json report{{"schemaVersion", 2}, {"pid", GetCurrentProcessId()}, {"requestId", done.id},
        {"processStartFileTime", processStart}, {"mode", done.mode}, {"reason", done.reason},
        {"scope", "Private physics diagnostic; bounded original-context queries, NOT optical visibility"},
        {"primitive", ray ? "native-ray" : "sphere"},
        {"captureWindowBytes", {{"query", ray ? 0x100 : 0x200}, {"collector", ray ? 0x140 : 0x200},
            {"transform", ray ? 0 : 0x100}, {"shape", ray ? 0 : 0x200}}},
        {"controlStatus", done.controlStatus ? done.controlStatus : "not-requested"},
        {"controlCalled", done.controlCalled}, {"controlMatched", done.controlMatched},
        {"segmentCalled", done.segmentCalled}, {"guardsIntact", done.guardsIntact}, {"callException", done.callException},
        {"originalsPreserved", done.originalsPreserved},
        {"segmentResultPlausible", done.segmentCalled && done.controlMatched && done.callException == 0 &&
            done.guardsIntact && done.originalsPreserved && PlausibleResult(done.segmentAfter)},
        {"segmentStart", done.segmentStart}, {"segmentEnd", done.segmentEnd},
        {"controlCollectorHex", Hex(done.controlAfter)}, {"segmentCollectorHex", Hex(done.segmentAfter)},
        {"segmentQueryHex", Hex(done.segmentQuery)},
        {"segmentRawResult", done.segmentCalled ? Json{{"count", Read<std::uint32_t>(done.segmentAfter, 0xC)},
            {"fraction", Read<double>(done.segmentAfter, 0x10)}, {"normal", Read<Vec3>(done.segmentAfter, 0x80)}} : Json(nullptr)},
        {"buildId", native_contract::BuildId}, {"executableSha256", Hex(native_contract::ExecutableSha256)},
        {"moduleBase", base}, {"worldCastShapeRva", 0x42B0C50}, {"collectorVtableRva", ray ? Json(nullptr) : Json(0x5D13528)},
        {"capturedCollectorVtable", done.copied ? Json(Read<std::uint64_t>(done.snapshot.collector, 0)) : Json(nullptr)},
        {"worldCastRayRva", 0x42B0B50},
        {"player", done.player},
        {"capturedTickMs", done.captured}, {"captureThread", done.thread}, {"world", done.world},
        {"worldInner", done.worldInner}, {"worldFilter", done.worldFilter},
        {"caller", done.caller}, {"returnValue", done.returnValue}, {"queryTransformCollectorShape", done.addresses},
        {"stackLow", done.stackLow}, {"stackHigh", done.stackHigh}, {"extraArgument", done.extra},
        {"extraArgumentEqualsCollector", done.copied && done.extra == done.addresses[2]},
        {"candidates", {{"attempts", done.attempts}, {"sphere", done.spheres}, {"collector", done.collectors},
            {"nearPlayer", done.nearPlayer}, {"contextRejected", done.contextRejected}}},
        {"templateCaptured", done.copied}, {"collectorAfterCaptured", done.afterCopied},
        {"rayQueryAfterCaptured", done.rayQueryAfterCopied}, {"rayQueryAfterHex", ray ? Json(Hex(done.rayQueryAfter)) : Json(nullptr)},
        {"rawResult", done.afterCopied && knownCollector ? Json{{"count", Read<std::uint32_t>(done.collectorAfter, 0xC)},
            {"fraction", Read<double>(done.collectorAfter, 0x10)}, {"normal", Read<Vec3>(done.collectorAfter, 0x80)}} : Json(nullptr)},
        {"queryHex", Hex(done.snapshot.query)}, {"transformHex", Hex(done.snapshot.transform)},
        {"collectorBeforeHex", Hex(done.snapshot.collector)}, {"collectorAfterHex", Hex(done.collectorAfter)}, {"shapeHex", Hex(done.snapshot.shape)}};
    const auto path = folder / ("physics-probe-" + std::to_string(GetCurrentProcessId()) + "-" + done.id + ".json");
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) { ch::Log("Physics probe output failed: %lu", GetLastError()); return; }
    const auto text = report.dump(2);
    DWORD written{};
    const bool ok = WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) && written == text.size();
    CloseHandle(file);
    ch::Log("Physics probe %s: %s; report %s (%s)", done.id.c_str(), done.reason.c_str(), path.string().c_str(), ok ? "saved" : "write failed");
}
}
bool Start(std::uint64_t moduleBase, const wchar_t* directory)
{
    // Instruments::Run already verified the loaded EXE against this contract.
    // Also pin this diagnostic to its actual research hash, not just a build label.
    constexpr std::string_view expectedHash = "57da440d72f4db974f25fef047cf84c4dadd999a88cb2a3c5af4c9bd67fde1e7";
    if (enabled.load() || native_contract::BuildId != "25477059" ||
        Hex(native_contract::ExecutableSha256) != expectedHash) return false;
    base = moduleBase; folder = directory;
    shapeTarget = reinterpret_cast<void*>(base + 0x42B0C50);
    constexpr std::array<std::uint8_t, 30> shapeHead{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,
        0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7C,0x24,0x20,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x83,0xEC,0x50};
    if (!Guard(base + 0x42B0C50, shapeHead)) return false;
    // Also guard the collector shape recovered offline, before a captured pointer can use it.
    std::array<std::uint64_t, 12> slots{};
    if (!Copy(base + 0x5D13528, slots.data(), sizeof slots) || slots[5] != base + 0x39DBE20 ||
        slots[0] != slots[9] || slots[3] != slots[7] || slots[6] != slots[8]) return false;
    FILETIME born{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &born, &exit, &kernel, &user)) return false;
    processStart = (static_cast<std::uint64_t>(born.dwHighDateTime) << 32) | born.dwLowDateTime;
    const auto init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) return false;
    if (MH_CreateHook(shapeTarget, ShapeHook, reinterpret_cast<void**>(&originalShape)) != MH_OK) return false;
    if (MH_EnableHook(shapeTarget) != MH_OK) { MH_RemoveHook(shapeTarget); return false; }
    enabled = true;
    constexpr std::array<std::uint8_t, 24> rayHead{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,
        0x48,0x89,0x74,0x24,0x18,0x57,0x41,0x56,0x41,0x57,0x48,0x83,0xEC,0x30};
    rayTarget = reinterpret_cast<void*>(base + 0x42B0B50);
    if (Guard(base + 0x42B0B50, rayHead) &&
        MH_CreateHook(rayTarget, RayHook, reinterpret_cast<void**>(&originalRay)) == MH_OK)
    {
        if (MH_EnableHook(rayTarget) == MH_OK) rayEnabled = true;
        else MH_RemoveHook(rayTarget); // Never enabled, so no in-flight trampoline.
    }
    ch::Log("Physics probe v4 ready (private, idle). Ray observer=%s; explicit requests only; max 12 total replay transactions per process.",
        rayEnabled.load() ? "ready" : "unavailable");
    return true;
}
void Poll()
{
    const auto now = GetTickCount64();
    if (!enabled.load() || now - lastPoll < 100) return;
    lastPoll = now;
    Work done;
    AcquireSRWLockExclusive(&mutex);
    if ((work.phase == Phase::Waiting) && now - work.issued > 2500)
    { armed = false; work.reason = "unknown-no-matching-natural-query"; work.phase = Phase::Done; }
    const bool complete = work.phase == Phase::Done;
    if (complete)
    {
        done = work;
        if (done.copied) done.reason = done.controlStatus ? done.controlStatus :
            (done.afterCopied ? "observed-game-query" : "unknown-result-unreadable");
        work = {};
    }
    const bool idle = work.phase == Phase::Idle;
    ReleaseSRWLockExclusive(&mutex);
    if (complete) Save(done);
    if (!idle) return;
    try
    {
        const auto path = folder / L"physics-probe-request.json";
        std::error_code ec;
        const auto stamp = std::filesystem::last_write_time(path, ec);
        if (ec || stamp == lastRequestWrite) return;
        lastRequestWrite = stamp; // Reject malformed files once, not every poll.
        const auto size = std::filesystem::file_size(path, ec);
        if (ec || size > 4096) return;
        std::ifstream stream(path);
        const auto request = Json::parse(stream);
        const auto id = request.at("id").get<std::string>();
        if (id == lastId || id.empty() || id.size() > 32 || id.find_first_not_of("0123456789abcdef-") != std::string::npos) return;
        lastId = id;
        if (request.at("pid").get<DWORD>() != GetCurrentProcessId() ||
            request.at("processStartFileTime").get<std::uint64_t>() != processStart) return;
        Work pending;
        pending.id = id;
        pending.mode = request.at("mode").get<std::string>();
        pending.player = request.at("player").get<Vec3>();
        pending.issued = request.at("issuedTickMs").get<std::uint64_t>();
        if (pending.mode == "segment" || pending.mode == "raysegment")
        {
            pending.segmentStart = request.at("start").get<Vec3>();
            pending.segmentEnd = request.at("end").get<Vec3>();
        }
        if (pending.issued > now || now - pending.issued > 1000 || !Finite(pending.player) ||
            (pending.mode != "observe" && pending.mode != "replay" && pending.mode != "segment" && !RayMode(pending.mode)))
        { pending.reason = "invalid-or-stale-request"; Save(pending); return; }
        if (RayMode(pending.mode) && !rayEnabled.load())
        { pending.reason = "unknown-ray-observer-unavailable"; Save(pending); return; }
        pending.phase = Phase::Waiting;
        AcquireSRWLockExclusive(&mutex);
        work = pending;
        armed = true;
        ReleaseSRWLockExclusive(&mutex);
    }
    catch (const std::exception& e) { ch::Log("Physics probe request rejected: %s", e.what()); }
}
void Stop()
{
    armed = false;
    if (rayEnabled.exchange(false)) MH_DisableHook(rayTarget);
    if (enabled.exchange(false)) MH_DisableHook(shapeTarget);
    // Keep trampolines alive for any in-flight original call; module is pinned.
}
bool OwnsCodeAddress(std::uint64_t address)
{
    return (enabled.load() && address >= base + 0x42B0C50 && address < base + 0x42B0C50 + 32) ||
        (rayEnabled.load() && address >= base + 0x42B0B50 && address < base + 0x42B0B50 + 32);
}
}
