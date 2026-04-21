/*
    SPDX-FileCopyrightText: 2019 Aleix Pol Gonzalez <aleixpol@kde.org>

    SPDX-License-Identifier: LGPL-2.0-or-later
*/

#include "startplasma.h"
#include "Constants.h"
#include <KConfig>
#include <KConfigGroup>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDebug>
#include <QProcess>
#include <QFile>
#include <QThread>
#include <QDir>
#include <QFileInfo>

#include <QCoreApplication>
#include <qdbusservicewatcher.h>
#include <signal.h>

#include <pwd.h>
#include <grp.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <cstring>

#include "MessageHandler.h"

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // Install message handler to log to plasmalogin.log
    qInstallMessageHandler(PLASMALOGIN::StartPlasmaMessageHandler);
    createConfigDirectory();
    setupCursor(true);
    signal(SIGTERM, sigtermHandler);

#ifndef HAVE_SYSTEMD
    // non-systemd: Fix environment BEFORE setupPlasmaEnvironment() is called
    // The parent process runs as root, so we need to set correct home for greeter
    {
        // Get the greeter user's home directory for proper environment
        // Some setups may have an empty pw_dir for system users.
        // In that case, fall back to STATE_DIR.
        QString greeterHome;
        struct passwd *pw = getpwnam("plasmalogin");
        if (pw && pw->pw_dir && pw->pw_dir[0] != '\0') {
            greeterHome = QString::fromLocal8Bit(pw->pw_dir);
        } else {
            greeterHome = QStringLiteral(STATE_DIR);
            qWarning() << "BSD: plasmalogin pw_dir missing/empty, falling back HOME to STATE_DIR:" << greeterHome;
        }

        if (!QDir().mkpath(greeterHome)) {
            qWarning() << "BSD: Failed to create/fetch HOME directory:" << greeterHome;
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

        QString xdgRuntimeDir = QStringLiteral("/tmp/runtime-plasmalogin");
        if (!ensureRuntimeDir(xdgRuntimeDir)) {
            qWarning() << "BSD: failed to prepare XDG_RUNTIME_DIR";
        }

        QDir().mkpath(xdgConfigHome);
        QDir().mkpath(xdgCacheHome);
        QDir().mkpath(xdgDataHome);
        qputenv("HOME", greeterHome.toLocal8Bit());
        qputenv("USER", QByteArray("plasmalogin"));
        qputenv("LOGNAME", QByteArray("plasmalogin"));
        qputenv("XDG_CONFIG_HOME", xdgConfigHome.toLocal8Bit());
        qputenv("XDG_CACHE_HOME", xdgCacheHome.toLocal8Bit());
        qputenv("XDG_DATA_HOME", xdgDataHome.toLocal8Bit());
        if (!xdgRuntimeDir.isEmpty()) {
            qputenv("XDG_RUNTIME_DIR", xdgRuntimeDir.toLocal8Bit());
        }

    }
#endif

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

#ifdef HAVE_SYSTEMD
    // Linux: use systemd to start the Plasma session components
    {
        auto msg = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.systemd1"),
                                                  QStringLiteral("/org/freedesktop/systemd1"),
                                                  QStringLiteral("org.freedesktop.systemd1.Manager"),
                                                  QStringLiteral("StartUnit"));
        msg << QStringLiteral("plasma-login-x11.target") << QStringLiteral("fail");
        QDBusReply<QDBusObjectPath> reply = QDBusConnection::sessionBus().call(msg);
        if (!reply.isValid()) {
            qWarning() << "Could not start systemd managed Plasma session:" << reply.error().name() << reply.error().message();
        }
    }
