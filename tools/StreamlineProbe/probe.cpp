// Bounded, external diagnostic debugger. No code patch, DLL injection or writes to game data.
// Streamline 2.11.1 ABI reference (field layout facts, not vendored SDK code):
// https://github.com/NVIDIA-RTX/Streamline/tree/v2.11.1/include
#include <windows.h>
#include <tlhelp32.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using Clock = std::chrono::steady_clock;
std::atomic<bool> interrupted = false;
BOOL WINAPI OnConsoleEvent(DWORD event)
{
    if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT) { interrupted = true; return TRUE; }
    return FALSE;
}
struct Handle
{
    HANDLE value = nullptr;
    explicit Handle(HANDLE h = nullptr) : value(h) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
void Require(bool ok, const char* operation)
{
    if (!ok) throw std::runtime_error(std::string(operation) + " (Win32 " + std::to_string(GetLastError()) + ")");
}
bool Read(HANDLE process, uint64_t address, void* bytes, size_t size)
{
    SIZE_T got = 0;
    return ReadProcessMemory(process, reinterpret_cast<void*>(address), bytes, size, &got) && got == size;
}
template<typename T> T ReadValue(HANDLE process, uint64_t address)
{
    T value{};
    Require(Read(process, address, &value, sizeof(value)), "ReadProcessMemory");
    return value;
}
template<typename T> T Field(const std::array<unsigned char, 444>& bytes, size_t offset)
{
    T value{};
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}
std::string Hex(uint64_t value)
{
    std::ostringstream out; out << "0x" << std::hex << std::uppercase << value; return out.str();
}
struct Module { std::wstring name; uint64_t base; DWORD size; };
std::vector<Module> Modules(DWORD pid)
{
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid));
    Require(snapshot.value != INVALID_HANDLE_VALUE, "Module snapshot");
    MODULEENTRY32W entry{}; entry.dwSize = sizeof(entry);
    std::vector<Module> modules;
    Require(Module32FirstW(snapshot.value, &entry) != FALSE, "Module32First");
    do { modules.push_back({entry.szModule, reinterpret_cast<uint64_t>(entry.modBaseAddr), entry.modBaseSize}); }
    while (Module32NextW(snapshot.value, &entry));
    return modules;
}
Module FindModule(const std::vector<Module>& modules, const std::wstring& name)
{
    for (const auto& module : modules) if (_wcsicmp(module.name.c_str(), name.c_str()) == 0) return module;
    throw std::runtime_error("Requested module is not loaded");
}
uint64_t Export(HANDLE process, const Module& module, const char* name)
{
    const auto dos = ReadValue<IMAGE_DOS_HEADER>(process, module.base);
    Require(dos.e_magic == IMAGE_DOS_SIGNATURE && dos.e_lfanew > 0 && dos.e_lfanew < 0x100000, "DOS header");
    const auto nt = ReadValue<IMAGE_NT_HEADERS64>(process, module.base + dos.e_lfanew);
    Require(nt.Signature == IMAGE_NT_SIGNATURE && nt.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC, "PE64 header");
    const auto dir = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    Require(dir.VirtualAddress > 0 && dir.VirtualAddress < module.size, "Export directory");
    const auto table = ReadValue<IMAGE_EXPORT_DIRECTORY>(process, module.base + dir.VirtualAddress);
    Require(table.NumberOfNames < 100000 && table.NumberOfFunctions < 100000, "Export count");
    for (DWORD i = 0; i < table.NumberOfNames; ++i)
    {
        const auto rva = ReadValue<DWORD>(process, module.base + table.AddressOfNames + i * sizeof(DWORD));
        std::array<char, 128> buffer{};
        if (!Read(process, module.base + rva, buffer.data(), buffer.size() - 1) || std::strcmp(buffer.data(), name)) continue;
        const auto ordinal = ReadValue<WORD>(process, module.base + table.AddressOfNameOrdinals + i * sizeof(WORD));
        Require(ordinal < table.NumberOfFunctions, "Export ordinal");
        const auto function = ReadValue<DWORD>(process, module.base + table.AddressOfFunctions + ordinal * sizeof(DWORD));
        Require(function && function < module.size && !(function >= dir.VirtualAddress && function < dir.VirtualAddress + dir.Size), "Direct export");
        return module.base + function;
    }
    throw std::runtime_error("Export not found");
}
constexpr std::array<unsigned char, 16> ConstantsGuid{0xd7,0x5a,0xd3,0xdc,0x4a,0x4e,0xad,0x4b,0xa9,0x0c,0xe0,0xc4,0x9e,0xb2,0x3a,0xfe};
bool KnownConstants(const std::array<unsigned char, 444>& bytes)
{
    const auto version = Field<uint64_t>(bytes, 24);
    return std::memcmp(bytes.data() + 8, ConstantsGuid.data(), ConstantsGuid.size()) == 0 && (version == 1 || version == 2);
}
bool ValidCamera(const std::array<unsigned char, 444>& bytes)
{
    if (!KnownConstants(bytes)) return false;
    // sl::BaseStructure = 32 bytes; five matrices + three float2s precede cameraPos.
    constexpr size_t camera = 376;
    for (int i = 0; i < 12; ++i)
    {
        const float value = Field<float>(bytes, camera + i * 4);
        if (!std::isfinite(value) || std::abs(value) > 1e8f) return false;
    }
    std::array<std::array<float, 3>, 3> axes{};
    for (int axis = 0; axis < 3; ++axis)
    {
        double norm = 0;
        for (int i = 0; i < 3; ++i) { axes[axis][i] = Field<float>(bytes, camera + 12 + axis * 12 + i * 4); norm += axes[axis][i] * axes[axis][i]; }
        if (std::abs(norm - 1) > 0.02) return false;
    }
    for (int a = 0; a < 3; ++a) for (int b = a + 1; b < 3; ++b)
        if (std::abs(axes[a][0]*axes[b][0] + axes[a][1]*axes[b][1] + axes[a][2]*axes[b][2]) > 0.02f) return false;
    const auto fov = Field<float>(bytes, camera + 56);
    const auto aspect = Field<float>(bytes, camera + 60);
    return std::isfinite(fov) && fov > 0.2f && fov < 3 && std::isfinite(aspect) && aspect > 0.5f && aspect < 5;
}
void ArrayJson(std::ostream& out, const std::array<unsigned char, 444>& bytes, size_t offset, size_t count)
{
    out << '[';
    for (size_t i = 0; i < count; ++i)
    {
        if (i) out << ',';
        const auto value = Field<float>(bytes, offset + i * 4);
        if (std::isfinite(value)) out << std::setprecision(9) << value; else out << "null";
    }
    out << ']';
}
struct Sample
{
    double ms; DWORD tid; uint64_t constants, caller, frame, viewport;
    bool readable; std::array<unsigned char, 444> bytes{};
    uint64_t rbx = 0, rsi = 0, rdi = 0;
    std::array<uint64_t, 14> registers{};
};
struct Report
{
    uint64_t calls = 0, readable = 0, known = 0, valid = 0;
    size_t armed = 0; bool detached = false; double milliseconds = 0;
    std::vector<Sample> samples;
};
struct Thread
{
    HANDLE handle; int slot = -1; DWORD64 originalAddress = 0, originalControl = 0;
};
DWORD64& AddressRegister(CONTEXT& c, int slot)
{
    switch (slot) { case 0: return c.Dr0; case 1: return c.Dr1; case 2: return c.Dr2; default: return c.Dr3; }
}
DWORD64 ControlMask(int slot) { return (3ull << (slot * 2)) | (15ull << (16 + slot * 4)); }
class Session
{
    DWORD pid_; HANDLE process_; uint64_t entry_; bool writeWatch_;
    bool attached_ = false, pending_ = false;
    DEBUG_EVENT event_{};
    std::map<DWORD, Thread> threads_;
public:
    Session(DWORD pid, HANDLE process, uint64_t entry, bool writeWatch = false) : pid_(pid), process_(process), entry_(entry), writeWatch_(writeWatch) {}
    ~Session()
    {
        if (attached_) { try { Detach(); } catch (...) { std::cerr << "WARNING: debugger cleanup failed; check target before continuing.\n"; } }
        for (const auto& [id, thread] : threads_) { (void)id; CloseHandle(thread.handle); }
    }
    void Arm(DWORD id, HANDLE handle)
    {
        // Debug-event handles need not grant SYNCHRONIZE/query rights. Keep our own
        // handle so cleanup can distinguish a terminated thread from access failure.
        CloseHandle(handle);
        if (threads_.contains(id)) return;
        handle = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION | SYNCHRONIZE, FALSE, id);
        Require(handle != nullptr, "Open debug thread with cleanup rights");
        const auto [it, inserted] = threads_.emplace(id, Thread{handle});
        (void)inserted;
        CONTEXT context{}; context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
        Require(GetThreadContext(handle, &context) != FALSE, "Read thread debug registers");
        for (int slot = 0; slot < 4; ++slot) if (!(context.Dr7 & (3ull << (slot * 2))))
        {
            auto& thread = it->second;
            thread.slot = slot; thread.originalAddress = AddressRegister(context, slot); thread.originalControl = context.Dr7 & ControlMask(slot);
            AddressRegister(context, slot) = entry_;
            context.Dr7 = (context.Dr7 & ~ControlMask(slot)) | (1ull << (slot * 2));
            if (writeWatch_) context.Dr7 |= 13ull << (16 + slot * 4); // RW=write, LEN=4 bytes.
            Require(SetThreadContext(handle, &context) != FALSE, "Arm hardware execution breakpoint");
            return;
        }
        throw std::runtime_error("No free hardware breakpoint slot; refusing to disturb existing breakpoints");
    }
    void Continue(DWORD disposition)
    {
        Require(ContinueDebugEvent(event_.dwProcessId, event_.dwThreadId, disposition) != FALSE, "ContinueDebugEvent");
        pending_ = false;
    }
    void Detach()
    {
        bool restored = true;
        // Stop each live thread while restoring just our slot; match Suspend/Resume counts.
        for (auto& [id, thread] : threads_)
        {
            (void)id;
            if (thread.slot < 0 || WaitForSingleObject(thread.handle, 0) == WAIT_OBJECT_0) continue;
            if (SuspendThread(thread.handle) == static_cast<DWORD>(-1)) { restored = false; continue; }
            CONTEXT context{}; context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            if (GetThreadContext(thread.handle, &context))
            {
                AddressRegister(context, thread.slot) = thread.originalAddress;
                context.Dr7 = (context.Dr7 & ~ControlMask(thread.slot)) | thread.originalControl;
                context.Dr6 &= ~(1ull << thread.slot);
                if (!SetThreadContext(thread.handle, &context)) restored = false;
                else thread.slot = -1;
            }
            else restored = false;
            if (ResumeThread(thread.handle) == static_cast<DWORD>(-1)) restored = false;
        }
        Require(restored, "Restore hardware breakpoint registers");
        if (pending_) Continue(DBG_CONTINUE);
        const bool stopped = DebugActiveProcessStop(pid_) != FALSE;
        if (stopped) attached_ = false;
        Require(restored && stopped, "Restore hardware breakpoints / detach");
    }
    Report Run(double seconds)
    {
        BOOL debugger = FALSE;
        Require(CheckRemoteDebuggerPresent(process_, &debugger) != FALSE, "Check debugger");
        if (debugger) throw std::runtime_error("Target already has a debugger; close its debug session first");
        Require(DebugActiveProcess(pid_) != FALSE, "DebugActiveProcess");
        attached_ = true;
        Require(DebugSetProcessKillOnExit(FALSE) != FALSE, "Keep target alive when debugger exits");
        Report report;
        const auto start = Clock::now();
        bool initialBreakpoint = true, exited = false;
        while (!interrupted && std::chrono::duration<double>(Clock::now() - start).count() < seconds)
        {
            if (!WaitForDebugEvent(&event_, 100))
            {
                if (GetLastError() == ERROR_SEM_TIMEOUT) continue;
                Require(false, "WaitForDebugEvent");
            }
            pending_ = true;
            DWORD disposition = DBG_CONTINUE;
            switch (event_.dwDebugEventCode)
            {
            case CREATE_PROCESS_DEBUG_EVENT:
                if (event_.u.CreateProcessInfo.hFile) CloseHandle(event_.u.CreateProcessInfo.hFile);
                CloseHandle(event_.u.CreateProcessInfo.hProcess);
                Arm(event_.dwThreadId, event_.u.CreateProcessInfo.hThread); ++report.armed;
                break;
            case CREATE_THREAD_DEBUG_EVENT:
                Arm(event_.dwThreadId, event_.u.CreateThread.hThread); ++report.armed;
                break;
            case EXIT_THREAD_DEBUG_EVENT:
                if (auto it = threads_.find(event_.dwThreadId); it != threads_.end()) { CloseHandle(it->second.handle); threads_.erase(it); }
                break;
            case LOAD_DLL_DEBUG_EVENT:
                if (event_.u.LoadDll.hFile) CloseHandle(event_.u.LoadDll.hFile);
                break;
            case EXIT_PROCESS_DEBUG_EVENT: exited = true; break;
            case EXCEPTION_DEBUG_EVENT:
            {
                disposition = DBG_EXCEPTION_NOT_HANDLED;
                const auto& exception = event_.u.Exception.ExceptionRecord;
                if (exception.ExceptionCode == EXCEPTION_BREAKPOINT && initialBreakpoint)
                { initialBreakpoint = false; disposition = DBG_CONTINUE; break; }
                const auto it = threads_.find(event_.dwThreadId);
                if (exception.ExceptionCode != EXCEPTION_SINGLE_STEP || (!writeWatch_ && reinterpret_cast<uint64_t>(exception.ExceptionAddress) != entry_) || it == threads_.end() || it->second.slot < 0) break;
                CONTEXT context{}; context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER | CONTEXT_DEBUG_REGISTERS;
                Require(GetThreadContext(it->second.handle, &context) != FALSE, "Read call registers");
                if (!(context.Dr6 & (1ull << it->second.slot))) break;
                ++report.calls;
                Sample sample{std::chrono::duration<double, std::milli>(Clock::now() - start).count(), event_.dwThreadId, context.Rcx, 0, context.Rdx, context.R8, false, {}};
                // Nonvolatile registers still hold the game's caller state at API entry.
                // These are research breadcrumbs, not an asserted engine-object contract.
                sample.rbx = context.Rbx; sample.rsi = context.Rsi; sample.rdi = context.Rdi;
                sample.registers = {context.Rip, context.Rax, context.Rcx, context.Rdx, context.Rbp, context.Rsp, context.R8, context.R9, context.R10, context.R11, context.R12, context.R13, context.R14, context.R15};
                if (writeWatch_) sample.caller = context.Rip; // Data breakpoints stop AFTER the writing instruction.
                else Read(process_, context.Rsp, &sample.caller, sizeof(sample.caller));
                sample.readable = !writeWatch_ && Read(process_, context.Rcx, sample.bytes.data(), sample.bytes.size());
                if (sample.readable) { ++report.readable; if (KnownConstants(sample.bytes)) ++report.known; if (ValidCamera(sample.bytes)) ++report.valid; }
                if (report.samples.size() < 1024) report.samples.push_back(sample);
                context.Dr6 &= ~(1ull << it->second.slot);
                context.EFlags |= 0x10000; // Resume past execution breakpoint without retriggering this instruction.
                Require(SetThreadContext(it->second.handle, &context) != FALSE, "Resume breakpoint instruction");
                disposition = DBG_CONTINUE;
                break;
            }
            default: break;
            }
            Continue(disposition);
            if (exited) { attached_ = false; break; }
        }
        report.milliseconds = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        if (!exited) { Detach(); report.detached = true; }
        return report;
    }
};
std::string Serialize(const Report& report, DWORD pid, uint64_t entry, const std::vector<Module>& modules, bool writeWatch = false)
{
    std::ostringstream out;
    out << "{\"type\":\"probe\",\"pid\":" << pid << ",\"entry\":\"" << Hex(entry) << "\",\"milliseconds\":" << report.milliseconds
        << ",\"calls\":" << report.calls << ",\"readable\":" << report.readable << ",\"knownConstants\":" << report.known << ",\"validCameras\":" << report.valid
        << ",\"armedThreads\":" << report.armed << ",\"detached\":" << (report.detached ? "true" : "false") << ",\"writeWatch\":" << (writeWatch ? "true" : "false") << "}\n";
    for (const auto& sample : report.samples)
    {
        out << "{\"type\":\"call\",\"ms\":" << sample.ms << ",\"thread\":" << sample.tid << ",\"constants\":\"" << Hex(sample.constants)
            << "\",\"caller\":\"" << Hex(sample.caller) << "\",\"framePointer\":\"" << Hex(sample.frame) << "\",\"viewportPointer\":\"" << Hex(sample.viewport)
            << "\",\"readable\":" << (sample.readable ? "true" : "false") << ",\"knownConstants\":" << (sample.readable && KnownConstants(sample.bytes) ? "true" : "false")
            << ",\"validCamera\":" << (sample.readable && ValidCamera(sample.bytes) ? "true" : "false");
        out << ",\"rbx\":\"" << Hex(sample.rbx) << "\",\"rsi\":\"" << Hex(sample.rsi) << "\",\"rdi\":\"" << Hex(sample.rdi) << '"';
        constexpr std::array<const char*, 14> names{"rip","rax","rcx","rdx","rbp","rsp","r8","r9","r10","r11","r12","r13","r14","r15"};
        for (size_t i = 0; i < names.size(); ++i) out << ",\"" << names[i] << "\":\"" << Hex(sample.registers[i]) << '"';
        for (const auto& module : modules) if (sample.caller >= module.base && sample.caller - module.base < module.size)
        {
            // Module filenames encountered here are ASCII. Escape non-ASCII / JSON punctuation conservatively.
            out << ",\"callerModule\":\"";
            for (wchar_t c : module.name) out << (c >= 32 && c < 127 && c != '\\' && c != '"' ? static_cast<char>(c) : '_');
            out << "\",\"callerRva\":\"" << Hex(sample.caller - module.base) << '"'; break;
        }
        if (sample.readable && KnownConstants(sample.bytes))
        {
            out << ",\"position\":"; ArrayJson(out, sample.bytes, 376, 3);
            out << ",\"up\":"; ArrayJson(out, sample.bytes, 388, 3);
            out << ",\"right\":"; ArrayJson(out, sample.bytes, 400, 3);
            out << ",\"forward\":"; ArrayJson(out, sample.bytes, 412, 3);
            out << ",\"nearFarFovAspect\":"; ArrayJson(out, sample.bytes, 424, 4);
            out << ",\"viewToClip\":"; ArrayJson(out, sample.bytes, 32, 16);
        }
        out << "}\n";
    }
    return out.str();
}

