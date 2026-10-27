/*
    SPDX-FileCopyrightText: 2019 Aleix Pol Gonzalez <aleixpol@kde.org>
    SPDX-FileCopyrightText: 2014-2015 Martin Klapetek <mklapetek@kde.org>
    SPDX-FileCopyrightText: 2018 Kai Uwe Broulik <kde@privat.broulik.de>
    SPDX-FileCopyrightText: 2023 Ismael Asensio <isma.af@gmail.com>
    SPDX-FileCopyrightText: 2024 Harald Sitter <sitter@kde.org>

    SPDX-License-Identifier: LGPL-2.0-or-later
*/

// #include <config-startplasma.h>

// #include <canberra.h>

#include <ranges>

#include "Constants.h"
#include "InitSystem.h"
#include <QCoreApplication>
#include <QDBusArgument>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QPoint>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QSize>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QtNumeric>

#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMetaType>
#include <QDBusReply>
#include <QDBusServiceWatcher>

#include <KConfig>
#include <KConfigGroup>
// #include <KDarkLightSchedule>
// #include <KNotifyConfig>
#include <KPackage/Package>
#include <KPackage/PackageLoader>
#include <KScreen/Config>
#include <KScreen/ConfigMonitor>
#include <KScreen/GetConfigOperation>
#include <KScreen/Mode>
#include <KScreen/Output>
#include <KScreen/SetConfigOperation>
#include <KSharedConfig>

#include <cstring>
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// #include <autostartscriptdesktopfile.h>

#include <KUpdateLaunchEnvironmentJob>

#include "start-soniclogin.h"

// #include "../config-workspace.h"
#include "debug.h"
#include "klookandfeelmanager.h"
#include "lookandfeelsettings.h"

#include <Sonic/Logging/Logging>

#include <X11/Xatom.h>
#include <X11/Xcursor/Xcursor.h>
#include <X11/extensions/Xfixes.h>
#include <private/qtx11extras_p.h>

SONIC_LOGGING_COMPONENT("org.sonicde.loginmanager.startx11")

using namespace Qt::StringLiterals;

QTextStream out(stderr);

void sigtermHandler(int signalNumber)
{
    Q_UNUSED(signalNumber)
    if (QCoreApplication::instance()) {
        QCoreApplication::instance()->exit(-1);
    }
}

QStringList allServices(const QLatin1String &prefix)
{
    const QStringList services = QDBusConnection::sessionBus().interface()->registeredServiceNames();
    QStringList names;

    std::copy_if(services.cbegin(), services.cend(), std::back_inserter(names), [&prefix](const QString &serviceName) {
        return serviceName.startsWith(prefix);
    });

    return names;
}

void gentleTermination(QProcess *p)
{
    if (p->state() != QProcess::Running) {
        return;
    }

    p->terminate();

    // Wait longer for a session than a greeter
    if (!p->waitForFinished(5000)) {
        p->kill();
        if (!p->waitForFinished(5000)) {
            qWarning() << "Could not fully finish the process" << p->program();
        }
    }
}

template<typename T>
concept ViewType = std::same_as<QByteArrayView, T> || std::same_as<QStringView, T>;
inline bool isShellVariable(ViewType auto name)
{
    return name == "_"_L1 || name == "SHELL"_L1 || name.startsWith("SHLVL"_L1);
}

inline bool isConfinementVariable(QStringView name)
{
    return name == "SNAP"_L1 || name.startsWith("SNAP_"_L1);
}

inline bool isSessionVariable(QStringView name)
{
    // Check is variable is specific to session.
    return name == "DISPLAY"_L1 || name == "XAUTHORITY"_L1 || //
        name == "WAYLAND_DISPLAY"_L1 || name == "WAYLAND_SOCKET"_L1 || //
        name.startsWith("XDG_"_L1);
}

void setEnvironmentVariable(const char *name, QByteArrayView value)
{
    const QByteArray currentValue = qgetenv(name);
    if (currentValue.isNull() || currentValue != value) {
        qputenv(name, value);
    }
}

