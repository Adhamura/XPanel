#pragma once

#include <QObject>
#include <QProcess>
#include <QPointer>
#include <QTimer>
#include <functional>

// Run a program without blocking the UI; call done(stdout, exitCode) at the end.
inline void runProcess(QObject *ctx, const QString &prog, const QStringList &args,
                       std::function<void(const QString &, int)> done = nullptr, int timeoutMs = 60000)
{
    auto *p = new QProcess(ctx);
    QObject::connect(p, &QProcess::finished, ctx, [p, done](int code, QProcess::ExitStatus) {
        if (done)
            done(QString::fromUtf8(p->readAllStandardOutput()), code);
        p->deleteLater();
    });
    QObject::connect(p, &QProcess::errorOccurred, ctx, [p, done](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            if (done)
                done(QString(), -1);
            p->deleteLater();
        }
    });
    p->start(prog, args);
    if (timeoutMs > 0) {
        QPointer<QProcess> guard(p);
        QTimer::singleShot(timeoutMs, ctx, [guard] { if (guard) guard->kill(); });
    }
}
