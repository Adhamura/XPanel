#pragma once

#include <QObject>
#include <QVariantMap>

// Read-only facts for Settings > General (Device Info, Device storage, About)
// and User Center > My Products. Values that need root come from xpanel-daemon.
class DeviceInfo : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    Q_INVOKABLE QVariantMap info() const;       // model, cpu, gpu, ram, os, kernel, display, bios
    Q_INVOKABLE QVariantList storage() const;   // [{name, path, total, free}]
    Q_INVOKABLE QVariantMap battery() const;    // health %, cycles, design / full Wh, status
    Q_INVOKABLE QVariantMap user() const;       // login, full name, avatar path
    Q_INVOKABLE QVariantList controllers() const; // external game controllers (not the Ally itself)
};
