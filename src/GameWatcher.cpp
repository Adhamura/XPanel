#include "GameWatcher.h"
#include "ProfileRules.h"

#include <algorithm>

#ifdef Q_OS_WIN
#include "GameLibrary.h"
#include <QCoreApplication>
#include <QSet>
#include <QSettings>
#include <windows.h>
#include <tlhelp32.h>
#else
#include <QCoreApplication>
#include <QSet>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#endif

GameWatcher::GameWatcher(QObject *parent) : QObject(parent)
{
    m_timer.setInterval(2000);
    connect(&m_timer, &QTimer::timeout, this, &GameWatcher::poll);
}

void GameWatcher::start()
{
    poll();
    m_timer.start();
}

QString GameWatcher::titleOf(const QString &id) const
{
    const QString t = m_titleOf ? m_titleOf(id) : QString();
    if (!t.isEmpty())
        return t;
    return id.startsWith("steam:") ? tr("Steam app %1").arg(id.mid(6)) : id.section(':', 1);
}

#ifdef Q_OS_WIN
QString GameWatcher::classify(int) const { return {}; }

// Windows: the Steam game from the registry (RunningAppID), plus library apps found by exe name.
void GameWatcher::poll()
{
    QSet<QString> running;
    QList<QString> fresh;
    const quint32 appid = QSettings("HKEY_CURRENT_USER/Software/Valve/Steam", QSettings::NativeFormat)
                              .value("RunningAppID", 0).toUInt();
    if (appid) {
        const QString id = "steam:" + QString::number(appid);
        if (!ProfileRules::isIgnoredSteamAppId(QString::number(appid)) &&
            !(m_titleOf && ProfileRules::isSteamToolName(m_titleOf(id))))
            running.insert(id);
    }
    const QHash<QString, QString> exes = GameLibrary::exeIds();
    if (!exes.isEmpty()) {
        const DWORD self = GetCurrentProcessId();
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W pe;
            pe.dwSize = sizeof pe;
            for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
                if (pe.th32ProcessID == self)
                    continue;
                const auto it = exes.constFind(QString::fromWCharArray(pe.szExeFile).toLower());
                if (it != exes.constEnd())
                    running.insert(it.value());
            }
            CloseHandle(snap);
        }
    }
    QStringList ended;
    for (auto it = m_games.begin(); it != m_games.end();) {
        if (running.contains(it.key())) { ++it; continue; }
        ended << it.key();
        it = m_games.erase(it);
    }
    for (const QString &id : std::as_const(running)) // new games (several at once: any order)
        if (!m_games.contains(id))
            m_games.insert(id, ++m_seq);

    QString newest;
    quint64 best = 0;
    for (auto it = m_games.begin(); it != m_games.end(); ++it)
        if (it.value() > best) { best = it.value(); newest = it.key(); }

    for (const QString &id : std::as_const(ended))
        emit gameEnded(id);
    if (newest != m_current) {
        m_current = newest;
        m_currentTitle = newest.isEmpty() ? QString() : titleOf(newest);
        if (!newest.isEmpty())
            emit gameStarted(m_current, m_currentTitle);
        emit currentChanged();
    }
}
#else

static QByteArray readProc(int pid, const char *file, qsizetype max)
{
    const QByteArray path = "/proc/" + QByteArray::number(pid) + "/" + file;
    const int fd = ::open(path.constData(), O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return {};
    QByteArray out;
    char buf[4096];
    while (out.size() < max) {
        const ssize_t n = ::read(fd, buf, sizeof buf);
        if (n <= 0)
            break;
        out.append(buf, n);
    }
    ::close(fd);
    return out;
}

// The game a process belongs to, "" for everything else. Called once per new pid.
QString GameWatcher::classify(int pid) const
{
    const QByteArray cmdline = readProc(pid, "cmdline", 64 * 1024);
    if (cmdline.isEmpty())
        return {}; // kernel thread, zombie, or gone
    QString id;
    const QString appid = ProfileRules::steamAppIdFromCmdline(cmdline);
    if (!appid.isEmpty())
        id = "steam:" + appid;
    else
        id = ProfileRules::gameIdFromEnviron(readProc(pid, "environ", 256 * 1024)); // only our own processes
    if (id.startsWith("steam:")) {
        if (ProfileRules::isIgnoredSteamAppId(id.mid(6)) ||
            (m_titleOf && ProfileRules::isSteamToolName(m_titleOf(id))))
            return {};
    }
    return id;
}

void GameWatcher::poll()
{
    const int self = int(QCoreApplication::applicationPid());
    DIR *d = ::opendir("/proc");
    if (!d)
        return;
    QSet<int> live;
    QList<int> fresh;
    while (dirent *e = ::readdir(d)) {
        const char *n = e->d_name;
        if (*n < '1' || *n > '9')
            continue;
        bool ok = false;
        const int pid = QByteArray(n).toInt(&ok);
        if (!ok || pid == self)
            continue;
        live.insert(pid);
        if (!m_pids.contains(pid))
            fresh << pid;
    }
    ::closedir(d);
    std::sort(fresh.begin(), fresh.end()); // the first scan: a higher pid counts as newer
    for (int pid : std::as_const(fresh))
        m_pids.insert(pid, classify(pid));
    for (auto it = m_pids.begin(); it != m_pids.end();)
        it = live.contains(it.key()) ? std::next(it) : m_pids.erase(it);

    QSet<QString> running;
    for (const QString &id : std::as_const(m_pids))
        if (!id.isEmpty())
            running.insert(id);
    QStringList ended;
    for (auto it = m_games.begin(); it != m_games.end();) {
        if (running.contains(it.key())) { ++it; continue; }
        ended << it.key();
        it = m_games.erase(it);
    }
    for (int pid : std::as_const(fresh)) { // new games in start order
        const QString id = m_pids.value(pid);
        if (!id.isEmpty() && !m_games.contains(id))
            m_games.insert(id, ++m_seq);
    }

    QString newest;
    quint64 best = 0;
    for (auto it = m_games.begin(); it != m_games.end(); ++it)
        if (it.value() > best) { best = it.value(); newest = it.key(); }

    for (const QString &id : std::as_const(ended))
        emit gameEnded(id);
    if (newest != m_current) {
        m_current = newest;
        m_currentTitle = newest.isEmpty() ? QString() : titleOf(newest);
        if (!newest.isEmpty())
            emit gameStarted(m_current, m_currentTitle);
        emit currentChanged();
    }
}
#endif
