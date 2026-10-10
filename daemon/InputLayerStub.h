#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>

// Windows stand-in for InputLayer (InputLayer.h is Linux evdev / uinput). Remapping the Ally
// controller on Windows needs a virtual-controller driver; until then Control Mode settings
// are kept but not applied, and the Quick Panel opens from the tray icon or Win+Shift+Q.
class InputLayer : public QObject {
    Q_OBJECT
public:
    explicit InputLayer(QObject *parent = nullptr) : QObject(parent) {}
    void setControl(const QJsonObject &) {}
    void setGrab(bool) {}
    void calibrateGyro() {}
    void setGyroBias(const QJsonArray &) {}
    QJsonObject status() const { return {{"inputLayer", "not available on Windows yet"}, {"inputGrab", false}}; }

signals:
    void special(const QString &name);
    void nav(const QString &name);
    void action(const QString &name);
    void unknownKey(int code);
    void statusChanged();
    void gyroBiasChanged(const QJsonArray &bias);
};
