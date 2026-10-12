#pragma once
// Windows transport for AllyMcu.h: opens the ASUS N-KEY collection and writes feature reports.
#include "AllyMcu.h"

#include <vector>

class AllyMcuWin {
public:
    AllyMcuWin() = default;
    ~AllyMcuWin() { close(); }
    AllyMcuWin(const AllyMcuWin &) = delete;
    AllyMcuWin &operator=(const AllyMcuWin &) = delete;

    // Opens the collection (VID 0B05, usage page FF31, usage 0076, feature report length 64). False if absent.
    bool open();
    void close();
    bool isOpen() const { return m_h != nullptr; }

    // Built-in controller ON + game mode (self-heal). True when both reports were accepted.
    bool selfHeal();
    // Writes the mapping packets, commit and flush. Empty map = factory mapping. True when every report was accepted.
    bool apply(const std::map<std::string, AllyMcu::Target> &remap);
    bool resetDefaults() { return apply({}); }

private:
    bool send(const AllyMcu::Report &r);
    bool sendAll(const std::vector<AllyMcu::Report> &rs);
    void *m_h = nullptr;
};
