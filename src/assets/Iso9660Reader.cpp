#include "openfear/assets/Iso9660Reader.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace openfear {

namespace {

constexpr uint32_t kSectorSize = 2048;
constexpr uint8_t kDirFlagBit = 0x02;

uint32_t ReadLE32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

// Standard ECMA-119 (ISO9660) Directory Record byte layout - the same
// format used both for entries listed inside a directory's own sectors and
// for the Root Directory Record embedded at fixed offset 156 inside the
// Primary/Supplementary Volume Descriptor. Read directly by byte offset
// rather than as a packed struct - the format is defined purely in terms of
// byte positions, this sidesteps compiler struct-packing edge cases
// entirely.
struct DirRecordView {
    uint8_t length;
    uint32_t lba;
    uint32_t size;
    uint8_t flags;
    uint8_t name_length;
    const char* name;
};

DirRecordView ParseDirRecord(const uint8_t* p) {
    DirRecordView r;
    r.length = p[0];
    r.lba = ReadLE32(p + 2);
    r.size = ReadLE32(p + 10);
    r.flags = p[25];
    r.name_length = p[32];
    r.name = reinterpret_cast<const char*>(p + 33);
    return r;
}

std::string ToUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::toupper(c); });
    return s;
}

}  // namespace

bool Iso9660Reader::Mount() {
    uint8_t sector16[kSectorSize];
    if (!source_.ReadSectors(16, 1, sector16)) {
        mount_error_ = "read16 fail";
        return false;
    }
    if (std::memcmp(sector16 + 1, "CD001", 5) != 0) {
        mount_error_ = "no CD001";
        return false;
    }
    const uint8_t type = sector16[0];
    if (type != 1 && type != 2) {  // 1 = ISO9660, 2 = Joliet
        mount_error_ = "bad vol type";
        return false;
    }

    const DirRecordView root = ParseDirRecord(sector16 + 156);

    entries_.clear();
    WalkDirectory(root.lba, root.size, "", 0);
    mount_error_ = "";
    return true;
}

void Iso9660Reader::WalkDirectory(uint32_t lba, uint32_t size_bytes, const std::string& prefix,
                                  int depth) {
    const uint32_t sector_count = (size_bytes + kSectorSize - 1) / kSectorSize;
    if (sector_count == 0) return;
    std::vector<uint8_t> buf(sector_count * kSectorSize);
    if (!source_.ReadSectors(lba, sector_count, buf.data())) return;

    for (uint32_t sector = 0; sector < sector_count; ++sector) {
        const uint8_t* base = buf.data() + sector * kSectorSize;
        uint32_t pos = 0;
        while (pos + 33 <= kSectorSize) {
            const DirRecordView rec = ParseDirRecord(base + pos);
            if (rec.length == 0) break;  // rest of this sector is padding
            const bool is_self_or_parent =
                rec.name_length == 1 && (rec.name[0] == '\0' || rec.name[0] == '\1');
            if (!is_self_or_parent) {
                std::string name(rec.name, rec.name_length);
                if (!(rec.flags & kDirFlagBit)) {
                    // Strip the ";1" ISO9660 version suffix - never present
                    // on directory names.
                    const auto semi = name.find(';');
                    if (semi != std::string::npos) name = name.substr(0, semi);
                    // ECMA-119 encodes an extension-less file with a bare
                    // trailing dot ("MAPFILE." not "MAPFILE") - stripped
                    // here so extension-less lookups (e.g. CdLayout.cpp's
                    // MAPFILE rule) actually match.
                    if (!name.empty() && name.back() == '.') name.pop_back();
                }
                name = ToUpper(name);
                const std::string full = prefix.empty() ? name : prefix + "/" + name;

                if (rec.flags & kDirFlagBit) {
                    if (depth == 0) WalkDirectory(rec.lba, rec.size, full, depth + 1);
                    // Two levels deep is never meaningful for CdLayout's rules - skip.
                } else {
                    entries_.push_back(Entry{full, rec.lba, rec.size});
                }
            }
            pos += rec.length;
        }
    }
}

const Iso9660Reader::Entry* Iso9660Reader::Find(const std::string& path) const {
    const std::string upper = ToUpper(path);
    for (const auto& e : entries_)
        if (e.path == upper) return &e;
    return nullptr;
}

namespace {
// 8 sectors (16KB) read-ahead window, tuned to a real optical drive's
// measured throughput ceiling - bigger reads don't increase throughput,
// just make a blocking caller wait longer per call for nothing. A larger
// window has correlated with silent AVI audio corruption, root cause
// unconfirmed - avoid raising this without retesting for that.
constexpr uint32_t kReadAheadSectors = 8;
}  // namespace

size_t Iso9660Reader::ReadAt(const Entry& entry, uint64_t offset, void* buf, size_t n) const {
    if (offset >= entry.size) return 0;
    if (offset + n > entry.size) n = entry.size - offset;
    if (n == 0) return 0;

    size_t total = 0;
    uint8_t* out = static_cast<uint8_t*>(buf);

    while (n > 0) {
        const uint32_t sector = entry.lba + static_cast<uint32_t>(offset / kSectorSize);
        const uint32_t sector_off = static_cast<uint32_t>(offset % kSectorSize);

        const bool cached =
            sector >= read_cache_lba_ && sector < read_cache_lba_ + read_cache_sectors_;
        if (!cached) {
            read_cache_.resize(kReadAheadSectors * kSectorSize);
            if (!source_.ReadSectors(sector, kReadAheadSectors, read_cache_.data())) {
                read_cache_lba_ = 0xFFFFFFFFu;
                read_cache_sectors_ = 0;
                return total;
            }
            read_cache_lba_ = sector;
            read_cache_sectors_ = kReadAheadSectors;
        }

        const uint32_t cache_byte_off = (sector - read_cache_lba_) * kSectorSize + sector_off;
        const uint32_t avail = read_cache_sectors_ * kSectorSize - cache_byte_off;
        const size_t chunk = std::min<size_t>(n, avail);

        std::memcpy(out, read_cache_.data() + cache_byte_off, chunk);
        out += chunk;
        offset += chunk;
        n -= chunk;
        total += chunk;
    }
    return total;
}

}  // namespace openfear
