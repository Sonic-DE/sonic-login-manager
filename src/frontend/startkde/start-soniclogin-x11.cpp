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
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QThread>
#include <QTimer>

#include <KScreen/Config>
#include <KScreen/ConfigMonitor>
#include <KScreen/GetConfigOperation>
#include <KScreen/Mode>
#include <KScreen/Output>
#include <KScreen/SetConfigOperation>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QPoint>
#include <QRegularExpression>
#include <QSet>
#include <QSize>
#include <QtNumeric>
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

#include <X11/Xcursor/Xcursor.h>
#include <private/qtx11extras_p.h>

void StartPlasmaMessageHandler(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    SONICLOGIN::messageHandler(type, QStringLiteral("SONICLOGIN STARTPLASMA"), msg);
}

static QSet<QString> s_lastAppliedLiveHashes;

static void doApply(KScreen::ConfigPtr config, const QString &kscreenDir)
{
    if (!config) {
        return;
    }

    QSet<QString> liveHashes;
    for (const auto &output : config->outputs()) {
        if (output->isConnected()) {
            liveHashes.insert(output->hash());
        }
    }
    qDebug() << "applyKScreen: live connected outputs:" << liveHashes.size();

    if (liveHashes == s_lastAppliedLiveHashes) {
        qDebug() << "applyKScreen: live output set unchanged, skipping";
        return;
    }

    QString candidatePath;
    const QString liveHash = config->connectedOutputsHash();
    const QString liveCandidate = kscreenDir + QLatin1Char('/') + liveHash;
    if (QFile::exists(liveCandidate)) {
        candidatePath = liveCandidate;
        qDebug() << "applyKScreen: using live hash config" << candidatePath;
    } else {
        qDebug() << "applyKScreen: no synced config for live hash" << liveHash << ", scanning for most-recent matching synced config";
        QDir dir(kscreenDir);
        const QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Time | QDir::Reversed);
        QRegularExpression hashRegex(QStringLiteral("^[0-9a-f]{32}$"));
        for (const QFileInfo &fi : entries) {
            if (!hashRegex.match(fi.fileName()).hasMatch()) {
                continue;
            }
            QFile f(fi.absoluteFilePath());
            if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                continue;
            }
            QJsonParseError err;
            const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
            if (doc.isNull() || !doc.isArray()) {
                continue;
            }
            bool intersects = false;
            for (const QVariant &v : doc.toVariant().toList()) {
                const QString id = v.toMap()[QStringLiteral("id")].toString();
                if (liveHashes.contains(id)) {
                    intersects = true;
                    break;
                }
            }
            if (intersects) {
                candidatePath = fi.absoluteFilePath();
                break;
            }
        }
        if (candidatePath.isEmpty()) {
            qDebug() << "applyKScreen: no synced config matches live outputs, skipping";
            return;
        }
        qDebug() << "applyKScreen: using fallback synced config" << candidatePath;
    }

    QFile file(candidatePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "applyKScreen: failed to open" << candidatePath << ":" << file.errorString();
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument jsonDocument = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (jsonDocument.isNull()) {
        qWarning() << "applyKScreen: failed to parse JSON from" << candidatePath << ":" << parseError.errorString();
        return;
    }

    const QVariantList outputsInfo = jsonDocument.toVariant().toList();
    if (outputsInfo.isEmpty()) {
        qDebug() << "applyKScreen: synced JSON is empty";
        return;
    }

    for (const QVariant &entry : outputsInfo) {
        if (entry.typeId() != QMetaType::QVariantMap) {
            qWarning() << "applyKScreen: synced JSON entry is not a map";
            return;
        }
    }

    QMap<KScreen::OutputPtr, uint32_t> priorities;
    int matchedCount = 0;

    for (const auto &output : config->outputs()) {
        if (!output->isConnected()) {
            output->setEnabled(false);
            continue;
        }

        const QString outputHash = output->hash();

        QVariantMap matchedEntry;
        int hashMatchCount = 0;
        for (const QVariant &entry : outputsInfo) {
            const QVariantMap info = entry.toMap();
            if (info[QStringLiteral("id")].toString() == outputHash) {
                ++hashMatchCount;
                matchedEntry = info;
            }
        }

        if (hashMatchCount > 1) {
            bool found = false;
            for (const QVariant &entry : outputsInfo) {
                const QVariantMap info = entry.toMap();
                if (info[QStringLiteral("id")].toString() == outputHash
                    && info[QStringLiteral("metadata")].toMap()[QStringLiteral("name")].toString() == output->name()) {
                    matchedEntry = info;
                    found = true;
                    break;
                }
            }
            if (!found) {
                continue;
            }
        } else if (hashMatchCount == 0) {
            qDebug() << "applyKScreen: no saved entry for output" << output->name() << "(" << outputHash << ")";
            continue;
        }

        ++matchedCount;
        qDebug() << "applyKScreen: matched" << outputHash << "-> output" << output->name() << "(id=" << output->id() << ")";

        const QVariantMap posMap = matchedEntry[QStringLiteral("pos")].toMap();
        output->setPos(QPoint(posMap[QStringLiteral("x")].toInt(), posMap[QStringLiteral("y")].toInt()));

        output->setEnabled(matchedEntry[QStringLiteral("enabled")].toBool());
        output->setRotation(static_cast<KScreen::Output::Rotation>(matchedEntry[QStringLiteral("rotation")].toInt()));
        output->setScale(matchedEntry[QStringLiteral("scale")].toDouble());

        if (matchedEntry.contains(QStringLiteral("mode"))) {
            const QVariantMap modeInfo = matchedEntry[QStringLiteral("mode")].toMap();
            const QVariantMap sizeInfo = modeInfo[QStringLiteral("size")].toMap();
            const int w = sizeInfo[QStringLiteral("width")].toInt();
            const int h = sizeInfo[QStringLiteral("height")].toInt();
            const float refresh = modeInfo[QStringLiteral("refresh")].toFloat();

            bool modeFound = false;
            const auto modes = output->modes();
            for (auto it = modes.constBegin(); it != modes.constEnd(); ++it) {
                const KScreen::ModePtr mode = it.value();
                if (mode->size() == QSize(w, h) && qFuzzyCompare(mode->refreshRate(), refresh)) {
                    output->setCurrentModeId(mode->id());
                    modeFound = true;
                    break;
                }
            }

            if (!modeFound) {
                qWarning() << "applyKScreen: mode" << w << "x" << h << "@" << refresh << "Hz not found for" << output->name() << ", using preferred";
                const QString preferredId = output->preferredModeId();
                if (!preferredId.isEmpty()) {
                    output->setCurrentModeId(preferredId);
                }
            }
        }

        if (matchedEntry.contains(QStringLiteral("vrrpolicy"))) {
            output->setVrrPolicy(static_cast<KScreen::Output::VrrPolicy>(matchedEntry[QStringLiteral("vrrpolicy")].toUInt()));
        }
        if (matchedEntry.contains(QStringLiteral("overscan"))) {
            output->setOverscan(matchedEntry[QStringLiteral("overscan")].toUInt());
        }
        if (matchedEntry.contains(QStringLiteral("rgbrange"))) {
            output->setRgbRange(static_cast<KScreen::Output::RgbRange>(matchedEntry[QStringLiteral("rgbrange")].toUInt()));
        }

        if (matchedEntry.contains(QStringLiteral("priority"))) {
            priorities[output] = matchedEntry[QStringLiteral("priority")].toUInt();
        } else if (matchedEntry.contains(QStringLiteral("primary"))) {
            priorities[output] = matchedEntry[QStringLiteral("primary")].toBool() ? 1u : 2u;
        } else {
            priorities[output] = 2u;
        }
    }

    config->setOutputPriorities(priorities);

    if (!KScreen::Config::canBeApplied(config, KScreen::Config::ValidityFlag::RequireAtLeastOneEnabledScreen)) {
        qWarning() << "applyKScreen: config failed validation, not applying";
        return;
    }

    KScreen::SetConfigOperation setOp(config);
    if (!setOp.exec()) {
        qWarning() << "applyKScreen: SetConfigOperation failed:" << setOp.errorString();
    } else {
        qDebug() << "applyKScreen: applied" << matchedCount << "outputs";
        s_lastAppliedLiveHashes = liveHashes;
    }
}

