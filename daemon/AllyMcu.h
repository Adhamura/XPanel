#pragma once
// ROG Ally controller MCU: button remapping with HID feature reports (report id 0x5A, 64 bytes, "5A D1 ..."),
// sent to the ASUS N-KEY collection (VID 0B05, usage page FF31, usage 0076). This header is plain C++ (no Qt,
// no Windows), so the Linux build can use it too. The transport is AllyMcuWin.h.
//
// Sources of the byte layout and the code tables:
//  - Handheld Companion, ROGAlly.cs (default packets, commit / flush reports, F17 / F18 paddles):
//    https://raw.githubusercontent.com/Valkirie/HandheldCompanion/main/HandheldCompanion/Devices/ASUS/ROGAlly.cs
//  - HHD, rog_ally/const.py (block layout, group and pad codes, comments):
//    https://raw.githubusercontent.com/hhd-dev/hhd/master/src/hhd/device/rog_ally/const.py
//  - Linux hid-asus-ally patch by Luke D. Jones (keyboard, mouse and media code tables, pair / side order):
//    https://lkml.iu.edu/hypermail/linux/kernel/2408.0/05244.html
//
// Packet: 5A D1 02 <group> 2C, then four 11-byte blocks: button 1, button 1 secondary, button 2, button 2
// secondary. Block: [kind, pad code, keyboard code, media code, mouse code, combo length, combo codes x5].
// Kind: 1 pad, 2 keyboard, 3 mouse, 4 combo, 5 media. After the nine groups: commit reports and a flush.
//
// Safety: the "built-in controller OFF" value (5A D1 0B 01 02) and mouse mode (5A D1 01 01 03) are never built here.

#include <array>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace AllyMcu {

using Report = std::array<uint8_t, 64>;

enum Group : uint8_t { DpadUD = 1, DpadLR, Sticks, Shoulders, AB, XY, ViewMenu, Paddles, Triggers };
enum Kind : uint8_t { None = 0, Pad = 1, Key = 2, Mouse = 3 };

struct Target {
    Kind kind = None;
    uint8_t code = 0;
    bool operator==(const Target &o) const { return kind == o.kind && code == o.code; }
};

struct Slot { Group group; int side; }; // side 0 = first button of the pair, 1 = second

// "Built-in Xbox controller ON" and "game mode": the only values of these two commands we ever send.
inline Report controllerOn() { Report r{}; r[0] = 0x5A; r[1] = 0xD1; r[2] = 0x0B; r[3] = 0x01; r[4] = 0x01; return r; }
inline Report gameMode() { Report r{}; r[0] = 0x5A; r[1] = 0xD1; r[2] = 0x01; r[3] = 0x01; r[4] = 0x01; return r; }

// True for a report we are allowed to send (blocks OFF and mouse mode, whatever the caller builds).
inline bool allowed(const Report &r)
{
    if (r[0] != 0x5A || r[1] != 0xD1)
        return false;
    if (r[2] == 0x0B && !(r[3] == 0x01 && r[4] == 0x01))
        return false;
    if (r[2] == 0x01 && !(r[3] == 0x01 && r[4] == 0x01))
        return false;
    return true;
}

namespace detail {

inline Report fromHex(const char *hex) // "5A D1 ..." -> zero padded report
{
    Report r{};
    size_t n = 0;
    for (const char *p = hex; *p && n < r.size();) {
        if (*p == ' ') { ++p; continue; }
        auto v = [](char c) { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; };
        r[n++] = uint8_t(v(p[0]) << 4 | v(p[1]));
        p += 2;
    }
    return r;
}

// The HC *Default arrays, group 1 to 9 (factory mapping). Two fixes from HHD: the A/B packet has 4 zero bytes
// before its combo length (HC has 3), and the M1/M2 packet has all four blocks.
inline const Report &defaultGroup(Group g)
{
    static const Report t[9] = {
        fromHex("5A D1 02 01 2C 01 09 00 00 00 00 00 00 00 00 00 05 00 00 19 00 00 00 00 00 00 00 01 0A 00 00 00 00 00 00 00 00 00 04 00 00 00 00 03 8C 88 76"),
        fromHex("5A D1 02 02 2C 01 0B 00 00 00 00 00 00 00 00 00 04 00 00 00 00 02 82 23 00 00 00 01 0C 00 00 00 00 00 00 00 00 04 00 00 00 00 02 82 0D"),
        fromHex("5A D1 02 03 2C 01 07 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 01 08"),
        fromHex("5A D1 02 04 2C 01 05 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 01 06"),
        fromHex("5A D1 02 05 2C 01 01 00 00 00 00 00 00 00 00 00 05 00 00 16 00 00 00 00 00 00 00 01 02 00 00 00 00 00 00 00 00 00 04 00 00 00 00 02 82 31"),
        fromHex("5A D1 02 06 2C 01 03 00 00 00 00 00 00 00 00 00 04 00 00 00 00 02 82 4D 00 00 00 01 04 00 00 00 00 00 00 00 00 00 05 00 00 1E"),
        fromHex("5A D1 02 07 2C 01 11 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 01 12"),
        fromHex("5A D1 02 08 2C 02 00 8E 00 00 00 00 00 00 00 00 02 00 8E 00 00 00 00 00 00 00 00 02 00 8F 00 00 00 00 00 00 00 00 02 00 8F"),
        fromHex("5A D1 02 09 2C 01 0D 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 01 0E"),
    };
    return t[int(g) - 1];
}

constexpr int kBlock = 11;

inline int blockOffset(int side, bool secondary) { return 5 + side * 2 * kBlock + (secondary ? kBlock : 0); }

} // namespace detail

