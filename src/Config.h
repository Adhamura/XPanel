#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantMap>

// User settings in ~/.config/XPanel/config.json
class Config : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList tiles READ tiles WRITE setTiles NOTIFY tilesChanged)
    Q_PROPERTY(bool monitorOsd READ monitorOsd WRITE setMonitorOsd NOTIFY monitorOsdChanged)
    // Which Ally button opens the panel: left_button, right_button, none
    Q_PROPERTY(QString button READ button WRITE setButton NOTIFY buttonChanged)

public:
    explicit Config(QObject *parent = nullptr);

    QStringList tiles() const { return m_tiles; }
    void setTiles(const QStringList &t);
    bool monitorOsd() const { return m_monitorOsd; }
    void setMonitorOsd(bool on);
    QString button() const { return m_button; }
    void setButton(const QString &b);

    Q_INVOKABLE void toggleTile(const QString &id);
    Q_INVOKABLE void moveTile(const QString &id, int delta);
    Q_INVOKABLE void resetTiles();

    // General store for the Control Center pages (control mode, per-game profiles,
    // personalization, shortcuts ...). Dotted keys, e.g. "control.gamepad.deadzoneL".
    Q_PROPERTY(int rev READ rev NOTIFY valuesChanged)
    int rev() const { return m_rev; }
    Q_INVOKABLE QVariant get(const QString &key, const QVariant &fallback = {}) const;
    Q_INVOKABLE void set(const QString &key, const QVariant &value);
    Q_INVOKABLE QVariantMap group(const QString &prefix) const; // all keys under "prefix."

signals:
    void tilesChanged();
    void monitorOsdChanged();
    void buttonChanged();
    void valuesChanged();

private:
    void load();
    void save() const;
    QString path() const;

    QStringList m_tiles;
    bool m_monitorOsd = false;
    QString m_button = "left_button";
    QVariantMap m_values;
    int m_rev = 0;
};