static bool applyKScreen(bool standalone)
{
    const QString xdgDataHome = QString::fromLocal8Bit(qgetenv("XDG_DATA_HOME"));
    qDebug() << "applyKScreen: starting, XDG_DATA_HOME=" << xdgDataHome << ", DISPLAY=" << qgetenv("DISPLAY");

    const QString kscreenDir = xdgDataHome + QStringLiteral("/kscreen");
    if (!QDir(kscreenDir).exists()) {
        qDebug() << "applyKScreen: kscreen directory does not exist:" << kscreenDir;
        return true;
    }

    KScreen::ConfigPtr config;
    {
        KScreen::GetConfigOperation getOp;
        if (getOp.exec() && getOp.config()) {
            config = getOp.config();
        }
    }
    if (!config) {
        qDebug() << "applyKScreen: GetConfigOperation failed";
        return true;
    }

    KScreen::ConfigMonitor::instance()->addConfig(config);

    doApply(config, kscreenDir);

    QTimer *reapplyTimer = new QTimer(config.data());
    reapplyTimer->setInterval(1000);
    reapplyTimer->setSingleShot(true);

    QObject::connect(KScreen::ConfigMonitor::instance(), &KScreen::ConfigMonitor::configurationChanged, config.data(), [reapplyTimer]() {
        qDebug() << "applyKScreen: configurationChanged — scheduling re-apply in 1s";
        reapplyTimer->start();
    });

    QObject::connect(reapplyTimer, &QTimer::timeout, config.data(), [config, kscreenDir]() {
        qDebug() << "applyKScreen: re-applying after settle delay";
        doApply(config, kscreenDir);
    });

    return true;
}

