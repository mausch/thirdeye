#include "dh_save.hpp"
#include <algorithm>
#include <cctype>

namespace THIRDEYE::savegame {
namespace {
void put(std::vector<uint8_t>& b, uint32_t v, int n) {
    for (int i = 0; i < n; ++i) b.push_back(uint8_t(v >> (8 * i)));
}
bool safeName(const std::string& name) {
    if (!name.starts_with("SAVEGAME\\")) return false;
    const auto file = name.substr(9);
    if (file == "ITEMS.DAT" || file == "LEVELS.DAT" || file == "VISIBLE.DAT") return true;
    if (file.size() != 9 || !std::isdigit(static_cast<unsigned char>(file[3])) ||
        !std::isdigit(static_cast<unsigned char>(file[4]))) return false;
    return (file.starts_with("FEA") && file.ends_with(".DAT")) ||
           (file.starts_with("LVL") && file.ends_with(".TMP"));
}
}
std::vector<uint8_t> encodeDHSave(const DHSave& s) {
    std::vector<uint8_t> b;
    put(b, s.character, 4); put(b, s.seed, 4);
    b.insert(b.end(), s.settings.begin(), s.settings.end());
    b.insert(b.end(), s.visited.begin(), s.visited.end());
    b.push_back(s.level);
    for (const auto& [name, data] : s.files) {
        put(b, uint32_t(name.size() + 1), 2);
        b.insert(b.end(), name.begin(), name.end()); b.push_back(0);
        put(b, uint32_t(data.size()), 4);
        b.insert(b.end(), data.begin(), data.end());
    }
    put(b, 0xFFFFFFFF, 4);
    return b;
}
bool decodeDHSave(std::span<const uint8_t> b, DHSave& save, std::string& error) {
    auto fail = [&](const char* text) { error = text; return false; };
    if (b.size() < 57 || b.size() > 64 * 1024 * 1024) return fail("Invalid DH save size");
    size_t pos = 0;
    auto get = [&](int n) {
        uint32_t value = 0;
        for (int i = 0; i < n; ++i) value |= uint32_t(b[pos++]) << (i * 8);
        return value;
    };
    DHSave s;
    s.character = get(4); s.seed = get(4);
    std::copy_n(b.begin() + pos, 19, s.settings.begin()); pos += 19;
    std::copy_n(b.begin() + pos, 25, s.visited.begin()); pos += 25;
    s.level = b[pos++];
    if (s.settings[0] > 15 || s.level >= s.settings[0] + 10)
        return fail("Invalid DH save level");
    while (pos + 2 <= b.size()) {
        const uint32_t len = get(2);
        if (len == 0xFFFF) {
            if (pos + 2 != b.size() || get(2) != 0xFFFF) return fail("Invalid DH save terminator");
            save = std::move(s);
            return true;
        }
        if (!len || len > 128 || len + 4 > b.size() - pos || b[pos + len - 1] != 0)
            return fail("Invalid DH save filename");
        std::string name(reinterpret_cast<const char*>(b.data() + pos), len - 1);
        pos += len;
        if (!safeName(name) || s.files.contains(name)) return fail("Unsafe or duplicate DH save filename");
        const uint32_t size = get(4);
        if (size > b.size() - pos) return fail("Truncated DH save file");
        s.files[name] = {b.begin() + pos, b.begin() + pos + size};
        pos += size;
    }
    return fail("Missing DH save terminator");
}
}
