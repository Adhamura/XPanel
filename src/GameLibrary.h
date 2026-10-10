#pragma once

#include <QHash>
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
    // name of a game id, also of Steam tools and hidden games ("" when unknown)
    QString titleOf(const QString &id) const;

signals:
    void gamesChanged();
    void launched(const QString &id);

private:
    QVariantList scanSteam();
    QVariantMap desktopEntry(const QString &path) const;
    Config *m_config;
    QVariantList m_games;
    QHash<QString, QString> m_steamNames; // app id -> name, tools included
};
