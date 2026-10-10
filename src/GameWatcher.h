#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QTimer>
#include <functional>

// Which game is running: polls /proc every 2 s (Linux; a no-op on Windows).
// Steam games: the "reaper SteamLaunch AppId=<id>" process Steam starts every game with
// (else the SteamAppId variable of the game's processes) -> "steam:<id>".
// Games XPanel starts itself carry XPANEL_GAME=<id> in their environment -> "app:<desktop id>".
// When several games run, the newest one is the current game.
class GameWatcher : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString currentGame READ currentGame NOTIFY currentChanged)
    Q_PROPERTY(QString currentTitle READ currentTitle NOTIFY currentChanged)

public:
    explicit GameWatcher(QObject *parent = nullptr);
    // name of a game id ("steam:<id>" / "app:<id>"); a Steam tool's name makes it ignored
    void setTitleLookup(std::function<QString(const QString &)> f) { m_titleOf = std::move(f); }
    void start(); // first scan right away, then every 2 s

    QString currentGame() const { return m_current; }
    QString currentTitle() const { return m_currentTitle; }
    bool isRunning(const QString &id) const { return m_games.contains(id); }
    Q_INVOKABLE void pollNow() { poll(); }

signals:
    void gameStarted(const QString &id, const QString &title); // became the current game
    void gameEnded(const QString &id);                         // no process of it left
    void currentChanged();

private:
    void poll();
    QString classify(int pid) const;
    QString titleOf(const QString &id) const;

    QTimer m_timer;
    std::function<QString(const QString &)> m_titleOf;
    QHash<int, QString> m_pids;  // every live pid seen -> its game id ("" for others)
    QHash<QString, quint64> m_games; // running game id -> when it was first seen (order)
    quint64 m_seq = 0;
    QString m_current, m_currentTitle;
};
