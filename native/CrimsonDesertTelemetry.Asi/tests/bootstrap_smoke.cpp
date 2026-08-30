#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

namespace
{
std::string HttpGet(const unsigned short port, const std::string_view path)
{
    SOCKET socketHandle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socketHandle == INVALID_SOCKET) return {};
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
        return {};
    }
    const std::string request = "GET " + std::string(path) +
        " HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
    if (send(socketHandle, request.data(), static_cast<int>(request.size()), 0) != static_cast<int>(request.size()))
    {
        closesocket(socketHandle);
        return {};
    }
    std::array<char, 4096> buffer{};
    std::string response;
    while (response.size() < 32768)
    {
        const int received = recv(socketHandle, buffer.data(), static_cast<int>(buffer.size()), 0);
        if (received <= 0) break;
        response.append(buffer.data(), static_cast<size_t>(received));
    }
    closesocket(socketHandle);
    return response;
}
}

int wmain(const int argc, wchar_t** argv)
{
    if (argc != 2)
    {
        std::wcerr << L"Usage: CrimsonDesertTelemetryBootstrapSmoke <path-to-asi>\n";
        return 2;
    }
    WSADATA sockets{};
    if (WSAStartup(MAKEWORD(2, 2), &sockets) != 0) return 3;

    const auto directory = std::filesystem::absolute(argv[1]).parent_path();
    const auto ini = directory / L"CrimsonDesertTelemetry.ini";
    const auto port = static_cast<unsigned short>(GetPrivateProfileIntW(L"Server", L"Port", 27311, ini.c_str()));
    if (std::filesystem::exists(directory / L"crimson-desert-telemetry.deps.json") ||
        std::filesystem::exists(directory / L"crimson-desert-telemetry.runtimeconfig.json"))
    {
        std::wcerr << L"The smoke test requires a JSON-free manager package.\n";
        WSACleanup();
        return 6;
    }
    if (!HttpGet(port, "/v1/health").empty())
    {
        std::wcerr << L"Port already in use; refusing to mistake an existing server for this test.\n";
        WSACleanup();
        return 7;
    }

    HMODULE module = LoadLibraryW(argv[1]);
    if (module == nullptr)
    {
        std::wcerr << L"LoadLibrary failed: " << GetLastError() << L"\n";
        WSACleanup();
        return 4;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (std::chrono::steady_clock::now() < deadline)
    {
        const auto health = HttpGet(port, "/v1/health");
        if (health.starts_with("HTTP/1.1 200") && health.find("\"schemaVersion\":\"1.0\"") != std::string::npos)
        {
            const auto schema = HttpGet(port, "/v1/schema");
            if (!schema.starts_with("HTTP/1.1 200") ||
                schema.find("Crimson Desert Telemetry snapshot v1") == std::string::npos)
            {
                std::wcerr << L"Embedded schema endpoint failed.\n";
                WSACleanup();
                return 8;
            }
            std::wcout << L"PASS ASI bootstrap started the host using .cfg metadata; health and schema respond.\n";
            WSACleanup();
            return 0;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    std::wcerr << L"Telemetry server did not become ready within 15 seconds.\n";
    WSACleanup();
    return 5;
}
