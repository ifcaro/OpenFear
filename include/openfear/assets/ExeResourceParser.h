#pragma once

#include "IAssetProvider.h"
#include <cstdint>
#include <string>
#include <vector>
#include <map>

namespace openfear::assets {

// Parses Win16 NE-format executables to extract resources.
class ExeResourceParser {
public:
    struct ResourceEntry {
        uint16_t id;      // integer ordinal
        uint32_t offset;  // file offset
        uint32_t size;    // size in bytes
        uint8_t* data;    // loaded data, null if not loaded

        ResourceEntry() : id(0), offset(0), size(0), data(nullptr) {}
        ~ResourceEntry() { delete[] data; }

        ResourceEntry(const ResourceEntry&) = delete;
        ResourceEntry& operator=(const ResourceEntry&) = delete;

        ResourceEntry(ResourceEntry&& other) noexcept
            : id(other.id), offset(other.offset), size(other.size), data(other.data) {
            other.data = nullptr;
        }
        ResourceEntry& operator=(ResourceEntry&& other) noexcept {
            if (this != &other) {
                delete[] data;
                id = other.id;
                offset = other.offset;
                size = other.size;
                data = other.data;
                other.data = nullptr;
            }
            return *this;
        }
    };

    ExeResourceParser() = default;
    ~ExeResourceParser() = default;

    bool Parse(IAssetProvider& provider, const std::string& exe_path);

    // resource_type: Win16 resource type, e.g. 0x8001 = CURSOR.
    const ResourceEntry* GetResourceById(uint16_t resource_type, uint16_t resource_id) const;

    bool LoadResource(uint16_t resource_type, uint16_t resource_id);

private:
    std::string exe_path_;
    std::vector<uint8_t> exe_data_;
    uint16_t res_table_offset_ = 0;  // relative to the NE header start

    std::map<uint16_t, std::vector<ResourceEntry>> resources_;  // resource_type -> entries

    bool ParseNEHeader();
    bool ParseResourceTable();

    // Fail-fast check that this is really an Atmosfear EXE (module name,
    // window proc export, description) before parsing the resource table.
    bool CheckAtmosSignature(uint32_t ne_offset) const;
};

}  // namespace openfear::assets
