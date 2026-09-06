#pragma once
#include "common.h"

namespace ch { namespace console {

HWND FindGameWindow();
bool HookWndProc(HWND hwnd);
void UnhookWndProc();

// Bound to the ~ key.
void Toggle();
// Bound to F11.
void Diagnostics();

// Types `text` into the game's console by posting keystrokes to its window:
// Home, Shift+End, Delete, then one WM_CHAR per character, then Return.
// Requires the console to be open and focused; there is no API call for this.
bool ExecConsoleCommand(const char* text);

}}  // namespace ch::console

