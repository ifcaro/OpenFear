#include "InstallFlow.h"
#include "ScreenLoop.h"

#include "openfear/AssetLocator.h"
#include "openfear/assets/CdLayout.h"
#include "openfear/assets/Iso9660Reader.h"
#include "openfear/assets/IsoFileSectorSource.h"
#include "openfear/platform/BitmapFont.h"
#include "openfear/platform/Platform.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace openfear {

namespace fs = std::filesystem;

namespace {

// --install [path]: path is optional and, if present, an explicit install
// destination - distinct from --assets, which always means the source to
// install FROM, same as in normal play. Only consumed if it doesn't look
// like another flag, so a bare `--install` followed by something else
// isn't misread as a destination.
bool ParseInstallFlag(int argc, char* argv[], std::optional<fs::path>* out_dest) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--install") != 0) continue;
        if (i + 1 < argc && std::strncmp(argv[i + 1], "--", 2) != 0) {
            *out_dest = fs::path(argv[i + 1]);
        }
        return true;
    }
    return false;
}

bool CanWriteTo(const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) return false;
    const fs::path probe = dir / ".openfear_write_test";
    {
        std::ofstream f(probe, std::ios::binary);
        if (!f) return false;
    }
    fs::remove(probe, ec);
    return true;
}

// assets/ under the current working directory if writable (matches
// AssetLocator's own default discovery location - no config needed
// afterward); otherwise the same per-user directory openfear.cfg already
// lives in. nullopt if neither is writable.
std::optional<fs::path> ChooseDestination() {
    const fs::path local = fs::current_path() / "assets";
    if (CanWriteTo(local)) return local;
    const fs::path config_fallback = AssetLocator::config_dir() / "assets";
    if (CanWriteTo(config_fallback)) return config_fallback;
    return std::nullopt;
}

// A real Atmosfear CD, as a plain directory - same marker LooksLikeCdLayout()
// (src/assets/CdLayout.cpp) and AssetLocator::is_valid_root() both key
// off. --install is only meant to install FROM a raw CD layout - an
// already-installed source has nothing to reorganize.
bool LooksLikeRawCdDir(const fs::path& source) {
    std::error_code ec;
    return fs::exists(source / "INSTALL" / "ATMOS.EXE", ec) ||
           fs::exists(source / "install" / "ATMOS.EXE", ec);
}

std::string ToUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

struct FileOp {
    fs::path src;
    fs::path dest_rel;
};

// Reorganizes a raw CD/ISO source into the installed layout the game
// reads from, in 3 steps: classify + copy files out of INSTALL/ (same
// shared rules runtime remapping already relies on - see CdLayout.h),
// copy the fixed CD-root files, then copy the remaining CD directories
// wholesale.
std::vector<FileOp> PlanCdInstall(const fs::path& source) {
    std::vector<FileOp> ops;
    std::error_code ec;

    for (const auto& entry : fs::directory_iterator(source / "INSTALL", ec)) {
        if (!entry.is_regular_file()) continue;
        const auto subdir = assets::ClassifyInstallFile(ToUpper(entry.path().filename().string()));
        if (!subdir) continue;
        const fs::path dest_rel = (*subdir == ".") ? entry.path().filename()
                                                   : fs::path(*subdir) / entry.path().filename();
        ops.push_back({entry.path(), dest_rel});
    }
    ec.clear();

    for (const auto& name : assets::CdRootFiles()) {
        const fs::path src = source / name;
        if (fs::exists(src, ec)) ops.push_back({src, name});
        ec.clear();
    }

    for (const auto& name : assets::CdDirs()) {
        for (const auto& entry : fs::directory_iterator(source / name, ec)) {
            if (entry.is_regular_file())
                ops.push_back({entry.path(), fs::path(name) / entry.path().filename()});
        }
        ec.clear();
    }

    return ops;
}

