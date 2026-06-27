/*
 * SPDX-FileCopyrightText: 2026 SonicDE Community
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "KcmIpcServer.h"

#include "Constants.h"

#include <QCoreApplication>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>

#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <security/pam_appl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#ifdef __linux__
#include <linux/openat2.h>
#include <sys/syscall.h>
#endif

#include <KUser>

namespace SONICLOGIN
{

namespace
{
constexpr quint32 MsgSyncSettings = 1;
constexpr quint32 MsgResetSettings = 2;
constexpr quint32 MsgSaveConfig = 3;
constexpr quint32 MsgResponse = 100;

constexpr QFile::Permissions standardPermissions = QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther;
constexpr QFile::Permissions standardDirectoryPermissions =
    QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner | QFile::ReadGroup | QFile::ExeGroup | QFile::ReadOther | QFile::ExeOther;

Q_LOGGING_CATEGORY(KCMIPC, "soniclogin.kcmipc")

struct PamConvData {
    QString password;
};

int pamConv(int n, const struct pam_message **msg, struct pam_response **resp, void *userdata)
{
    PamConvData *data = static_cast<PamConvData *>(userdata);
    *resp = static_cast<struct pam_response *>(calloc(n, sizeof(struct pam_response)));
    if (!*resp) {
        return PAM_CONV_ERR;
    }
    for (int i = 0; i < n; ++i) {
        if (msg[i]->msg_style == PAM_PROMPT_ECHO_OFF) {
            const QByteArray bytes = data->password.toLocal8Bit();
            char *copy = strdup(bytes.constData());
            if (!copy) {
                for (int j = 0; j < i; ++j) {
                    free((*resp)[j].resp);
                }
                free(*resp);
                *resp = nullptr;
                return PAM_CONV_ERR;
            }
            (*resp)[i].resp = copy;
            (*resp)[i].resp_retcode = 0;
        } else {
            (*resp)[i].resp = nullptr;
            (*resp)[i].resp_retcode = 0;
        }
    }
    return PAM_SUCCESS;
}
} // namespace

KcmIpcServer::KcmIpcServer(QObject *parent)
    : QObject(parent)
{
}

KcmIpcServer::~KcmIpcServer()
{
    stop();
}

QString KcmIpcServer::boundSocketName() const
{
    return m_server ? m_server->fullServerName() : QString();
}

bool KcmIpcServer::start()
{
    if (m_server) {
        qCWarning(KCMIPC) << "KcmIpcServer: already started";
        return false;
    }

    m_server = new QLocalServer(this);

    // WorldAccessOption => mode 0666 so the unprivileged KCM can connect.
    // Authentication is enforced by the daemon via PAM on every operation;
    // an unauthenticated connection cannot perform any privileged action.
    m_server->setSocketOptions(QLocalServer::WorldAccessOption);

    const QString name = QStringLiteral("/run/soniclogin/kcm-ipc-") + QString::number(QCoreApplication::applicationPid());

    QDir().mkpath(QStringLiteral("/run/soniclogin"));

    QLocalServer::removeServer(name);

    if (!m_server->listen(name)) {
        qCWarning(KCMIPC) << "KcmIpcServer: failed to listen on" << name << ":" << m_server->errorString();
        delete m_server;
        m_server = nullptr;
        return false;
    }

    qCInfo(KCMIPC) << "KcmIpcServer: listening on" << m_server->fullServerName();

    connect(m_server, &QLocalServer::newConnection, this, &KcmIpcServer::onNewConnection);

    return m_server->isListening();
}

void KcmIpcServer::stop()
{
    if (!m_server) {
        return;
    }
    qCInfo(KCMIPC) << "KcmIpcServer: stopping on" << m_server->fullServerName();
    m_server->close();
    m_server->deleteLater();
    m_server = nullptr;
}

void KcmIpcServer::onNewConnection()
{
    while (m_server && m_server->hasPendingConnections()) {
        QLocalSocket *socket = m_server->nextPendingConnection();
        if (!socket) {
            continue;
        }
        connect(socket, &QLocalSocket::readyRead, this, &KcmIpcServer::onReadyRead);
        connect(socket, &QLocalSocket::disconnected, this, &KcmIpcServer::onDisconnected);
    }
}

void KcmIpcServer::onReadyRead()
{
    QLocalSocket *socket = qobject_cast<QLocalSocket *>(sender());
    if (!socket) {
        return;
    }

    // Each connection handles exactly one operation: the client connects,
    // sends [username][password][type][id][payload...], reads the response,
    // and disconnects. We read the whole message in a single transaction so
    // partial reads just wait for more data.
    QDataStream reader(socket);
    reader.setVersion(QDataStream::Qt_6_0);

    reader.startTransaction();
    QString username;
    QString password;
    quint32 type = 0;
    quint32 id = 0;
    reader >> username >> password >> type >> id;

    // Authenticate before dispatching. The sudo PAM service restricts
    // access to admin/wheel users, matching the old polkit auth_admin.
    QString authErr;
    const bool authed = (reader.status() == QDataStream::Ok) && authenticate(username, password, &authErr);

    QPair<bool, QString> result;
    if (!authed) {
        result = qMakePair(false, QStringLiteral("auth: %1").arg(authErr));
    } else if (type == MsgSyncSettings) {
        result = handleSync(reader);
    } else if (type == MsgResetSettings) {
        result = handleReset(reader);
    } else if (type == MsgSaveConfig) {
        result = handleSave(reader);
    } else {
        qCWarning(KCMIPC) << "KcmIpcServer: unknown messageType" << type;
        result = qMakePair(false, QStringLiteral("unknown message type"));
    }

    if (!reader.commitTransaction()) {
        // Full message not yet available; wait for more data.
        return;
    }

    sendResponse(socket, id, result.first, result.second);
}

void KcmIpcServer::onDisconnected()
{
    QLocalSocket *socket = qobject_cast<QLocalSocket *>(sender());
    if (!socket) {
        return;
    }
    qCInfo(KCMIPC) << "KcmIpcServer: client disconnected:" << socket;
    socket->deleteLater();
}

void KcmIpcServer::sendResponse(QLocalSocket *socket, quint32 messageId, bool ok, const QString &error)
{
    QByteArray block;
    QDataStream out(&block, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_6_0);
    out << MsgResponse << messageId << static_cast<quint8>(ok ? 1 : 0) << error;
    socket->write(block);
    socket->flush();
}

bool KcmIpcServer::authenticate(const QString &username, const QString &password, QString *errorOut)
{
    if (username.isEmpty() || password.isEmpty()) {
        if (errorOut) {
            *errorOut = QStringLiteral("missing credentials");
        }
        return false;
    }

    if (!getpwnam(username.toLocal8Bit().constData())) {
        if (errorOut) {
            *errorOut = QStringLiteral("user '%1' does not exist").arg(username);
        }
        return false;
    }

    PamConvData convData{password};
    struct pam_conv conv = {pamConv, &convData};
    pam_handle_t *pamh = nullptr;

    // The sudo PAM service restricts authentication to admin/wheel users,
    // matching the old polkit auth_admin restriction.
    int rc = pam_start("sudo", username.toLocal8Bit().constData(), &conv, &pamh);
    if (rc != PAM_SUCCESS) {
        if (errorOut) {
            *errorOut = QStringLiteral("pam_start: %1").arg(QString::fromLocal8Bit(pam_strerror(pamh, rc)));
        }
        if (pamh) {
            pam_end(pamh, rc);
        }
        return false;
    }

    rc = pam_authenticate(pamh, PAM_SILENT);
    if (rc != PAM_SUCCESS) {
        if (errorOut) {
            *errorOut = QStringLiteral("authentication failed: %1").arg(QString::fromLocal8Bit(pam_strerror(pamh, rc)));
        }
        pam_end(pamh, rc);
        return false;
    }

    rc = pam_acct_mgmt(pamh, PAM_SILENT | PAM_DISALLOW_NULL_AUTHTOK);
    if (rc != PAM_SUCCESS) {
        if (errorOut) {
            *errorOut = QStringLiteral("account validation: %1").arg(QString::fromLocal8Bit(pam_strerror(pamh, rc)));
        }
        pam_end(pamh, rc);
        return false;
    }

    pam_end(pamh, PAM_SUCCESS);
    convData.password.fill(QChar(0));
    return true;
}

static std::optional<QString> sonicloginUserHomeDir()
{
    const QString path = KUser(QStringLiteral("soniclogin")).homeDir();
    if (path.isEmpty()) {
        return std::nullopt;
    }
    return path;
}

static void chownPath(const QString &path)
{
    static const KUser sonicloginUser(QStringLiteral("soniclogin"));
    chown(path.toLocal8Bit().data(), sonicloginUser.userId().nativeId(), sonicloginUser.groupId().nativeId());
}

QPair<bool, QString> KcmIpcServer::handleSync(QDataStream &in)
{
    QString homeDir;
    if (auto opt = sonicloginUserHomeDir()) {
        homeDir = *opt;
    } else {
        return qMakePair(false, QStringLiteral("soniclogin user not found"));
    }

    quint32 nFiles = 0;
    in >> nFiles;

    QDir cacheLocation(homeDir + QStringLiteral("/.cache"));
    if (cacheLocation.exists()) {
        cacheLocation.removeRecursively();
    }

    QDir homeLocation(homeDir);
    QDir configLocation(homeDir + QStringLiteral("/.config"));
    if (!configLocation.exists()) {
        homeLocation.mkdir(QStringLiteral(".config"), standardDirectoryPermissions);
        chownPath(configLocation.path());
    }
    QDir fontConfigLocation(homeDir + QStringLiteral("/.config/fontconfig"));
    if (!fontConfigLocation.exists()) {
        configLocation.mkdir(QStringLiteral("fontconfig"), standardDirectoryPermissions);
        chownPath(fontConfigLocation.path());
    }

    QStringList fileNames;
    QHash<QString, QString> contents;
    for (quint32 i = 0; i < nFiles; ++i) {
        QString name;
        QString content;
        in >> name >> content;
        fileNames.append(name);
        contents.insert(name, content);
    }

    auto createConfigFile = [&](const QString &name) {
        if (!fileNames.contains(name)) {
            QFile::remove(homeDir + QStringLiteral("/.config/") + name);
            return;
        }
        QFile file(homeDir + QStringLiteral("/.config/") + name);
        if (file.open(QFile::WriteOnly | QFile::Text | QFile::Truncate, standardPermissions)) {
            QTextStream out(&file);
            out << contents.value(name);
            chownPath(file.fileName());
        }
    };

    createConfigFile(QStringLiteral("kdeglobals"));
    createConfigFile(QStringLiteral("plasmarc"));
    createConfigFile(QStringLiteral("kcminputrc"));
    createConfigFile(QStringLiteral("kwinoutputconfig.json"));
    createConfigFile(QStringLiteral("fontconfig/fonts.conf"));
    createConfigFile(QStringLiteral("kxkbrc"));

    qCInfo(KCMIPC) << "handleSync:" << nFiles << "files for" << homeDir;
    return qMakePair(true, QString());
}

QPair<bool, QString> KcmIpcServer::handleReset(QDataStream &in)
{
    Q_UNUSED(in);

    QString homeDir;
    if (auto opt = sonicloginUserHomeDir()) {
        homeDir = *opt;
    } else {
        return qMakePair(false, QStringLiteral("soniclogin user not found"));
    }

    QDir cacheLocation(homeDir + QStringLiteral("/.cache"));
    if (cacheLocation.exists()) {
        cacheLocation.removeRecursively();
    }
    QDir fontConfigDir(homeDir + QStringLiteral("/.config/fontconfig"));
    if (fontConfigDir.exists()) {
        fontConfigDir.removeRecursively();
    }
    QFile(homeDir + QStringLiteral("/.config/kdeglobals")).remove();
    QFile(homeDir + QStringLiteral("/.config/plasmarc")).remove();
    QFile(homeDir + QStringLiteral("/.config/kcminputrc")).remove();
    QFile(homeDir + QStringLiteral("/.config/kwinoutputconfig.json")).remove();
    QFile(homeDir + QStringLiteral("/.config/kxkbrc")).remove();

    qCInfo(KCMIPC) << "handleReset:" << homeDir;
    return qMakePair(true, QString());
}

QPair<bool, QString> KcmIpcServer::handleSave(QDataStream &in)
{
    QString configText;
    in >> configText;

    quint32 nWallpapers = 0;
    in >> nWallpapers;

    QList<QPair<QString, QByteArray>> wallpapers;
    wallpapers.reserve(nWallpapers);
    for (quint32 i = 0; i < nWallpapers; ++i) {
        QString relPath;
        QByteArray content;
        in >> relPath >> content;
        wallpapers.append(qMakePair(relPath, content));
    }

    QFile file(QString::fromLatin1(CONFIG_FILE));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate, standardPermissions)) {
        return qMakePair(false, QStringLiteral("cannot open config file for writing"));
    }

    QTextStream out(&file);
    out << configText;
    out.flush();
    file.close();

    if (file.permissions() != standardPermissions) {
        file.setPermissions(standardPermissions);
    }

    QString homeDirPath;
    if (auto opt = sonicloginUserHomeDir()) {
        homeDirPath = *opt;
    } else {
        return qMakePair(false, QStringLiteral("soniclogin user not found"));
    }

    QDir homeDir(homeDirPath);
    QDir wallpaperDir(homeDir.absoluteFilePath(QStringLiteral("wallpapers")));
    if (!wallpaperDir.removeRecursively()) {
        qCWarning(KCMIPC) << "Could not clean old wallpaper directory";
    }
    homeDir.mkdir(QStringLiteral("wallpapers"));

    int rootWallpaperFd = open(wallpaperDir.path().toUtf8().constData(), O_RDONLY | O_DIRECTORY);
    if (rootWallpaperFd < 0) {
        return qMakePair(false, QStringLiteral("could not open wallpaper directory"));
    }

    auto closeRoot = qScopeGuard([&]() {
        close(rootWallpaperFd);
    });

    for (const auto &wp : wallpapers) {
        const QString &wallpaper = wp.first;
        const QByteArray &content = wp.second;

        if (wallpaper.contains(QStringLiteral(".."))) {
            qCWarning(KCMIPC) << "Badly formed wallpaper name detected, aborting";
            return qMakePair(false, QStringLiteral("badly formed wallpaper name"));
        }

        const QString relativeFilePath = QStringLiteral("wallpapers/") + wallpaper;
        const QString relativeParentDirectory = relativeFilePath.left(relativeFilePath.lastIndexOf(QLatin1Char('/')));
        if (!homeDir.mkpath(relativeParentDirectory)) {
            qCWarning(KCMIPC) << "Could not create new wallpaper directory";
            return qMakePair(false, QStringLiteral("could not create wallpaper directory"));
        }

#ifdef __linux__
        struct open_how how = {
            .flags = O_CREAT | O_WRONLY | O_TRUNC,
            .mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH,
            .resolve = RESOLVE_BENEATH | RESOLVE_NO_MAGICLINKS,
        };
        int outFd = syscall(SYS_openat2, rootWallpaperFd, wallpaper.toUtf8().constData(), &how, sizeof(struct open_how));
#else
        int outFd = openat(rootWallpaperFd, wallpaper.toUtf8().constData(), O_CREAT | O_WRONLY | O_TRUNC | O_NOFOLLOW, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
#endif
        if (outFd < 0) {
            qCWarning(KCMIPC) << "Could not open wallpaper file." << strerror(errno);
            return qMakePair(false, QStringLiteral("could not open wallpaper file"));
        }

        QFile outFile;
        if (!outFile.open(outFd, QIODevice::WriteOnly | QIODevice::Truncate, QFileDevice::AutoCloseHandle)) {
            qCWarning(KCMIPC) << "Could not open wallpaper file from FD.";
            return qMakePair(false, QStringLiteral("could not open wallpaper file from FD"));
        }

        outFile.write(content);
    }

    qCInfo(KCMIPC) << "handleSave:" << nWallpapers << "wallpapers for" << homeDirPath;
    return qMakePair(true, QString());
}

} // namespace SONICLOGIN

#include "moc_KcmIpcServer.cpp"
