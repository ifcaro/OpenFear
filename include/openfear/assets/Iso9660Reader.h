#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace openfear {

// Where an Iso9660Reader actually gets its sectors from - a real optical
// drive (Ps2CdSectorSource, PS2's sceCdRead, bypassing the platform's own
// filesystem driver) or a plain .iso file on any regular filesystem
// (IsoFileSectorSource, any platform). Iso9660Reader itself never touches
// a device/file directly, only this interface.
class Iso9660SectorSource {
public:
    virtual ~Iso9660SectorSource() = default;

    // Reads `count` consecutive 2048-byte sectors starting at `lba` into
    // buf (must be at least count*2048 bytes). Returns false on any error.
    virtual bool ReadSectors(uint32_t lba, uint32_t count, void* buf) = 0;
};

// Minimal ISO9660 reader, source-agnostic (see Iso9660SectorSource above) -
// handles only what asset providers need: root dir + one level of
// subdirectories, plain ISO9660 or Joliet volume descriptor, no
// RockRidge/multi-session/write support.
class Iso9660Reader {
public:
    struct Entry {
        std::string
            path;  // full path from the CD root, uppercase, "/"-separated (e.g. "W1/AT_0634.WAV")
        uint32_t lba = 0;
        uint32_t size = 0;
    };

    explicit Iso9660Reader(Iso9660SectorSource& source) : source_(source) {}

    // Reads the volume descriptor (sector 16) and walks the full directory
    // tree. Returns false only if no ISO9660/Joliet volume descriptor is
    // found at all (not a data disc/image) - never false because of any
    // disc-type classification, which this class never checks.
    bool Mount();

    // Diagnostic only - which specific step Mount() failed at, for
    // on-screen display. Meaningless after a successful Mount().
    const char* last_mount_error() const { return mount_error_; }

    const std::vector<Entry>& entries() const { return entries_; }

    // nullptr if not found. path is matched case-insensitively against the
    // already-uppercased entries.
    const Entry* Find(const std::string& path) const;

    // Reads up to n bytes starting at offset within entry, sector-aligned
    // under the hood. Returns bytes actually read (0 at/past EOF).
    size_t ReadAt(const Entry& entry, uint64_t offset, void* buf, size_t n) const;

private:
    void WalkDirectory(uint32_t lba, uint32_t size_bytes, const std::string& prefix, int depth);

    Iso9660SectorSource& source_;
    std::vector<Entry> entries_;
    const char* mount_error_ = "";

    // Read-ahead cache for ReadAt() - avoids reissuing a real ReadSectors()
    // call (real seek/spin latency on a physical drive) per small
    // sequential chunk, which is what causes stuttering. Not thread-safe:
    // a caller reached from more than one thread (e.g. IsoAssetProvider)
    // must serialize access itself.
    mutable std::vector<uint8_t> read_cache_;
    mutable uint32_t read_cache_lba_ = 0xFFFFFFFFu;
    mutable uint32_t read_cache_sectors_ = 0;
};

}  // namespace openfear
