#pragma once
#include <cctype>
#include <string>

namespace openfear {

// Converts backslashes to forward slashes, uppercases everything, and strips
// leading slashes. Real CD asset filenames are uppercase (original CD, DOS
// 8.3 convention) — Windows path APIs are case-insensitive but POSIX/MEMFS/
// ISO9660 readers aren't, so callers normalize once here instead of at every
// comparison site.
inline std::string NormalizeVirtualPath(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    for (char c : raw) {
        char n = (c == '\\') ? '/' : c;
        out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(n))));
    }
    size_t start = 0;
    while (start < out.size() && out[start] == '/') ++start;
    return out.substr(start);
}

}  // namespace openfear
