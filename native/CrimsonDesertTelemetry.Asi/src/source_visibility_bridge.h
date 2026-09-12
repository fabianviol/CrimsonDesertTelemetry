#pragma once
#include <windows.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include "sdf_visibility.h"

namespace cdt::source_visibility
{
inline constexpr std::uint32_t QueryMagic = 0x51564443; // CDVQ
inline constexpr std::uint32_t ResultMagic = 0x52564443; // CDVR
inline constexpr std::uint32_t Version = 1;
inline constexpr std::uint32_t HeaderBytes = 128;
inline constexpr std::uint32_t MaximumTargets = sdf::MaximumBatchTargets;

struct QueryEntry
{
    std::uint32_t id{};
    std::array<float, 3> position{};
};
static_assert(sizeof(QueryEntry) == 16);

struct alignas(8) QueryHeader
{
    std::uint32_t magic{}, version{}, headerBytes{}, totalBytes{};
    volatile LONG64 seqlock{};
    std::uint32_t pid{}, targetCount{};
    std::uint64_t processStartFileTime{}, querySequence{}, publishedTickMs{};
    std::array<float, 3> receiver{};
    std::uint32_t reserved0{};
    std::uint8_t reserved[56]{};
};
static_assert(sizeof(QueryHeader) == HeaderBytes);

struct QueryMapping
{
    QueryHeader header;
    QueryEntry entries[MaximumTargets];
};

enum class State : std::uint32_t { Waiting, Active, Fault, Stopped };
struct ResultEntry
{
    std::uint32_t id{}, code{};
    float closest{};
    std::array<float, 3> position{};
};
static_assert(sizeof(ResultEntry) == 24);

struct alignas(8) ResultHeader
{
    std::uint32_t magic{}, version{}, headerBytes{}, totalBytes{};
    volatile LONG64 seqlock{};
    std::uint32_t pid{};
    State state{State::Waiting};
    std::uint64_t processStartFileTime{}, querySequence{}, publishedTickMs{}, requestTickMs{};
    std::uint64_t volumeSequence{}, volumeTickMs{};
    std::uint32_t contextFrame{}, targetCount{};
    std::array<float, 3> receiver{};
    std::uint32_t error{};
    std::uint8_t reserved[24]{};
};
static_assert(sizeof(ResultHeader) == HeaderBytes);

struct ResultMapping
{
    ResultHeader header;
    ResultEntry entries[MaximumTargets];
};

static_assert(sizeof(QueryMapping) == HeaderBytes + MaximumTargets * sizeof(QueryEntry));
static_assert(sizeof(ResultMapping) == HeaderBytes + MaximumTargets * sizeof(ResultEntry));
static_assert(offsetof(QueryHeader, seqlock) == 16 && offsetof(QueryHeader, receiver) == 56);
static_assert(offsetof(ResultHeader, seqlock) == 16 && offsetof(ResultHeader, receiver) == 88);

bool Open();
void Poll();
void Close() noexcept;
}
