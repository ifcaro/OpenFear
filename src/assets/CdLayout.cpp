#include "openfear/assets/CdLayout.h"
#include <optional>

namespace openfear::assets {

namespace {

// True if `name` (already uppercase) matches a DOS 8.3-style glob pattern:
// '?' = exactly one character, '*' = any run of characters (only ever
// appears once, as a prefix wildcard in this table - e.g. "*.BIN").
bool MatchesGlob(const std::string& name, const std::string& pattern) {
    const auto star = pattern.find('*');
    if (star != std::string::npos) {
        const std::string suffix = pattern.substr(star + 1);
        if (name.size() < suffix.size()) return false;
        return name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0;
    }
    if (name.size() != pattern.size()) return false;
    for (size_t i = 0; i < name.size(); ++i) {
        if (pattern[i] != '?' && pattern[i] != name[i]) return false;
    }
    return true;
}

// Exact transcription of the real Atmosfear installer's own file list -
// INSTALL/INSTALL.CFG on the real CD (a plain-text script read by the
// generic INSTALL/INSTALL.EXE engine, identical byte-for-byte across all
// 3 real discs checked: Spanish/English/Brazilian). This is what actually
// gets copied from INSTALL/ into each destination subdir - ground truth,
// not a guess from file extensions. $winsysdir$ entries (VB3 runtime
// DLLs/VBXes) are Windows-system installs, not game assets - omitted.
//
// STOP.WAV/WITCH.WAV/HELEN.WAV: the .cfg lists these as "STOP?.WAV" etc.,
// but the real disc's INSTALL/ only ever has the bare name - the "?"
// never matches anything, so transcribed here as the literal name
// actually present.
struct InstallFilePattern {
    const char* dest;  // "." = dest root
    const char* pattern;
};
constexpr InstallFilePattern kInstallFilePatterns[] = {
    {".", "ATMOS.EXE"},       {"AVI", "GKLOOP1.AVI"},   {"AVI", "GKLOOP2.AVI"},
    {"AVI", "GKLOOP3.AVI"},   {"DATA", "*.BIN"},        {"DATA", "*.PNT"},
    {"WAV", "RIBBIT1.WAV"},   {"WAV", "STOP.WAV"},      {"WAV", "DR_OPEN.WAV"},
    {"WAV", "AT_0426.WAV"},   {"WAV", "AT_0514A.WAV"},  {"WAV", "AT_0514B.WAV"},
    {"WAV", "AT_0514C.WAV"},  {"WAV", "AT_0514E.WAV"},  {"WAV", "AT_0471E.WAV"},
    {"WAV", "AT_0472C.WAV"},  {"WAV", "AT_0474?.WAV"},  {"WAV", "AT_0475?.WAV"},
    {"WAV", "MOVE?.WAV"},     {"WAV", "TURN?.WAV"},     {"WAV", "AT_0631.WAV"},
    {"WAV", "AT_GLASS.WAV"},  {"WAV", "HOWL.WAV"},      {"WAV", "BJUJU2.WAV"},
    {"WAV", "EGYPT.WAV"},     {"WAV", "BATS.WAV"},      {"WAV", "WITCH.WAV"},
    {"WAV", "HELEN.WAV"},     {"MISC", "MAP0?.BMP"},    {"MISC", "COUNTER.BMP"},
    {"MISC", "MVDICE.BMP"},   {"MISC", "GFEARS_0.BMP"}, {"MISC", "GFEARS_2.BMP"},
    {"MISC", "ATTKCHSE.BMP"}, {"MISC", "ATTKEMPT.BMP"}, {"CHARS", "INFO0_0.BMP"},
    {"CHARS", "INFO1_0.BMP"}, {"CHARS", "INFO2_0.BMP"}, {"CHARS", "INFO3_0.BMP"},
    {"CHARS", "INFO4_0.BMP"}, {"CHARS", "INFO5_0.BMP"}, {"CHARS", "INFO0_1.BMP"},
    {"CHARS", "INFO1_1.BMP"}, {"CHARS", "INFO2_1.BMP"}, {"CHARS", "INFO3_1.BMP"},
    {"CHARS", "INFO4_1.BMP"}, {"CHARS", "INFO5_1.BMP"},
};

}  // namespace

std::optional<std::string> ClassifyInstallFile(const std::string& upper_filename) {
    for (const auto& rule : kInstallFilePatterns) {
        if (MatchesGlob(upper_filename, rule.pattern)) return rule.dest;
    }
    return std::nullopt;
}

const std::vector<std::string>& CdRootFiles() {
    static const std::vector<std::string> kFiles = {
        "FONDOL.BMP",  "MANUAL.HLP", "MANUAL.FTS", "SIMBOLOS.SHG", "PLAYVID.EXE",  "MAPFILE",
        "DESFEAR.ICO", "7.ICO",      "CHESS.ICO",  "GOLF.ICO",     "MONSTERS.ICO",
    };
    return kFiles;
}

const std::vector<std::string>& CdDirs() {
    static const std::vector<std::string> kDirs = {
        "W1",  "W2", "W3", "W4", "W5", "W6", "W7", "W8",  "W9",  "W10", "W11", "AV1", "AV2", "AV3",
        "AVI", "M1", "M2", "A1", "C1", "C2", "CR", "FF1", "FF2", "FF3", "G1",  "G2",  "G3",  "G4",
    };
    return kDirs;
}

namespace {

// True if paths (relative, uppercase, '/'-separated) look like a raw,
// un-installed CD layout.
bool LooksLikeCdLayout(const std::vector<std::string>& paths) {
    for (const auto& p : paths) {
        if (p == "INSTALL/ATMOS.EXE") return true;
    }
    return false;
}

}  // namespace

std::unordered_map<std::string, std::string>
ReclassifyCdLayout(const std::vector<std::string>& paths) {
    std::unordered_map<std::string, std::string> out;
    const auto& root_files = CdRootFiles();
    const auto& dirs = CdDirs();

    for (const auto& raw : paths) {
        const auto slash = raw.find('/');

        if (slash == std::string::npos) {
            for (const auto& f : root_files) {
                if (raw == f) {
                    out[raw] = raw;
                    break;
                }
            }
            continue;
        }

        const std::string first = raw.substr(0, slash);
        const std::string rest = raw.substr(slash + 1);

        if (first == "INSTALL" && rest.find('/') == std::string::npos) {
            auto subdir = ClassifyInstallFile(rest);
            if (!subdir) continue;
            out[(*subdir == ".") ? rest : (*subdir + "/" + rest)] = raw;
            continue;
        }

        if (rest.find('/') == std::string::npos) {
            // Only files directly inside a wholesale-copied CD dir count — matches the
            // installer's own non-recursive copy of these directories.
            for (const auto& d : dirs) {
                if (first == d) {
                    out[raw] = raw;
                    break;
                }
            }
        }
    }

    return out;
}

void CdPathRemap::Build(const std::vector<std::string>& paths) {
    map_ = LooksLikeCdLayout(paths) ? ReclassifyCdLayout(paths)
                                    : std::unordered_map<std::string, std::string>{};
}

std::string CdPathRemap::Resolve(const std::string& normalized_path) const {
    auto it = map_.find(normalized_path);
    return it != map_.end() ? it->second : normalized_path;
}

}  // namespace openfear::assets