void DrawMessage(Platform& platform, platform::BitmapFont& font, bool font_ok, const char* line1,
                 const char* line2) {
    platform.renderer->beginFrame();
    platform.renderer->clear(0, 0, 0);
    if (font_ok) {
        font.DrawText(*platform.renderer, 40, 220, line1, 255, 255, 255);
        if (line2)
            font.DrawTextWrapped(*platform.renderer, 40, 250, line2, 640 - 40 - 40, 200, 200, 200);
    }
    platform.renderer->endFrame();
    WaitForDismissOrTimeout(platform);
}

enum class InstallResult { Completed, Quit, Failed };

void DrawProgress(Platform& platform, platform::BitmapFont& font, bool font_ok, int percent) {
    constexpr int kBarW = 300, kBarH = 20;
    const int bar_x = (640 - kBarW) / 2, bar_y = 240;
    platform.renderer->beginFrame();
    platform.renderer->clear(0, 0, 0);
    if (font_ok) {
        const char* kMsg = "Installing...";
        const int x = (640 - font.TextWidth(kMsg)) / 2;
        font.DrawText(*platform.renderer, x, 200, kMsg, 255, 255, 255);
    }
    platform.renderer->fillRect(bar_x - 2, bar_y - 2, kBarW + 4, kBarH + 4, 90, 90, 90);
    platform.renderer->fillRect(bar_x, bar_y, kBarW, kBarH, 20, 20, 20);
    const int fill_w = (kBarW * percent) / 100;
    if (fill_w > 0) platform.renderer->fillRect(bar_x, bar_y, fill_w, kBarH, 200, 60, 60);
    platform.renderer->endFrame();
}

InstallResult InstallFromDir(Platform& platform, platform::BitmapFont& font, bool font_ok,
                             const fs::path& source, const fs::path& dest) {
    const std::vector<FileOp> ops = PlanCdInstall(source);
    DrawProgress(platform, font, font_ok, 0);
    for (size_t i = 0; i < ops.size(); ++i) {
        std::error_code ec;
        const fs::path out_path = dest / ops[i].dest_rel;
        fs::create_directories(out_path.parent_path(), ec);
        fs::copy_file(ops[i].src, out_path, fs::copy_options::overwrite_existing, ec);
        if (ec) return InstallResult::Failed;

        if (!platform.poll(0.016f)) return InstallResult::Quit;
        DrawProgress(platform, font, font_ok, static_cast<int>((i + 1) * 100 / ops.size()));
    }
    return InstallResult::Completed;
}

// Same reclassification CdPathRemap uses for in-memory remapping, but a
// loose .iso has no filesystem path for fs::copy_file to read from - bytes
// come from Iso9660Reader::ReadAt instead.
InstallResult InstallFromIso(Platform& platform, platform::BitmapFont& font, bool font_ok,
                             Iso9660Reader& iso, const fs::path& dest) {
    std::vector<std::string> paths;
    paths.reserve(iso.entries().size());
    for (const auto& e : iso.entries()) paths.push_back(e.path);
    const auto remap = assets::ReclassifyCdLayout(paths);

    DrawProgress(platform, font, font_ok, 0);
    size_t i = 0;
    std::vector<uint8_t> buf;
    for (const auto& [dest_rel, raw] : remap) {
        const auto* entry = iso.Find(raw);
        if (entry) {
            buf.resize(entry->size);
            if (iso.ReadAt(*entry, 0, buf.data(), buf.size()) != buf.size())
                return InstallResult::Failed;

            std::error_code ec;
            const fs::path out_path = dest / fs::path(dest_rel);
            fs::create_directories(out_path.parent_path(), ec);
            if (ec) return InstallResult::Failed;
            std::ofstream out(out_path, std::ios::binary);
            out.write(reinterpret_cast<const char*>(buf.data()),
                      static_cast<std::streamsize>(buf.size()));
            if (!out) return InstallResult::Failed;
        }

        if (!platform.poll(0.016f)) return InstallResult::Quit;
        DrawProgress(platform, font, font_ok, static_cast<int>((++i) * 100 / remap.size()));
    }
    return InstallResult::Completed;
}

