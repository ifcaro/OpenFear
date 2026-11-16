#include "openfear/AssetLocator.h"
#include "openfear/assets/Iso9660Reader.h"
#include "openfear/assets/IsoFileSectorSource.h"
#include "openfear/assets/PathNormalize.h"
#include <cctype>
#include <fstream>
#include <cstdlib>
#include <string>
#include <cstring>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

#if defined(__PS2__)
#  include "openfear/assets/Ps2AssetProvider.h"
#endif

namespace openfear {

// ---------------------------------------------------------------------------
// AssetPaths
// ---------------------------------------------------------------------------

fs::path AssetPaths::data(const std::string& subdir, const std::string& name,
                          const std::string& ext) {
    // Bare virtual path, not joined with root - the real root is only ever
    // known to whichever IAssetProvider is in use, never to gameplay code.
    // An .iso root IS the file itself, not a directory to descend into,
    // so there is nothing meaningful to join here.
    fs::path p = fs::path(NormalizeVirtualPath(subdir)) / NormalizeVirtualPath(name);
    if (!ext.empty()) p += "." + NormalizeVirtualPath(ext);
    return p;
}

fs::path AssetPaths::avi(const std::string& subdir, const std::string& name) {
    return fs::path(NormalizeVirtualPath(subdir)) / (NormalizeVirtualPath(name) + ".AVI");
}

// ---------------------------------------------------------------------------
// AssetLocator — validation
// ---------------------------------------------------------------------------

namespace {
// A real open, not fs::exists()/stat(): a driver can report a file as
// present via stat() while its own open() then refuses to serve it (a
// real driver quirk, not a path or suffix issue). Opening the file is
// both the fix (no false positives) and more meaningful anyway: what
// matters is whether the candidate root is actually readable, not what
// stat says.
bool FileOpens(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return f.good();
}

// True if iso_path is a mountable ISO9660 image containing the same
// marker files is_valid_root() looks for in a plain folder - lets a loose
// .iso sitting anywhere discoverable (a removable drive's root or one
// level of subfolders, same places a folder-based install is looked for)
// stand in for an un-extracted asset root. Case-insensitive match against
// Iso9660Reader's own already-uppercased entries, same as Find().
bool IsoFileLooksLikeAssetRoot(const fs::path& iso_path) {
    IsoFileSectorSource source(iso_path);
    if (!source.is_open()) return false;
    Iso9660Reader iso(source);
    if (!iso.Mount()) return false;
    if (!iso.Find("MAPFILE")) return false;
    return iso.Find("ATMOS.EXE") || iso.Find("INSTALL/ATMOS.EXE");
}

bool EqualsCaseInsensitive(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

// Case-insensitive, single listing - real mounts can present these
// filenames in any case (ISO9660 driver/mastering tool dependent). All 3
// markers live in the same directory, so one pass resolves all of them.
struct RootMarkers {
    std::optional<fs::path> mapfile;
    std::optional<fs::path> atmos_exe;
    std::optional<fs::path> install_dir;
};

RootMarkers FindRootMarkers(const fs::path& dir) {
    RootMarkers markers;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        const std::string name = entry.path().filename().string();
        if (!markers.mapfile && EqualsCaseInsensitive(name, "MAPFILE")) {
            markers.mapfile = entry.path();
        } else if (!markers.atmos_exe && EqualsCaseInsensitive(name, "ATMOS.EXE")) {
            markers.atmos_exe = entry.path();
        } else if (!markers.install_dir && EqualsCaseInsensitive(name, "INSTALL")) {
            markers.install_dir = entry.path();
        }
    }
    return markers;
}

std::optional<fs::path> FindEntryCaseInsensitive(const fs::path& dir, const std::string& name) {
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (EqualsCaseInsensitive(entry.path().filename().string(), name)) return entry.path();
    }
    return std::nullopt;
}
}  // namespace

std::optional<fs::path> AssetLocator::find_case_insensitive(const fs::path& dir,
                                                            const std::string& name) {
    return FindEntryCaseInsensitive(dir, name);
}

