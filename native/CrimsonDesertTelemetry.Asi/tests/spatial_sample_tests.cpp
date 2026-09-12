// Synthetic controls for the native sky-visibility sampler. No game data here:
// bit-exactness against real captures is checked separately by the --vector mode,
// which reads preserved artifacts that deliberately stay out of the repository.
#include "../src/spatial_sample.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace cdt::spatial;
unsigned checks{};
void Check(bool ok, const char* what)
{
    ++checks;
    if (!ok) { std::cerr << "FAIL " << checks << ' ' << what << '\n'; std::exit(1); }
}
void Near(double actual, double expected, double tolerance, const char* what)
{
    if (std::fabs(actual - expected) > tolerance)
        std::cerr << actual << " != " << expected << ' ';
    Check(std::fabs(actual - expected) <= tolerance, what);
}

struct Constants
{
    std::vector<uint8_t> bytes = std::vector<uint8_t>(ConstantBytes, 0);
    void Set(size_t offset, unsigned index, float value)
    {
        std::memcpy(bytes.data() + offset + index * sizeof(float), &value, sizeof(value));
    }
    void Inverse(float x, float y, float z) { Set(0x10, 0, x); Set(0x10, 1, y); Set(0x10, 2, z); }
    void Wrapped(float x, float y, float z) { Set(0x130, 0, x); Set(0x130, 1, y); Set(0x130, 2, z); }
    void Uv(float x, float y, float z) { Set(0x2E0, 0, x); Set(0x2E0, 1, y); Set(0x2E0, 2, z); }
    void Clip(unsigned level, float ox, float oy, float oz, float scale, float rx, float ry, float rz)
    {
        Set(0x140 + level * 16, 0, ox); Set(0x140 + level * 16, 1, oy);
        Set(0x140 + level * 16, 2, oz); Set(0x140 + level * 16, 3, scale);
        Set(0x240 + level * 16, 0, rx); Set(0x240 + level * 16, 1, ry);
        Set(0x240 + level * 16, 2, rz);
    }
};

Constants Valid()
{
    Constants c;
    c.Inverse(0.03125f, 0.0625f, 0.03125f);   // the live clipmap-1 extents
    c.Wrapped(0, 0, 0);
    c.Uv(0.25f, 0.5f, 0.125f);
    c.Clip(1, 0, 0, 0, 2.0f, 0, 0, 0);        // cell 0 is inside +-63/31/63
    return c;
}

int RunVector(const std::string& directory)
{
    auto read = [&](const char* name, size_t expected) {
        std::ifstream in(directory + "/" + name, std::ios::binary);
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (data.size() != expected)
        {
            std::cerr << name << " size " << data.size() << " expected " << expected << '\n';
            std::exit(2);
        }
        return data;
    };
    const auto constants = read("constants.bin", ConstantBytes);
    const auto volume = read("volume.bin", VolumeBytes);
    std::ifstream offsets(directory + "/offsets.txt");
    const auto reference = SampleAtReference(constants.data(), volume.data());
    std::printf("reference %d %d %.17g %.17g %.17g %.17g\n", static_cast<int>(reference.status),
        reference.clipmap, reference.world[0], reference.world[1], reference.world[2],
        reference.skyVisibility);
    double world[3];
    while (offsets >> world[0] >> world[1] >> world[2])
    {
        const auto at = SampleAtWorld(constants.data(), volume.data(), world);
        std::printf("world %.17g %.17g %.17g -> %d %.17g\n", world[0], world[1], world[2],
            static_cast<int>(at.status), at.skyVisibility);
    }
    return 0;
}