// Research-only mapping from Steam build 24994088, caller RVA 0x386B64E.
// At that call RBX holds the third argument of function RVA 0x386B370.
// This is an observed source object, NOT a restart-stable resolver.
std::array<unsigned char, 444> DecodeSource(const std::array<unsigned char, 0x868>& source)
{
    std::array<unsigned char, 444> constants{};
    std::memcpy(constants.data() + 8, ConstantsGuid.data(), ConstantsGuid.size());
    const uint64_t version = 2; std::memcpy(constants.data() + 24, &version, sizeof(version));
    constexpr std::array<size_t, 12> offsets{0x80,0x84,0x88, 0x3e4,0x3f4,0x404, 0x3e0,0x3f0,0x400, 0x90,0x94,0x98};
    for (size_t i = 0; i < offsets.size(); ++i) std::memcpy(constants.data() + 376 + i * 4, source.data() + offsets[i], 4);
    std::memcpy(constants.data() + 32, source.data() + 0x4e0, 64);
    std::memcpy(constants.data() + 424, source.data() + 0x860, 8);
    float sx{}, sy{};
    std::memcpy(&sx, source.data() + 0x4e0, 4); std::memcpy(&sy, source.data() + 0x4f4, 4);
    const float fov = 2.0f * std::atan(1.0f / sy), aspect = sy / sx;
    std::memcpy(constants.data() + 432, &fov, 4); std::memcpy(constants.data() + 436, &aspect, 4);
    return constants;
}
void SourceMappingTest()
{
    std::array<unsigned char, 0x868> source{};
    const auto put = [&](size_t offset, float value) { std::memcpy(source.data() + offset, &value, 4); };
    put(0x80, 12); put(0x84, 34); put(0x88, 56);
    put(0x3f4, 1); put(0x3e0, 1); put(0x98, 1);
    put(0x4e0, 1.20628512f); put(0x4f4, 2.14450693f);
    put(0x860, 0.2f); put(0x864, 10000);
    const auto camera = DecodeSource(source);
    Require(ValidCamera(camera) && Field<float>(camera, 376) == 12 && Field<float>(camera, 380) == 34 && Field<float>(camera, 384) == 56, "Source vector mapping");
    Require(std::abs(Field<float>(camera, 432) - 0.87266463f) < 0.00001f && std::abs(Field<float>(camera, 436) - 1.7777778f) < 0.00001f, "Source projection mapping");
    put(0x3f4, 0);
    Require(!ValidCamera(DecodeSource(source)), "Reject invalid source basis");
    std::cout << "PASS source mapping and invalid-basis rejection\n";
}
struct EngineGlobals { uint64_t mainRoot, camera; };
struct EngineAddress { uint64_t owner = 0, context = 0, source = 0; };
EngineGlobals LocateEngineGlobals(HANDLE process, const Module& module)
{
    // Build 24994088 only. Match the observed instructions before following any
    // pointer; no heap address from a previous process is accepted by this mode.
    const auto instruction = module.base + 0x284b749;
    std::array<unsigned char, 21> bytes{};
    Require(Read(process, instruction, bytes.data(), bytes.size()), "Read native camera reference");
    constexpr std::array<unsigned char, 14> tail{0xc5,0xfb,0x10,0xb0,0xc8,0,0,0,0x8b,0x98,0xd0,0,0,0};
    Require(bytes[0] == 0x48 && bytes[1] == 0x8b && bytes[2] == 0x05 &&
        std::equal(tail.begin(), tail.end(), bytes.begin() + 7), "Build-specific camera instruction signature");
    int32_t displacement{}; std::memcpy(&displacement, bytes.data() + 3, 4);
    const auto camera = instruction + 7 + displacement;
    Require(camera == module.base + 0x6259638, "Expected native camera global RVA");
    const auto mainInstruction = module.base + 0x2ea08a;
    Require(Read(process, mainInstruction, bytes.data(), 7), "Read main root reference");
    Require(bytes[0] == 0x48 && bytes[1] == 0x8b && bytes[2] == 0x0d, "Main root instruction signature");
    std::memcpy(&displacement, bytes.data() + 3, 4);
    const auto mainRoot = mainInstruction + 7 + displacement;
    Require(mainRoot == module.base + 0x62593b0, "Expected main root global RVA");
    return {mainRoot, camera};
}
template<typename Reader>
bool ResolveEngineAddress(Reader read, uint64_t moduleBase, EngineGlobals globals, EngineAddress& result)
{
    const auto pointer = [&](uint64_t base, uint64_t offset, uint64_t& value)
    {
        return base >= 0x10000 && base < 0x00007ffffffe0000ull && offset < 0x10000 &&
            read(base + offset, value) && value >= 0x10000 && value < 0x00007ffffffe0000ull;
    };
    uint64_t main{}, application{}, context{}, owner{}, direct{}, source{}, vtable{};
    if (!pointer(globals.mainRoot, 0, main) || !pointer(main, 0x28, application) ||
        !pointer(application, 0x18, context) || !pointer(context, 0xe0, owner) ||
        !pointer(globals.camera, 0, direct) || direct != owner ||
        !pointer(context, 0, vtable) || vtable != moduleBase + 0x53e0008 ||
        !pointer(owner, 0, vtable) || vtable != moduleBase + 0x53bed60 ||
        !pointer(owner, 0x428, source)) return false;
    result = {owner, context, source}; return true;
}
void EngineResolverTest()
{
    constexpr uint64_t base = 0x140000000;
    const EngineGlobals globals{base + 0x62593b0, base + 0x6259638};
    std::map<uint64_t, uint64_t> memory{{globals.mainRoot, 0x100000}, {0x100028, 0x200000},
        {0x200018, 0x300000}, {0x3000e0, 0x400000}, {globals.camera, 0x400000},
        {0x300000, base + 0x53e0008}, {0x400000, base + 0x53bed60}, {0x400428, 0x500000}};
    const auto read = [&](uint64_t address, uint64_t& value)
    {
        const auto it = memory.find(address); if (it == memory.end()) return false;
        value = it->second; return true;
    };
    EngineAddress result;
    Require(ResolveEngineAddress(read, base, globals, result) && result.source == 0x500000, "Resolve matching engine paths");
    memory[globals.camera] = 0x600000;
    Require(!ResolveEngineAddress(read, base, globals, result), "Reject conflicting camera roots");
    memory[globals.camera] = 0x400000; memory[0x400000] = base + 0x53bed68;
    Require(!ResolveEngineAddress(read, base, globals, result), "Reject wrong camera type");
    memory[0x400000] = base + 0x53bed60; memory[0x200018] = 0;
    Require(!ResolveEngineAddress(read, base, globals, result), "Reject missing camera context");
    memory[0x200018] = 0x300000; memory[0x400428] = 0x700000;
    Require(ResolveEngineAddress(read, base, globals, result) && result.source == 0x700000, "Follow source reallocation");
    std::cout << "PASS native camera resolver; mismatches, nulls and source reallocation\n";
}
int ReadSource(DWORD pid, uint64_t address, double seconds, const wchar_t* path, bool engine = false)
{
    Require(pid > 0 && (engine || (address >= 0x10000 && address < 0x00007fffffff0000ull)) && std::isfinite(seconds) && seconds >= 1 && seconds <= 10, "Bounded source arguments");
    Handle process(OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | SYNCHRONIZE, FALSE, pid));
    Require(process.value != nullptr, "Open source target read-only");
    const auto modules = Modules(pid);
    Require(!modules.empty() && _wcsicmp(modules.front().name.c_str(), L"CrimsonDesert.exe") == 0, "CrimsonDesert target only");
    const auto setupStart = Clock::now();
    const auto globals = engine ? LocateEngineGlobals(process.value, modules.front()) : EngineGlobals{};
    const auto resolve = [&](EngineAddress& found)
    {
        return ResolveEngineAddress([&](uint64_t where, uint64_t& value) { return Read(process.value, where, &value, 8); }, modules.front().base, globals, found);
    };
    EngineAddress found{};
    if (engine) { Require(resolve(found), "Native camera roots agree and types match"); address = found.source; }
    const auto setupMs = std::chrono::duration<double, std::milli>(Clock::now() - setupStart).count();
    Handle output(CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    Require(output.value != INVALID_HANDLE_VALUE, "Create NEW source report");
    SetConsoleCtrlHandler(OnConsoleEvent, TRUE);
    std::ostringstream out;
    out << "{\"type\":\"source-probe\",\"pid\":" << pid << ",\"address\":\"" << Hex(address) << "\",\"debugger\":false,\"atomicEngineFrame\":false,\"engineResolver\":" << (engine ? "true" : "false") << ",\"resolveMs\":" << setupMs << "}\n";
    const auto start = Clock::now(); size_t samples = 0, valid = 0, changed = 0;
    std::array<unsigned char, 48> previous{};
    while (!interrupted && std::chrono::duration<double>(Clock::now() - start).count() < seconds && WaitForSingleObject(process.value, 0) != WAIT_OBJECT_0)
    {
        std::array<unsigned char, 0x868> source{};
        const bool resolved = !engine || resolve(found);
        if (engine && resolved) address = found.source;
        const bool readable = resolved && Read(process.value, address, source.data(), source.size());
        const auto camera = DecodeSource(source);
        bool stable = true;
        if (engine)
        {
            std::array<unsigned char, 0x868> second{}; EngineAddress after{};
            stable = readable && Read(process.value, address, second.data(), second.size()) &&
                DecodeSource(second) == camera && resolve(after) && after.source == found.source &&
                after.owner == found.owner && after.context == found.context;
        }
        const bool ok = readable && stable && ValidCamera(camera);
        if (ok)
        {
            ++valid;
            if (valid > 1 && std::memcmp(previous.data(), camera.data() + 376, previous.size())) ++changed;
            std::memcpy(previous.data(), camera.data() + 376, previous.size());
        }
        out << "{\"type\":\"source\",\"ms\":" << std::chrono::duration<double, std::milli>(Clock::now() - start).count()
            << ",\"readable\":" << (readable ? "true" : "false") << ",\"validCamera\":" << (ok ? "true" : "false")
            << ",\"address\":\"" << Hex(address) << "\",\"stableAcrossRead\":" << (stable ? "true" : "false");
        if (readable)
        {
            out << ",\"position\":"; ArrayJson(out, camera, 376, 3);
            out << ",\"up\":"; ArrayJson(out, camera, 388, 3);
            out << ",\"right\":"; ArrayJson(out, camera, 400, 3);
            out << ",\"forward\":"; ArrayJson(out, camera, 412, 3);
            out << ",\"nearFarFovAspect\":"; ArrayJson(out, camera, 424, 4);
            out << ",\"viewToClip\":"; ArrayJson(out, camera, 32, 16);
        }
        out << "}\n"; ++samples; Sleep(20);
    }
    const auto json = out.str(); DWORD written = 0;
    Require(WriteFile(output.value, json.data(), static_cast<DWORD>(json.size()), &written, nullptr) && written == json.size(), "Write source report");
    std::cout << "source=" << Hex(address) << " resolveMs=" << setupMs << " samples=" << samples << " valid=" << valid << " transformChanges=" << changed << " debugger=none\n";
    return samples > 0 && valid == samples ? 0 : 1;
}