// The four reports that follow the group packets, then the flush (HC commitReset1of4..4of4 and
// flushBufferWriteChanges).
inline std::vector<Report> commitReports()
{
    return {detail::fromHex("5A D1 0F 20"), detail::fromHex("5A D1 06 02 64 64"),
            detail::fromHex("5A D1 04 04 00 64 00 64"), detail::fromHex("5A D1 05 04 00 64 00 64"),
            detail::fromHex("5A D1 0A 01")};
}

// Physical button (logical name as in qml/ControlDefaults.js) -> group and side. Left paddle = M2 (first block,
// code 0x4B on the 0xFF31 collection), Right paddle = M1 (second block, 0xA5), as in the Linux driver.
inline bool slotFor(const std::string &name, Slot *out)
{
    static const std::map<std::string, Slot> t = {
        {"D-pad up", {DpadUD, 0}}, {"D-pad down", {DpadUD, 1}}, {"D-pad left", {DpadLR, 0}}, {"D-pad right", {DpadLR, 1}},
        {"L3", {Sticks, 0}}, {"R3", {Sticks, 1}}, {"LB", {Shoulders, 0}}, {"RB", {Shoulders, 1}},
        {"A", {AB, 0}}, {"B", {AB, 1}}, {"X", {XY, 0}}, {"Y", {XY, 1}}, {"View", {ViewMenu, 0}}, {"Menu", {ViewMenu, 1}},
        {"Left paddle", {Paddles, 0}}, {"Right paddle", {Paddles, 1}}, {"LT", {Triggers, 0}}, {"RT", {Triggers, 1}}};
    const auto it = t.find(name);
    if (it == t.end())
        return false;
    *out = it->second;
    return true;
}

// Pad button name -> MCU pad code (PAD_* in HHD const.py; LT / RT = 0x0D / 0x0E from the HC trigger default).
inline bool padCode(const std::string &n, uint8_t *c)
{
    static const std::map<std::string, uint8_t> t = {
        {"A", 1}, {"B", 2}, {"X", 3}, {"Y", 4}, {"LB", 5}, {"RB", 6}, {"L3", 7}, {"R3", 8},
        {"D-pad up", 9}, {"D-pad down", 10}, {"D-pad left", 11}, {"D-pad right", 12},
        {"LT", 0x0D}, {"RT", 0x0E}, {"View", 0x11}, {"Menu", 0x12}, {"Guide", 0x13}};
    const auto it = t.find(n);
    if (it == t.end())
        return false;
    *c = it->second;
    return true;
}

