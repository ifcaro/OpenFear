#pragma once

#include "openfear/platform/IRenderer.h"
#include <string>

namespace openfear {

// Short, human-readable memory diagnostic (platform-specific content and
// format, possibly with embedded '\n' to fit on-screen), empty where no
// such stats exist. Safe to draw on-screen for a crash report - unlike
// stderr, visible even with no console attached.
std::string FormatMemoryStats(IRenderer& renderer);
void LogMemoryStats(const char* tag, IRenderer& renderer);
void InstallRuntimeDiagnostics();
void PS2NewHandler();

}  // namespace openfear