int ModuleReferences(DWORD pid, const std::vector<uint64_t>& targets, bool codeReferences = false)
{
    Handle process(OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid));
    Require(process.value != nullptr, "Open reference target read-only");
    const auto modules = Modules(pid);
    Require(!modules.empty() && _wcsicmp(modules.front().name.c_str(), L"CrimsonDesert.exe") == 0, "CrimsonDesert target only");
    const auto& module = modules.front();
    const auto started = Clock::now();
    std::vector<unsigned char> bytes(1024 * 1024);
    uint64_t cursor = module.base, scanned = 0; size_t matches = 0;
    while (cursor < module.base + module.size && scanned < 128ull * 1024 * 1024 && matches < 128 && std::chrono::duration<double>(Clock::now() - started).count() < 2)
    {
        MEMORY_BASIC_INFORMATION region{};
        if (!VirtualQueryEx(process.value, reinterpret_cast<void*>(cursor), &region, sizeof(region))) break;
        const auto end = (std::min)(module.base + module.size, reinterpret_cast<uint64_t>(region.BaseAddress) + region.RegionSize);
        if (end <= cursor) break;
        const auto protection = region.Protect & 0xff;
        const bool writable = protection == PAGE_READWRITE || protection == PAGE_WRITECOPY || protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
        const bool executable = protection == PAGE_EXECUTE_READ || protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
        if (region.State == MEM_COMMIT && (codeReferences ? executable : writable) && !(region.Protect & PAGE_GUARD))
        {
            const auto size = static_cast<size_t>((std::min)(end - cursor, static_cast<uint64_t>(bytes.size())));
            if (Read(process.value, cursor, bytes.data(), size))
                for (size_t offset = 0; offset + 8 <= size; offset += codeReferences ? 1 : 8)
                {
                    uint64_t value{};
                    if (codeReferences)
                    {
                        const auto* op = bytes.data() + offset;
                        if ((op[0] & 0xfb) != 0x48 || op[1] != 0x8b || (op[2] & 0xc7) != 5) continue;
                        int32_t displacement{}; std::memcpy(&displacement, op + 3, 4);
                        value = cursor + offset + 7 + displacement;
                    }
                    else std::memcpy(&value, bytes.data() + offset, 8);
                    if (std::find(targets.begin(), targets.end(), value) != targets.end())
                    {
                        std::cout << "reference=" << Hex(cursor + offset) << " rva=" << Hex(cursor + offset - module.base) << " value=" << Hex(value) << '\n';
                        if (++matches >= 128) break;
                    }
                }
            scanned += size; cursor += size;
        }
        else cursor = end;
    }
    std::cout << "matches=" << matches << " scannedBytes=" << scanned << " complete=" << (cursor == module.base + module.size) << '\n';
    return 0;
}

