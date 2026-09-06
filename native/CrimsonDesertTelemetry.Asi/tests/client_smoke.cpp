#include <winsock2.h>
#include <ws2tcpip.h>
#include "overlay.h"
#include <bcrypt.h>
#include <wincrypt.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <format>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace cdt::overlay;
namespace
{
void Require(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
struct Socket
{
    SOCKET value = INVALID_SOCKET;
    ~Socket() { if (value != INVALID_SOCKET) { shutdown(value, SD_BOTH); closesocket(value); } }
};
struct Client
{
    HANDLE stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::thread worker;
    ~Client() { SetEvent(stop); if (worker.joinable()) worker.join(); CloseHandle(stop); }
};
void Send(SOCKET socket, const std::string& bytes)
{
    size_t sent{};
    while (sent < bytes.size())
    {
        const int count = send(socket, bytes.data() + sent, static_cast<int>(bytes.size() - sent), 0);
        Require(count > 0, "Socket send");
        sent += count;
    }
}
void Frame(SOCKET socket, const std::string& payload, unsigned char type = 0x81)
{
    std::string frame(1, static_cast<char>(type));
    if (payload.size() < 126) frame.push_back(static_cast<char>(payload.size()));
    else if (payload.size() <= 65535)
    {
        frame.push_back(126);
        frame.push_back(static_cast<char>((payload.size() >> 8) & 255));
        frame.push_back(static_cast<char>(payload.size() & 255));
    }
    else
    {
        frame.push_back(127);
        const auto length = static_cast<std::uint64_t>(payload.size());
        for (int shift = 56; shift >= 0; shift -= 8)
            frame.push_back(static_cast<char>((length >> shift) & 255));
    }
    Send(socket, frame + payload);
}
SOCKET Accept(SOCKET listener)
{
    fd_set readable{}; FD_ZERO(&readable); FD_SET(listener, &readable);
    timeval timeout{5, 0};
    Require(select(0, &readable, nullptr, nullptr, &timeout) > 0, "Client did not connect");
    const SOCKET peer = accept(listener, nullptr, nullptr);
    Require(peer != INVALID_SOCKET, "Socket accept");
    const DWORD milliseconds = 3000;
    setsockopt(peer, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&milliseconds), sizeof(milliseconds));
    return peer;
}
void Upgrade(SOCKET socket)
{
    std::string request;
    std::array<char, 4096> buffer{};
    while (request.find("\r\n\r\n") == std::string::npos)
    {
        const int count = recv(socket, buffer.data(), static_cast<int>(buffer.size()), 0);
        Require(count > 0 && request.size() < 8192, "No valid upgrade request");
        request.append(buffer.data(), count);
    }
    std::string lower = request;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const auto offset = lower.find("sec-websocket-key:");
    Require(offset != std::string::npos, "Missing WebSocket key");
    const auto start = request.find_first_not_of(' ', offset + 18);
    const auto key = request.substr(start, request.find("\r\n", start) - start) + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::array<unsigned char, 20> hash{};
    Require(BCryptHash(BCRYPT_SHA1_ALG_HANDLE, nullptr, 0, reinterpret_cast<PUCHAR>(const_cast<char*>(key.data())),
        static_cast<ULONG>(key.size()), hash.data(), static_cast<ULONG>(hash.size())) >= 0, "WebSocket SHA1");
    std::array<char, 128> base64{}; DWORD length = static_cast<DWORD>(base64.size());
    Require(CryptBinaryToStringA(hash.data(), static_cast<DWORD>(hash.size()), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
        base64.data(), &length), "WebSocket base64");
    Send(socket, std::string("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ")
        + base64.data() + "\r\n\r\n");
}
nlohmann::json MakeSample(int sequence, const char* state)
{
    SYSTEMTIME time{}; GetSystemTime(&time);
    return {{"schemaVersion", "1.1"}, {"sequence", sequence},
        {"capturedAt", std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}.{:03}Z", time.wYear, time.wMonth, time.wDay,
            time.wHour, time.wMinute, time.wSecond, time.wMilliseconds)},
        {"game", {{"build", "test"}, {"state", state}}},
        {"coordinateSystem", {{"unit", "game-unit"}, {"upAxis", "y"}, {"handedness", "right"}}},
        {"player", {{"position", {{"x", 1}, {"y", 2}, {"z", 3}}}, {"orientation", nullptr}}},
        {"camera", nullptr}, {"quality", nullptr}};
}
template<class Predicate> View Await(Predicate predicate)
{
    const auto deadline = Clock::now() + std::chrono::seconds(4);
    View view;
    do
    {
        if (TryRead(view) && predicate(view)) return view;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while (Clock::now() < deadline);
    throw std::runtime_error("Client state did not arrive");
}
}
int main()
{
    try
    {
        WSADATA sockets{}; Require(WSAStartup(MAKEWORD(2, 2), &sockets) == 0, "Winsock startup");
        Socket listener; listener.value = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in address{}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        Require(bind(listener.value, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0, "Loopback bind");
        int length = sizeof(address); Require(getsockname(listener.value, reinterpret_cast<sockaddr*>(&address), &length) == 0, "Port query");
        Require(listen(listener.value, 2) == 0, "Listen");
        Config config; config.port = ntohs(address.sin_port);
        Client client;
        Require(client.stop != nullptr, "Stop event creation");
        // Must return without connecting even though the stop event is unsignaled.
        RunClient(config, client.stop);
        fd_set pending{}; FD_ZERO(&pending); FD_SET(listener.value, &pending);
        timeval immediate{};
        Require(select(0, &pending, nullptr, nullptr, &immediate) == 0, "Disabled HUD opened a connection");
        std::cout << "PASS disabled HUD skips WebSocket connection\n";
        // Marker-only startup must connect without the HUD or notices enabled.
        config.lightOverlay = true;
        client.worker = std::thread([&] { RunClient(config, client.stop); });
        {
            Socket peer; peer.value = Accept(listener.value); Upgrade(peer.value);
            const auto playing = MakeSample(1, "playing").dump();
            // Real WebSocket fragmentation must not publish an incomplete JSON object.
            Frame(peer.value, playing.substr(0, 30), 0x01);
            Frame(peer.value, playing.substr(30), 0x80);
            const auto live = Await([](const View& v) { return v.hasSample && v.sample.sequence == 1; });
            Require(IsLive(live, Clock::now(), 1000) && live.sample.playerPosition.has_value(), "Live fragmented sample");
            auto lightSample = MakeSample(2, "playing");
            lightSample["schemaVersion"] = "1.4";
            auto sources = nlohmann::json::array();
            for (int i = 0; i < 1200; ++i)
                sources.push_back({{"sampleIndex", i}, {"position", {{"x", i * .01}, {"y", 3}, {"z", 10}}},
                    {"colorLinear", {{"x", .8}, {"y", .2}, {"z", .05}}}, {"luminanceLinear", .317}, {"kind", "point"}});
            lightSample["lights"] = {{"status", "available"}, {"sources", nlohmann::json::array()},
                {"rendered", {{"status", "available"}, {"ageMilliseconds", 20}, {"sources", sources}}}};
            const auto lightJson = lightSample.dump();
            Require(lightJson.size() > 65536, "Large light payload fixture too small");
            Frame(peer.value, lightJson.substr(0, 50000), 0x01);
            Frame(peer.value, lightJson.substr(50000), 0x80);
            const auto lights = Await([](const View& v) { return v.hasSample && v.sample.sequence == 2; });
            Require(lights.sample.renderedLights.records && lights.sample.renderedLights.records->size() == 1200,
                "Large fragmented light payload missing");
            View same; Require(TryRead(same) && same.sample.renderedLights.records == lights.sample.renderedLights.records,
                "Render-thread View copies must share immutable light storage");
            Frame(peer.value, MakeSample(3, "loading").dump());
            const auto loading = Await([](const View& v) { return v.hasSample && v.sample.sequence == 3; });
            Require(!loading.sample.playerPosition && !loading.sample.renderedLights.records, "Loading retained coordinates/lights");
            std::cout << "PASS marker-only WebSocket, >64KiB fragmented lights, shared storage and loading invalidation\n";
        }
        Await([](const View& v) { return !v.connected; });
        {
            Socket peer; peer.value = Accept(listener.value); Upgrade(peer.value);
            Frame(peer.value, MakeSample(1, "playing").dump());
            Await([](const View& v) { return v.connected && v.hasSample && v.sample.sequence == 1; });
            Frame(peer.value, "{broken-json");
            Await([](const View& v) { return !v.connected && v.connection == "INVALID / INCOMPATIBLE DATA"; });
            SetEvent(client.stop);
            std::cout << "PASS disconnect, reconnect with reset sequence and malformed-data rejection\n";
        }
        return 0;
    }
    catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
