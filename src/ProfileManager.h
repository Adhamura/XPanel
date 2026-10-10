#pragma once

#include <QObject>
#include <QTimer>
#include <QVariantMap>

class Config;
class DeviceClient;
class GameLibrary;
class GameWatcher;

// Per-game profiles ("game.<id>" in Config: opmode, tdp, fan, control, enabled).
// When a game starts (GameWatcher, or a desktop game launched from the library) its profile is
// applied; the settings it changed are saved in "profiles.restore" and put back when the game
// ends, except the ones the user changed while it ran. The saved record survives a restart of
// XPanel: it is restored at the next start when the game no longer runs.
class ProfileManager : public QObject {
    Q_OBJECT
    // the running game ("" when none), and whether its profile is applied
    Q_PROPERTY(QString activeGame READ activeGame NOTIFY activeChanged)
    Q_PROPERTY(QString activeTitle READ activeTitle NOTIFY activeChanged)
    Q_PROPERTY(bool activeProfile READ activeProfile NOTIFY activeChanged)

public:
    ProfileManager(Config *config, DeviceClient *device, GameWatcher *watcher, GameLibrary *library,
                   QObject *parent = nullptr);

    QString activeGame() const { return m_game; }
    QString activeTitle() const { return m_title; }
    bool activeProfile() const { return m_active; }

    // Game Library "Play": launches the game. A desktop game's profile applies at once; a Steam
    // game's when Steam starts it (also when "use when started from Steam" is off).
    Q_INVOKABLE void play(const QString &id);

signals:
    void activeChanged();

private:
    void sync();
    bool applies(const QString &id) const;
    QVariant current(const QString &key) const;
    void set(const QString &key, const QVariant &value);
    void apply(const QString &id, const QString &title);
    void restore(const QVariantMap &record);

    Config *m_config;
    DeviceClient *m_device;
    GameWatcher *m_watcher;
    GameLibrary *m_library;
    QTimer m_syncTimer;   // coalesces the triggers
    QTimer m_launchTimer; // how long a desktop game may take to show up
    QString m_launch, m_launchTitle; // desktop game started from the library, not seen yet
    QString m_forced;                // Steam game started from the library
    QString m_game, m_title;
    bool m_active = false;
};