int RunInstall(int argc, char* argv[], const std::optional<fs::path>& install_to) {
    platform_bootstrap();
    auto platform = create_platform();
    if (!platform->init("Atmosfear - Install")) {
        std::fputs("[INSTALL] Platform init failed\n", stderr);
        return 1;
    }
    // No game cursor loaded here (no AssetManager) and no gameplay
    // fidelity to preserve - show the real OS pointer instead of leaving
    // the user with no cursor at all.
    platform->renderer->setCursorVisible(true);

    platform::BitmapFont font;
    const bool font_ok = font.Load(*platform->renderer);
    // Unloads the font and shuts the platform down on every exit from this
    // function, including early returns - avoids repeating that teardown
    // at each one.
    struct ShutdownGuard {
        Platform* platform;
        platform::BitmapFont* font;
        bool font_ok;
        ~ShutdownGuard() {
            if (font_ok) font->Unload(*platform->renderer);
            platform->shutdown();
        }
    } shutdown_guard{platform.get(), &font, font_ok};

    // generic_string(), not string(), on every path drawn below - the bitmap font has no '\'
    // glyph, so a native Windows path would render with gaps instead of its directory separators.
    std::optional<fs::path> dest_opt;
    if (install_to) {
        // Resolved to absolute - a relative path saved to openfear.cfg would resolve against
        // whatever cwd a later, unrelated launch happens to have.
        std::error_code ec_abs;
        const fs::path absolute_dest = fs::absolute(*install_to, ec_abs);
        const fs::path& resolved_dest = ec_abs ? *install_to : absolute_dest;
        if (!CanWriteTo(resolved_dest)) {
            DrawMessage(*platform, font, font_ok, "Cannot write to the specified install path:",
                        resolved_dest.generic_string().c_str());
            return 1;
        }
        dest_opt = resolved_dest;
    } else {
        dest_opt = ChooseDestination();
        if (!dest_opt) {
            DrawMessage(*platform, font, font_ok,
                        "Could not find a writable location to install to.", nullptr);
            return 1;
        }
    }
    const fs::path dest = *dest_opt;

    std::error_code ec;
    if (fs::exists(dest, ec) && !fs::is_empty(dest, ec)) {
        DrawMessage(*platform, font, font_ok,
                    "Install folder already exists:", dest.generic_string().c_str());
        return 0;
    }

    const auto not_found = [&] {
        DrawMessage(*platform, font, font_ok,
                    "Could not find your Atmosfear CD or ISO to install from.", nullptr);
        return 1;
    };

    const auto source = AssetLocator::discover(argc, argv);
    if (!source) return not_found();

    InstallResult result;
    if (fs::is_directory(*source, ec)) {
        if (!LooksLikeRawCdDir(*source)) return not_found();
        result = InstallFromDir(*platform, font, font_ok, *source, dest);
    } else if (fs::is_regular_file(*source, ec)) {
        IsoFileSectorSource iso_source(*source);
        Iso9660Reader iso(iso_source);
        if (!iso_source.is_open() || !iso.Mount() || !iso.Find("INSTALL/ATMOS.EXE"))
            return not_found();
        result = InstallFromIso(*platform, font, font_ok, iso, dest);
    } else {
        return not_found();
    }
    if (result != InstallResult::Completed) {
        // Partial copy left behind would otherwise block every future install attempt (the
        // dest-exists guard above) and could get picked up as if it were a real install.
        std::error_code ec_cleanup;
        fs::remove_all(dest, ec_cleanup);
        if (result == InstallResult::Failed) {
            DrawMessage(*platform, font, font_ok,
                        "Installation failed (disk full, or a copy error) - nothing was left "
                        "behind, try again.",
                        nullptr);
            return 1;
        }
        return 0;
    }

    if (dest != fs::current_path() / "assets") {
        AssetLocator::save_config(dest);
    }

    DrawMessage(*platform, font, font_ok, "Installation complete.", "Restart OpenFear to play.");
    return 0;
}

}  // namespace

std::optional<int> RunInstallIfRequested(int argc, char* argv[]) {
    std::optional<fs::path> install_to;
    if (!ParseInstallFlag(argc, argv, &install_to)) return std::nullopt;
    return RunInstall(argc, argv, install_to);
}

}  // namespace openfear
