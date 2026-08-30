# Changelog

## 0.1.0 - Unreleased

- Added a preview ASI bootstrap and mod-manager packaging for the existing read-only host.
- Fixed DMM misclassifying .NET dependency/runtime JSON as mod patches: the manager
  package now uses .cfg companions selected explicitly by dotnet exec.
- Added exact package-content and embedded-resource regression checks.
- Added read-only support for Crimson Desert Steam build `24994088`.
- Added version-bound static player-position discovery.
- Added address-independent render-camera structural discovery.
- Added redundant-copy consensus and automatic rediscovery.
- Added the versioned community JSON contract and JSON Lines CLI output.
- Added loopback HTTP snapshot/health/schema endpoints and a WebSocket stream.
- Added configurable 1–240 Hz shared sampling and automatic game-process reconnects.
- Classified the observed origin/height-1000 startup sentinel family, including floating-point noise,
  as loading state.
- Added offline validation and local API integration tests.
