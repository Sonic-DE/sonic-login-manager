/*
 *  SPDX-FileCopyrightText: 2014 Martin Gräßlin <mgraesslin@kde.org>
 *  SPDX-FileCopyrightText: 2019 Kevin Ottens <kevin.ottens@enioka.com>
 *  SPDX-FileCopyrightText: 2020 David Redondo <kde@david-redondo.de>
 *  SPDX-FileCopyrightText: 2025 Oliver Beard <olib141@outlook.com>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <QDataStream>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QLocalSocket>
#include <QLoggingCategory>
#include <QTemporaryDir>
#include <QTextStream>

#include <pwd.h>
#include <unistd.h>

Q_LOGGING_CATEGORY(KCMSONICLOGIN, "soniclogin.kcm")

#include <KConfigLoader>
#include <KConfigPropertyMap>
#include <KIO/ApplicationLauncherJob>
#include <KLazyLocalizedString>
#include <KLocalizedString>
#include <KPluginFactory>
#include <KService>
#include <KUser>

#include "models/sessionmodel.h"
#include "models/usermodel.h"
#include "soniclogindata.h"
#include "wallpapersettings.h"

#include "kcm.h"

extern void installKcmMessageHandler();

K_PLUGIN_FACTORY_WITH_JSON(SonicLoginKcmFactory, "kcm_soniclogin.json",
                           registerPlugin<SonicLoginKcm>();
                           registerPlugin<SonicLoginData>();)

SonicLoginKcm::SonicLoginKcm(QObject *parent, const KPluginMetaData &data)
    : KQuickManagedConfigModule(parent, data),
      m_wallpaperSettings(new WallpaperSettings(this)) {
    installKcmMessageHandler();
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

void SonicLoginKcm::load() {
  KQuickManagedConfigModule::load();
  m_wallpaperSettings->load();

  updateState();
  Q_EMIT loadCalled();
}

void SonicLoginKcm::save()
{
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
        qCWarning(KCMSONICLOGIN) << "save: no wallpaper configuration available; skipping wallpaper settings";
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

void SonicLoginKcm::synchronizeSettings() {
  if (KUser("soniclogin").homeDir().isEmpty()) {
      Q_EMIT errorOccurred(kxi18n("Unable to synchronise Plasma settings because the 'soniclogin' "
                                  "user does not exist. Please check your Sonic Login install.")
                               .toString());
      return;
  }

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
  addConfigFile(QStandardPaths::locate(QStandardPaths::GenericConfigLocation, QStringLiteral("kcminputrc")), QStringLiteral("kcminputrc"));
  addConfigFile(QStandardPaths::locate(QStandardPaths::GenericConfigLocation,
                                       QStringLiteral("kwinoutputconfig.json")),
                QStringLiteral("kwinoutputconfig.json"));

  const QString fontconfigPath = QStandardPaths::locate(
      QStandardPaths::GenericConfigLocation, QStringLiteral("fontconfig"),
      QStandardPaths::LocateDirectory);
  if (!fontconfigPath.isEmpty()) {
    addConfigFile(fontconfigPath + QStringLiteral("/fonts.conf"),
                  QStringLiteral("fontconfig/fonts.conf"));
  }

  addConfigFile(QStandardPaths::locate(QStandardPaths::GenericConfigLocation,
                                       QStringLiteral("kxkbrc")),
                QStringLiteral("kxkbrc"));

  QByteArray argsJson;
  {
      QJsonObject args;
      args[QStringLiteral("files")] = files;
      argsJson = QJsonDocument(args).toJson(QJsonDocument::Compact);
  }

  m_pendingContinuation = [this, argsJson]() {
      QJsonObject args = QJsonDocument::fromJson(argsJson).object();
      sendToDaemon(QStringLiteral("sync"), args);
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

void SonicLoginKcm::resetSynchronizedSettings() {
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
    const auto clearCredentials = qScopeGuard([this]() {
        m_pendingAuthUser.clear();
        m_pendingAuthPassword.clear();
    });

    QFile pidFile(QStringLiteral("/run/soniclogin/daemon.pid"));
    if (!pidFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        Q_EMIT errorOccurred(kxi18n("Sonic Login daemon is not running. Start it and try again.").toString());
        return;
    }
    bool ok = false;
    const qint64 pid = pidFile.readAll().trimmed().toLongLong(&ok);
    pidFile.close();
    if (!ok || pid <= 0) {
        Q_EMIT errorOccurred(kxi18n("Sonic Login daemon is not running. Start it and try again.").toString());
        return;
    }

    QLocalSocket socket;
    socket.connectToServer(QStringLiteral("/run/soniclogin/kcm-ipc-") + QString::number(pid));
    if (!socket.waitForConnected(5000)) {
        Q_EMIT errorOccurred(kxi18n("Cannot connect to the Sonic Login daemon: %1").subs(socket.errorString()).toString());
        return;
    }

    static const quint32 MsgSyncSettings = 1;
    static const quint32 MsgResetSettings = 2;
    static const quint32 MsgSaveConfig = 3;
    static const quint32 MsgResponse = 100;

    quint32 type = 0;
    if (op == QLatin1String("sync")) {
        type = MsgSyncSettings;
    } else if (op == QLatin1String("reset")) {
        type = MsgResetSettings;
    } else if (op == QLatin1String("save")) {
        type = MsgSaveConfig;
    } else {
        Q_EMIT errorOccurred(kxi18n("Unknown operation: %1").subs(op).toString());
        return;
    }

    QByteArray block;
    {
        QDataStream out(&block, QIODevice::WriteOnly);
        out.setVersion(QDataStream::Qt_6_0);
        out << m_pendingAuthUser << m_pendingAuthPassword << type << static_cast<quint32>(1);
        if (type == MsgSyncSettings) {
            const QJsonObject files = args.value(QStringLiteral("files")).toObject();
            out << static_cast<quint32>(files.size());
            for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
                out << it.key() << it.value().toString();
            }
        } else if (type == MsgSaveConfig) {
            const QString config = args.value(QStringLiteral("config")).toString();
            const QJsonObject wallpapers = args.value(QStringLiteral("wallpapers")).toObject();
            out << config;
            out << static_cast<quint32>(wallpapers.size());
            for (auto it = wallpapers.constBegin(); it != wallpapers.constEnd(); ++it) {
                out << it.key() << QByteArray::fromBase64(it.value().toString().toLatin1());
            }
        }
    }
    socket.write(block);
    socket.flush();

    QByteArray buffer;
    QDataStream in(&socket);
    in.setVersion(QDataStream::Qt_6_0);
    in.startTransaction();
    quint32 responseType = 0;
    quint32 responseId = 0;
    quint8 okRaw = 0;
    QString err;
    in >> responseType >> responseId >> okRaw >> err;
    while (!in.commitTransaction()) {
        if (!socket.waitForReadyRead(15000)) {
            Q_EMIT errorOccurred(kxi18n("Daemon did not respond: %1").subs(socket.errorString()).toString());
            return;
        }
        in.startTransaction();
        in >> responseType >> responseId >> okRaw >> err;
    }

    if (responseType != MsgResponse) {
        Q_EMIT errorOccurred(kxi18n("Unexpected response from daemon: %1").subs(responseType).toString());
        return;
    }

    if (okRaw) {
        if (op == QLatin1String("save")) {
            updateState();
            setNeedsSave(false);
        }
        Q_EMIT syncAttempted();
    } else {
        m_pendingContinuation = [this, op, args]() {
            sendToDaemon(op, args);
        };
        Q_EMIT authRequired();
        Q_EMIT errorOccurred(err.isEmpty() ? kxi18n("Operation failed.").toString() : err);
    }
}

void SonicLoginKcm::defaults() {
  KQuickManagedConfigModule::defaults();
  m_wallpaperSettings->defaults();

  updateState();
  Q_EMIT defaultsCalled();
}

void SonicLoginKcm::updateState() {
  m_forceUpdateState = false;
  settingsChanged();
  Q_EMIT isDefaultsWallpaperChanged();
}

void SonicLoginKcm::forceUpdateState() {
  m_forceUpdateState = true;
  settingsChanged();
  Q_EMIT isDefaultsWallpaperChanged();
}

bool SonicLoginKcm::isSaveNeeded() const {
  return m_forceUpdateState || m_wallpaperSettings->isSaveNeeded();
}

bool SonicLoginKcm::isDefaults() const {
  return m_wallpaperSettings->isDefaults();
}

KConfigPropertyMap *SonicLoginKcm::wallpaperConfiguration() const {
  return m_wallpaperSettings->wallpaperConfiguration();
}

SonicLoginSettings *SonicLoginKcm::settings() const {
  return &SonicLoginSettings::getInstance();
}

QString SonicLoginKcm::currentWallpaper() const {
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

bool SonicLoginKcm::isDefaultsWallpaper() const {
  return m_wallpaperSettings->isDefaults();
}

QUrl SonicLoginKcm::wallpaperConfigFile() const {
  return m_wallpaperSettings->wallpaperConfigFile();
}

WallpaperIntegration *SonicLoginKcm::wallpaperIntegration() const {
  return m_wallpaperSettings->wallpaperIntegration();
}

UserModel *SonicLoginKcm::userModel() const {
  static UserModel userModel;
  return &userModel;
}

SessionModel *SonicLoginKcm::sessionModel() const {
  static SessionModel sessionModel;
  return &sessionModel;
}

bool SonicLoginKcm::KDEWalletAvailable() {
  return !QStandardPaths::findExecutable(QLatin1String("kwalletmanager5"))
              .isEmpty();
}

void SonicLoginKcm::openKDEWallet() {
  KService::Ptr kwalletmanagerService =
      KService::serviceByDesktopName(QStringLiteral("org.kde.kwalletmanager5"));
  auto *job = new KIO::ApplicationLauncherJob(kwalletmanagerService);
  job->start();
}

#include "kcm.moc"

#include "moc_kcm.cpp"
