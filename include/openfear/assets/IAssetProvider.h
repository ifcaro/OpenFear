#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace openfear {

// Opaque handle to a streaming asset read. 0 is never valid.
using AssetHandle = uintptr_t;

// Seek origin, matches SEEK_SET/SEEK_CUR/SEEK_END — its own enum so this
// header doesn't need <cstdio>.
enum class AssetSeekOrigin { Set, Cur, End };

// Abstraction boundary for reading a virtual asset path's bytes. Game
// code never calls fopen/std::ifstream directly - always through an
// IAssetProvider, so the same code runs against a local folder, a
// browser-picked source, or an ISO without knowing which.
//
// Two access patterns:
//   - Whole-file reads (bitmaps, WAVs): ReadFile().
//   - Streaming reads (video: open once, many small seek+read calls):
//     Open/Read/Seek/Close, mirroring a C FILE*.
class IAssetProvider {
public:
    virtual ~IAssetProvider() = default;

    virtual bool Exists(const std::string& path) = 0;

    // Reads the entire file into memory. Returns an empty vector on
    // failure — callers that need to distinguish "empty file" from
    // "not found" should check Exists() first.
    virtual std::vector<uint8_t> ReadFile(const std::string& path) = 0;

    // Opens a streaming handle for repeated Read/Seek calls. Returns 0 on
    // failure.
    virtual AssetHandle Open(const std::string& path) = 0;

    // Reads up to n bytes into buf. Returns the number of bytes actually
    // read (may be less than n at EOF).
    virtual size_t Read(AssetHandle handle, void* buf, size_t n) = 0;

    virtual bool Seek(AssetHandle handle, int64_t offset, AssetSeekOrigin origin) = 0;

    // Current stream position, or -1 on failure.
    virtual int64_t Tell(AssetHandle handle) = 0;

    virtual void Close(AssetHandle handle) = 0;
};

}  // namespace openfear