bool AssetLocator::is_valid_root(const fs::path& path) {
    // A loose .iso file - checked first since path is a file, not a
    // directory, so none of the folder-based checks below would ever find
    // anything to open under it.
    std::error_code ec;
    if (fs::is_regular_file(path, ec)) return IsoFileLooksLikeAssetRoot(path);

    // Two markers required, not one - a single generic filename risks
    // matching an unrelated game's folder by coincidence on a USB stick
    // with several homebrew installs. MAPFILE (no extension, same path in
    // both layouts) plus ATMOS.EXE (specific to this game) together make a
    // false positive unrealistic.
    const RootMarkers markers = FindRootMarkers(path);
    if (!markers.mapfile) return false;

    // Installed layout: ATMOS.EXE at root.
    if (markers.atmos_exe) return FileOpens(*markers.atmos_exe);

    // Raw, un-installed CD layout - same marker assets::LooksLikeCdLayout()
    // uses. Lets discovery succeed against a real CD directly, without
    // installing first; the asset provider handles reclassifying paths.
    if (markers.install_dir) {
        if (auto atmos = FindEntryCaseInsensitive(*markers.install_dir, "ATMOS.EXE"))
            return FileOpens(*atmos);
    }
    return false;
}

// ---------------------------------------------------------------------------
// AssetLocator — config dir
// ---------------------------------------------------------------------------

fs::path AssetLocator::config_dir() {
#ifdef _WIN32
    const char* appdata = std::getenv("APPDATA");
    fs::path base = appdata ? fs::path(appdata) : fs::temp_directory_path();
    return base / "OpenFear";
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    fs::path base = home ? fs::path(home) : fs::path("/tmp");
    return base / "Library" / "Application Support" / "OpenFear";
#else
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0] != '\0') return fs::path(xdg) / "openfear";
    const char* home = std::getenv("HOME");
    fs::path base = home ? fs::path(home) : fs::path("/tmp");
    return base / ".config" / "openfear";
#endif
}

// ---------------------------------------------------------------------------
// AssetLocator — save / load config
// ---------------------------------------------------------------------------

bool AssetLocator::save_config(const fs::path& asset_root) {
    const fs::path dir = config_dir();
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) return false;

    std::ofstream f(dir / "openfear.cfg");
    if (!f) return false;
    f << asset_root.string() << "\n";
    return f.good();
}

std::optional<fs::path> AssetLocator::load_config() {
    const fs::path cfg = config_dir() / "openfear.cfg";
    std::ifstream f(cfg);
    if (!f) return std::nullopt;

    std::string line;
    std::getline(f, line);
    if (line.empty()) return std::nullopt;

    while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' '))
        line.pop_back();

    fs::path p(line);
    return is_valid_root(p) ? std::optional<fs::path>(p) : std::nullopt;
}

// ---------------------------------------------------------------------------
// AssetLocator — discovery strategies
// ---------------------------------------------------------------------------

std::optional<fs::path> AssetLocator::from_cli(int argc, char* argv[]) {
    for (int i = 1; i < argc - 1; ++i) {
        if (std::strcmp(argv[i], "--assets") == 0) {
            fs::path p(argv[i + 1]);
            return is_valid_root(p) ? std::optional<fs::path>(p) : std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<fs::path> AssetLocator::from_env() {
    const char* val = std::getenv("OPENFEAR_ASSETS");
    if (!val || val[0] == '\0') return std::nullopt;
    fs::path p(val);
    return is_valid_root(p) ? std::optional<fs::path>(p) : std::nullopt;
}

std::optional<fs::path> AssetLocator::from_local() {
    fs::path candidates[] = {
        fs::current_path() / "assets",
        fs::current_path(),
    };
    for (const auto& p : candidates)
        if (is_valid_root(p)) return p;
    return std::nullopt;
}

std::optional<fs::path> AssetLocator::from_cd_scan() {
#if defined(__PS2__)
    if (auto p = Ps2FindCdfsRoot()) return p;
    return Ps2FindMassRoot();
#elif defined(_WIN32)
    char root[] = "A:\\";
    for (char letter = 'A'; letter <= 'Z'; ++letter) {
        root[0] = letter;
        const UINT type = GetDriveTypeA(root);
        if (type == DRIVE_CDROM) {
            fs::path drive(root);
            if (is_valid_root(drive)) return drive;
        }
    }
#else
    // No auto-scan on macOS/Linux (or any other non-PS2/Windows target) -
    // --assets/OPENFEAR_ASSETS/the config file/local folder checks above
    // already cover every deliberate case.
#endif
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// AssetLocator — main entry point
// ---------------------------------------------------------------------------

std::optional<fs::path> AssetLocator::discover(int argc, char* argv[]) {
    if (auto p = from_cli(argc, argv)) return p;
    if (auto p = from_env()) return p;
    if (auto p = load_config()) return p;
    // Order matters: from_local() could also match a real disc, but
    // through a less specific path than this scan returns.
    if (auto p = from_cd_scan()) return p;
    if (auto p = from_local()) return p;
    return std::nullopt;
}

}  // namespace openfear
