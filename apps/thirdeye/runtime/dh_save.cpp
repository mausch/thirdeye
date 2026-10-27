#include "internal.hpp"
#include "../resources/res.hpp"
#include "../vm/objects.hpp"
#include "../vm/events.hpp"
#include "../savegame/dh_save.hpp"
#include "../savegame/lvl_tmp.hpp"
#include "../savegame/savegame_dir.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>
#include <optional>

namespace THIRDEYE::runtime::dh {
namespace {
using Bytes = std::vector<uint8_t>;
using Save = savegame::DHSave;
std::optional<Save> pending;

std::filesystem::path child(const std::filesystem::path& dir, const std::string& name) {
    auto result = dir / name;
    if (std::filesystem::exists(result)) return result;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
        std::string candidate = e.path().filename().string();
        std::transform(candidate.begin(), candidate.end(), candidate.begin(),
                       [](unsigned char c) { return std::toupper(c); });
        if (candidate == name) return e.path();
    }
    return result;
}
std::filesystem::path file(Context& c, const std::string& name) {
    return child(child(c.res.resourcePath().parent_path(), "SAVEGAME"), name);
}
std::string numbered(const char* prefix, int number, const char* suffix) {
    return std::string(prefix) + char('0' + number / 10) + char('0' + number % 10) + suffix;
}
Bytes read(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw VM::VmError("Cannot read DH save file: " + path.string());
    if (std::filesystem::file_size(path) > 64 * 1024 * 1024)
        throw VM::VmError("DH save file exceeds size limit");
    return {std::istreambuf_iterator<char>(f), {}};
}
bool write(const std::filesystem::path& path, std::span<const uint8_t> bytes) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    auto tmp = path; tmp += ".tmpwrite";
    if (ec || std::filesystem::is_symlink(path) || std::filesystem::is_symlink(tmp)) return false;
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    out.close();
    if (out.fail()) { std::filesystem::remove(tmp, ec); return false; }
    std::filesystem::rename(tmp, path, ec);
    return !ec;
}
uint32_t integer(const Bytes& b, size_t at, int size) {
    uint32_t v = 0;
    for (int i = 0; i < size; ++i) v |= uint32_t(b[at + i]) << (8 * i);
    return v;
}
struct Record { int slot; uint32_t cls; Bytes data; };
std::vector<Record> records(Context& c, const Bytes& bytes, int first, int last) {
    if (bytes.empty() || bytes[0] != 0x1A) throw VM::VmError("Invalid DH object save header");
    std::vector<Record> out;
    size_t pos = 1;
    int previous = first - 1;
    while (pos < bytes.size()) {
        if (bytes.size() - pos < 8) throw VM::VmError("Truncated DH object save record");
        const int slot = int(integer(bytes, pos, 2));
        const uint32_t cls = integer(bytes, pos + 2, 4), size = integer(bytes, pos + 6, 2);
        pos += 8;
        if (slot <= previous || slot < first || slot > last || size > bytes.size() - pos ||
            (cls != 0xFFFFFFFF && (cls > 0xFFFF || !c.objects.classByNumber(uint16_t(cls)) ||
             size > c.objects.instanceStaticSize(uint16_t(cls)))))
            throw VM::VmError("Invalid DH object save record");
        out.push_back({slot, cls, Bytes(bytes.begin() + pos, bytes.begin() + pos + size)});
        previous = slot; pos += size;
    }
    return out;
}
void destroy(Context& c, int slot) {
    c.objects.destroyObject(slot);
    c.events.cancelEntityRequests(slot);
}
void restore(Context& c, const std::vector<Record>& saved, int first, int last) {
    size_t i = 0;
    for (int slot = first; slot <= last; ++slot) {
        const Record* r = i < saved.size() && saved[i].slot == slot ? &saved[i++] : nullptr;
        if (!r || r->cls == 0xFFFFFFFF) {
            if (c.objects.objectLookup(slot) != -1) destroy(c, slot);
            continue;
        }
        if (c.objects.classOf(slot) != r->cls) {
            if (c.objects.objectLookup(slot) != -1) destroy(c, slot);
            c.objects.createInstance(slot, uint16_t(r->cls));
        } else {
            c.events.cancelEntityRequests(slot);
        }
        if (!r->data.empty()) std::memcpy(c.objects.staticsPtr(slot, 0, uint32_t(r->data.size())),
                                         r->data.data(), r->data.size());
        c.objects.send(slot, 2, {-1}); // MSG_RESTORE, native restore_range notify=1
    }
}
}

