// Stand-ins for the private spatial research probe, compiled instead of it when
// CDT_RESEARCH is OFF. Release packages are built that way: the probe's GPU
// readback, barrier observation and sphere tracing are diagnostics that no
// released feature reaches, and shipping them puts ~200 KB of hooking and
// copying machinery into a binary that never runs it.
//
// Nothing is deleted. src/spatial_probe.cpp and its siblings stay in the tree,
// their tests keep building and running against the real sources, and a
// research build (the default) is unchanged.
//
// These must stay signature-compatible with spatial_probe.h. Start() returning
// false is the same answer the real probe gives when it refuses to initialise,
// and instruments.cpp already handles that path.
#include "spatial_probe.h"

namespace cdt::spatial
{
bool Start(uint64_t, const wchar_t*, bool, unsigned, unsigned, unsigned, bool, bool, bool)
{
    return false;
}

void Poll() {}

void Stop() {}

// No detour is installed, so no address in this process belongs to the probe.
bool OwnsCodeAddress(uint64_t)
{
    return false;
}
}
