#pragma once

#include "openfear/assets/IAssetProvider.h"
#include "openfear/assets/PathNormalize.h"
#include <map>
#include <string>

namespace openfear::testing {

// Reports every file as "not found" by default. BoardGameScreen::load()
// hard-fails on most assets routed through this provider (missing/wrong-
// size data stops load() outright, same as FULLMAP.AVI, handled separately
// via FakeVideoPlayer) - PLAYER.BIN is the one confirmed exception,
// cosmetic-only (key icons just don't render).
//
// A test that needs load() to succeed must register synthetic versions of
// everything it reads - see RegisterBoardLoadDefaults()
// (fakes/RegisterBoardLoadDefaults.h) for the current full list and the
// builders it uses - own fixture data, never the real copyrighted game
// files.
class FakeAssetProvider : public IAssetProvider {
public:
    // Normalizes slashes/case the same way real providers do (see
    // PathNormalize.h) - a path built via AssetPaths::data() (fs::path,
    // backslash on Windows) matches one registered as a hand-written
    // forward-slash literal, and vice versa.
    bool Exists(const std::string& path) override {
        return files_.count(NormalizeVirtualPath(path)) != 0;
    }

    std::vector<uint8_t> ReadFile(const std::string& path) override {
        auto it = files_.find(NormalizeVirtualPath(path));
        return it != files_.end() ? it->second : std::vector<uint8_t>{};
    }

    AssetHandle Open(const std::string&) override { return 0; }
    size_t Read(AssetHandle, void*, size_t) override { return 0; }
    bool Seek(AssetHandle, int64_t, AssetSeekOrigin) override { return false; }
    int64_t Tell(AssetHandle) override { return -1; }
    void Close(AssetHandle) override {}

    // Registers synthetic bytes for an exact path — subsequent
    // Exists()/ReadFile() calls for that path serve this data.
    void SetFile(const std::string& path, std::vector<uint8_t> bytes) {
        files_[NormalizeVirtualPath(path)] = std::move(bytes);
    }

private:
    std::map<std::string, std::vector<uint8_t>> files_;
};

}  // namespace openfear::testing