int FunctionRange(DWORD pid, uint64_t address)
{
    Handle process(OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid));
    Require(process.value != nullptr, "Open function target read-only");
    const auto modules = Modules(pid);
    Require(!modules.empty() && _wcsicmp(modules.front().name.c_str(), L"CrimsonDesert.exe") == 0, "CrimsonDesert target only");
    const auto& module = modules.front();
    Require(address >= module.base && address - module.base < module.size, "Function address inside game");
    const auto dos = ReadValue<IMAGE_DOS_HEADER>(process.value, module.base);
    Require(dos.e_magic == IMAGE_DOS_SIGNATURE && dos.e_lfanew > 0 && dos.e_lfanew < 0x100000, "Function DOS header");
    const auto nt = ReadValue<IMAGE_NT_HEADERS64>(process.value, module.base + dos.e_lfanew);
    Require(nt.Signature == IMAGE_NT_SIGNATURE && nt.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC, "Function PE64 header");
    const auto dir = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    Require(dir.VirtualAddress && dir.Size && dir.Size < 16000000 && dir.VirtualAddress < module.size && dir.Size <= module.size - dir.VirtualAddress, "Bounded function table");
    size_t low = 0, high = dir.Size / sizeof(RUNTIME_FUNCTION);
    const auto rva = address - module.base;
    while (low < high)
    {
        const auto mid = low + (high - low) / 2;
        const auto f = ReadValue<RUNTIME_FUNCTION>(process.value, module.base + dir.VirtualAddress + mid * sizeof(RUNTIME_FUNCTION));
        if (rva < f.BeginAddress) high = mid;
        else if (rva >= f.EndAddress) low = mid + 1;
        else
        {
            std::cout << "begin=" << Hex(module.base + f.BeginAddress) << " end=" << Hex(module.base + f.EndAddress) << " unwindRva=" << Hex(f.UnwindData) << '\n';
            return 0;
        }
    }
    throw std::runtime_error("No containing runtime function entry");
}

