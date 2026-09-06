# Integrated research instruments

Imported on 2026-09-06 from the project's `research/console-enabler/src`, the
CrimsonHueConsole 0.3.17 research line. Original sources and Git history remain
in the research repository. The obsolete XInput proxy `proxy.cpp` and export
forwarders are deliberately excluded; `bootstrap.cpp` is the only DllMain.

The research files describe a source reimplementation based on analysis of
Nexus mod 803. No DLL from that mod, game binary, shader asset or external
repository source was imported here. Existing comments and attribution are
preserved. This provenance record does not invent an upstream license grant.

Integration changes use the unified INI/log names, disable research features by
default and prevent manual ManyLights/bpcapture from overwriting the recurring
capture detour. Future research fixes should be ported consciously; this copy
is the product source and the research copy remains historical evidence.