int main(int argc, char** argv)
{
    if (argc == 3 && std::string(argv[1]) == "--vector") return RunVector(argv[2]);

    const auto constants = Valid();
    std::vector<uint8_t> volume(VolumeBytes, 0);

    // Reference decode: world is the normalized coordinate divided by the extent.
    auto decoded = DecodeReference(constants.bytes.data());
    Check(decoded.status == SampleStatus::Ok && decoded.clipmap == 1, "clipmap 1 selected");
    Near(decoded.world[0], 0.25 / 0.03125, 1e-12, "world x");
    Near(decoded.world[1], 0.5 / 0.0625, 1e-12, "world y");
    Near(decoded.world[2], 0.125 / 0.03125, 1e-12, "world z");

    // A constant volume samples to that constant, so the weights sum to one.
    std::fill(volume.begin(), volume.end(), uint8_t{64});
    auto flat = SampleAtReference(constants.bytes.data(), volume.data());
    Check(flat.status == SampleStatus::Ok, "flat sample ok");
    Near(flat.sampled, 64.0 / 255.0, 1e-12, "constant volume samples its constant");
    Near(flat.skyVisibility, 1.0 - 64.0 / 255.0, 1e-12, "saturate(1-sample)");

    // Sampling the reference through the world path must agree with the exact path.
    auto viaWorld = SampleAtWorld(constants.bytes.data(), volume.data(), decoded.world);
    Near(viaWorld.sampled, flat.sampled, 1e-12, "world path reproduces the reference path");

    // The z coordinate is forced into the selected clipmap's slab, so uv.z = 0
    // lands halfway between slices 66 and 67. Markers must live in both.
    auto mark = [&volume](unsigned x, unsigned y, uint8_t value) {
        for (unsigned z : {66u, 67u}) volume[(z * VolumeHeight + y) * VolumeWidth + x] = value;
    };
    const float centredY = static_cast<float>(0.5 / VolumeHeight * 2);

    // Trilinear weights: halfway between texel 0 and 1 reads half of each.
    std::fill(volume.begin(), volume.end(), uint8_t{0});
    mark(1, 0, 255);
    Constants between = Valid();
    between.Uv(static_cast<float>(1.0 / VolumeWidth * 2), centredY, 0);
    auto half = SampleAtReference(between.bytes.data(), volume.data());
    Check(half.status == SampleStatus::Ok, "midpoint sample ok");
    Near(half.sampled, 0.5, 1e-12, "midpoint between two texels weights both equally");

    // Wrapping: a coordinate just below zero must address the far edge, not clamp.
    std::fill(volume.begin(), volume.end(), uint8_t{0});
    mark(VolumeWidth - 1, 0, 255);
    Constants negative = Valid();
    negative.Uv(-0.0001f, centredY, 0);
    auto wrapped = SampleAtReference(negative.bytes.data(), volume.data());
    Check(wrapped.status == SampleStatus::Ok && wrapped.sampled > 0.4,
          "negative coordinates wrap to the far edge instead of clamping");
    Constants clampProbe = Valid();
    clampProbe.Uv(static_cast<float>(2.0 / VolumeWidth * 2), centredY, 0);
    Check(SampleAtReference(clampProbe.bytes.data(), volume.data()).sampled == 0.0,
          "an interior coordinate does not see the wrapped edge");

    // No clipmap covers the cell: the shader's fallback yields 1 WITHOUT coverage.
    Constants uncovered = Valid();
    uncovered.Wrapped(1000, 1000, 1000);
    for (unsigned level = 1; level < 8; ++level) uncovered.Clip(level, 0, 0, 0, 2.0f, 0, 0, 0);
    auto fell = DecodeReference(uncovered.bytes.data());
    Check(fell.status == SampleStatus::Fallback && fell.skyVisibility == 1.0,
          "fallback reports one without coverage");
    Check(SampleAtReference(uncovered.bytes.data(), volume.data()).status == SampleStatus::Fallback,
          "fallback never samples the texture");

    // Guards.
    Constants zeroInverse = Valid();
    zeroInverse.Inverse(0, 0.0625f, 0.03125f);
    Check(DecodeReference(zeroInverse.bytes.data()).status == SampleStatus::InvalidConstants,
          "zero inverse extent rejected");
    Constants hugeInverse = Valid();
    hugeInverse.Inverse(1.5f, 0.0625f, 0.03125f);
    Check(DecodeReference(hugeInverse.bytes.data()).status == SampleStatus::InvalidConstants,
          "inverse extent above one rejected");
    Constants badScale = Valid();
    badScale.Clip(1, 0, 0, 0, 0.0f, 0, 0, 0);
    Check(DecodeReference(badScale.bytes.data()).status == SampleStatus::InvalidClipmap,
          "zero clipmap scale rejected");
    Check(DecodeReference(nullptr).status == SampleStatus::InvalidConstants, "null constants rejected");
    Check(SampleAtReference(constants.bytes.data(), nullptr).status == SampleStatus::NoVolume,
          "missing volume reported, not sampled");

    // Offsets move the sample, which is what a directional read depends on.
    std::fill(volume.begin(), volume.end(), uint8_t{0});
    for (unsigned z = 0; z < VolumeDepth; ++z)
        for (unsigned y = 0; y < VolumeHeight; ++y)
            for (unsigned x = 0; x < VolumeWidth; ++x)
                volume[(z * VolumeHeight + y) * VolumeWidth + x] = static_cast<uint8_t>(x * 4);
    auto base = SampleAtReference(constants.bytes.data(), volume.data());
    double shifted[3]{decoded.world[0] + 8.0, decoded.world[1], decoded.world[2]};
    auto moved = SampleAtWorld(constants.bytes.data(), volume.data(), shifted);
    Check(base.status == SampleStatus::Ok && moved.status == SampleStatus::Ok, "gradient samples ok");
    Check(std::fabs(moved.sampled - base.sampled) > 1e-6, "an offset changes the sampled value");
    Check(moved.clipmap == base.clipmap, "an offset reuses the reference clipmap");

    // A real readback has aligned rows. Distinct values in every dimension and
    // poisoned padding expose treating that allocation as a packed R8 volume.
    constexpr size_t paddedRowPitch = 256;
    std::vector<uint8_t> padded(paddedRowPitch * VolumeHeight * VolumeDepth, uint8_t{255});
    for (unsigned z = 0; z < VolumeDepth; ++z)
        for (unsigned y = 0; y < VolumeHeight; ++y)
            for (unsigned x = 0; x < VolumeWidth; ++x)
            {
                const auto value = static_cast<uint8_t>((x * 7 + y * 13 + z * 19) % 251);
                volume[(z * VolumeHeight + y) * VolumeWidth + x] = value;
                padded[(z * VolumeHeight + y) * paddedRowPitch + x] = value;
            }
    const auto packedReference = SampleAtReference(constants.bytes.data(), volume.data());
    const auto paddedReference = SampleAtReference(constants.bytes.data(), padded.data(), paddedRowPitch);
    Check(packedReference.status == SampleStatus::Ok && paddedReference.status == SampleStatus::Ok,
          "packed and padded reference samples valid");
    Near(paddedReference.sampled, packedReference.sampled, 0.0,
         "aligned rows preserve the exact reference sample");
    Check(std::fabs(SampleAtReference(constants.bytes.data(), padded.data()).sampled -
                    packedReference.sampled) > 1e-6,
          "poisoned fixture detects incorrectly assuming packed rows");

    Constants paddedWrap = Valid();
    paddedWrap.Uv(-0.0001f, -0.0001f, -0.125f);
    const auto packedWrapped = SampleAtReference(paddedWrap.bytes.data(), volume.data());
    const auto paddedWrapped = SampleAtReference(paddedWrap.bytes.data(), padded.data(), paddedRowPitch);
    Check(packedWrapped.status == SampleStatus::Ok && paddedWrapped.status == SampleStatus::Ok,
          "negative coordinates valid with aligned rows");
    Near(paddedWrapped.sampled, packedWrapped.sampled, 0.0,
         "aligned rows preserve wrapping across x and y edges");

    const double shiftedAll[3]{decoded.world[0] + 3.0, decoded.world[1] - 5.0, decoded.world[2] + 7.0};
    const auto packedShifted = SampleAtWorld(constants.bytes.data(), volume.data(), shiftedAll);
    const auto paddedShifted = SampleAtWorld(constants.bytes.data(), padded.data(), shiftedAll, paddedRowPitch);
    Check(packedShifted.status == SampleStatus::Ok && paddedShifted.status == SampleStatus::Ok,
          "shifted world samples valid with aligned rows");
    Near(paddedShifted.sampled, packedShifted.sampled, 0.0,
         "aligned rows preserve samples shifted in all dimensions");
    Check(std::fabs(packedShifted.sampled - packedReference.sampled) > 1e-6,
          "nonuniform fixture distinguishes shifted world sample");
    Check(SampleAtReference(constants.bytes.data(), padded.data(), VolumeWidth - 1).status != SampleStatus::Ok,
          "reference rejects a row pitch narrower than the volume");
    Check(SampleAtWorld(constants.bytes.data(), padded.data(), shiftedAll, VolumeWidth - 1).status != SampleStatus::Ok,
          "world sample rejects a row pitch narrower than the volume");

    // All coordinates needed for the reference live beyond the first 256 bytes.
    // Finite, plausible values in the header and a separate scene snapshot must
    // not substitute for this part of the complete CPU GI block.
    Constants fullGi = Valid();
    fullGi.Wrapped(4, 5, -3);
    fullGi.Uv(31.25f, 62.5f, -15.625f);
    fullGi.Clip(1, 2000, 2000, -1000, 2, 1992, 1990, -994);
    const float headerWrapped[4]{4000, 4000, -2000, 4};
    const float headerOrigin[4]{1000, 1000, -500, 1};
    for (unsigned axis = 0; axis < 4; ++axis)
    {
        fullGi.Set(0x30, axis, headerWrapped[axis]);
        fullGi.Set(0x50, axis, headerOrigin[axis]);
    }
    const auto fullReference = DecodeReference(fullGi.bytes.data());
    Check(fullReference.status == SampleStatus::Ok && fullReference.clipmap == 1,
          "complete GI block selects the covered nonzero reference");
    Near(fullReference.world[0], 1000, 0.0, "complete GI reference x");
    Near(fullReference.world[1], 1000, 0.0, "complete GI reference y");
    Near(fullReference.world[2], -500, 0.0, "complete GI reference z");
    std::vector<uint8_t> unrelatedScene(512);
    const float sceneValue = 1.0f;
    for (size_t offset = 0; offset < unrelatedScene.size(); offset += sizeof(float))
        std::memcpy(unrelatedScene.data() + offset, &sceneValue, sizeof(sceneValue));
    std::vector<uint8_t> malformedConstants(1024);
    std::memcpy(malformedConstants.data(), fullGi.bytes.data(), 256);
    std::memcpy(malformedConstants.data() + 256, fullGi.bytes.data(), 256);
    std::memcpy(malformedConstants.data() + 512, unrelatedScene.data(), unrelatedScene.size());
    Check(DecodeReference(malformedConstants.data()).status == SampleStatus::InvalidClipmap,
          "duplicated GI header plus scene corrupts a covered reference");

    std::cout << "PASS " << checks << " native spatial sampler controls. Synthetic, no game proof.\n";
}
