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

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // Install message handler to log to soniclogin.log
    qInstallMessageHandler(SONICLOGIN::StartPlasmaMessageHandler);

    createConfigDirectory();
    setupCursor(true);
    signal(SIGTERM, sigtermHandler);

    // Detect init system once and reuse the result
    const InitSystem initSystem = detectInitSystem();

    // Non-systemd: Fix environment BEFORE setupPlasmaEnvironment() is called
    // The parent process runs as root, so we need to set correct home for greeter
    // This is only needed when not running under systemd (which handles environment differently)
    if (initSystem != InitSystem::Systemd) {
        // Get the greeter user's home directory for proper environment
        // Some setups may have an empty pw_dir for system users.
        // In that case, fall back to STATE_DIR.
        QString greeterHome;
        struct passwd *pw = getpwnam("soniclogin");
        if (pw && pw->pw_dir && pw->pw_dir[0] != '\0') {
            greeterHome = QString::fromLocal8Bit(pw->pw_dir);
        } else {
            greeterHome = QStringLiteral(STATE_DIR);
            qWarning() << "NON-SYSTEMD: Greeter pw_dir missing/empty, falling back HOME to STATE_DIR:" << greeterHome;
        }

        if (!QDir().mkpath(greeterHome)) {
            qWarning() << "NON-SYSTEMD: Failed to create/fetch HOME directory:" << greeterHome;
        }

        const QString xdgConfigHome = greeterHome + QStringLiteral("/.config");
        const QString xdgCacheHome = greeterHome + QStringLiteral("/.cache");
        const QString xdgDataHome = greeterHome + QStringLiteral("/.local/share");
        auto ensureRuntimeDir = [](const QString &path) -> bool {
            if (path.isEmpty()) {
                return false;
            }

            QDir dir;
            if (!dir.mkpath(path)) {
                return false;
            }

            ::chmod(path.toLocal8Bit().constData(), 0700);

            QFileInfo fi(path);
            return fi.exists() && fi.isDir() && fi.isWritable();
        };

        // use XDG_RUNTIME_DIR from environment, fall back to /tmp/xauth_<uid> if not set
        QString xdgRuntimeDir = qEnvironmentVariable("XDG_RUNTIME_DIR");
        if (xdgRuntimeDir.isEmpty()) {
            xdgRuntimeDir = QStringLiteral("/tmp/xauth_%1").arg(getuid());
            qWarning() << "NON-SYSTEMD: XDG_RUNTIME_DIR empty, using fallback:" << xdgRuntimeDir;
        }
        if (!ensureRuntimeDir(xdgRuntimeDir)) {
            qWarning() << "NON-SYSTEMD: Failed to prepare XDG_RUNTIME_DIR";
        }

        QDir().mkpath(xdgConfigHome);
        QDir().mkpath(xdgCacheHome);
        QDir().mkpath(xdgDataHome);
        qputenv("HOME", greeterHome.toLocal8Bit());
        qputenv("USER", QByteArray("soniclogin"));
        qputenv("LOGNAME", QByteArray("soniclogin"));
        qputenv("XDG_CONFIG_HOME", xdgConfigHome.toLocal8Bit());
        qputenv("XDG_CACHE_HOME", xdgCacheHome.toLocal8Bit());
        qputenv("XDG_DATA_HOME", xdgDataHome.toLocal8Bit());
        if (!xdgRuntimeDir.isEmpty()) {
            qputenv("XDG_RUNTIME_DIR", xdgRuntimeDir.toLocal8Bit());
        }
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
            qWarning() << "not a reply org.freedesktop.locale1" << resultMessage;
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
            qWarning() << "Could not start systemd managed Plasma session:" << reply.error().name() << reply.error().message();
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
            qWarning() << "NON-SYSTEMD: HOME unexpectedly empty before child launch; earlier env fix did not persist";
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
            qWarning() << "NON-SYSTEMD: SONICLOGIN_SOCKET is empty!";
        }

        // Start KWin X11 first (required for the greeter)
        QString kwinPath = QStringLiteral(BIN_INSTALL_DIR "/kwin_x11");
        QProcess *kwinProcess = new QProcess();
        kwinProcess->setProcessChannelMode(QProcess::ForwardedChannels);
        kwinProcess->setProcessEnvironment(greeterEnv);
        kwinProcess->start(kwinPath, QStringList());
        if (!kwinProcess->waitForStarted()) {
            qWarning() << "NON-SYSTEMD: Failed to start " << kwinPath << "with error:" << kwinProcess->errorString();
        }
        if (kwinProcess->state() != QProcess::Running) {
            qWarning() << "NON-SYSTEMD: kwin_x11 not running after start, state=" << kwinProcess->state() << "error=" << kwinProcess->errorString();
        }

        // Start the greeter
        QString greeterPath = QStringLiteral(LIBEXEC_INSTALL_DIR "/soniclogin-greeter");
        QProcess *greeterProcess = new QProcess();
        greeterProcess->setProcessChannelMode(QProcess::ForwardedChannels);
        greeterProcess->setProcessEnvironment(greeterEnv);
        greeterProcess->start(greeterPath);
        if (!greeterProcess->waitForStarted()) {
            qWarning() << "NON-SYSTEMD: Failed to start" << greeterPath << "with error:" << greeterProcess->errorString();
        }
        if (greeterProcess->state() != QProcess::Running) {
            qWarning() << "NON-SYSTEMD: greeter not running after start, state=" << greeterProcess->state() << "error=" << greeterProcess->errorString();
        }

        // Start the wallpaper service
        QString wallpaperPath = QStringLiteral(BIN_INSTALL_DIR "/soniclogin-wallpaper");
        QProcess *wallpaperProcess = new QProcess();
        wallpaperProcess->setProcessChannelMode(QProcess::ForwardedChannels);
        wallpaperProcess->setProcessEnvironment(greeterEnv);
        wallpaperProcess->start(wallpaperPath, QStringList());
        if (!wallpaperProcess->waitForStarted()) {
            qWarning() << "NON-SYSTEMD: Failed to start" << wallpaperPath << "with error:" << wallpaperProcess->errorString();
        }
        if (wallpaperProcess->state() != QProcess::Running) {
            qWarning() << "NON-SYSTEMD: wallpaper not running after start, state=" << wallpaperProcess->state() << "error=" << wallpaperProcess->errorString();
        }
    }

    // stopped by the sigterm handler
    app.exec();

    qDebug() << "stopping";

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
            qWarning() << "Could not stop systemd managed Plasma session:" << reply.error().name() << reply.error().message();
        }

        qDebug() << "final cleanup";

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
                qWarning() << "Could not close up systemd managed Plasma session:" << reply.error().name() << reply.error().message();
            }
        }
    }
    // Non-systemd: child processes are children of start-soniclogin-x11.
    // They receive SIGTERM when this process exits via sigtermHandler.

    return 0;
}
