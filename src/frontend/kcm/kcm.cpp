/*
 *  SPDX-FileCopyrightText: 2014 Martin Gräßlin <mgraesslin@kde.org>
 *  SPDX-FileCopyrightText: 2019 Kevin Ottens <kevin.ottens@enioka.com>
 *  SPDX-FileCopyrightText: 2020 David Redondo <kde@david-redondo.de>
 *  SPDX-FileCopyrightText: 2025 Oliver Beard <olib141@outlook.com>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>
#include <QtConcurrentRun>
#include <utility>

#include <pwd.h>
#include <unistd.h>

#include <KConfigLoader>
#include <KConfigPropertyMap>
#include <KIO/ApplicationLauncherJob>
#include <KLazyLocalizedString>
#include <KLocalizedString>
#include <KPluginFactory>
#include <KService>
#include <KUser>

#include "ThemeSync.h"
#include "models/sessionmodel.h"
#include "models/usermodel.h"
#include "soniclogindata.h"
#include "wallpapersettings.h"

#include "daemonrequest.h"
#include "kcm.h"

K_PLUGIN_FACTORY_WITH_JSON(SonicLoginKcmFactory, "kcm_soniclogin.json", registerPlugin<SonicLoginKcm>(); registerPlugin<SonicLoginData>();)

SonicLoginKcm::SonicLoginKcm(QObject *parent, const KPluginMetaData &data)
    : KQuickManagedConfigModule(parent, data)
    , m_wallpaperSettings(new WallpaperSettings(this))
{
    setAuthActionName(QStringLiteral("org.kde.kcontrol.kcmsoniclogin.save"));
    registerSettings(&SonicLoginSettings::getInstance());

    constexpr const char *url = "org.kde.private.kcms.soniclogin";
    qRegisterMetaType<QList<WallpaperInfo>>("QList<WallpaperInfo>");
    qmlRegisterAnonymousType<SonicLoginSettings>(url, 1);
    qmlRegisterAnonymousType<WallpaperInfo>(url, 1);
    qmlRegisterAnonymousType<WallpaperIntegration>(url, 1);
    qmlRegisterAnonymousType<KConfigPropertyMap>(url, 1);
    qmlRegisterAnonymousType<UserModel>(url, 1);
    qmlRegisterAnonymousType<SessionModel>(url, 1);
    qmlProtectModule(url, 1);

    constexpr const char *uri = "org.kde.plasma.plasmoid";
    qmlRegisterUncreatableType<QObject>(uri, 2, 0, "PlasmoidPlaceholder", QStringLiteral("Do not create objects of type Plasmoid"));

    connect(&SonicLoginSettings::getInstance(), &SonicLoginSettings::WallpaperPluginIdChanged, m_wallpaperSettings, &WallpaperSettings::loadWallpaperConfig);
    connect(m_wallpaperSettings, &WallpaperSettings::currentWallpaperChanged, this, &SonicLoginKcm::currentWallpaperChanged);
}

void SonicLoginKcm::load()
{
    KQuickManagedConfigModule::load();
    m_wallpaperSettings->load();

    updateState();
    Q_EMIT loadCalled();
}

void SonicLoginKcm::save()
{
    if (busy()) {
        return;
    }
    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        Q_EMIT errorOccurred(kxi18n("Unable to save settings because a temporary "
                                    "directory could not be created.")
                                 .toString());
        return;
    }

    const QString tempFileName = tempDir.path() + QLatin1String("/soniclogin.conf");
    KConfig tempConfig(tempFileName, KConfig::SimpleConfig);

    for (const auto &item : SonicLoginSettings::getInstance().items()) {
        if (!item->isDefault()) {
            tempConfig.group(item->group()).writeEntry(item->key(), item->property());
        }
    }

    KCoreConfigSkeleton *wallpaperSkeleton = m_wallpaperSettings->wallpaperSkeleton();
    KConfigPropertyMap *wallpaperConfig = wallpaperConfiguration();
    const QString wallpaperPluginId = SonicLoginSettings::getInstance().wallpaperPluginId();
    if (wallpaperSkeleton && wallpaperConfig) {
        for (const auto item : wallpaperSkeleton->items()) {
            if (item->isDefault()) {
                continue;
            }
            tempConfig.group(QLatin1String("Greeter"))
                .group(QLatin1String("Wallpaper"))
                .group(wallpaperPluginId)
                .group(QLatin1String("General"))
                .writeEntry(item->key(), wallpaperConfig->value(item->key()));
        }
    } else {
        qWarning() << "save: no wallpaper configuration available; skipping wallpaper settings";
    }

    QJsonObject wallpaperPayload;
    auto wallpaperGroup = tempConfig.group(QLatin1String("Greeter")).group(QLatin1String("Wallpaper")).group(wallpaperPluginId);
    if (wallpaperGroup.exists()) {
        wallpaperGroup.group(QLatin1String("General")).deleteEntry(QLatin1String("PreviewImage"));

        const QUrl imageUri = QUrl(wallpaperGroup.group(QLatin1String("General")).readEntry(QLatin1String("Image"))).adjusted(QUrl::StripTrailingSlash);
        if (imageUri.isLocalFile()) {
            wallpaperPayload = collectWallpapers(imageUri);

            QUrl adjustedUri = QUrl();
            adjustedUri.setScheme(QStringLiteral("file"));
            adjustedUri.setPath(KUser(QStringLiteral("soniclogin")).homeDir() + QStringLiteral("/wallpapers/") + imageUri.fileName());
            adjustedUri.setFragment(imageUri.fragment());
            wallpaperGroup.group(QLatin1String("General")).writeEntry(QLatin1String("Image"), adjustedUri);
        }
    }

    const bool configHasContent = tempConfig.isDirty();
    if (configHasContent) {
        tempConfig.sync();
    }

    QString config;
    if (configHasContent) {
        QFile tempFile(tempFileName);
        if (!tempFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            Q_EMIT errorOccurred(kxi18n("Unable to save settings because the config could not be opened.").toString());
            return;
        }
        QTextStream in(&tempFile);
        config = in.readAll();
    }

    // Stage the continuation for auth.
    QByteArray argsJson;
    {
        QJsonObject envelope;
        envelope[QStringLiteral("op")] = QStringLiteral("save");
        envelope[QStringLiteral("config")] = config;
        envelope[QStringLiteral("wallpapers")] = wallpaperPayload;
        argsJson = QJsonDocument(envelope).toJson(QJsonDocument::Compact);
    }

    m_pendingContinuation = [this, argsJson]() {
        QJsonObject args = QJsonDocument::fromJson(argsJson).object();
        sendToDaemon(QStringLiteral("save"), args);
    };

    if (m_pendingAuthUser.isEmpty()) {
        // Take ownership of the save so the host stops calling save() in a
        // tight loop while we wait for the user's admin credentials.
        setNeedsSave(false);
        Q_EMIT authRequired();
    } else {
        setNeedsSave(false);
        m_pendingContinuation();
        m_pendingContinuation = nullptr;
    }
}

QJsonObject SonicLoginKcm::collectWallpapers(const QUrl &imageUri)
{
    const QString baseName = imageUri.fileName();
    const QString imagePath = imageUri.toLocalFile();

    QJsonObject result;

    if (baseName.isEmpty()) {
        return result;
    }

    if (imagePath.isEmpty()) {
        return result;
    }

    auto addFile = [&result](const QString &relativePath, const QString &fullPath) {
        QFile imageFile(fullPath);
        if (imageFile.open(QIODevice::ReadOnly)) {
            result[relativePath] = QString::fromLatin1(imageFile.readAll().toBase64());
        } else {
            qWarning() << "Could not read file" << fullPath;
        }
    };

    QFileInfo fileInfo(imagePath);
    if (fileInfo.isDir()) {
        addFile(baseName + "/metadata.json", imagePath + "/metadata.json");
        QDir imagesDir(imagePath + "/contents/images");
        for (const QString &imageFileName : imagesDir.entryList(QDir::Files)) {
            addFile(baseName + "/contents/images/" + imageFileName, imagePath + "/contents/images/" + imageFileName);
        }
        QDir darkImagesDir(imagePath + "/contents/images_dark");
        for (const QString &imageFileName : darkImagesDir.entryList(QDir::Files)) {
            addFile(baseName + "/contents/images_dark/" + imageFileName, imagePath + "/contents/images_dark/" + imageFileName);
        }
    } else {
        addFile(baseName, imagePath);
    }
    return result;
}

namespace {
struct SyncArguments {
    QJsonObject arguments;
    QString error;
};

SyncArguments collectSyncArguments()
{
    QJsonObject files;
    auto addConfigFile = [&files](const QString &path, const QString &key) {
        if (path.isEmpty()) {
            return;
        }
        QFile file(path);
        if (file.open(QFile::ReadOnly | QFile::Text)) {
            QTextStream in(&file);
            files[key] = in.readAll();
        }
    };

    addConfigFile(QStandardPaths::locate(QStandardPaths::GenericConfigLocation, QStringLiteral("kdeglobals")), QStringLiteral("kdeglobals"));
    addConfigFile(QStandardPaths::locate(QStandardPaths::GenericConfigLocation, QStringLiteral("plasmarc")), QStringLiteral("plasmarc"));
    addConfigFile(QStandardPaths::locate(QStandardPaths::GenericConfigLocation, QStringLiteral("plasma-localerc")), QStringLiteral("plasma-localerc"));
    addConfigFile(QStandardPaths::locate(QStandardPaths::GenericConfigLocation, QStringLiteral("kcminputrc")), QStringLiteral("kcminputrc"));
    addConfigFile(QStandardPaths::locate(QStandardPaths::GenericConfigLocation, QStringLiteral("kwinoutputconfig.json")),
                  QStringLiteral("kwinoutputconfig.json"));

    const QString fontconfigPath = QStandardPaths::locate(QStandardPaths::GenericConfigLocation, QStringLiteral("fontconfig"), QStandardPaths::LocateDirectory);
    if (!fontconfigPath.isEmpty()) {
        addConfigFile(fontconfigPath + QStringLiteral("/fonts.conf"), QStringLiteral("fontconfig/fonts.conf"));
    }

    addConfigFile(QStandardPaths::locate(QStandardPaths::GenericConfigLocation, QStringLiteral("kxkbrc")), QStringLiteral("kxkbrc"));

    QJsonObject themes;
    QMap<QString, QString> themeConfigs;
    QMap<QString, QByteArray> themeAssets;
    QString themeError;
    if (!SONICLOGIN::ThemeSync::collectSettings(&themeConfigs, &themeAssets, &themeError)) {
        return {{}, kxi18n("Unable to copy desktop themes: %1").subs(themeError).toString()};
    }
    for (auto it = themeConfigs.cbegin(); it != themeConfigs.cend(); ++it) {
        files[it.key()] = it.value();
    }
    for (auto it = themeAssets.cbegin(); it != themeAssets.cend(); ++it) {
        themes[it.key()] = QString::fromLatin1(it.value().toBase64());
    }

    QJsonObject arguments;
    {
        QJsonObject args;
        args[QStringLiteral("files")] = files;

        // Collect KScreen per-output and combined config JSONs from
        // ~/.local/share/kscreen/.
        QJsonObject kscreenFiles;
        const QString kscreenDataDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/kscreen");
        auto addDir = [&kscreenFiles, &kscreenDataDir](const QString &subdir) {
            const QString root = kscreenDataDir + QLatin1Char('/') + subdir;
            QDir d(root);
            if (!d.exists()) {
                return;
            }
            const QFileInfoList entries = d.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
            for (const QFileInfo &entry : entries) {
                QFile f(entry.absoluteFilePath());
                if (!f.open(QFile::ReadOnly)) {
                    continue;
                }
                const QString rel = subdir + QLatin1Char('/') + entry.fileName();
                kscreenFiles[rel] = QString::fromLatin1(f.readAll().toBase64());
            }
        };
        // The top-level $XDG_DATA_HOME/kscreen/<hash> files hold the full
        // combined config (positions, primary, modes, rotations, per-output
        // enable).
        if (QDir d(kscreenDataDir); d.exists()) {
            const QFileInfoList rootEntries = d.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
            for (const QFileInfo &entry : rootEntries) {
                const QString fileName = entry.fileName();
                if (fileName == QStringLiteral("kscreen.log")) {
                    continue;
                }
                QFile f(entry.absoluteFilePath());
                if (!f.open(QFile::ReadOnly)) {
                    continue;
                }
                const QByteArray bytes = f.readAll();
                // Only ship files that look like a KScreen config
                // (a JSON array of output records)
                QJsonParseError err;
                const QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
                if (err.error != QJsonParseError::NoError || !doc.isArray()) {
                    continue;
                }
                kscreenFiles[fileName] = QString::fromLatin1(bytes.toBase64());
            }
        }
        addDir(QStringLiteral("outputs"));
        addDir(QStringLiteral("control/configs"));

        qInfo() << "synchronizeSettings: collected" << kscreenFiles.size() << "kscreen entries from" << kscreenDataDir;
        for (auto it = kscreenFiles.constBegin(); it != kscreenFiles.constEnd(); ++it) {
            const QString rel = it.key();
            const QByteArray decoded = QByteArray::fromBase64(it.value().toString().toLatin1());
            qInfo() << "synchronizeSettings:   kscreen file" << rel << "size=" << decoded.size();
        }

        args[QStringLiteral("themes")] = themes;
        args[QStringLiteral("kscreen")] = kscreenFiles;
        arguments = args;
    }

    return {arguments, {}};
}
}

void SonicLoginKcm::synchronizeSettings()
{
    if (busy()) {
        return;
    }
    if (KUser("soniclogin").homeDir().isEmpty()) {
        Q_EMIT errorOccurred(kxi18n("Unable to synchronise Plasma settings because the 'soniclogin' "
                                    "user does not exist. Please check your Sonic Login install.")
                                 .toString());
        return;
    }
    setOperationStatus(kxi18n("Preparing selected desktop themes…").toString());
    auto *watcher = new QFutureWatcher<SyncArguments>(this);
    connect(watcher, &QFutureWatcher<SyncArguments>::finished, this, [this, watcher]() {
        const auto result = watcher->result();
        watcher->deleteLater();
        setOperationStatus({});
        if (!result.error.isEmpty()) {
            Q_EMIT errorOccurred(result.error);
            return;
        }
        m_pendingContinuation = [this, args = result.arguments]() {
            sendToDaemon(QStringLiteral("sync"), args);
        };
        setNeedsSave(false);
        if (m_pendingAuthUser.isEmpty()) {
            Q_EMIT authRequired();
        } else {
            m_pendingContinuation();
            m_pendingContinuation = nullptr;
        }
    });
    // Worker inputs/results are plain values; no GUI object is accessed there.
    // Destroying the page safely disconnects the watcher callback.
    watcher->setFuture(QtConcurrent::run(collectSyncArguments));
}

void SonicLoginKcm::resetSynchronizedSettings()
{
    if (busy()) {
        return;
    }
    if (KUser("soniclogin").homeDir().isEmpty()) {
        Q_EMIT errorOccurred(kxi18n("Unable to reset Plasma settings because the 'soniclogin' user "
                                    "does not exist. Please check your Sonic Login install.")
                                 .toString());
        return;
    }

    m_pendingContinuation = [this]() {
        sendToDaemon(QStringLiteral("reset"), QJsonObject());
    };

    if (m_pendingAuthUser.isEmpty()) {
        setNeedsSave(false);
        Q_EMIT authRequired();
    } else {
        setNeedsSave(false);
        m_pendingContinuation();
        m_pendingContinuation = nullptr;
    }
}

void SonicLoginKcm::submitAuth(const QString &username, const QString &password)
{
    if (busy()) {
        return;
    }
    m_pendingAuthUser = username;
    m_pendingAuthPassword = password;
    if (m_pendingContinuation) {
        auto cont = m_pendingContinuation;
        m_pendingContinuation = nullptr;
        cont();
    }
}

void SonicLoginKcm::cancelAuth()
{
    m_pendingContinuation = nullptr;
    m_pendingAuthUser.clear();
    m_pendingAuthPassword.clear();
}

void SonicLoginKcm::sendToDaemon(const QString &op, const QJsonObject &args)
{
    if (busy()) {
        return;
    }
    const QString username = std::exchange(m_pendingAuthUser, {});
    const QString password = std::exchange(m_pendingAuthPassword, {});
    setOperationStatus(kxi18n("Applying login screen settings…").toString());
    auto *watcher = new QFutureWatcher<SONICLOGIN::DaemonReply>(this);
    connect(watcher, &QFutureWatcher<SONICLOGIN::DaemonReply>::finished, this, [this, watcher, op, args]() {
        const auto reply = watcher->result();
        watcher->deleteLater();
        setOperationStatus({});
        if (reply.success) {
            if (op == QLatin1String("save")) {
                updateState();
                setNeedsSave(false);
            }
            Q_EMIT syncAttempted();
        } else {
            if (reply.rejected) {
                m_pendingContinuation = [this, op, args]() {
                    sendToDaemon(op, args);
                };
                Q_EMIT authRequired();
            }
            Q_EMIT errorOccurred(reply.error);
        }
    });
    watcher->setFuture(QtConcurrent::run([op, args, username, password]() {
        return SONICLOGIN::requestDaemon(op, args, username, password);
    }));
}

void SonicLoginKcm::setOperationStatus(const QString &status)
{
    if (m_operationStatus != status) {
        m_operationStatus = status;
        Q_EMIT operationStatusChanged();
    }
}

void SonicLoginKcm::defaults()
{
    KQuickManagedConfigModule::defaults();
    m_wallpaperSettings->defaults();

    updateState();
    Q_EMIT defaultsCalled();
}

void SonicLoginKcm::updateState()
{
    m_forceUpdateState = false;
    settingsChanged();
    Q_EMIT isDefaultsWallpaperChanged();
}

void SonicLoginKcm::forceUpdateState()
{
    m_forceUpdateState = true;
    settingsChanged();
    Q_EMIT isDefaultsWallpaperChanged();
}

bool SonicLoginKcm::isSaveNeeded() const
{
    return m_forceUpdateState || m_wallpaperSettings->isSaveNeeded();
}

bool SonicLoginKcm::isDefaults() const
{
    return m_wallpaperSettings->isDefaults();
}

KConfigPropertyMap *SonicLoginKcm::wallpaperConfiguration() const
{
    return m_wallpaperSettings->wallpaperConfiguration();
}

SonicLoginSettings *SonicLoginKcm::settings() const
{
    return &SonicLoginSettings::getInstance();
}

QString SonicLoginKcm::currentWallpaper() const
{
    return SonicLoginSettings::getInstance().wallpaperPluginId();
}

QString SonicLoginKcm::currentUser() const
{
    // Prefer getlogin() since it reflects the user running the KCM
    // (systemsettings), which is the user that must authenticate.
    if (const char *login = ::getlogin(); login && *login) {
        return QString::fromLocal8Bit(login);
    }
    // Fall back to the passwd entry for our real uid.
    if (struct passwd *pw = ::getpwuid(::getuid()); pw && pw->pw_name) {
        return QString::fromLocal8Bit(pw->pw_name);
    }
    // Last resort: environment variables.
    const QString envUser = qEnvironmentVariable("USER");
    if (!envUser.isEmpty()) {
        return envUser;
    }
    return QString();
}

bool SonicLoginKcm::isDefaultsWallpaper() const
{
    return m_wallpaperSettings->isDefaults();
}

QUrl SonicLoginKcm::wallpaperConfigFile() const
{
    return m_wallpaperSettings->wallpaperConfigFile();
}

WallpaperIntegration *SonicLoginKcm::wallpaperIntegration() const
{
    return m_wallpaperSettings->wallpaperIntegration();
}

UserModel *SonicLoginKcm::userModel() const
{
    static UserModel userModel;
    return &userModel;
}

SessionModel *SonicLoginKcm::sessionModel() const
{
    static SessionModel sessionModel;
    return &sessionModel;
}

bool SonicLoginKcm::KDEWalletAvailable()
{
    return !QStandardPaths::findExecutable(QLatin1String("kwalletmanager5")).isEmpty();
}

void SonicLoginKcm::openKDEWallet()
{
    KService::Ptr kwalletmanagerService = KService::serviceByDesktopName(QStringLiteral("org.kde.kwalletmanager"));
    auto *job = new KIO::ApplicationLauncherJob(kwalletmanagerService);
    job->start();
}

#include "kcm.moc"

#include "moc_kcm.cpp"