int main(int argc, char **argv)
{
    QGuiApplication::setDesktopSettingsAware(false);
    applyCursorEnv();
    QGuiApplication app(argc, argv);

    // Install message handler to log to soniclogin.log
    qInstallMessageHandler(StartPlasmaMessageHandler);

    qDebug() << "StartSonicLoginX11: Starting...";

    signal(SIGTERM, sigtermHandler);

    // Point HOME/XDG_* at the soniclogin system account before anything that
    // resolves configs (createConfigDirectory, setupCursor) reads them.
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

    // Check for --apply-kscreen argument
    bool applyOnly = false;
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == QStringLiteral("--apply-kscreen")) {
            applyOnly = true;
            break;
        }
    }

    if (applyOnly) {
        applyKScreen(/*standalone=*/true);
        return 0;
    }

    createConfigDirectory();

    // Detect init system once and reuse the result
    const InitSystem initSystem = detectInitSystem();

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
        // Propagate the cursor theme variables set by setupCursor() so that
        // kwin, the greeter, and the wallpaper service load the same cursor
        // assets from the synced icons directory.
        const QByteArray cursorTheme = qgetenv("XCURSOR_THEME");
        const QByteArray cursorSize = qgetenv("XCURSOR_SIZE");
        const QByteArray cursorPath = qgetenv("XCURSOR_PATH");
        if (!cursorTheme.isEmpty()) {
            greeterEnv.insert(QStringLiteral("XCURSOR_THEME"), QString::fromLocal8Bit(cursorTheme));
        }
        if (!cursorSize.isEmpty()) {
            greeterEnv.insert(QStringLiteral("XCURSOR_SIZE"), QString::fromLocal8Bit(cursorSize));
        }
        if (!cursorPath.isEmpty()) {
            greeterEnv.insert(QStringLiteral("XCURSOR_PATH"), QString::fromLocal8Bit(cursorPath));
        }
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

        // Bounded wait for kwin DBus name to ensure RandR state is populated
        bool kwinRegistered = false;
        for (int i = 0; i < 15; ++i) {
            QDBusReply<bool> reply = QDBusConnection::sessionBus().interface()->isServiceRegistered(QStringLiteral("org.kde.KWin"));
            if (reply.isValid() && reply.value()) {
                kwinRegistered = true;
                break;
            }
            QThread::msleep(200);
        }
        if (!kwinRegistered) {
            qWarning() << "StartSonicLoginX11: org.kde.KWin did not register on session bus within timeout";
        }

        KConfig cursorCfg(QStringLiteral("kcminputrc"));
        KConfigGroup cursorInputCfg = cursorCfg.group(QStringLiteral("Mouse"));
        const QString themeName = cursorInputCfg.readEntry("cursorTheme", QStringLiteral("breeze_cursors"));
        const int themeSize = cursorInputCfg.readEntry("cursorSize", 24);
        applyCursorTheme(themeName, themeSize);

        // Apply synced KScreen config before starting the greeter
        applyKScreen(/*standalone=*/false);

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
