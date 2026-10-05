#include "test_framework.h"
#include "openfear/assets/NativeAssetProvider.h"
#include <filesystem>
#include <fstream>

using namespace openfear;
namespace fs = std::filesystem;

namespace {

// Raw, un-installed CD layout - matches the real disc's own structure
// (loose .BIN files under INSTALL/, reclassified to DATA/ by CdPathRemap).
fs::path MakeRawCdLayoutDir() {
    const fs::path root = fs::temp_directory_path() / "openfear_test_rawcd";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "INSTALL", ec);
    std::ofstream(root / "INSTALL" / "ATMOS.EXE", std::ios::binary) << "x";
    std::ofstream(root / "INSTALL" / "PLAYER.BIN", std::ios::binary) << "player-data";
    return root;
}

fs::path MakeInstalledLayoutDir() {
    const fs::path root = fs::temp_directory_path() / "openfear_test_installed";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "DATA", ec);
    std::ofstream(root / "DATA" / "PLAYER.BIN", std::ios::binary) << "player-data";
    return root;
}

}  // namespace

void RegisterNativeAssetProviderTests(Tests& tests) {
    tests.AddCase(
        "NativeAssetProvider: raw CD layout - DATA/PLAYER.BIN resolves to INSTALL/PLAYER.BIN",
        []() {
            NativeAssetProvider provider(MakeRawCdLayoutDir());
            ASSERT_TRUE(provider.Exists("DATA/PLAYER.BIN"));
            const auto data = provider.ReadFile("DATA/PLAYER.BIN");
            const std::string text(data.begin(), data.end());
            ASSERT_TRUE(text == "player-data");
        });

    tests.AddCase("NativeAssetProvider: raw CD layout - unmapped virtual path still misses", []() {
        NativeAssetProvider provider(MakeRawCdLayoutDir());
        ASSERT_FALSE(provider.Exists("DATA/NOPE.BIN"));
    });

    tests.AddCase("NativeAssetProvider: already-installed layout - no remap needed", []() {
        NativeAssetProvider provider(MakeInstalledLayoutDir());
        ASSERT_TRUE(provider.Exists("DATA/PLAYER.BIN"));
    });
}
