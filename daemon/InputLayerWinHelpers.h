#pragma once
// Helpers for InputLayerWin.cpp (the only includer): the "control" JSON as a struct, stick / trigger
// curves (copied from the Linux InputLayer.cpp), SendInput wrappers.

#include "AllyMcu.h"
#include "GyroMath.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonObject>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <string>

#include <windows.h>

namespace {

// ---------------------------------------------------------------- settings (the "control" JSON)

struct StickCfg {
    QString mode = "stick"; // stick | mouse | scroll
    double sens = 50, deadzone = 5, outer = 98, anti = 0;
    QString curve = "linear";
};

struct Cfg {
    bool desktop = false;
    QHash<QString, QString> primary, secondary; // button -> "kind:name"
    bool repeatOn = false;
    int repeatMs = 100;
    StickCfg l, r;
    bool mirror = true;
    double ltStart = 5, ltEnd = 96, rtStart = 5, rtEnd = 96;
    double vibL = 100, vibR = 100;
    QString gyroMode = "off";
    bool ratchet = false;
    Gyro::Settings gyro;
    bool swapPaddles = false; // control.swapPaddles: Left / Right paddle names traded
    bool needVirtual = false; // game mode with something only the virtual pad can do (curves, gyro to stick, repeat, ...)
    std::map<std::string, AllyMcu::Target> mcuMap; // game mode: pure button remaps, programmed into the controller MCU
};

const QStringList kButtons = {"A", "B", "X", "Y", "LB", "RB", "LT", "RT", "View", "Menu", "L3", "R3",
                              "D-pad up", "D-pad down", "D-pad left", "D-pad right", "Left paddle", "Right paddle"};

StickCfg parseStick(const QJsonObject &c)
{
    StickCfg s;
    s.mode = c["mode"].toString("stick");
    s.sens = c["sens"].toDouble(50);
    s.deadzone = c["deadzone"].toDouble(5);
    s.outer = c["outer"].toDouble(98);
    s.anti = c["anti"].toDouble(0);
    s.curve = c["curve"].toString("linear");
    return s;
}

// Same selection as the Linux InputLayer::preset(): control.<preset>
std::shared_ptr<const Cfg> parseCfg(const QJsonObject &control)
{
    auto c = std::make_shared<Cfg>();
    const QString name = control["preset"].toString("gamepad");
    const QJsonObject p = control[name].toObject();
    c->desktop = name == "desktop";
    c->swapPaddles = control["swapPaddles"].toBool(false);
    const QJsonObject map = p["map"].toObject();
    for (const QString &b : kButtons) {
        const QJsonObject m = map[b].toObject();
        c->primary[b] = m["primary"].toString(b.contains("paddle") ? "none:Disabled" : "pad:" + b);
        c->secondary[b] = m["secondary"].toString("none:Disabled");
    }
    c->repeatOn = p["repeat"].toObject()["on"].toBool(false);
    c->repeatMs = p["repeat"].toObject()["ms"].toInt(100);
    c->l = parseStick(p["lstick"].toObject());
    c->r = parseStick(p["rstick"].toObject());
    c->mirror = p["trigger"].toObject()["mirror"].toBool(true);
    c->ltStart = p["lt"].toObject()["start"].toDouble(5);
    c->ltEnd = p["lt"].toObject()["end"].toDouble(96);
    c->rtStart = p["rt"].toObject()["start"].toDouble(5);
    c->rtEnd = p["rt"].toObject()["end"].toDouble(96);
    c->vibL = p["vibration"].toObject()["left"].toDouble(100);
    c->vibR = p["vibration"].toObject()["right"].toDouble(100);
    if (!control["vibration"].toBool(true))
        c->vibL = c->vibR = 0;
    const QJsonObject g = p["gyro"].toObject();
    c->gyroMode = g["mode"].toString("off");
    c->ratchet = g["ratchet"].toBool(false);
    c->gyro.sensX = qBound(10.0, g["sensX"].toDouble(100), 300.0); // as Linux gyroSettings()
    c->gyro.sensY = qBound(10.0, g["sensY"].toDouble(100), 300.0);
    c->gyro.axis = g["axis"].toString("yaw") == "roll" ? Gyro::TurnAxis::Roll : Gyro::TurnAxis::Yaw;
    c->gyro.invertX = g["invertX"].toBool(false);
    c->gyro.invertY = g["invertY"].toBool(false);

    // Pure button remaps (pad -> pad / key / mouse button, paddles) go into the controller MCU. Everything the MCU
    // cannot do needs the virtual pad: curves, deadzones, trigger ranges, vibration scale, gyro to stick, repeat, a
    // second function while a paddle is held, disabled buttons, combos, mouse Back / Forward.
    auto plainStick = [](const StickCfg &s) {
        return s.mode == "stick" && s.deadzone == 5 && s.outer == 98 && s.curve == "linear" && s.anti == 0;
    };
    bool virt = !c->desktop && !(plainStick(c->l) && plainStick(c->r) && !c->repeatOn && c->vibL == 100 && c->vibR == 100
                                 && c->gyroMode != "rstick" && c->ltStart == 5 && c->ltEnd == 96
                                 && (c->mirror || (c->rtStart == 5 && c->rtEnd == 96)));
    std::map<std::string, AllyMcu::Target> mcu;
    for (const QString &b : kButtons) {
        const bool paddle = b.contains("paddle");
        const QString plainFn = paddle ? QString("none:Disabled") : "pad:" + b;
        if (!paddle && !c->secondary[b].startsWith("none"))
            virt = true; // second function while the left paddle is held
        if (c->primary[b] == plainFn)
            continue;
        AllyMcu::Target t;
        if (!AllyMcu::targetFor(c->primary[b].toStdString(), &t)) {
            virt = true;
            continue;
        }
        // "swapPaddles" trades the physical paddles: the binding of Left goes to the Right MCU button
        QString phys = b;
        if (paddle && c->swapPaddles)
            phys = b == "Left paddle" ? "Right paddle" : "Left paddle";
        mcu[phys.toStdString()] = t;
    }
    c->needVirtual = !c->desktop && virt;
    if (!c->desktop && !virt)
        c->mcuMap = mcu;
    return c;
}

// Same curve as the Linux InputLayer::shapeStick / shapeTrigger.
double shapeStick(double r, const StickCfg &c)
{
    const double dz = c.deadzone / 100.0, outer = std::max(dz + 0.01, c.outer / 100.0), anti = c.anti / 100.0;
    if (r <= dz)
        return 0;
    double t = std::min(1.0, (r - dz) / (outer - dz));
    if (c.curve == "precise") t = t * t;
    else if (c.curve == "aggressive") t = std::sqrt(t);
    else if (c.curve == "instant") t = 1.0;
    return anti + (1.0 - anti) * t;
}

int shapeTrigger(double p, double startPct, double endPct)
{
    const double start = startPct / 100.0, end = std::max(start + 0.01, endPct / 100.0);
    return int(std::lround(255.0 * std::clamp((p - start) / (end - start), 0.0, 1.0)));
}

// ---------------------------------------------------------------- SendInput (Linux: uinput keyboard + mouse)

struct Vk { WORD vk = 0; bool ext = false; };

const QHash<QString, Vk> &keyTable()
{
    static QHash<QString, Vk> k;
    if (!k.isEmpty())
        return k;
    k = {{"Esc", {VK_ESCAPE}}, {"Enter", {VK_RETURN}}, {"Space", {VK_SPACE}}, {"Tab", {VK_TAB}},
         {"Backspace", {VK_BACK}}, {"Shift", {VK_LSHIFT}}, {"Ctrl", {VK_LCONTROL}}, {"Alt", {VK_LMENU}},
         {"Meta", {VK_LWIN, true}}, {"Up", {VK_UP, true}}, {"Down", {VK_DOWN, true}}, {"Left", {VK_LEFT, true}},
         {"Right", {VK_RIGHT, true}}, {"Page Up", {VK_PRIOR, true}}, {"Page Down", {VK_NEXT, true}},
         {"Home", {VK_HOME, true}}, {"End", {VK_END, true}},
         {"Num +", {VK_ADD}}, {"Num -", {VK_SUBTRACT}}, {"Num *", {VK_MULTIPLY}}, {"Num /", {VK_DIVIDE, true}},
         {"Num Enter", {VK_RETURN, true}}, {"Num .", {VK_DECIMAL}}};
    for (int i = 0; i < 10; ++i)
        k.insert(QString("Num %1").arg(i), {WORD(VK_NUMPAD0 + i)});
    for (int i = 0; i < 12; ++i)
        k.insert(QString("F%1").arg(i + 1), {WORD(VK_F1 + i)});
    for (char ch = 'A'; ch <= 'Z'; ++ch)
        k.insert(QString(QChar(ch)), {WORD(ch)});
    for (char ch = '0'; ch <= '9'; ++ch)
        k.insert(QString(QChar(ch)), {WORD(ch)});
    return k;
}

const QHash<QString, QList<Vk>> &comboTable()
{
    static const QHash<QString, QList<Vk>> t = {
        {"Alt+Tab", {{VK_LMENU}, {VK_TAB}}}, {"Alt+F4", {{VK_LMENU}, {VK_F4}}},
        {"Ctrl+C", {{VK_LCONTROL}, {'C'}}}, {"Ctrl+V", {{VK_LCONTROL}, {'V'}}},
        {"Ctrl+Z", {{VK_LCONTROL}, {'Z'}}}, {"Ctrl+S", {{VK_LCONTROL}, {'S'}}},
        {"Meta+D (desktop)", {{VK_LWIN, true}, {'D'}}}, {"Screenshot", {{VK_SNAPSHOT, true}}},
        {"Volume up", {{VK_VOLUME_UP, true}}}, {"Volume down", {{VK_VOLUME_DOWN, true}}}, {"Mute", {{VK_VOLUME_MUTE, true}}},
    };
    return t;
}

void sendKey(const Vk &k, bool down)
{
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = k.vk;
    in.ki.wScan = WORD(MapVirtualKeyW(k.vk, MAPVK_VK_TO_VSC));
    in.ki.dwFlags = (down ? 0 : KEYEVENTF_KEYUP) | (k.ext ? KEYEVENTF_EXTENDEDKEY : 0);
    SendInput(1, &in, sizeof(in));
}

void sendMouse(DWORD flags, LONG dx = 0, LONG dy = 0, DWORD data = 0)
{
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = flags;
    in.mi.dx = dx;
    in.mi.dy = dy;
    in.mi.mouseData = data;
    SendInput(1, &in, sizeof(in));
}

bool isMouseButton(const QString &n)
{
    return n == "Left click" || n == "Right click" || n == "Middle click" || n == "Back" || n == "Forward";
}

void mouseButton(const QString &name, bool down)
{
    if (name == "Left click") sendMouse(down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP);
    else if (name == "Right click") sendMouse(down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP);
    else if (name == "Middle click") sendMouse(down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP);
    else if (name == "Back") sendMouse(down ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP, 0, 0, XBUTTON1);
    else if (name == "Forward") sendMouse(down ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP, 0, 0, XBUTTON2);
}

} // namespace