// Key name (as in the Control Mode key list) -> MCU keyboard code (Linux patch keyboard_codes; arrows, Home / End,
// Page Up / Down from HHD / HC, Left arrow is 0x9A (the patch lists 0x91, which is Pause)). T is 0x2C.
inline bool keyCode(const std::string &n, uint8_t *c)
{
    static const std::map<std::string, uint8_t> t = {
        {"Esc", 0x76}, {"F1", 0x50}, {"F2", 0x60}, {"F3", 0x40}, {"F4", 0x0C}, {"F5", 0x03}, {"F6", 0x0B},
        {"F7", 0x80}, {"F8", 0x0A}, {"F9", 0x01}, {"F10", 0x09}, {"F11", 0x78}, {"F12", 0x07},
        {"F17", 0x28}, {"F18", 0x30},
        {"1", 0x16}, {"2", 0x1E}, {"3", 0x26}, {"4", 0x25}, {"5", 0x2E}, {"6", 0x36}, {"7", 0x3D}, {"8", 0x3E},
        {"9", 0x46}, {"0", 0x45}, {"Backspace", 0x66}, {"Tab", 0x0D},
        {"Q", 0x15}, {"W", 0x1D}, {"E", 0x24}, {"R", 0x2D}, {"T", 0x2C}, {"Y", 0x35}, {"U", 0x3C}, {"I", 0x43},
        {"O", 0x44}, {"P", 0x4D}, {"A", 0x1C}, {"S", 0x1B}, {"D", 0x23}, {"F", 0x2B}, {"G", 0x34}, {"H", 0x33},
        {"J", 0x3B}, {"K", 0x42}, {"L", 0x4B}, {"Enter", 0x5A}, {"Shift", 0x88},
        {"Z", 0x1A}, {"X", 0x22}, {"C", 0x21}, {"V", 0x2A}, {"B", 0x32}, {"N", 0x31}, {"M", 0x3A},
        {"Ctrl", 0x8C}, {"Meta", 0x82}, {"Alt", 0xBA}, {"Space", 0x29},
        {"Home", 0x94}, {"End", 0x95}, {"Page Up", 0x96}, {"Page Down", 0x97},
        {"Up", 0x99}, {"Down", 0x98}, {"Left", 0x9A}, {"Right", 0x9B},
        {"Num /", 0x90}, {"Num *", 0x7C}, {"Num -", 0x7B}, {"Num +", 0x79}, {"Num Enter", 0x81}, {"Num .", 0x71},
        {"Num 0", 0x70}, {"Num 1", 0x69}, {"Num 2", 0x72}, {"Num 3", 0x7A}, {"Num 4", 0x6B}, {"Num 5", 0x73},
        {"Num 6", 0x74}, {"Num 7", 0x6C}, {"Num 8", 0x75}, {"Num 9", 0x7D}};
    const auto it = t.find(n);
    if (it == t.end())
        return false;
    *c = it->second;
    return true;
}

// Mouse button name -> MCU mouse code (Linux patch mouse_codes). Back / Forward are not in the table.
inline bool mouseCode(const std::string &n, uint8_t *c)
{
    if (n == "Left click") *c = 1;
    else if (n == "Right click") *c = 2;
    else if (n == "Middle click") *c = 3;
    else if (n == "Wheel up") *c = 4;
    else if (n == "Wheel down") *c = 5;
    else return false;
    return true;
}

// "kind:name" (Control Mode function) -> MCU target. False when the MCU cannot do it (combos, Back / Forward,
// disabled, unknown).
inline bool targetFor(const std::string &fn, Target *out)
{
    const size_t colon = fn.find(':');
    if (colon == std::string::npos)
        return false;
    const std::string kind = fn.substr(0, colon), name = fn.substr(colon + 1);
    uint8_t c = 0;
    if (kind == "pad" && padCode(name, &c)) { *out = {Pad, c}; return true; }
    if ((kind == "key" || kind == "numpad") && keyCode(name, &c)) { *out = {Key, c}; return true; }
    if (kind == "mouse" && mouseCode(name, &c)) { *out = {Mouse, c}; return true; }
    return false;
}

// The factory mapping of one button (what the default packet holds in its primary block).
inline Target defaultTarget(const Slot &s)
{
    const Report &d = detail::defaultGroup(s.group);
    const int o = detail::blockOffset(s.side, false);
    if (d[o] == Pad) return {Pad, d[o + 1]};
    if (d[o] == Key) return {Key, d[o + 2]};
    return {};
}

// All packets for a mapping: the nine groups (factory packets with the remapped primary blocks replaced), the
// commit reports and the flush. Empty map = factory mapping.
// Paddles also lose their secondary block (as HC does for F17 / F18).
inline std::vector<Report> buildMapping(const std::map<std::string, Target> &remap)
{
    std::vector<Report> out;
    for (int g = DpadUD; g <= Triggers; ++g) {
        Report r = detail::defaultGroup(Group(g));
        for (const auto &kv : remap) {
            Slot s{};
            if (!slotFor(kv.first, &s) || s.group != g)
                continue;
            const int o = detail::blockOffset(s.side, false);
            std::memset(&r[o], 0, 2 * detail::kBlock); // primary and secondary of this button
            r[o] = kv.second.kind;
            if (kv.second.kind == Pad) r[o + 1] = kv.second.code;
            else if (kv.second.kind == Key) r[o + 2] = kv.second.code;
            else if (kv.second.kind == Mouse) r[o + 4] = kv.second.code;
            if (g != Paddles) { // keep the factory secondary (media / combo shortcut) of normal buttons
                const Report &d = detail::defaultGroup(Group(g));
                std::memcpy(&r[o + detail::kBlock], &d[o + detail::kBlock], detail::kBlock);
            }
        }
        out.push_back(r);
    }
    for (const Report &c : commitReports())
        out.push_back(c);
    return out;
}

} // namespace AllyMcu