#else
    // No systemd, manually start kwin and greeter
    {
        // Reuse the already-corrected environment from the earlier env fix.
        const QString greeterHome = QString::fromLocal8Bit(qgetenv("HOME"));
        const QString xdgConfigHome = QString::fromLocal8Bit(qgetenv("XDG_CONFIG_HOME"));
        const QString xdgCacheHome = QString::fromLocal8Bit(qgetenv("XDG_CACHE_HOME"));
        const QString xdgDataHome = QString::fromLocal8Bit(qgetenv("XDG_DATA_HOME"));
        const QString xdgRuntimeDir = QString::fromLocal8Bit(qgetenv("XDG_RUNTIME_DIR"));

        if (greeterHome.isEmpty()) {
            qWarning() << "BSD mode: HOME unexpectedly empty before child launch; earlier env fix did not persist";
        }

        // Create proper environment for greeter processes
        QProcessEnvironment greeterEnv = QProcessEnvironment::systemEnvironment();
        greeterEnv.insert(QStringLiteral("HOME"), greeterHome);
        greeterEnv.insert(QStringLiteral("USER"), QStringLiteral("plasmalogin"));
        greeterEnv.insert(QStringLiteral("LOGNAME"), QStringLiteral("plasmalogin"));
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
            qWarning() << "BSD mode: SONICLOGIN_SOCKET is empty!";
        }

        // Start KWin X11 first (required for the greeter)
        QString kwinPath = QStringLiteral(BIN_INSTALL_DIR "/kwin_x11");
        QProcess *kwinProcess = new QProcess();
        kwinProcess->setProcessChannelMode(QProcess::ForwardedChannels);
        kwinProcess->setProcessEnvironment(greeterEnv);
        kwinProcess->start(kwinPath, QStringList());
        if (!kwinProcess->waitForStarted()) {
            qWarning() << "BSD mode: Failed to start kwin_x11:" << kwinProcess->errorString();
        }
        if (kwinProcess->state() != QProcess::Running) {
            qWarning() << "BSD mode: kwin_x11 not running after start, state=" << kwinProcess->state()
                       << "error=" << kwinProcess->errorString();
        }

        // Start the greeter
        QString greeterPath = QStringLiteral(LIBEXEC_INSTALL_DIR "/plasma-login-greeter");
        QProcess *greeterProcess = new QProcess();
        greeterProcess->setProcessChannelMode(QProcess::ForwardedChannels);
        greeterProcess->setProcessEnvironment(greeterEnv);
        greeterProcess->start(greeterPath);
        if (!greeterProcess->waitForStarted()) {
            qWarning() << "BSD mode: Failed to start greeter:" << greeterProcess->errorString();
        }
        if (greeterProcess->state() != QProcess::Running) {
            qWarning() << "BSD mode: greeter not running after start, state=" << greeterProcess->state()
                       << "error=" << greeterProcess->errorString();
        }

        // Start the wallpaper service
        QString wallpaperPath = QStringLiteral(BIN_INSTALL_DIR "/plasma-login-wallpaper");
        QProcess *wallpaperProcess = new QProcess();
        wallpaperProcess->setProcessChannelMode(QProcess::ForwardedChannels);
        wallpaperProcess->setProcessEnvironment(greeterEnv);
        wallpaperProcess->start(wallpaperPath, QStringList());
        if (!wallpaperProcess->waitForStarted()) {
            qWarning() << "BSD mode: Failed to start wallpaper:" << wallpaperProcess->errorString();
        }
        if (wallpaperProcess->state() != QProcess::Running) {
            qWarning() << "BSD mode: wallpaper not running after start, state=" << wallpaperProcess->state()
                       << "error=" << wallpaperProcess->errorString();
        }
    }
#endif

    // stopped by the sigterm handler
    app.exec();

    qDebug() << "stopping";

#ifdef HAVE_SYSTEMD
    // Linux: use systemd to stop the Plasma session components
    {
        auto msg = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.systemd1"),
                                                  QStringLiteral("/org/freedesktop/systemd1"),
                                                  QStringLiteral("org.freedesktop.systemd1.Manager"),
                                                  QStringLiteral("StopUnit"));
        msg << QStringLiteral("plasma-login-x11.target") << QStringLiteral("fail");
        QDBusReply<QDBusObjectPath> reply = QDBusConnection::sessionBus().call(msg);
        if (!reply.isValid()) {
            qWarning() << "Could not stop systemd managed Plasma session:" << reply.error().name() << reply.error().message();
        }
    }

    qDebug() << "final cleanup";

    // systemd returns when the call is made, but not all jobs are torn down
    // this waits until kwin is definitely gone too, which helps logind
    {
        auto msg = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.systemd1"),
                                                  QStringLiteral("/org/freedesktop/systemd1"),
                                                  QStringLiteral("org.freedesktop.systemd1.Manager"),
                                                  QStringLiteral("StopUnit"));
        msg << QStringLiteral("plasma-login-kwin_x11.service") << QStringLiteral("fail");
        QDBusReply<QDBusObjectPath> reply = QDBusConnection::sessionBus().call(msg);
        if (!reply.isValid()) {
            qWarning() << "Could not close up systemd managed Plasma session:" << reply.error().name() << reply.error().message();
        }
    }
#endif

    return 0;
}