void createConfigDirectory()
{
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    qInfo() << "createConfigDirectory: HOME=" << qgetenv("HOME") << "XDG_CONFIG_HOME=" << qgetenv("XDG_CONFIG_HOME") << "resolved=" << configDir;
    if (!QDir().mkpath(configDir)) {
        qWarning() << "Could not create config directory XDG_CONFIG_HOME:" << configDir;
    }
}

void runStartupConfig()
{
    // export LC_* variables set by kcmshell5 formats into environment
    // so it can be picked up by QLocale and friends.
    KConfig config(QStringLiteral("plasma-localerc"));
    KConfigGroup formatsConfig = KConfigGroup(&config, QStringLiteral("Formats"));

    // Note: not all of these (e.g. LC_CTYPE) can currently be changed through system settings (but they can be changed by modifying
    // plasma-localrc manually).
    const auto lcValues = {"LANG",
                           "LC_ADDRESS",
                           "LC_COLLATE",
                           "LC_CTYPE",
                           "LC_IDENTIFICATION",
                           "LC_MONETARY",
                           "LC_MESSAGES",
                           "LC_MEASUREMENT",
                           "LC_NAME",
                           "LC_NUMERIC",
                           "LC_PAPER",
                           "LC_TELEPHONE",
                           "LC_TIME",
                           "LC_ALL"};
    for (auto lc : lcValues) {
        const QString value = formatsConfig.readEntry(lc, QString());
        if (!value.isEmpty()) {
            qputenv(lc, value.toUtf8());
        }
    }

    KConfigGroup languageConfig = KConfigGroup(&config, QStringLiteral("Translations"));
    const QString value = languageConfig.readEntry("LANGUAGE", QString());
    if (!value.isEmpty()) {
        qputenv("LANGUAGE", value.toUtf8());
    }

    if (!formatsConfig.hasKey("LANG") && !qEnvironmentVariableIsEmpty("LANG")) {
        formatsConfig.writeEntry("LANG", qgetenv("LANG"));
        formatsConfig.sync();
    }
}

void applyCursorEnv()
{
    const QString kcminputrcPath = QStringLiteral(STATE_DIR "/.config/kcminputrc");
    const QString syncedIcons = QStringLiteral(STATE_DIR "/.local/share/icons");

    QStringList cursorPath;
    cursorPath << syncedIcons;
    cursorPath << QStringLiteral("/usr/local/share/icons");
    cursorPath << QStringLiteral("/usr/share/icons");
    cursorPath << QStringLiteral("/var/lib/flatpak/exports/share/icons");
    cursorPath << QString(qgetenv("XCURSOR_PATH"));

    QStringList uniquePath;
    for (const QString &entry : std::as_const(cursorPath)) {
        if (!entry.isEmpty() && !uniquePath.contains(entry)) {
            uniquePath.append(entry);
        }
    }
    qputenv("XCURSOR_PATH", uniquePath.join(QLatin1Char(':')).toLocal8Bit());

    const KConfig cfg(kcminputrcPath);
    const KConfigGroup inputCfg = cfg.group(QStringLiteral("Mouse"));

    const QString cursorTheme = inputCfg.readEntry("cursorTheme", QStringLiteral("silver_cursors_dark"));
    const int cursorSize = inputCfg.readEntry("cursorSize", 24);

    qputenv("XCURSOR_THEME", cursorTheme.toLocal8Bit());
    qputenv("XCURSOR_SIZE", QByteArray::number(cursorSize));
}

