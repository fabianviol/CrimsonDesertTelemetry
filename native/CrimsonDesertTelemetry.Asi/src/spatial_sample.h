#pragma once
#include <cstddef>
#include <cstdint>

namespace cdt::spatial
{
// Native port of the offline sky-visibility model. Same layout assumption, same
// float rounding and the same linear-WRAP sampler, so a native result can be
// compared byte-for-byte against the Python decoder on preserved captures.
// This computes a CANDIDATE engine sky-visibility factor. It is not irradiance,
// room brightness, a fraction of visible sky or per-source occlusion.
inline constexpr unsigned VolumeWidth = 64, VolumeHeight = 32, VolumeDepth = 264;
inline constexpr size_t VolumeBytes = size_t{VolumeWidth} * VolumeHeight * VolumeDepth;
inline constexpr size_t ConstantBytes = 768;

enum class SampleStatus
{
    Ok,                 // texture-sample branch
    Fallback,           // shader's fallback-one branch; skyVisibility is 1 by definition
    InvalidConstants,   // inverse extent or coordinates out of contract
    InvalidClipmap,     // a clipmap scale outside its bound
    NoVolume,
};

struct SpatialSample
{
    SampleStatus status = SampleStatus::InvalidConstants;
    int clipmap = -1;
    double world[3]{};
    double coordinates[3]{};
    double sampled{};       // trilinear R8_UNORM value
    double skyVisibility{}; // saturate(1 - sampled)
};

// Reference position and clipmap selection from 768 bytes of GI constants.
SpatialSample DecodeReference(const uint8_t* constants);

// Samples at the reference position itself, using the constants' own normalized
// coordinates. This is the exact path; prefer it when no offset is wanted.
SpatialSample SampleAtReference(const uint8_t* constants, const uint8_t* volume);

// Samples at an arbitrary world position, which is how a directional read works.
// The clipmap selected for the reference is REUSED rather than recomputed, since
// selection depends on constants that describe the reference. That holds for small
// offsets well inside the clipmap and is not valid for arbitrary distances.
// Neighbouring offsets can also fall in different amortised update blocks, so
// results from several offsets are not necessarily of the same age.
SpatialSample SampleAtWorld(const uint8_t* constants, const uint8_t* volume, const double world[3]);
}
