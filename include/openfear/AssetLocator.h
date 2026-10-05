#pragma once
#include <filesystem>
#include <optional>
#include <string>

namespace openfear {

namespace fs = std::filesystem;

// Discovers the Atmosfear asset root directory through multiple strategies.
//
// A valid root is an installed folder (MAPFILE + ATMOS.EXE at root), a
// raw/un-installed CD layout (MAPFILE at root + INSTALL/ATMOS.EXE), or a
// loose .iso file standing in for either - see is_valid_root(). Expected
// layout, all from the original CD:
//   DATA/     — *.BIN and *.PNT hotspot/coordinate data
//   CHARS/    — character portrait BMPs
//   M1/, M2/  — UI background BMPs
//   MISC/     — misc background BMPs
//   WAV/      — game-board audio
//   W1/-W11/  — setup and gatekeeper audio
//   AV1/-AV3/, AVI/, CR/ — AVI video sequences
//
// Discovery order:
//   1. CLI argument:        openfear --assets <path>
//   2. Environment var:     OPENFEAR_ASSETS=<path>
//   3. User config file:    %APPDATA%\OpenFear\openfear.cfg  (Windows)
//                           ~/.config/openfear/openfear.cfg  (Linux/macOS)
//   4. CD/DVD drive scan:   detects the original disc by known marker files
//   5. Local assets folder: <current working directory>/assets/, or the
//                           working directory itself
class AssetLocator {
public:
    // Runs full discovery. argc/argv are forwarded to check --assets.
    // Returns the asset root, or nullopt if not found.
    static std::optional<fs::path> discover(int argc, char* argv[]);

    // Saves a confirmed path to the user config file so future launches
    // skip discovery.
    static bool save_config(const fs::path& asset_root);

    // Loads only from the user config file (no full discovery).
    static std::optional<fs::path> load_config();

    // Platform-specific user config directory:
    //   Windows : %APPDATA%\OpenFear\
    //   Linux   : ~/.config/openfear/
    //   macOS   : ~/Library/Application Support/OpenFear/
    static fs::path config_dir();

    // True if path is an installed folder (MAPFILE + ATMOS.EXE at root), a
    // raw CD layout (MAPFILE at root + INSTALL/ATMOS.EXE), or a loose .iso
    // file standing in for either.
    static bool is_valid_root(const fs::path& path);

private:
    static std::optional<fs::path> from_cli(int argc, char* argv[]);
    static std::optional<fs::path> from_env();
    static std::optional<fs::path> from_local();
    static std::optional<fs::path> from_cd_scan();
};

// Stateless utility: builds bare virtual asset paths. Static (no
// instance needed) and pure - never touches the real asset root (see
// assets::AssetManager::asset_root()).
struct AssetPaths {
    // subdir/NAME.ext, uppercased — a bare virtual path, never joined
    // with any root here: the real asset root is only ever known to
    // whichever IAssetProvider is in use, never to gameplay code.
    // (DATA, CHARS, WAV, ...)
    static fs::path data(const std::string& subdir, const std::string& name,
                         const std::string& ext);

    // subdir/NAME.avi, uppercased, same as data(). (AV1, AV2, AV3, AVI, CR)
    static fs::path avi(const std::string& subdir, const std::string& name);
};

}  // namespace openfear