static void setCursorXResources(const QString &theme, int size)
{
    Display *disp = QX11Info::display();
    if (!disp) {
        return; // defensive; callers run post-QGuiApplication
    }
    const Window root = DefaultRootWindow(disp);

    Atom actualType = None;
    int actualFormat = 0;
    unsigned long nitems = 0;
    unsigned long bytesAfter = 0;
    unsigned char *prop = nullptr;

    // long_length is in 32-bit multiples; 8192 -> 32 KiB, ample for
    // RESOURCE_MANAGER. Returns Success even if the property is absent
    // (actualFormat == 0 in that case). Returned data is null-terminated.
    const int status = XGetWindowProperty(disp, root, XA_RESOURCE_MANAGER, 0, 8192, False, XA_STRING, &actualType, &actualFormat, &nitems, &bytesAfter, &prop);

    QStringList lines;
    if (status == Success && actualFormat == 8 && prop) {
        lines = QString::fromUtf8(reinterpret_cast<const char *>(prop)).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    }
    if (prop) {
        XFree(prop);
    }

    // Remove any existing Xcursor.theme / Xcursor.size entries.
    lines.removeIf([](const QString &l) {
        return l.startsWith(QStringLiteral("Xcursor.theme:")) || l.startsWith(QStringLiteral("Xcursor.size:"));
    });

    lines.append(QStringLiteral("Xcursor.theme:\t%1").arg(theme));
    lines.append(QStringLiteral("Xcursor.size:\t%1").arg(size));

    const QByteArray newData = lines.join(QLatin1Char('\n')).toUtf8();

    XChangeProperty(disp,
                    root,
                    XA_RESOURCE_MANAGER,
                    XA_STRING,
                    8,
                    PropModeReplace,
                    reinterpret_cast<const unsigned char *>(newData.constData()),
                    newData.size()); // nelements = byte count, NO trailing null
    XFlush(disp);
}

bool applyCursorTheme(const QString &theme, int size, bool standalone)
{
    // The systemd setup helper exits before the greeter starts. Give its
    // cursor a separate connection so retaining that resource does not also
    // retain Qt's windows, selections, and other application resources.
    Display *display = standalone ? XOpenDisplay(nullptr) : QX11Info::display();
    if (!display) {
        qWarning() << "Cannot open the X11 display to apply the cursor";
        return false;
    }
    if (!theme.isEmpty()) {
        XcursorSetTheme(display, QFile::encodeName(theme).constData());
    }
    if (size >= 0) {
        XcursorSetDefaultSize(display, size);
    }
    Cursor handle = XcursorLibraryLoadCursor(display, "left_ptr");
    if (handle == None) {
        qWarning() << "Cannot load the X11 cursor for theme" << theme;
        if (standalone) {
            XCloseDisplay(display);
        }
        return false;
    }
    XDefineCursor(display, DefaultRootWindow(display), handle);
    if (standalone) {
        // Keep the cursor ID alive after this short-lived client disconnects.
        // The X server reclaims temporary retained resources on reset.
        XSetCloseDownMode(display, RetainTemporary);
        XSync(display, False);
        XCloseDisplay(display);
    } else {
        XFreeCursor(display, handle);
        XFlush(display);
    }

    // Also publish the theme/size on the root window RESOURCE_MANAGER .
    setCursorXResources(theme, size);
    return true;
}

std::optional<QProcessEnvironment> getSystemdEnvironment()
{
    auto msg = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.systemd1"),
                                              QStringLiteral("/org/freedesktop/systemd1"),
                                              QStringLiteral("org.freedesktop.DBus.Properties"),
                                              QStringLiteral("Get"));
    msg << QStringLiteral("org.freedesktop.systemd1.Manager") << QStringLiteral("Environment");
    auto reply = QDBusConnection::sessionBus().call(msg);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        return std::nullopt;
    }

    // Make sure the returned type is correct.
    auto arguments = reply.arguments();
    if (arguments.isEmpty() || arguments[0].userType() != qMetaTypeId<QDBusVariant>()) {
        return std::nullopt;
    }
    auto variant = qdbus_cast<QVariant>(arguments[0]);
    if (variant.typeId() != QMetaType::QStringList) {
        return std::nullopt;
    }

    const auto assignmentList = variant.toStringList();
    QProcessEnvironment ret;
    for (auto &env : assignmentList) {
        const int idx = env.indexOf(QLatin1Char('='));
        if (Q_LIKELY(idx > 0)) {
            ret.insert(env.left(idx), env.mid(idx + 1));
        }
    }

    return ret;
}

