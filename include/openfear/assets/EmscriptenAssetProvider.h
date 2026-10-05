#pragma once
#include "openfear/assets/IAssetProvider.h"
#include "openfear/assets/CdLayout.h"
#include <memory>
#include <string>
#include <unordered_map>

namespace openfear {

// Browser IAssetProvider backend — see apps/openfear/web/asset_provider.js
// for the JS half. No game assets are bundled; the user picks their own
// folder at runtime (File System Access API, or <input webkitdirectory>
// fallback), read on demand via File.slice()/arrayBuffer().
//
// Only usable under Emscripten (uses EM_JS/EM_ASYNC_JS internally).
class EmscriptenAssetProvider : public IAssetProvider {
public:
    // Shows a "click to start" gate (satisfies the audio-autoplay-gesture
    // requirement), then from that same click prompts for the assets
    // folder. Call once before any other method, from an
    // Asyncify-compatible context (main(), before the game loop starts).
    // Returns false if cancelled or the folder was empty.
    bool StartGate();

    bool Exists(const std::string& path) override;
    std::vector<uint8_t> ReadFile(const std::string& path) override;

    AssetHandle Open(const std::string& path) override;
    size_t Read(AssetHandle handle, void* buf, size_t n) override;
    bool Seek(AssetHandle handle, int64_t offset, AssetSeekOrigin origin) override;
    int64_t Tell(AssetHandle handle) override;
    void Close(AssetHandle handle) override;

private:
    // Virtual path -> raw picked-folder path, built once in StartGate().
    // Empty when the picked folder is already in the installed layout;
    // ResolveRaw() then passes the path straight through.
    assets::CdPathRemap remap_;
    std::string ResolveRaw(const std::string& virtual_path) const;

    // Open() buffers the whole file in one JS round-trip; Read/Seek/Tell
    // then work on that in-memory copy with no further JS calls. Video
    // header parsing does many small seek+read calls per file, and each
    // as its own async round-trip made opening a video take seconds.
    struct StreamHandle {
        std::string path;
        std::vector<uint8_t> data;
        int64_t pos = 0;
    };
    std::unordered_map<StreamHandle*, std::unique_ptr<StreamHandle>> handles_;
};

}  // namespace openfear
