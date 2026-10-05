#pragma once
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openfear::assets {

// Classifies a file found directly inside INSTALL/, matching the real
// installer's file list (see kInstallFilePatterns). Returns the dest
// subdir ("." = dest root), or nullopt if not part of the real install.
std::optional<std::string> ClassifyInstallFile(const std::string& upper_filename);

// Fixed files that live loose at the CD root and copy straight to dest root.
const std::vector<std::string>& CdRootFiles();

// CD asset dirs copied wholesale (one level, non-recursive) into the
// same-named dir at dest.
const std::vector<std::string>& CdDirs();

// Given a raw CD-layout path list (relative, uppercase, '/'-separated),
// returns a map from the reclassified "installed" virtual path to the
// original raw path. Entries matching no rule are omitted. Shared by
// CdPathRemap::Build() (in-memory remap) and the --install flow (actual
// file copy, from either a real directory or a mounted .iso).
std::unordered_map<std::string, std::string>
ReclassifyCdLayout(const std::vector<std::string>& paths);

// Maps a raw Atmosfear CD layout onto the installed layout the game reads
// from. Used by any IAssetProvider that can see a raw CD, for in-memory
// path remapping (no filesystem writes).
class CdPathRemap {
public:
    // paths: every file at the asset root (relative, uppercase,
    // '/'-separated). Builds the virtual->raw translation if they look
    // like a raw CD layout (e.g. "WAV/AT_0205.WAV" -> "W9/AT_0205.WAV");
    // otherwise stays empty.
    void Build(const std::vector<std::string>& paths);

    // Translates a normalized virtual path to its real location, or
    // returns it unchanged if there's no entry for it.
    std::string Resolve(const std::string& normalized_path) const;

    size_t size() const { return map_.size(); }

private:
    std::unordered_map<std::string, std::string> map_;
};

}  // namespace openfear::assets