// Import systemd user environment.
//
// Systemd read ~/.config/environment.d which applies to all systemd user unit.
// But it won't work if plasma is not started by systemd.
void importSystemdEnvrionment()
{
    const auto environment = getSystemdEnvironment();
    if (!environment) {
        return;
    }

    const auto keys = environment.value().keys();
    for (const QString &nameStr : keys) {
        if (!isShellVariable(QStringView(nameStr)) && !isSessionVariable(nameStr)) {
            setEnvironmentVariable(nameStr.toLocal8Bit().constData(), environment.value().value(nameStr).toLocal8Bit());
        }
    }
}

/*
static std::optional<std::pair<QString, KLookAndFeelManager::Contents>> dayNightLookAndFeel(const LookAndFeelSettings &settings)
{
    const KConfig lookandfeelautoswitcherstaterc(QStringLiteral("lookandfeelautoswitcherstaterc"), KConfig::SimpleConfig, QStandardPaths::GenericStateLocation);
    const KConfigGroup darkNightCycleGroup(&lookandfeelautoswitcherstaterc, QStringLiteral("DarkLightCycle"));
    if (!darkNightCycleGroup.isValid()) {
        return std::nullopt;
    }

    const std::optional<KDarkLightSchedule> darkLightSchedule =
        KDarkLightSchedule::fromState(darkNightCycleGroup.readEntry(QStringLiteral("SerializedSchedule")));
    if (!darkLightSchedule) {
        return std::nullopt;
    }

    const QDateTime now = QDateTime::currentDateTime();
    const auto previousTransition = darkLightSchedule->previousTransition(now);

    bool wantsDarkTheme = false;
    switch (previousTransition->test(now)) {
    case KDarkLightTransition::Upcoming:
    case KDarkLightTransition::InProgress:
        wantsDarkTheme = previousTransition->type() == KDarkLightTransition::Morning;
        break;
    case KDarkLightTransition::Passed:
        wantsDarkTheme = previousTransition->type() != KDarkLightTransition::Morning;
        break;
    }

    const QString lookAndFeelName = wantsDarkTheme ? settings.defaultDarkLookAndFeel() : settings.defaultLightLookAndFeel();
    return std::make_pair(lookAndFeelName, KLookAndFeelManager::AppearanceSettings);
}
*/

static std::pair<QString, KLookAndFeelManager::Contents> determineLookAndFeel()
{
    const LookAndFeelSettings settings;

    /*
    if (settings.automaticLookAndFeel()) {
        if (const auto lookAndFeel = dayNightLookAndFeel(settings)) {
            return *lookAndFeel;
        }
    }
    */

    return std::make_pair(settings.lookAndFeelPackage(), KLookAndFeelManager::AllSettings);
}

void setupPlasmaEnvironment()
{
    // Manually disable auto scaling because we are scaling above
    // otherwise apps that manually opt in for high DPI get auto scaled by the developer AND manually scaled by us
    qputenv("QT_AUTO_SCREEN_SCALE_FACTOR", "0");
    qputenv("QT_QPA_PLATFORMTHEME", "kde"); // KDE doesn't load by default as we don't register as a full session
    qputenv("KDE_APPLICATIONS_AS_SCOPE", "1");

    // Add kdedefaults dir to allow config defaults overriding from a writable location
    QByteArray currentConfigDirs = qgetenv("XDG_CONFIG_DIRS");
    if (currentConfigDirs.isEmpty()) {
        currentConfigDirs = "/etc/xdg";
    }
    const QString extraConfigDir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QLatin1String("/kdedefaults");
    QDir().mkpath(extraConfigDir);
    qputenv("XDG_CONFIG_DIRS", QByteArray(QFile::encodeName(extraConfigDir) + ':' + currentConfigDirs));

    const auto &[lookAndFeelName, lookAndFeelContents] = determineLookAndFeel();
    QFile activeLnf(extraConfigDir + QLatin1String("/package"));
    if (!activeLnf.open(QIODevice::ReadOnly) || activeLnf.readLine() != lookAndFeelName.toUtf8()) {
        KPackage::Package package = KPackage::PackageLoader::self()->loadPackage(QStringLiteral("Plasma/LookAndFeel"), lookAndFeelName);
        KLookAndFeelManager lnfManager;
        lnfManager.setMode(KLookAndFeelManager::Mode::Defaults);
        lnfManager.save(package, lookAndFeelContents);
    }
    // check if colors changed, if so apply them and discard plasma cache
    {
        KLookAndFeelManager lnfManager;
        lnfManager.setMode(KLookAndFeelManager::Mode::Apply);
        KConfig globals(QStringLiteral("kdeglobals")); // Reload the config
        KConfigGroup generalGroup(&globals, QStringLiteral("General"));
        const QString colorScheme = generalGroup.readEntry("ColorScheme", QStringLiteral("SilverDark"));
        QString path = lnfManager.colorSchemeFile(colorScheme);

        if (!path.isEmpty()) {
            QFile f(path);
            QCryptographicHash hash(QCryptographicHash::Sha1);
            if (f.open(QFile::ReadOnly) && hash.addData(&f)) {
                const QString fileHash = QString::fromUtf8(hash.result().toHex());
                if (fileHash != generalGroup.readEntry("ColorSchemeHash", QString())) {
                    lnfManager.setColors(colorScheme, path);
                    generalGroup.writeEntry("ColorSchemeHash", fileHash);
                    generalGroup.sync();
                }
            }
        }
    }
}