int Peek(DWORD pid, uint64_t address)
{
    Handle process(OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid));
    Require(process.value != nullptr, "Open peek target read-only");
    const auto modules = Modules(pid);
    Require(!modules.empty() && _wcsicmp(modules.front().name.c_str(), L"CrimsonDesert.exe") == 0, "CrimsonDesert target only");
    std::array<unsigned char, 256> bytes{};
    Require(Read(process.value, address, bytes.data(), bytes.size()), "Read bounded object header");
    std::cout << "address=" << Hex(address) << '\n';
    for (size_t offset = 0; offset < bytes.size(); offset += 8)
    {
        uint64_t value{}; std::memcpy(&value, bytes.data() + offset, 8);
        float a{}, b{}; std::memcpy(&a, bytes.data() + offset, 4); std::memcpy(&b, bytes.data() + offset + 4, 4);
        std::cout << Hex(offset) << " q=" << Hex(value) << " floats=" << a << ',' << b;
        for (const auto& module : modules) if (value >= module.base && value - module.base < module.size)
        {
            std::wcout << L" module=" << module.name;
            std::cout << '+' << Hex(value - module.base);
            if (offset == 0 && value >= module.base + 8)
            {
                uint64_t locator{}; DWORD typeRva{};
                if (Read(process.value, value - 8, &locator, 8) && locator >= module.base && locator - module.base < module.size &&
                    Read(process.value, locator + 12, &typeRva, 4) && typeRva < module.size)
                {
                    std::array<char, 128> name{};
                    if (Read(process.value, module.base + typeRva + 16, name.data(), name.size() - 1)) std::cout << " type=" << name.data();
                }
            }
            break;
        }
        std::cout << '\n';
    }
    return 0;
}

