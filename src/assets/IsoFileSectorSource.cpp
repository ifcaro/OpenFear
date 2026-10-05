#include "openfear/assets/IsoFileSectorSource.h"
#include <cstring>

namespace openfear {

namespace {
constexpr uint32_t kSectorSize = 2048;
}

IsoFileSectorSource::IsoFileSectorSource(const std::filesystem::path& iso_path)
    : file_(iso_path, std::ios::binary) {}

bool IsoFileSectorSource::ReadSectors(uint32_t lba, uint32_t count, void* buf) {
    if (!file_.is_open()) return false;
    // A prior short/failed read leaves failbit set, which blocks seekg()/
    // read() on every call after it - clear it so one bad read doesn't
    // permanently disable the stream for subsequent valid reads.
    file_.clear();
    file_.seekg(static_cast<std::streamoff>(lba) * kSectorSize);
    const auto want = static_cast<std::streamsize>(count) * kSectorSize;
    file_.read(reinterpret_cast<char*>(buf), want);
    if (file_.good()) return true;
    // Iso9660Reader's fixed read-ahead window can legitimately run past a short image's real
    // end - accept it and zero-fill the rest instead of failing the whole read.
    const auto got = file_.gcount();
    if (!file_.eof() || got <= 0) return false;
    std::memset(static_cast<char*>(buf) + got, 0, static_cast<size_t>(want - got));
    file_.clear();
    return true;
}

}  // namespace openfear