void cleanupPlasmaEnvironment(const std::optional<QProcessEnvironment> &oldSystemdEnvironment)
{
    if (!oldSystemdEnvironment) {
        return;
    }

    auto currentEnv = getSystemdEnvironment();
    if (!currentEnv) {
        return;
    }

    // According to systemd documentation:
    // If a variable is listed in both, the variable is set after this method returns, i.e. the set list overrides the unset list.
    // So this will effectively restore the state to the values in oldSystemdEnvironment.
    QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.systemd1"),
                                                          QStringLiteral("/org/freedesktop/systemd1"),
                                                          QStringLiteral("org.freedesktop.systemd1.Manager"),
                                                          QStringLiteral("UnsetAndSetEnvironment"));
    message.setArguments({currentEnv.value().keys(), oldSystemdEnvironment.value().toStringList()});

    // The session program gonna quit soon, ensure the message is flushed.
    auto reply = QDBusConnection::sessionBus().asyncCall(message);
    reply.waitForFinished();
}

// Drop session-specific variables from the systemd environment.
// Those can be leftovers from previous sessions, which can interfere with the session
// we want to start now, e.g. $DISPLAY might break kwin_wayland.
static void dropSessionVarsFromSystemdEnvironment()
{
    const auto environment = getSystemdEnvironment();
    if (!environment) {
        return;
    }

    QStringList varsToDrop;
    const auto keys = environment.value().keys();
    for (const QString &nameStr : keys) {
        // If it's set in this process, it'll be overwritten by the following UpdateLaunchEnvJob
        if (!qEnvironmentVariableIsSet(nameStr.toLocal8Bit().constData()) && isSessionVariable(nameStr)) {
            varsToDrop.append(nameStr);
        }
    }

    auto msg = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.systemd1"),
                                              QStringLiteral("/org/freedesktop/systemd1"),
                                              QStringLiteral("org.freedesktop.systemd1.Manager"),
                                              QStringLiteral("UnsetEnvironment"));
    msg << varsToDrop;
    auto reply = QDBusConnection::sessionBus().call(msg);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qWarning() << "Failed to unset systemd environment variables:" << reply.errorName() << reply.errorMessage();
    }
}

// kwin_wayland can possibly also start dbus-activated services which need env variables.
// In that case, the update in startplasma might be too late.
bool syncDBusEnvironment()
{
    dropSessionVarsFromSystemdEnvironment();

    // Shell and confinement variables are filtered out of things we explicitly load, but they
    // still might have been inherited from the parent process
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    const auto keys = environment.keys();
    for (const QString &name : keys) {
        if (isShellVariable(QStringView(name)) || isConfinementVariable(QStringView(name))) {
            environment.remove(name);
        }
    }

    // At this point all environment variables are set, let's send it to the DBus session server to update the activation environment
    auto job = new KUpdateLaunchEnvironmentJob(environment);
    QEventLoop e;
    QObject::connect(job, &KUpdateLaunchEnvironmentJob::finished, &e, &QEventLoop::quit);
    e.exec();
    return true;
}