extern "C" { __declspec(dllexport) volatile LONG ProbeFixtureHeartbeat = 0; }
volatile LONG fixtureCalls = 0;
extern "C" __declspec(dllexport) __declspec(noinline) int ProbeFixtureEntry(const void* constants, const void* frame, const void* viewport)
{
    (void)frame; (void)viewport;
    InterlockedIncrement(&fixtureCalls); // Observable side effect prevents optimized-away fixture calls.
    return static_cast<const unsigned char*>(constants)[0];
}
int Fixture(bool active, HANDLE stop)
{
    std::array<unsigned char, 444> bytes{};
    std::memcpy(bytes.data() + 8, ConstantsGuid.data(), ConstantsGuid.size());
    const uint64_t version = 2; std::memcpy(bytes.data() + 24, &version, 8);
    const std::array<float, 16> camera{1,2,3, 0,1,0, 1,0,0, 0,0,1, 0.2f,10000,0.8726646f,1.777777f};
    std::memcpy(bytes.data() + 376, camera.data(), sizeof(camera));
    while (WaitForSingleObject(stop, 10) == WAIT_TIMEOUT)
    {
        InterlockedIncrement(&ProbeFixtureHeartbeat);
        if (active) ProbeFixtureEntry(bytes.data(), nullptr, nullptr);
    }
    CloseHandle(stop); return 0;
}
void SelfTest(bool active, bool writeWatch = false)
{
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), nullptr, TRUE};
    Handle stop(CreateEventW(&attributes, TRUE, FALSE, nullptr));
    Require(stop.value != nullptr, "Fixture stop event");
    std::array<wchar_t, 32768> executable{};
    Require(GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size())) > 0, "Self path");
    std::wstring command = L"\"" + std::wstring(executable.data()) + L"\" --fixture " + (active ? L"active " : L"quiet ") + std::to_wstring(reinterpret_cast<uint64_t>(stop.value));
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION pi{};
    Require(CreateProcessW(executable.data(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &pi) != FALSE, "Start fixture");
    Handle process(pi.hProcess), thread(pi.hThread);
    try
    {
        WaitForInputIdle(process.value, 100); Sleep(100);
        const auto modules = Modules(pi.dwProcessId);
        const auto module = FindModule(modules, std::filesystem::path(executable.data()).filename().wstring());
        const auto entry = Export(process.value, module, writeWatch ? "ProbeFixtureHeartbeat" : "ProbeFixtureEntry");
        Session session(pi.dwProcessId, process.value, entry, writeWatch);
        const auto report = session.Run(0.8);
        std::cout << "fixture=" << (active ? "active" : "quiet") << " calls=" << report.calls << " known=" << report.known << " valid=" << report.valid << " armed=" << report.armed << " detached=" << report.detached << '\n';
        Require(report.detached && (writeWatch ? report.calls >= 3 : active ? report.calls >= 3 && report.valid == report.calls : report.calls == 0), "Fixture call capture");
        BOOL debugger = TRUE;
        Require(CheckRemoteDebuggerPresent(process.value, &debugger) && !debugger, "Debugger detached");
        Require(SuspendThread(thread.value) != static_cast<DWORD>(-1), "Suspend fixture to verify cleanup");
        CONTEXT clean{}; clean.ContextFlags = CONTEXT_DEBUG_REGISTERS;
        const bool registersRead = GetThreadContext(thread.value, &clean) != FALSE;
        ResumeThread(thread.value);
        Require(registersRead && !(clean.Dr7 & 0xff), "All fixture hardware breakpoints removed");
        const auto heartbeatAddress = Export(process.value, module, "ProbeFixtureHeartbeat");
        const auto before = ReadValue<LONG>(process.value, heartbeatAddress); Sleep(60);
        Require(ReadValue<LONG>(process.value, heartbeatAddress) > before, "Target continues after probe");
        SetEvent(stop.value);
        Require(WaitForSingleObject(process.value, 2000) == WAIT_OBJECT_0, "Fixture exits normally");
        DWORD exitCode = 1; GetExitCodeProcess(process.value, &exitCode);
        Require(exitCode == 0, "Fixture exit code");
        std::cout << "PASS " << (writeWatch ? "data-write watch" : active ? "active calls + constants ABI" : "no calls timeout") << "; detached and target continues\n";
    }
    catch (...) { SetEvent(stop.value); WaitForSingleObject(process.value, 2000); throw; }
}
int wmain(int argc, wchar_t** argv)
{
    try
    {
        if (argc == 4 && std::wstring(argv[1]) == L"--fixture") return Fixture(std::wstring(argv[2]) == L"active", reinterpret_cast<HANDLE>(std::stoull(argv[3])));
        if (argc == 2 && std::wstring(argv[1]) == L"--self-test") { SourceMappingTest(); EngineResolverTest(); SelfTest(true); SelfTest(false); SelfTest(false, true); return 0; }
        if (argc == 5 && std::wstring(argv[1]) == L"--read-engine")
        {
            const auto pid = std::stoull(argv[2]); Require(pid > 0 && pid <= MAXDWORD, "Engine PID");
            return ReadSource(static_cast<DWORD>(pid), 0, std::stod(argv[3]), argv[4], true);
        }
        if (argc >= 4 && argc <= 10 && (std::wstring(argv[1]) == L"--find-module-refs" || std::wstring(argv[1]) == L"--find-rip-refs"))
        {
            const auto pid = std::stoull(argv[2]); Require(pid > 0 && pid <= MAXDWORD, "Reference PID");
            std::vector<uint64_t> targets;
            for (int i = 3; i < argc; ++i) targets.push_back(std::stoull(argv[i], nullptr, 0));
            return ModuleReferences(static_cast<DWORD>(pid), targets, std::wstring(argv[1]) == L"--find-rip-refs");
        }
        if (argc == 6 && (std::wstring(argv[1]) == L"--watch-write" || std::wstring(argv[1]) == L"--watch-exec"))
        {
            const bool writeWatch = std::wstring(argv[1]) == L"--watch-write";
            const auto pidValue = std::stoull(argv[2]), address = std::stoull(argv[3], nullptr, 0);
            const double seconds = std::stod(argv[4]);
            Require(pidValue > 0 && pidValue <= MAXDWORD && address >= 0x10000 && address < 0x00007fffffff0000ull && (!writeWatch || address % 4 == 0) && std::isfinite(seconds) && seconds >= 1 && seconds <= 10, "Bounded watch arguments");
            const DWORD pid = static_cast<DWORD>(pidValue);
            Handle process(OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | SYNCHRONIZE, FALSE, pid));
            Require(process.value != nullptr, "Open write-watch target read-only");
            const auto modules = Modules(pid);
            Require(!modules.empty() && _wcsicmp(modules.front().name.c_str(), L"CrimsonDesert.exe") == 0, "CrimsonDesert target only");
            Require(writeWatch || (address >= modules.front().base && address - modules.front().base < modules.front().size), "Execution watch must be inside game module");
            (void)ReadValue<DWORD>(process.value, address);
            Handle output(CreateFileW(argv[5], GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
            Require(output.value != INVALID_HANDLE_VALUE, "Create NEW write-watch report");
            SetConsoleCtrlHandler(OnConsoleEvent, TRUE);
            Session session(pid, process.value, address, writeWatch);
            const auto report = session.Run(seconds);
            const auto json = Serialize(report, pid, address, modules, writeWatch); DWORD written = 0;
            Require(WriteFile(output.value, json.data(), static_cast<DWORD>(json.size()), &written, nullptr) && written == json.size(), "Write write-watch report");
            std::cout << "hits=" << report.calls << " threads=" << report.armed << " detached=" << report.detached << '\n';
            return report.detached ? 0 : 1;
        }
        if (argc == 4 && std::wstring(argv[1]) == L"--function")
        {
            const auto pid = std::stoull(argv[2]); Require(pid > 0 && pid <= MAXDWORD, "Function PID");
            return FunctionRange(static_cast<DWORD>(pid), std::stoull(argv[3], nullptr, 0));
        }
        if (argc == 4 && std::wstring(argv[1]) == L"--peek")
        {
            const auto pid = std::stoull(argv[2]); Require(pid > 0 && pid <= MAXDWORD, "Peek PID");
            return Peek(static_cast<DWORD>(pid), std::stoull(argv[3], nullptr, 0));
        }
        if (argc == 6 && std::wstring(argv[1]) == L"--read-source")
        {
            const auto pid = std::stoull(argv[2]); Require(pid > 0 && pid <= MAXDWORD, "Source PID");
            return ReadSource(static_cast<DWORD>(pid), std::stoull(argv[3], nullptr, 0), std::stod(argv[4]), argv[5]);
        }
        if (argc != 4) { std::cerr << "Usage: StreamlineProbe <CrimsonDesert PID> <seconds 1..10> <new output.jsonl>\n"; return 2; }
        const auto pidValue = std::stoull(argv[1]); const auto seconds = std::stod(argv[2]);
        Require(pidValue > 0 && pidValue <= MAXDWORD && std::isfinite(seconds) && seconds >= 1 && seconds <= 10, "Bounded arguments");
        const auto pid = static_cast<DWORD>(pidValue);
        Handle output(CreateFileW(argv[3], GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
        Require(output.value != INVALID_HANDLE_VALUE, "Create NEW diagnostic output (existing files refused)");
        Handle process(OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | SYNCHRONIZE, FALSE, pid));
        Require(process.value != nullptr, "Open target read-only");
        const auto modules = Modules(pid);
        Require(!modules.empty() && _wcsicmp(modules.front().name.c_str(), L"CrimsonDesert.exe") == 0, "CrimsonDesert target only");
        const auto module = FindModule(modules, L"sl.interposer.dll");
        const auto entry = Export(process.value, module, "slSetConstants");
        SetConsoleCtrlHandler(OnConsoleEvent, TRUE);
        std::cout << "Observing slSetConstants at " << Hex(entry) << " for " << seconds << " seconds. No code patch or game-data writes.\n" << std::flush;
        Session session(pid, process.value, entry);
        const auto report = session.Run(seconds);
        const auto json = Serialize(report, pid, entry, modules);
        DWORD written = 0;
        Require(WriteFile(output.value, json.data(), static_cast<DWORD>(json.size()), &written, nullptr) && written == json.size(), "Write diagnostic report");
        std::cout << "calls=" << report.calls << " knownConstants=" << report.known << " validCameras=" << report.valid << " threads=" << report.armed << " detached=" << report.detached << '\n';
        return report.detached ? 0 : 1;
    }
    catch (const std::exception& error) { std::cerr << "ERROR: " << error.what() << '\n'; return 1; }
}
