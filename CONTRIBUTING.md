# Contributing

Contributions are welcome, especially independent validation on other machines and support for newer Crimson Desert builds.

## Before opening a pull request

1. Describe the game distribution, Steam build ID, executable version, and SHA-256.
2. Explain the controlled experiment used to distinguish the value from look-alikes.
3. Validate any resolver after a complete game exit and restart.
4. Add or update a definition under `definitions/` without replacing support for older builds.
5. Add offline tests for new parsing, validation, or selection behavior.
6. Run the Release build and test commands from the README.
7. Record external research sources in `docs/PROVENANCE.md`.

Never submit game binaries, decompiled game code, private keys, access tokens, complete memory dumps, or personal save files. Small synthetic byte fixtures are welcome. Contributions that write to the game, bypass anti-cheat systems, or enable competitive cheating are out of scope.

Keep public schema changes backward-compatible whenever possible. New optional fields are preferred over renaming or removing existing fields.

By submitting a contribution, you agree that it may be distributed under this repository's MIT License and confirm that you have the right to contribute it.
