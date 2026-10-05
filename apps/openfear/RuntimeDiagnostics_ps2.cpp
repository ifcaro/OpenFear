#include "RuntimeDiagnostics.h"

#include <cstdio>
#include <malloc.h>
#include <new>
#include <debug.h>

namespace openfear {

std::string FormatMemoryStats(IRenderer& renderer) {
    struct mallinfo mi = mallinfo();
    // Split across 2 lines - the full one-line version can run past 640px
    // on-screen (DrawText doesn't wrap).
    char buf[160];
    std::snprintf(buf, sizeof(buf),
                  "arena=%uKB used=%uKB free=%uKB\nfree_chunks=%u avg=%uB tex=%zu",
                  (unsigned)(mi.arena / 1024), (unsigned)(mi.uordblks / 1024),
                  (unsigned)(mi.fordblks / 1024), (unsigned)mi.ordblks,
                  mi.ordblks ? (unsigned)(mi.fordblks / mi.ordblks) : 0, renderer.textureCount());
    return buf;
}

void LogMemoryStats(const char* tag, IRenderer& renderer) {
    std::fprintf(stderr, "[MEM] %-20s %s\n", tag, FormatMemoryStats(renderer).c_str());
}

void PS2NewHandler() {
    struct mallinfo mi = mallinfo();
    std::fprintf(
        stderr, "[ALLOC FAIL] arena=%uKB used=%uKB free=%uKB free_chunks=%u avg_free_chunk=%uB\n",
        (unsigned)(mi.arena / 1024), (unsigned)(mi.uordblks / 1024), (unsigned)(mi.fordblks / 1024),
        (unsigned)mi.ordblks, mi.ordblks ? (unsigned)(mi.fordblks / mi.ordblks) : 0);
    unsigned int trace[8] = {};
    ps2GetStackTrace(trace, 8);
    for (int i = 0; i < 8 && trace[i]; ++i) {
        std::fprintf(stderr, "[ALLOC FAIL]   #%d %08x\n", i, trace[i]);
    }
    std::fflush(stderr);
    throw std::bad_alloc();
}

void InstallRuntimeDiagnostics() {
    std::set_new_handler(PS2NewHandler);
}

}  // namespace openfear