// If something went on an endless restart crash loop it will get blacklisted, as this is a clean login we will want to reset those counters
// This is independent of whether we use the Plasma systemd boot
void resetSystemdFailedUnits()
{
    QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.systemd1"),
                                                          QStringLiteral("/org/freedesktop/systemd1"),
                                                          QStringLiteral("org.freedesktop.systemd1.Manager"),
                                                          QStringLiteral("ResetFailed"));
    QDBusConnection::sessionBus().call(message);
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

static bool waitForWindowManager(int timeoutMs)
{
    qInfo() << "Waiting for window manager readiness (org.kde.KWin)";
    QElapsedTimer elapsed;
    elapsed.start();
    auto *bus = QDBusConnection::sessionBus().interface();
    if (!bus) {
        qWarning() << "Cannot wait for window manager: session bus is unavailable";
        return false;
    }
    do {
        const QDBusReply<bool> registered = bus->isServiceRegistered(QStringLiteral("org.kde.KWin"));
        if (!registered.isValid()) {
            qWarning() << "Cannot query window manager readiness:" << registered.error();
            return false;
        }
        if (registered.value()) {
            return true;
        }
        QThread::msleep(100);
    } while (elapsed.elapsed() < timeoutMs);
    qWarning() << "Timed out waiting for org.kde.KWin after" << timeoutMs << "ms";
    return false;
}

static bool applyKScreen(bool standalone)
{
    // The login compositor cannot use BusName=org.kde.KWin: the desktop unit
    // already declares it, and systemd rejects duplicate bus-name watchers.
    // Its simple service starts before initialization finishes, so gate the
    // setup oneshot here rather than allowing the greeter to race readiness.
    if (standalone && !waitForWindowManager(30000)) {
        return false;
    }
    // Both the direct launcher and systemd's --apply-kscreen oneshot reach
    // this point after SonicWin is ready and before the greeter starts. Cursor
    // initialization must also run when there is no saved KScreen config.
    const KConfig cursorCfg(QStringLiteral("kcminputrc"));
    const KConfigGroup cursorInputCfg = cursorCfg.group(QStringLiteral("Mouse"));
    const auto cursorTheme = cursorInputCfg.readEntry("cursorTheme", QStringLiteral("silver_cursors_dark"));
    const int cursorSize = cursorInputCfg.readEntry("cursorSize", 24);
    if (!applyCursorTheme(cursorTheme, cursorSize, standalone)) {
        return false;
    }

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

    // Logs to ~/.local/state/sonic/start-soniclogin-x11.log, rotated on every run.
    Sonic::Logging::install({.rotateOnStartup = true});
    qInfo() << "StartSonicLoginX11: logging to" << Sonic::Logging::logFilePath() << "as uid" << geteuid();

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
        return applyKScreen(/*standalone=*/true) ? 0 : 1;
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
        QString kwinPath = QStringLiteral(BIN_INSTALL_DIR "/sonic-win");
        QProcess *kwinProcess = new QProcess();
        kwinProcess->setProcessChannelMode(QProcess::ForwardedChannels);
        kwinProcess->setProcessEnvironment(greeterEnv);
        kwinProcess->start(kwinPath, QStringList());
        if (!kwinProcess->waitForStarted()) {
            qWarning() << "StartSonicLoginX11: Failed to start " << kwinPath << "with error:" << kwinProcess->errorString();
        }
        if (kwinProcess->state() != QProcess::Running) {
            qWarning() << "StartSonicLoginX11: sonic-win not running after start, state=" << kwinProcess->state() << "error=" << kwinProcess->errorString();
        }

        // Bounded wait for kwin DBus name to ensure RandR state is populated
        if (!waitForWindowManager(3000)) {
            qWarning() << "StartSonicLoginX11: org.kde.KWin did not register on session bus within timeout";
        }

        // Apply the cursor and synced KScreen config before starting the greeter.
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
            msg << QStringLiteral("soniclogin-sonic-win.service") << QStringLiteral("fail");
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
