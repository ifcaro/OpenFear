#include "RuntimeDiagnostics.h"

namespace openfear {

std::string FormatMemoryStats(IRenderer&) {
    return "";
}
void LogMemoryStats(const char*, IRenderer&) {}
void InstallRuntimeDiagnostics() {}
void PS2NewHandler() {}

}  // namespace openfear
