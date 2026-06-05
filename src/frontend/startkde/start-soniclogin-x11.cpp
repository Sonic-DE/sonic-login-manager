/*
    SPDX-FileCopyrightText: 2019 Aleix Pol Gonzalez <aleixpol@kde.org>

    SPDX-License-Identifier: LGPL-2.0-or-later
*/

#include "Constants.h"
#include "start-soniclogin.h"
#include <KConfig>
#include <KConfigGroup>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QThread>

#include <QCoreApplication>
#include <qdbusservicewatcher.h>
#include <signal.h>

#include <cstring>
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "InitSystem.h"
#include "MessageHandler.h"

void StartPlasmaMessageHandler(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    SONICLOGIN::messageHandler(type, QStringLiteral("SONICLOGIN STARTPLASMA"), msg);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // Install message handler to log to soniclogin.log
    qInstallMessageHandler(StartPlasmaMessageHandler);

    qDebug() << "StartSonicLoginX11: Starting...";

    createConfigDirectory();
    setupCursor(true);
    signal(SIGTERM, sigtermHandler);

    // Detect init system once and reuse the result
    const InitSystem initSystem = detectInitSystem();

    // Ensure greeter home and XDG directories exist before setupPlasmaEnvironment()
    // and before greeter components start. This is needed on both systemd and
    // non-systemd; systemd may provide environment variables but does not create
    // these application-owned directories for the soniclogin system account.
    QString greeterHome = QStringLiteral(STATE_DIR);

    const QString xdgConfigHome = greeterHome + QStringLiteral("/.config");
    const QString xdgCacheHome = greeterHome + QStringLiteral("/.cache");
    const QString xdgDataHome = greeterHome + QStringLiteral("/.local/share");
    const QString xdgStateHome = greeterHome + QStringLiteral("/.local/state");

    qputenv("HOME", greeterHome.toLocal8Bit());
    qputenv("USER", QByteArray("soniclogin"));
    qputenv("LOGNAME", QByteArray("soniclogin"));
    qputenv("XDG_CONFIG_HOME", xdgConfigHome.toLocal8Bit());
    qputenv("XDG_CACHE_HOME", xdgCacheHome.toLocal8Bit());
    qputenv("XDG_DATA_HOME", xdgDataHome.toLocal8Bit());
    qputenv("XDG_STATE_HOME", xdgStateHome.toLocal8Bit());

    // XDG_RUNTIME_DIR can still prefer an existing systemd-provided value.
    QString xdgRuntimeDir = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (xdgRuntimeDir.isEmpty()) {
        xdgRuntimeDir = QStringLiteral(RUNTIME_DIR);
    }

    auto ensureRuntimeDir = [](const QString &path) -> bool {
        if (path.isEmpty()) {
            return false;
        }

        QDir dir;
        if (!dir.mkpath(path)) {
            return false;
        }

        ::chmod(path.toLocal8Bit().constData(), 0755);

        QFileInfo fi(path);
        return fi.exists() && fi.isDir() && fi.isWritable();
    };

    if (!ensureRuntimeDir(xdgRuntimeDir)) {
        qWarning() << "StartSonicLoginX11: Failed to prepare XDG_RUNTIME_DIR:" << xdgRuntimeDir;
    } else {
        qputenv("XDG_RUNTIME_DIR", xdgRuntimeDir.toLocal8Bit());
    }

    // Query whether org.freedesktop.locale1 is available. If it is, try to
    // set XKB_DEFAULT_{MODEL,LAYOUT,VARIANT,OPTIONS} accordingly.
    {
        const QString locale1Service = QStringLiteral("org.freedesktop.locale1");
        const QString locale1Path = QStringLiteral("/org/freedesktop/locale1");
        QDBusMessage message =
            QDBusMessage::createMethodCall(locale1Service, locale1Path, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("GetAll"));
        message << locale1Service;
        QDBusMessage resultMessage = QDBusConnection::systemBus().call(message);
        if (resultMessage.type() == QDBusMessage::ReplyMessage) {
            QVariantMap result;
            QDBusArgument dbusArgument = resultMessage.arguments().at(0).value<QDBusArgument>();
            while (!dbusArgument.atEnd()) {
                dbusArgument >> result;
            }

            auto queryAndSet = [&result](const char *var, const QString &value) {
                const auto r = result.value(value).toString();
                if (!r.isEmpty()) {
                    qputenv(var, r.toUtf8());
                }
            };

            queryAndSet("XKB_DEFAULT_MODEL", QStringLiteral("X11Model"));
            queryAndSet("XKB_DEFAULT_LAYOUT", QStringLiteral("X11Layout"));
            queryAndSet("XKB_DEFAULT_VARIANT", QStringLiteral("X11Variant"));
            queryAndSet("XKB_DEFAULT_OPTIONS", QStringLiteral("X11Options"));
        } else {
            qWarning() << "StartSonicLoginX11: not a reply org.freedesktop.locale1" << resultMessage;
        }
    }

    setupPlasmaEnvironment();
    runStartupConfig();

    auto oldSystemdEnvironment = getSystemdEnvironment();
    if (!syncDBusEnvironment()) {
        out << "Could not sync environment to dbus.\n";
        return 1;
    };

    // Start greeter session components
    if (initSystem == InitSystem::Systemd) {
        // systemd: use DBus to start the Plasma session components
        auto msg = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.systemd1"),
                                                  QStringLiteral("/org/freedesktop/systemd1"),
                                                  QStringLiteral("org.freedesktop.systemd1.Manager"),
                                                  QStringLiteral("StartUnit"));
        msg << QStringLiteral("soniclogin-x11.target") << QStringLiteral("fail");
        QDBusReply<QDBusObjectPath> reply = QDBusConnection::sessionBus().call(msg);
        if (!reply.isValid()) {
            qWarning() << "StartSonicLoginX11: Could not start systemd managed Plasma session:" << reply.error().name() << reply.error().message();
        }
    } else {
        // Generic non-systemd: spawn greeter components directly via QProcess
        // This path is used by OpenRC, sysvinit, dinit, runit, s6, etc.
        // Reuse the already-corrected environment from the earlier env fix.
        const QString greeterHome = QString::fromLocal8Bit(qgetenv("HOME"));
        const QString xdgConfigHome = QString::fromLocal8Bit(qgetenv("XDG_CONFIG_HOME"));
        const QString xdgCacheHome = QString::fromLocal8Bit(qgetenv("XDG_CACHE_HOME"));
        const QString xdgDataHome = QString::fromLocal8Bit(qgetenv("XDG_DATA_HOME"));
        const QString xdgRuntimeDir = QString::fromLocal8Bit(qgetenv("XDG_RUNTIME_DIR"));

        if (greeterHome.isEmpty()) {
            qWarning() << "StartSonicLoginX11: HOME unexpectedly empty before child launch; earlier env fix did not persist";
        }

        // Create proper environment for greeter processes
        QProcessEnvironment greeterEnv = QProcessEnvironment::systemEnvironment();
        greeterEnv.insert(QStringLiteral("HOME"), greeterHome);
        greeterEnv.insert(QStringLiteral("USER"), QStringLiteral("soniclogin"));
        greeterEnv.insert(QStringLiteral("LOGNAME"), QStringLiteral("soniclogin"));
        // Pass through XDG directories from the earlier corrected flow.
        greeterEnv.insert(QStringLiteral("XDG_CONFIG_HOME"), xdgConfigHome);
        greeterEnv.insert(QStringLiteral("XDG_CACHE_HOME"), xdgCacheHome);
        greeterEnv.insert(QStringLiteral("XDG_DATA_HOME"), xdgDataHome);
        greeterEnv.insert(QStringLiteral("XDG_RUNTIME_DIR"), xdgRuntimeDir);
        // Pass socket path to greeter via environment
        QString socketPath = qgetenv("SONICLOGIN_SOCKET");
        if (!socketPath.isEmpty()) {
            greeterEnv.insert("SONICLOGIN_SOCKET", socketPath);
        } else {
            qWarning() << "StartSonicLoginX11: SONICLOGIN_SOCKET is empty!";
        }

        // Start KWin X11 first (required for the greeter)
        QString kwinPath = QStringLiteral(BIN_INSTALL_DIR "/kwin_x11");
        QProcess *kwinProcess = new QProcess();
        kwinProcess->setProcessChannelMode(QProcess::ForwardedChannels);
        kwinProcess->setProcessEnvironment(greeterEnv);
        kwinProcess->start(kwinPath, QStringList());
        if (!kwinProcess->waitForStarted()) {
            qWarning() << "StartSonicLoginX11: Failed to start " << kwinPath << "with error:" << kwinProcess->errorString();
        }
        if (kwinProcess->state() != QProcess::Running) {
            qWarning() << "StartSonicLoginX11: kwin_x11 not running after start, state=" << kwinProcess->state() << "error=" << kwinProcess->errorString();
        }

        // Start the greeter
        QString greeterPath = QStringLiteral(LIBEXEC_INSTALL_DIR "/soniclogin-greeter");
        QProcess *greeterProcess = new QProcess();
        greeterProcess->setProcessChannelMode(QProcess::ForwardedChannels);
        greeterProcess->setProcessEnvironment(greeterEnv);
        greeterProcess->start(greeterPath);
        if (!greeterProcess->waitForStarted()) {
            qWarning() << "StartSonicLoginX11: Failed to start" << greeterPath << "with error:" << greeterProcess->errorString();
        }
        if (greeterProcess->state() != QProcess::Running) {
            qWarning() << "StartSonicLoginX11: greeter not running after start, state=" << greeterProcess->state() << "error=" << greeterProcess->errorString();
        }

        // Start the wallpaper service
        QString wallpaperPath = QStringLiteral(BIN_INSTALL_DIR "/soniclogin-wallpaper");
        QProcess *wallpaperProcess = new QProcess();
        wallpaperProcess->setProcessChannelMode(QProcess::ForwardedChannels);
        wallpaperProcess->setProcessEnvironment(greeterEnv);
        wallpaperProcess->start(wallpaperPath, QStringList());
        if (!wallpaperProcess->waitForStarted()) {
            qWarning() << "StartSonicLoginX11: Failed to start" << wallpaperPath << "with error:" << wallpaperProcess->errorString();
        }
        if (wallpaperProcess->state() != QProcess::Running) {
            qWarning() << "StartSonicLoginX11: wallpaper not running after start, state=" << wallpaperProcess->state()
                       << "error=" << wallpaperProcess->errorString();
        }
    }

    // stopped by the sigterm handler
    app.exec();

    qDebug() << "StartSonicLoginX11: stopping";

    // Stop greeter session components
    if (initSystem == InitSystem::Systemd) {
        // systemd: use DBus to stop the Plasma session components
        auto msg = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.systemd1"),
                                                  QStringLiteral("/org/freedesktop/systemd1"),
                                                  QStringLiteral("org.freedesktop.systemd1.Manager"),
                                                  QStringLiteral("StopUnit"));
        msg << QStringLiteral("soniclogin-x11.target") << QStringLiteral("fail");
        QDBusReply<QDBusObjectPath> reply = QDBusConnection::sessionBus().call(msg);
        if (!reply.isValid()) {
            qWarning() << "StartSonicLoginX11: Could not stop systemd managed Plasma session:" << reply.error().name() << reply.error().message();
        }

        qDebug() << "StartSonicLoginX11: final cleanup";

        // systemd returns when the call is made, but not all jobs are torn down
        // this waits until kwin is definitely gone too, which helps logind
        {
            auto msg = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.systemd1"),
                                                      QStringLiteral("/org/freedesktop/systemd1"),
                                                      QStringLiteral("org.freedesktop.systemd1.Manager"),
                                                      QStringLiteral("StopUnit"));
            msg << QStringLiteral("soniclogin-kwin_x11.service") << QStringLiteral("fail");
            QDBusReply<QDBusObjectPath> reply = QDBusConnection::sessionBus().call(msg);
            if (!reply.isValid()) {
                qWarning() << "StartSonicLoginX11: Could not close up systemd managed Plasma session:" << reply.error().name() << reply.error().message();
            }
        }
    }
    // Non-systemd: child processes are children of start-soniclogin-x11.
    // They receive SIGTERM when this process exits via sigtermHandler.

    return 0;
}
