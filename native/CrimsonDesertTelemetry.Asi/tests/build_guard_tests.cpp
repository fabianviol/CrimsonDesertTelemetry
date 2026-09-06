#include <array>
#include <iostream>
#include <string>
#include <string_view>
#include "build_guard.h"

namespace cdt::instruments::test
{
std::array<unsigned char, 32> HashBytes(const unsigned char*, size_t, size_t);
bool HashFile(const wchar_t*, std::array<unsigned char, 32>&);
}

static std::string Hex(const std::array<unsigned char, 32>& digest)
{
    std::string result;
    for (auto byte : digest)
    {
        result += "0123456789abcdef"[byte >> 4];
        result += "0123456789abcdef"[byte & 15];
    }
    return result;
}

int wmain(int argc, wchar_t** argv)
{
    struct Vector { std::string_view input; std::string_view expected; };
    const Vector vectors[]{
        {"", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
        {"abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
        {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"},
        {"abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu",
            "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1"}
    };
    for (const auto& vector : vectors)
        for (const size_t chunk : {size_t(0),size_t(1),size_t(7),size_t(55),size_t(56),size_t(63),size_t(64),size_t(65),size_t(65536)})
            if (Hex(cdt::instruments::test::HashBytes(reinterpret_cast<const unsigned char*>(vector.input.data()),
                vector.input.size(), chunk)) != vector.expected)
            {
                std::cerr << "SHA256 vector/stream boundary mismatch: " << vector.input.size() << "/" << chunk << '\n';
                return 1;
            }
    const std::string million(1000000, 'a');
    if (Hex(cdt::instruments::test::HashBytes(reinterpret_cast<const unsigned char*>(million.data()), million.size(), 65536)) !=
        "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0") return 2;
    // A test executable must never be accepted as the supported game.
    if (cdt::instruments::VerifyExecutable()) return 3;
    std::array<unsigned char, 32> digest{};
    if (cdt::instruments::test::HashFile(L"?:\\nonexistent-game.exe", digest)) return 4;
    if (argc == 2)
    {
        if (!cdt::instruments::test::HashFile(argv[1], digest) || Hex(digest) !=
            "4d99c15c58bd20a94d354d10ae395d1fac777d59ef52cba8080dc3fc8dc6f454") return 5;
        std::cout << "PASS actual supported game executable SHA256\n";
    }
    std::cout << "PASS SHA256 known vectors, streaming boundaries, million bytes, executable rejection\n";
    return 0;
}
