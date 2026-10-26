#include "dharchive.hpp"

#include <algorithm>
#include <cstring>

namespace {
uint32_t le32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 |
           uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
}

bool extractDungeonHackCD(std::span<const uint8_t> bin,
                          std::vector<ArjEntry>& files, std::string& err) {
    auto fail = [&](const char* message) { err = message; return false; };
    constexpr size_t rawSector = 2352, sector = 2048;
    if (bin.size() % rawSector || bin.size() / rawSector < 17)
        return fail("Truncated MODE1/2352 CD image");
    std::vector<uint8_t> iso;
    iso.reserve(bin.size() / rawSector * sector);
    for (size_t i = 0; i < bin.size(); i += rawSector) {
        if (bin[i] != 0 || bin[i + 11] != 0 || bin[i + 15] != 1 ||
            !std::all_of(bin.begin() + i + 1, bin.begin() + i + 11,
                         [](uint8_t b) { return b == 0xFF; }))
            return fail("Invalid MODE1 CD sector");
        iso.insert(iso.end(), bin.begin() + i + 16, bin.begin() + i + 16 + sector);
    }
    const uint8_t* pvd = iso.data() + 16 * sector;
    if (pvd[0] != 1 || std::memcmp(pvd + 1, "CD001", 5) || pvd[6] != 1 ||
        pvd[128] != 0 || pvd[129] != 8 || pvd[156] < 34)
        return fail("Unsupported ISO9660 volume descriptor");
    const uint64_t root = uint64_t(le32(pvd + 158)) * sector;
    const uint64_t length = le32(pvd + 166);
    if (root > iso.size() || length > iso.size() - root)
        return fail("CD root directory outside image");
    for (uint64_t pos = 0; pos < length;) {
        const uint8_t* r = iso.data() + root + pos;
        if (!r[0]) { pos = (pos / sector + 1) * sector; continue; }
        if (r[0] < 34 || r[0] > length - pos || r[0] > sector - pos % sector ||
            r[32] > r[0] - 33)
            return fail("Malformed ISO9660 directory record");
        const std::string name(reinterpret_cast<const char*>(r + 33), r[32]);
        if (name == "DATA1.ARJ;1" && !(r[25] & 2)) {
            const uint64_t offset = uint64_t(le32(r + 2)) * sector;
            const uint64_t size = le32(r + 10);
            if (offset > iso.size() || size > iso.size() - offset)
                return fail("DATA1.ARJ outside CD image");
            std::vector<std::vector<uint8_t>> volumes{
                std::vector<uint8_t>(iso.begin() + offset, iso.begin() + offset + size)};
            std::vector<ArjEntry> decoded;
            if (!unarjExtract(volumes, decoded, err)) return false;
            const auto has = [&](const char* wanted) {
                return std::any_of(decoded.begin(), decoded.end(), [&](const auto& e) {
                    return e.name == wanted;
                });
            };
            if (!has("HACK.RES") || !has("OPEN.RES"))
                return fail("CD archive is missing HACK.RES or OPEN.RES");
            files = std::move(decoded);
            return true;
        }
        pos += r[0];
    }
    return fail("DATA1.ARJ not found on CD");
}