bool trySaveCall(Context& c, const std::string& fn,
                 const std::vector<VM::Value>& a, VM::Value& result) {
    // AESOP.EXE 0f36:10db deletes all slots belonging to this character.
    if (fn == "delete_saves" && !a.empty()) {
        auto titles = savegame::loadSaveDir(file(c, "SAVEGAME.DIR"));
        titles.resize(12);
        for (int slot = 1; slot <= 12; ++slot) {
            const auto path = file(c, numbered("SAVE_", slot, ".BIN"));
            if (!std::filesystem::is_regular_file(path)) continue;
            const auto bytes = read(path);
            if (bytes.size() < 4 || integer(bytes, 0, 4) != uint32_t(a[0])) continue;
            std::error_code ec;
            std::filesystem::remove(path, ec);
            if (ec) throw VM::VmError("Cannot delete DH save");
            std::filesystem::remove(file(c, numbered("ITEMS_", slot, ".BIN")), ec);
            if (ec) throw VM::VmError("Cannot delete DH item save");
            titles[slot - 1] = {};
        }
        std::string directory;
        for (const auto& title : titles)
            directory += (title.used ? title.name : "<empty>") + "\r\n";
        directory += '\x1a';
        if (!write(file(c, "SAVEGAME.DIR"),
                   {reinterpret_cast<const uint8_t*>(directory.data()), directory.size()}))
            throw VM::VmError("Cannot update DH save directory");
        VM::Value ignored;
        eye::tryHandle(c, "read_save_directory", {}, ignored);
        result = 0; return true;
    }
    if (fn == "save_visibility" && a.size() >= 2) {
        if (a[1] < 0 || a[1] >= 25) throw VM::VmError("Invalid DH visibility level");
        auto bytes = read(file(c, "VISIBLE.DAT"));
        const auto* src = staticBytePtr(c, a[0], 1024);
        const size_t pos = size_t(a[1]) * 1024;
        if (!src || pos + 1024 > bytes.size()) throw VM::VmError("Invalid DH visibility buffer");
        std::copy_n(src, 1024, bytes.begin() + pos);
        if (!write(file(c, "VISIBLE.DAT"), bytes)) throw VM::VmError("Cannot save DH visibility");
        result = 0; return true;
    }
    if (fn == "save_game" && a.size() >= 6) {
        const int slot = a[5], level = a[4];
        if (slot < 1 || slot > 12 || level < 0 || level >= 25) {
            result = 0; return true;
        }
        Save save;
        save.character = uint32_t(a[0]); save.seed = uint32_t(a[1]); save.level = uint8_t(level);
        auto* settings = staticBytePtr(c, a[2], 19);
        auto* visited = staticBytePtr(c, a[3], 25);
        if (!settings || !visited) throw VM::VmError("Invalid DH save buffers");
        std::copy_n(settings, 19, save.settings.begin());
        std::copy_n(visited, 25, save.visited.begin());
        const int count = save.settings[0] + 10;
        if (count > 25 || level >= count) throw VM::VmError("Invalid DH dungeon depth");
        if (!savegame::saveRange(c.objects, file(c, numbered("LVL", level, ".TMP")), 1000, 1999) ||
            !savegame::saveRange(c.objects, file(c, numbered("ITEMS_", slot, ".BIN")), 0, 999)) {
            result = 0; return true;
        }
        for (const char* name : {"ITEMS.DAT", "LEVELS.DAT", "VISIBLE.DAT"})
            save.files[std::string("SAVEGAME\\") + name] = read(file(c, name));
        for (int l = 0; l < count; ++l) {
            const auto fea = numbered("FEA", l, ".DAT");
            save.files["SAVEGAME\\" + fea] = read(file(c, fea));
            if (save.visited[l]) {
                const auto lvl = numbered("LVL", l, ".TMP");
                save.files["SAVEGAME\\" + lvl] = read(file(c, lvl));
            }
        }
        result = write(file(c, numbered("SAVE_", slot, ".BIN")), savegame::encodeDHSave(save)) ? 1 : 0;
        return true;
    }
    if (fn == "prepare_save" && !a.empty()) {
        pending.reset();
        const int slot = a[0];
        if (slot < 1 || slot > 12) throw VM::VmError("Invalid DH save slot");
        Save save; std::string error;
        if (!savegame::decodeDHSave(read(file(c, numbered("SAVE_", slot, ".BIN"))), save, error))
            throw VM::VmError(error);
        // Parse all object streams before replacing any live objects.
        const auto items = records(c, read(file(c, numbered("ITEMS_", slot, ".BIN"))), 0, 999);
        for (const char* name : {"ITEMS.DAT", "LEVELS.DAT", "VISIBLE.DAT"})
            if (!save.files.contains(std::string("SAVEGAME\\") + name)) throw VM::VmError("Incomplete DH save");
        for (const auto& [name, bytes] : save.files)
            if (name.ends_with(".TMP")) (void)records(c, bytes, 1000, 1999);
        for (int i = 1; i < 2000; ++i)
            if (c.objects.objectLookup(i) != -1) destroy(c, i);
        restore(c, items, 0, 999);
        result = VM::Value(save.character);
        pending = std::move(save);
        return true;
    }
    if (fn == "explode_save" && a.size() >= 2) {
        if (!pending) throw VM::VmError("explode_save without prepare_save");
        auto* settings = staticBytePtr(c, a[0], 19);
        auto* visited = staticBytePtr(c, a[1], 25);
        if (!settings || !visited) throw VM::VmError("Invalid DH restore buffers");
        Save save = std::move(*pending); pending.reset();
        for (const auto& [name, bytes] : save.files)
            if (!write(file(c, name.substr(9)), bytes)) throw VM::VmError("Cannot unpack DH save");
        std::copy(save.settings.begin(), save.settings.end(), settings);
        std::copy(save.visited.begin(), save.visited.end(), visited);
        restore(c, records(c, read(file(c, numbered("LVL", save.level, ".TMP"))), 1000, 1999), 1000, 1999);
        result = VM::Value(save.seed); return true;
    }
    if (fn == "change_level" && a.size() >= 3) {
        if (a[0] < 0 || a[0] >= 25 || a[1] < 0 || a[1] >= 25)
            throw VM::VmError("Invalid DH level transition");
        std::vector<Record> destination;
        if (a[2]) destination = records(c, read(file(c, numbered("LVL", a[1], ".TMP"))), 1000, 1999);
        if (!savegame::saveRange(c.objects, file(c, numbered("LVL", a[0], ".TMP")), 1000, 1999))
            throw VM::VmError("Cannot save DH level");
        for (int i = 1000; i < 2000; ++i)
            if (c.objects.objectLookup(i) != -1) destroy(c, i);
        if (a[2]) restore(c, destination, 1000, 1999);
        result = 0; return true;
    }
    return false;
}
}
