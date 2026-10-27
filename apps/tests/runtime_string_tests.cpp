#include "gtest/gtest.h"
#include "config.hpp"
#include "../thirdeye/runtime/internal.hpp"
#include "../thirdeye/resources/res.hpp"
#include "../thirdeye/vm/objects.hpp"
#include "../thirdeye/vm/events.hpp"
#include "../thirdeye/savegame/transfer.hpp"
#include <cstring>

namespace {
TEST(RuntimeStrings, DungeonHackEndingLaunchPreservesResourceAndBootObject) {
    RESOURCES::Resource res{std::filesystem::path(TESTS_DATA_DIR) / "SAMPLE.RES"};
    VM::ObjectSystem objects;
    VM::EventSystem events(objects);
    std::map<int32_t, int32_t> mem;
    THIRDEYE::savegame::TransferState transfer;
    std::vector<uint8_t> code(14);
    const char strings[] = ".\0aesop.exe\0open\0TM";
    code.insert(code.end(), strings, strings + sizeof(strings));
    VM::Interpreter vm(code);
    THIRDEYE::runtime::Context ctx{objects, events, nullptr, res, mem, transfer, vm};
    auto address = [](uint32_t offset) { return VM::makeAddr(VM::AddrSpace::Code, offset); };
    const bool old = THIRDEYE::runtime::gDungeonHack;
    THIRDEYE::runtime::gDungeonHack = true;
    VM::Value result = 0;
    bool launched = false;
    try {
        THIRDEYE::runtime::rtobject::tryHandle(ctx, "launch",
            {address(14), address(16), address(26), address(31)}, result);
    } catch (const THIRDEYE::runtime::Relaunch& launch) {
        launched = true;
        EXPECT_EQ(launch.program, "aesop.exe");
        EXPECT_EQ(launch.extras, (std::vector<std::string>{"open", "TM"}));
    }
    THIRDEYE::runtime::gDungeonHack = old;
    EXPECT_TRUE(launched);
}

TEST(RuntimeStrings, DungeonHackTextInputIsNotMovement) {
    VM::ObjectSystem objects;
    const bool old = THIRDEYE::runtime::gDungeonHack;
    THIRDEYE::runtime::gDungeonHack = true;
    EXPECT_TRUE(THIRDEYE::runtime::uiScreenActive(objects));
    VM::SopClass kernel;
    kernel.number = 1972;
    kernel.header.static_size = 1140;
    kernel.header.parent = 0xFFFFFFFF;
    kernel.code.resize(14);
    objects.addClass(std::move(kernel));
    int index = objects.createInstance(1972);
    EXPECT_FALSE(THIRDEYE::runtime::uiScreenActive(objects));
    *objects.classStaticPtr(index, 1972, 1136, 2) = 1;
    EXPECT_TRUE(THIRDEYE::runtime::uiScreenActive(objects));
    *objects.classStaticPtr(index, 1972, 1136, 2) = 0;
    VM::SopClass challenge;
    challenge.number = 2926;
    challenge.header.static_size = 65;
    challenge.header.parent = 0xFFFFFFFF;
    challenge.code.resize(14);
    objects.addClass(std::move(challenge));
    objects.createInstance(2926);
    EXPECT_TRUE(THIRDEYE::runtime::uiScreenActive(objects));
    THIRDEYE::runtime::gDungeonHack = old;
}

TEST(RuntimeStrings, DungeonHackConcatenationAndLowercase) {
    RESOURCES::Resource res{std::filesystem::path(TESTS_DATA_DIR) / "SAMPLE.RES"};
    VM::ObjectSystem objects;
    VM::SopClass cls;
    cls.number = 1;
    cls.header.static_size = 64;
    cls.header.parent = 0xFFFFFFFF;
    cls.code.resize(14);
    objects.addClass(std::move(cls));
    int obj = objects.createInstance(1);
    auto* data = objects.staticsPtr(obj, 0, 64);
    std::memcpy(data, "DUNGEON", 8);
    std::memcpy(data + 32, " Hack", 6);
    VM::Interpreter vm(std::vector<uint8_t>(14));
    vm.setExternStatics([&](int index, uint32_t offset, uint32_t size) {
        return objects.staticsPtr(index, offset, size);
    });
    VM::EventSystem events(objects);
    std::map<int32_t, int32_t> mem;
    THIRDEYE::savegame::TransferState transfer;
    THIRDEYE::runtime::Context ctx{objects, events, nullptr, res, mem, transfer, vm};
    const auto dest = VM::makeAddr(VM::AddrSpace::Extern, 0, obj);
    const auto src = VM::makeAddr(VM::AddrSpace::Extern, 32, obj);
    VM::Value result = 0;
    ASSERT_TRUE(THIRDEYE::runtime::rtcode::tryHandle(ctx, "cat_string", {src, dest}, result));
    EXPECT_EQ(result, dest);
    EXPECT_STREQ(reinterpret_cast<const char*>(data), "DUNGEON Hack");
    ASSERT_TRUE(THIRDEYE::runtime::rtcode::tryHandle(ctx, "string_force_lower", {dest}, result));
    EXPECT_EQ(result, dest);
    EXPECT_STREQ(reinterpret_cast<const char*>(data), "dungeon hack");
    const auto overflow = VM::makeAddr(VM::AddrSpace::Extern, 60, obj);
    EXPECT_THROW(THIRDEYE::runtime::rtcode::tryHandle(ctx, "cat_string", {src, overflow}, result), VM::VmError);
}
}
