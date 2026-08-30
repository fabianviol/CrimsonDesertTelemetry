#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
HMODULE g_module = nullptr;
HANDLE g_stopEvent = nullptr;

std::filesystem::path ModuleDirectory()
{
    std::vector<wchar_t> buffer(512);
    for (;;)
    {
        const DWORD length = GetModuleFileNameW(g_module, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) return {};
        if (length < buffer.size() - 1)
            return std::filesystem::path(std::wstring(buffer.data(), length)).parent_path();
        buffer.resize(buffer.size() * 2);
    }
}

std::string Utf8(const std::wstring& value)
{
    if (value.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        nullptr, 0, nullptr, nullptr);
    if (count <= 0) return {};
    std::string result(static_cast<size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        result.data(), count, nullptr, nullptr);
    return result;
}

void Log(const std::wstring& message)
{
    const auto directory = ModuleDirectory();
    if (directory.empty()) return;
    const auto path = directory / L"CrimsonDesertTelemetry.bootstrap.log";
    HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;

    SYSTEMTIME time{};
    GetLocalTime(&time);
    const auto prefix = std::format(L"{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03} ",
        time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
    const auto line = Utf8(prefix + message + L"\r\n");
    DWORD written = 0;
    WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
    CloseHandle(file);
}

std::filesystem::path FindDotnet()
{
    std::array<wchar_t, 32768> programFiles{};
    const DWORD length = GetEnvironmentVariableW(L"ProgramFiles", programFiles.data(),
        static_cast<DWORD>(programFiles.size()));
    if (length > 0 && length < programFiles.size())
    {
        const auto candidate = std::filesystem::path(programFiles.data()) / L"dotnet" / L"dotnet.exe";
        if (std::filesystem::is_regular_file(candidate)) return candidate;
    }

    std::array<wchar_t, 32768> resolved{};
    const DWORD found = SearchPathW(nullptr, L"dotnet.exe", nullptr,
        static_cast<DWORD>(resolved.size()), resolved.data(), nullptr);
    if (found > 0 && found < resolved.size()) return std::filesystem::path(resolved.data());
    return {};
}

bool IsServerReady(const unsigned short port)
{
    SOCKET socketHandle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socketHandle == INVALID_SOCKET) return false;

    const DWORD timeoutMilliseconds = 500;
    setsockopt(socketHandle, SOL_SOCKET, SO_RCVTIMEO,
        reinterpret_cast<const char*>(&timeoutMilliseconds), sizeof(timeoutMilliseconds));
    setsockopt(socketHandle, SOL_SOCKET, SO_SNDTIMEO,
        reinterpret_cast<const char*>(&timeoutMilliseconds), sizeof(timeoutMilliseconds));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(socketHandle, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR)
    {
        closesocket(socketHandle);
        return false;
    }

    constexpr char request[] =
        "GET /v1/health HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
    if (send(socketHandle, request, static_cast<int>(sizeof(request) - 1), 0) == SOCKET_ERROR)
    {
        closesocket(socketHandle);
        return false;
    }

    std::array<char, 256> response{};
    const int received = recv(socketHandle, response.data(), static_cast<int>(response.size() - 1), 0);
    closesocket(socketHandle);
    if (received <= 0) return false;
    const std::string_view firstBytes(response.data(), static_cast<size_t>(received));
    return firstBytes.starts_with("HTTP/1.1 200") || firstBytes.starts_with("HTTP/1.0 200");
}

std::wstring Quote(const std::filesystem::path& value)
{
    return L"\"" + value.wstring() + L"\"";
}

bool ConfigureKillOnCloseJob(HANDLE job)
{
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION information{};
    information.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    return SetInformationJobObject(job, JobObjectExtendedLimitInformation,
        &information, sizeof(information)) != FALSE;
}

