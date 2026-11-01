#pragma once
#include <windows.h>
#include <string>

// Logging + crash capture. Everything lands next to the ASI in the game root:
//   RiffedMenu.log        - plain-text log (flushed every line, tailed by build_and_run.sh)
//   RiffedMenu-crash.dmp  - minidump written by the unhandled-exception filter

void LogInit(HMODULE self);
void Log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void InstallCrashHandlers();

const std::string& GameDir();   // game root, with trailing backslash
