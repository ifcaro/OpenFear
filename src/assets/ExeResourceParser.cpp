#include "openfear/assets/ExeResourceParser.h"

#include <cstring>
#include <cstdio>

namespace openfear::assets {

namespace {

uint16_t read_u16(const uint8_t* p) {
    uint16_t v;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

uint32_t read_u32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

}  // namespace

bool ExeResourceParser::Parse(IAssetProvider& provider, const std::string& exe_path) {
    exe_path_ = exe_path;

    exe_data_ = provider.ReadFile(exe_path);
    if (exe_data_.empty()) {
        return false;
    }

    return ParseNEHeader() && ParseResourceTable();
}

bool ExeResourceParser::ParseNEHeader() {
    if (exe_data_.size() < 0x40) {
        return false;
    }
    if (exe_data_[0] != 'M' || exe_data_[1] != 'Z') {
        return false;
    }

    // e_lfanew: offset to the NE header, at fixed MZ offset 0x3C.
    const uint32_t ne_offset = read_u32(&exe_data_[0x3C]);
    if (ne_offset >= exe_data_.size() - 0x40) {
        return false;
    }
    if (exe_data_[ne_offset] != 'N' || exe_data_[ne_offset + 1] != 'E') {
        return false;
    }

    if (ne_offset + 0x30 > exe_data_.size()) {
        return false;
    }
    if (!CheckAtmosSignature(ne_offset)) {
        return false;
    }

    // Resource table offset, relative to the NE header start.
    res_table_offset_ = ne_offset + read_u16(&exe_data_[ne_offset + 0x24]);
    return true;
}

namespace {

// Reads one Pascal-style name (1-byte length prefix, no null terminator)
// at `pos`. Returns false if it doesn't fit in the buffer.
bool ReadPascalString(const std::vector<uint8_t>& data, uint32_t pos, std::string& out) {
    if (pos >= data.size()) return false;
    const uint8_t len = data[pos];
    if (pos + 1 + len > data.size()) return false;
    out.assign(reinterpret_cast<const char*>(&data[pos + 1]), len);
    return true;
}

}  // namespace

bool ExeResourceParser::CheckAtmosSignature(uint32_t ne_offset) const {
    // Resident Name Table (+0x26, relative to the NE header): entry 0 is
    // the module's own name ("ATMOS"); the exported entries right after it
    // include "ATMOSWNDPROC" (the main window proc). Both are specific to
    // this game, not generic NE boilerplate.
    uint32_t pos = ne_offset + read_u16(&exe_data_[ne_offset + 0x26]);
    std::string module_name;
    if (!ReadPascalString(exe_data_, pos, module_name) || module_name != "ATMOS") {
        return false;
    }
    pos += 1 + static_cast<uint32_t>(module_name.size()) + 2;  // name + ordinal

    bool found_wndproc = false;
    for (int i = 0; i < 64 && pos < exe_data_.size(); ++i) {
        std::string name;
        if (!ReadPascalString(exe_data_, pos, name) || name.empty()) break;
        if (name == "ATMOSWNDPROC") {
            found_wndproc = true;
            break;
        }
        pos += 1 + static_cast<uint32_t>(name.size()) + 2;  // name + ordinal
    }
    if (!found_wndproc) return false;

    // Nonresident Name Table (+0x2C, DWORD absolute file offset — the only
    // NE header offset that isn't relative to the NE header start): holds
    // the module description from the original .DEF file, "ATMOS.EXE".
    const uint32_t nonresident_off = read_u32(&exe_data_[ne_offset + 0x2C]);
    std::string description;
    if (!ReadPascalString(exe_data_, nonresident_off, description) || description != "ATMOS.EXE") {
        return false;
    }

    return true;
}

bool ExeResourceParser::ParseResourceTable() {
    if (res_table_offset_ >= exe_data_.size()) {
        return false;
    }

    uint32_t pos = res_table_offset_;

    if (pos + 2 > exe_data_.size()) return false;
    const uint16_t alignment_shift = read_u16(&exe_data_[pos]);
    pos += 2;

    while (pos + 8 <= exe_data_.size()) {
        const uint16_t type_id = read_u16(&exe_data_[pos]);
        pos += 2;
        if (type_id == 0) break;  // end of table

        const uint16_t count = read_u16(&exe_data_[pos]);
        pos += 2;
        pos += 4;  // reserved DWORD, unused

        std::vector<ResourceEntry> entries;
        for (uint16_t i = 0; i < count; ++i) {
            if (pos + 12 > exe_data_.size()) {
                return false;
            }

            ResourceEntry entry;
            const uint16_t offset_units = read_u16(&exe_data_[pos]);
            entry.offset = static_cast<uint32_t>(offset_units) << alignment_shift;
            pos += 2;

            const uint16_t size_units = read_u16(&exe_data_[pos]);
            entry.size = static_cast<uint32_t>(size_units) << alignment_shift;
            pos += 2;

            pos += 2;  // resource flags, unused
            // All 3 known Atmosfear releases use ordinal-only resource IDs
            // (bit 15 set) — no named entries exist to parse.
            entry.id = read_u16(&exe_data_[pos]);
            pos += 2;
            pos += 4;  // reserved DWORD, unused

            entries.push_back(std::move(entry));
        }

        resources_[type_id] = std::move(entries);
    }

    return true;
}

const ExeResourceParser::ResourceEntry*
ExeResourceParser::GetResourceById(uint16_t resource_type, uint16_t resource_id) const {
    auto it = resources_.find(resource_type);
    if (it == resources_.end()) return nullptr;
    for (const auto& entry : it->second) {
        if (entry.id == resource_id) return &entry;
    }
    return nullptr;
}

bool ExeResourceParser::LoadResource(uint16_t resource_type, uint16_t resource_id) {
    const ResourceEntry* entry = GetResourceById(resource_type, resource_id);
    if (!entry) return false;
    if (entry->data) return true;  // already loaded

    if (entry->offset + entry->size > exe_data_.size()) {
        return false;
    }

    auto& resource = const_cast<ResourceEntry&>(*entry);
    resource.data = new uint8_t[entry->size];
    std::memcpy(resource.data, &exe_data_[entry->offset], entry->size);
    return true;
}

}  // namespace openfear::assets