std::filesystem::path CacheRuntimeConfig(const std::filesystem::path& source)
{
    // hostfxr normalizes --runtimeconfig to a .json filename. DMM scans those
    // filenames as mods, so materialize only this small metadata file in the
    // user's application cache, never in the game or manager library.
    HANDLE file = CreateFileW(source.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot read runtime configuration.");
    const DWORD size = GetFileSize(file, nullptr);
    if (size == INVALID_FILE_SIZE || size == 0 || size > 65536)
    {
        CloseHandle(file);
        throw std::runtime_error("Invalid runtime configuration size.");
    }
    std::vector<unsigned char> bytes(size);
    DWORD read = 0;
    const bool readOk = ReadFile(file, bytes.data(), size, &read, nullptr) != FALSE && read == size;
    CloseHandle(file);
    if (!readOk) throw std::runtime_error("Cannot read complete runtime configuration.");

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        throw std::runtime_error("Cannot initialize runtime configuration hash.");
    std::array<unsigned char, 32> digest{};
    const auto hashStatus = BCryptHash(algorithm, nullptr, 0, bytes.data(), size,
        digest.data(), static_cast<ULONG>(digest.size()));
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (hashStatus < 0) throw std::runtime_error("Cannot hash runtime configuration.");
    std::wstring key;
    for (const auto value : digest) key += std::format(L"{:02x}", value);

    std::array<wchar_t, 32768> localAppData{};
    const auto length = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData.data(),
        static_cast<DWORD>(localAppData.size()));
    if (length == 0 || length >= localAppData.size())
        throw std::runtime_error("LOCALAPPDATA is unavailable.");
    const auto cacheDirectory = std::filesystem::path(localAppData.data()) /
        L"CrimsonDesertTelemetry" / L"Runtime";
    std::filesystem::create_directories(cacheDirectory);
    const auto cached = cacheDirectory / (L"runtime-" + key + L".json");
    file = CreateFileW(cached.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        if (GetLastError() != ERROR_FILE_EXISTS)
            throw std::runtime_error("Cannot create cached runtime configuration.");
        // Do not trust a stale or altered cache file simply because its name matches.
        file = CreateFileW(cached.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot verify cached runtime configuration.");
        std::vector<unsigned char> existing(size);
        DWORD existingRead = 0;
        const bool matches = GetFileSize(file, nullptr) == size &&
            ReadFile(file, existing.data(), size, &existingRead, nullptr) &&
            existingRead == size && existing == bytes;
        CloseHandle(file);
        if (!matches) throw std::runtime_error("Cached runtime configuration differs from the package.");
    }
    else
    {
        DWORD written = 0;
        const bool writtenOk = WriteFile(file, bytes.data(), size, &written, nullptr) && written == size;
        CloseHandle(file);
        if (!writtenOk)
        {
            DeleteFileW(cached.c_str());
            throw std::runtime_error("Cannot write complete cached runtime configuration.");
        }
    }
    return cached;
}

DWORD RunBootstrap()
{
    const auto directory = ModuleDirectory();
    if (directory.empty()) return 1;
    const auto iniPath = directory / L"CrimsonDesertTelemetry.ini";
    const int enabled = GetPrivateProfileIntW(L"Server", L"Enabled", 1, iniPath.c_str());
    const int requestedPort = GetPrivateProfileIntW(L"Server", L"Port", 27311, iniPath.c_str());
    const int requestedRate = GetPrivateProfileIntW(L"Server", L"SampleRateHz", 60, iniPath.c_str());
    const unsigned short port = static_cast<unsigned short>(std::clamp(requestedPort, 1024, 65535));
    const int sampleRate = std::clamp(requestedRate, 1, 240);

    if (enabled == 0)
    {
        Log(L"Bootstrap disabled by CrimsonDesertTelemetry.ini.");
        return 0;
    }

    WSADATA sockets{};
    if (WSAStartup(MAKEWORD(2, 2), &sockets) != 0)
    {
        Log(L"Could not initialize Winsock.");
        return 2;
    }
    if (IsServerReady(port))
    {
        Log(std::format(L"Telemetry server already available on 127.0.0.1:{}.", port));
        WSACleanup();
        return 0;
    }

    const auto dotnet = FindDotnet();
    if (dotnet.empty())
    {
        Log(L".NET 8 was not found. Install the .NET 8 ASP.NET Core Runtime (x64).");
        WSACleanup();
        return 3;
    }
    const auto host = directory / L"crimson-desert-telemetry.dll";
    const auto dependencies = directory / L"crimson-desert-telemetry.deps.cfg";
    const auto runtimeConfig = directory / L"crimson-desert-telemetry.runtimeconfig.cfg";
    if (!std::filesystem::is_regular_file(host) ||
        !std::filesystem::is_regular_file(dependencies) ||
        !std::filesystem::is_regular_file(runtimeConfig))
    {
        Log(L"Missing host DLL or .deps.cfg/.runtimeconfig.cfg companion file; reinstall the complete package.");
        WSACleanup();
        return 4;
    }
    std::filesystem::path cachedRuntimeConfig;
    try { cachedRuntimeConfig = CacheRuntimeConfig(runtimeConfig); }
    catch (const std::exception& error)
    {
        const std::string message = error.what();
        Log(L"Runtime configuration preparation failed: " + std::wstring(message.begin(), message.end()));
        WSACleanup();
        return 6;
    }

    const auto hostLogPath = directory / L"CrimsonDesertTelemetry.host.log";
    HANDLE hostLog = CreateFileW(hostLogPath.c_str(), FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hostLog == INVALID_HANDLE_VALUE) hostLog = nullptr;

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    if (hostLog != nullptr)
    {
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdOutput = hostLog;
        startup.hStdError = hostLog;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    }

    // Mod managers interpret loose .json files as game patches. Keep the .NET
    // dependency metadata as .cfg; hostfxr's runtime JSON lives outside the library.
    std::wstring commandLine = Quote(dotnet) + L" exec --depsfile " + Quote(dependencies) +
        L" --runtimeconfig " + Quote(cachedRuntimeConfig) + L" " + Quote(host) + L" serve " +
        std::to_wstring(port) + L" " + std::to_wstring(sampleRate);
    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');
    PROCESS_INFORMATION process{};
    const BOOL started = CreateProcessW(dotnet.c_str(), mutableCommand.data(), nullptr, nullptr,
        hostLog != nullptr, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, nullptr,
        directory.c_str(), &startup, &process);
    if (hostLog != nullptr) CloseHandle(hostLog);
    if (!started)
    {
        Log(std::format(L"Could not start telemetry host (Win32 error {}).", GetLastError()));
        WSACleanup();
        return 5;
    }
    CloseHandle(process.hThread);

    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job != nullptr && (!ConfigureKillOnCloseJob(job) || !AssignProcessToJobObject(job, process.hProcess)))
    {
        Log(std::format(L"Warning: could not attach host to lifetime job (Win32 error {}).", GetLastError()));
        CloseHandle(job);
        job = nullptr;
    }
    Log(std::format(L"Started telemetry host PID {} on 127.0.0.1:{} at {} Hz.",
        process.dwProcessId, port, sampleRate));

    const std::array<HANDLE, 2> waits{g_stopEvent, process.hProcess};
    const DWORD waitResult = WaitForMultipleObjects(static_cast<DWORD>(waits.size()), waits.data(), FALSE, INFINITE);
    if (waitResult == WAIT_OBJECT_0)
        Log(L"Game process is closing; stopping telemetry host.");
    else if (waitResult == WAIT_OBJECT_0 + 1)
    {
        DWORD exitCode = 0;
        GetExitCodeProcess(process.hProcess, &exitCode);
        Log(std::format(L"Telemetry host exited with code {}.", exitCode));
    }

    if (job != nullptr) CloseHandle(job);
    CloseHandle(process.hProcess);
    WSACleanup();
    return 0;
}

DWORD WINAPI BootstrapThread(void*)
{
    try { return RunBootstrap(); }
    catch (...)
    {
        // Configuration/filesystem errors in the optional bootstrap must not
        // escape the thread entry point and terminate the game process.
        try { Log(L"Bootstrap stopped after an unexpected configuration or filesystem error."); }
        catch (...) { }
        return 7;
    }
}
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_module = instance;
        DisableThreadLibraryCalls(instance);
        g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (g_stopEvent == nullptr) return FALSE;
        HANDLE thread = CreateThread(nullptr, 0, BootstrapThread, nullptr, 0, nullptr);
        if (thread == nullptr)
        {
            CloseHandle(g_stopEvent);
            g_stopEvent = nullptr;
            return FALSE;
        }
        CloseHandle(thread);
    }
    else if (reason == DLL_PROCESS_DETACH && g_stopEvent != nullptr)
    {
        SetEvent(g_stopEvent);
    }
    return TRUE;
}
