# Security policy

Please do not include exploitable details, memory dumps, credentials, or personal data in a public issue. Use GitHub's private vulnerability-reporting feature when available for security-sensitive reports; do not publish sensitive evidence if that channel is unavailable.

The external host opens the game process with query and read permissions only. The unified ASI starts that host and uses guarded native code/D3D12 hooks for light capture and in-game UI. The supplied configuration enables lights, both HUD views and status notices. Disable `Overlay`, `LightOverlay` and `Notifications` to skip all UI hooks; disable `Lights.ManyLights` separately to stop native light capture. Hiding a view is not the same as disabling its hooks.

The optional research console is disabled by default and can change game debug values when enabled. The package therefore must not be described as entirely read-only. No public gameplay-control API, arbitrary injection service, anti-cheat bypass or competitive-cheating feature is supported. The HTTP/WebSocket server binds only to loopback; do not expose it through an untrusted proxy.

The mod-manager package stores .NET metadata as .cfg companions to avoid game-patch JSON detection. Only the small runtime configuration is materialized in the user's application cache, keyed by SHA-256 and verified before reuse. No game data or executable payload is extracted there.

Unknown executable hashes fail closed for native instrumentation. Historical basic telemetry has a separate guarded compatibility path; that does not approve the current camera/light layout. If a supported build definition appears to resolve incorrect data, stop the tool and report the build metadata plus non-sensitive diagnostic output. Error notices are best-effort if graphics initialization itself fails; retain the bootstrap, native and host logs.
