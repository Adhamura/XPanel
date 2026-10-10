#pragma once

#include <QObject>
#include <QVariantList>

class Config;

// Game Library: Steam games (appmanifest files) + installed apps marked as games,
// + apps the user added. Favorites, hidden games and per-game profiles live in Config.
class GameLibrary : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList games READ games NOTIFY gamesChanged)

public:
    explicit GameLibrary(Config *config, QObject *parent = nullptr);
    QVariantList games() const { return m_games; }

    Q_INVOKABLE void rescan();
    Q_INVOKABLE QVariantList installedApps() const; // for "Manage Library" > add
    Q_INVOKABLE void addApp(const QString &desktopId);
    Q_INVOKABLE void removeGame(const QString &id);  // hides it from the library
    Q_INVOKABLE void launch(const QString &id);

signals:
    void gamesChanged();
    void launched(const QString &id); // QML applies the per-game profile

private:
    QVariantList scanSteam() const;
    QVariantMap desktopEntry(const QString &path) const;
    Config *m_config;
    QVariantList m_games;
};
