# Security policy

Please do not include exploitable details, memory dumps, credentials, or personal data in a public issue. Once the GitHub repository is published, report security-sensitive problems through GitHub's private security-advisory feature.

The external host intentionally opens the game process with query and read permissions only. The optional ASI bootstrap is loaded by a user-installed ASI loader for automatic host startup/lifetime management. Its separately configurable HUD uses graphics-presentation hooks when enabled, but does not modify gameplay values. The HUD is disabled by default; no HUD hooks are installed in that configuration. Gameplay-memory writes, arbitrary code injection features, anti-cheat bypasses, and competitive advantages are outside the supported product's scope.

The mod-manager package stores .NET metadata as .cfg companions to avoid game-patch JSON detection. Only the small runtime configuration is materialized in the user's application cache, keyed by SHA-256 and verified before reuse. No game data or executable payload is extracted there.

Unknown executable hashes fail closed. If a supported build definition appears to resolve incorrect data, stop the tool and report the build metadata plus non-sensitive diagnostic output.
