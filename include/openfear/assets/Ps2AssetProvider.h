#pragma once
#include "openfear/assets/CdLayout.h"
#include "openfear/assets/Iso9660Reader.h"
#include "openfear/assets/NativeAssetProvider.h"
#include <optional>
#include <vector>

namespace openfear {

// Iso9660SectorSource backed by PS2's raw sceCdRead() - bypasses
// ps2sdk's cdfs.irx entirely.
class Ps2CdSectorSource : public Iso9660SectorSource {
public:
    bool ReadSectors(uint32_t lba, uint32_t count, void* buf) override;
};

// Diagnostic only - detail behind the most recent Ps2CdSectorSource::
// ReadSectors() failure (sceCdRead() returning false vs. a nonzero
// sceCdGetError()), log-only (too low-level for on-screen display).
// Empty if nothing has failed yet.
const char* Ps2LastCdReadDiagnostic();

// Tries the real CD ("cdfs:") as an asset root. Returns nullopt if
// there's no disc, it doesn't mount, or it mounts but isn't Atmosfear.
std::optional<std::filesystem::path> Ps2FindCdfsRoot();

// Diagnostic only - why the most recent Ps2FindCdfsRoot() call failed, for
// on-screen display. Empty if it hasn't run yet or last succeeded.
const char* Ps2LastCdScanDiagnostic();

// Tries "mass:" (USB storage) as an asset root, so the game runs with no
// install step. Checks the root itself, then one level of subfolders (a
// folder or a loose .iso in either). Returns nullopt if nothing valid is
// found.
std::optional<std::filesystem::path> Ps2FindMassRoot();

// PS2 asset provider. Real CD ("cdfs:") goes through Iso9660Reader/
// Ps2CdSectorSource, never cdfs.irx. USB mass storage ("mass:", a
// different driver) uses the normal NativeAssetProvider base-class path
// for actual reads, but builds the inherited remap_ itself from its own
// PS2-safe directory walk (opendir/readdir, or Iso9660Reader::entries()
// for cdfs:) instead of the base class's std::filesystem walk.
class Ps2AssetProvider : public NativeAssetProvider {
public:
    explicit Ps2AssetProvider(std::filesystem::path root);

    bool Exists(const std::string& path) override;
    std::vector<uint8_t> ReadFile(const std::string& path) override;

    AssetHandle Open(const std::string& path) override;
    size_t Read(AssetHandle handle, void* buf, size_t n) override;
    bool Seek(AssetHandle handle, int64_t offset, AssetSeekOrigin origin) override;
    int64_t Tell(AssetHandle handle) override;
    void Close(AssetHandle handle) override;

private:
    std::string NormalizeAndRemap(const std::string& path) const;

    Ps2CdSectorSource cd_source_;
    Iso9660Reader iso_;
    bool using_iso_ = false;
    // Reused sector-read scratch for Read()'s synchronous fallback path -
    // grows to the largest request seen, never shrinks. A fresh
    // std::vector per call would churn the heap at AVI streaming rates, a
    // real fragmentation risk on a small/embedded allocator.
    std::vector<uint8_t> read_scratch_;
};

}  // namespace openfear
