#pragma once
#include "openfear/assets/Iso9660Reader.h"
#include <filesystem>
#include <fstream>

namespace openfear {

// Iso9660SectorSource backed by a plain .iso file on any regular
// filesystem - lets Iso9660Reader parse and serve a CD image file
// directly, without the OS having mounted/extracted it first.
class IsoFileSectorSource : public Iso9660SectorSource {
public:
    explicit IsoFileSectorSource(const std::filesystem::path& iso_path);

    bool is_open() const { return file_.is_open(); }

    bool ReadSectors(uint32_t lba, uint32_t count, void* buf) override;

private:
    std::ifstream file_;
};

}  // namespace openfear
