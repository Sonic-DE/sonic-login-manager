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
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>

#include <QDBusConnectionInterface>
#include <QDBusMetaType>
#include <QDBusServiceWatcher>

#include <KConfig>
#include <KConfigGroup>
// #include <KDarkLightSchedule>
// #include <KNotifyConfig>
#include <KPackage/Package>
#include <KPackage/PackageLoader>
#include <KSharedConfig>

#include <unistd.h>

// #include <autostartscriptdesktopfile.h>

#include <KUpdateLaunchEnvironmentJob>

#include "start-soniclogin.h"

// #include "../config-workspace.h"
#include "debug.h"
#include "klookandfeelmanager.h"
#include "lookandfeelsettings.h"

#include <X11/Xatom.h>
#include <X11/Xcursor/Xcursor.h>
#include <X11/extensions/Xfixes.h>
#include <private/qtx11extras_p.h>

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

    const QString cursorTheme = inputCfg.readEntry("cursorTheme", QStringLiteral("breeze_cursors"));
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

void applyCursorTheme(const QString &theme, int size)
{
    if (!theme.isEmpty()) {
        XcursorSetTheme(QX11Info::display(), QFile::encodeName(theme));
    }
    if (size >= 0) {
        XcursorSetDefaultSize(QX11Info::display(), size);
    }
    Cursor handle = XcursorLibraryLoadCursor(QX11Info::display(), "left_ptr");
    XDefineCursor(QX11Info::display(), DefaultRootWindow(QX11Info::display()), handle);
    XFreeCursor(QX11Info::display(), handle);
    XFlush(QX11Info::display());

    // Also publish the theme/size on the root window RESOURCE_MANAGER .
    setCursorXResources(theme, size);
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
        const QString colorScheme = generalGroup.readEntry("ColorScheme", QStringLiteral("BreezeLight"));
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
                    const QString svgCache =
                        QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QLatin1Char('/') + QStringLiteral("plasma-svgelements");
                    if (!svgCache.isEmpty()) {
                        QFile::remove(svgCache);
                    }
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
