/*
 * SPDX-FileCopyrightText: 2026 SonicDE Community
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "KcmIpcServer.h"

#include "MessageHandler.h"

#include "Constants.h"

#include <QCoreApplication>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QProcess>

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
        qWarning() << "KcmIpcServer: already started";
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
        qWarning() << "KcmIpcServer: failed to listen on" << name << ":" << m_server->errorString();
        delete m_server;
        m_server = nullptr;
        return false;
    }

    qInfo() << "KcmIpcServer: listening on" << m_server->fullServerName();

    connect(m_server, &QLocalServer::newConnection, this, &KcmIpcServer::onNewConnection);

    return m_server->isListening();
}

void KcmIpcServer::stop()
{
    if (!m_server) {
        return;
    }
    qInfo() << "KcmIpcServer: stopping on" << m_server->fullServerName();
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
    // partial reads just wait for more data. Only after commitTransaction()
    // succeeds do we authenticate and apply side effects (write files).
    QDataStream reader(socket);
    reader.setVersion(QDataStream::Qt_6_0);

    reader.startTransaction();
    QString username;
    QString password;
    quint32 type = 0;
    quint32 id = 0;
    reader >> username >> password >> type >> id;

    // Read the full payload inside the transaction. No side effects yet.
    std::optional<SyncPayload> syncPayload;
    std::optional<SavePayload> savePayload;

    if (reader.status() != QDataStream::Ok) {
        reader.commitTransaction();
        return;
    }

    if (type == MsgSyncSettings) {
        syncPayload = handleSyncRead(reader);
    } else if (type == MsgSaveConfig) {
        savePayload = handleSaveRead(reader);
    } else if (type == MsgResetSettings) {
        // handleReset reads no payload; just commit the header.
    } else {
        qWarning() << "KcmIpcServer: unknown messageType" << type;
    }

    if (!reader.commitTransaction()) {
        // Full message not yet available; wait for more data.
        return;
    }

    // Full message verified. Authenticate and apply side effects.
    QString authErr;
    if (!authenticate(username, password, &authErr)) {
        sendResponse(socket, id, false, QStringLiteral("auth: %1").arg(authErr));
        return;
    }

    QPair<bool, QString> result;
    if (type == MsgSyncSettings) {
        if (syncPayload) {
            result = handleSyncWrite(*syncPayload);
        } else {
            result = qMakePair(false, QStringLiteral("malformed sync payload"));
        }
    } else if (type == MsgResetSettings) {
        result = handleReset(reader);
    } else if (type == MsgSaveConfig) {
        if (savePayload) {
            result = handleSaveWrite(*savePayload);
        } else {
            result = qMakePair(false, QStringLiteral("malformed save payload"));
        }
    } else {
        result = qMakePair(false, QStringLiteral("unknown message type"));
    }

    sendResponse(socket, id, result.first, result.second);
}

void KcmIpcServer::onDisconnected()
{
    QLocalSocket *socket = qobject_cast<QLocalSocket *>(sender());
    if (!socket) {
        return;
    }
    qInfo() << "KcmIpcServer: client disconnected:" << socket;
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

static void chownRecursive(const QString &path)
{
    QDir dir(path);
    if (!dir.exists()) {
        return;
    }
    const QFileInfoList entries = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot);
    for (const QFileInfo &entry : entries) {
        if (entry.isDir()) {
            chownRecursive(entry.absoluteFilePath());
        }
        chownPath(entry.absoluteFilePath());
    }
    chownPath(path);
}

std::optional<SyncPayload> KcmIpcServer::handleSyncRead(QDataStream &in)
{
    SyncPayload payload;

    quint32 nFiles = 0;
    in >> nFiles;

    payload.files.reserve(nFiles);
    for (quint32 i = 0; i < nFiles; ++i) {
        QString name;
        QString content;
        in >> name >> content;
        payload.files.append(qMakePair(name, content));
    }

    quint32 nThemes = 0;
    in >> nThemes;

    payload.cursorThemes.reserve(nThemes);
    for (quint32 i = 0; i < nThemes; ++i) {
        QString themeName;
        QByteArray tarBytes;
        in >> themeName >> tarBytes;
        payload.cursorThemes.append(qMakePair(themeName, tarBytes));
    }

    quint32 nKscreenFiles = 0;
    in >> nKscreenFiles;

    payload.kscreenFiles.reserve(nKscreenFiles);
    for (quint32 i = 0; i < nKscreenFiles; ++i) {
        QString relPath;
        QByteArray content;
        in >> relPath >> content;
        payload.kscreenFiles.append(qMakePair(relPath, content));
    }

    if (in.status() != QDataStream::Ok) {
        return std::nullopt;
    }
    return payload;
}

static QPair<bool, QString> extractCursorTheme(const QString &homeDir, const QString &themeName, const QByteArray &tarBytes)
{
    // Reject obviously unsafe names that could escape the icons directory.
    if (themeName.isEmpty() || themeName.contains(QLatin1Char('/')) || themeName.contains(QLatin1Char('\\')) || themeName.contains(QLatin1Char('\0'))
        || themeName == QStringLiteral(".") || themeName == QStringLiteral("..")) {
        return qMakePair(false, QStringLiteral("refusing to extract unsafe theme name"));
    }

    QDir iconsDir(homeDir + QStringLiteral("/.local/share/icons"));
    if (!iconsDir.exists()) {
        // mkpath creates all missing intermediates ("<home>/.local",
        // "<home>/.local/share") and the icons directory itself, and
        // returns true if the directory exists or was created. Note that
        // QDir::mkdir(name) cannot be used here: it creates <iconsDir>/<name>
        // rather than the iconsDir path itself, which is the bug that
        // produced "could not create .local/share/icons" on first sync.
        const QString iconsPath = iconsDir.absolutePath();
        if (!QDir().mkpath(iconsPath)) {
            return qMakePair(false, QStringLiteral("could not create %1").arg(iconsPath));
        }
        // Chown and chmod the intermediates we (may have) created so the
        // soniclogin user owns and can write to its icons directory tree.
        // mkpath returns true both for newly created and pre-existing
        // directories; chowning an already-correct path is harmless.
        const QString localPath = homeDir + QStringLiteral("/.local");
        const QString sharePath = homeDir + QStringLiteral("/.local/share");
        for (const QString &p : {localPath, sharePath, iconsPath}) {
            QFile::setPermissions(p, standardDirectoryPermissions);
            chownPath(p);
        }
    }

    const QString themeDir = iconsDir.absoluteFilePath(themeName);
    // Remove any previously synced copy of this theme so we don't mix stale
    // files with the freshly extracted set.
    QDir existing(themeDir);
    if (existing.exists()) {
        existing.removeRecursively();
    }

    QProcess tar;
    tar.setProcessChannelMode(QProcess::MergedChannels);
    // Extract into iconsDir so that the resulting layout is
    // <homeDir>/.local/share/icons/<themeName>/...
    tar.setWorkingDirectory(iconsDir.absolutePath());
    tar.start(QStringLiteral("tar"), {QStringLiteral("-xf"), QStringLiteral("-")});
    if (!tar.waitForStarted(5000)) {
        return qMakePair(false, QStringLiteral("failed to start tar: %1").arg(tar.errorString()));
    }
    tar.write(tarBytes);
    tar.closeWriteChannel();
    if (!tar.waitForFinished(60000)) {
        tar.kill();
        return qMakePair(false, QStringLiteral("tar extraction timed out"));
    }
    if (tar.exitStatus() != QProcess::NormalExit || tar.exitCode() != 0) {
        return qMakePair(false, QStringLiteral("tar extraction failed: %1").arg(QString::fromLocal8Bit(tar.readAll().trimmed())));
    }
    if (!QFileInfo::exists(themeDir)) {
        return qMakePair(false, QStringLiteral("tar did not produce expected theme directory"));
    }

    // Drop a marker file so reset() knows this is a synced theme we own.
    const QString markerPath = themeDir + QStringLiteral("/.soniclogin-synced");
    QFile marker(markerPath);
    if (marker.open(QFile::WriteOnly | QFile::Text | QFile::Truncate)) {
        marker.write("synced from user session\n");
        marker.close();
    }
    chownRecursive(themeDir);

    return qMakePair(true, QString());
}

QPair<bool, QString> KcmIpcServer::handleSyncWrite(const SyncPayload &payload)
{
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
    for (const auto &file : payload.files) {
        fileNames.append(file.first);
        contents.insert(file.first, file.second);
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

    // Extract any bundled cursor themes into the soniclogin user's icons
    // directory so the greeter can load them via $XCURSOR_PATH.
    QStringList extractedCursorThemes;
    for (const auto &theme : payload.cursorThemes) {
        auto result = extractCursorTheme(homeDir, theme.first, theme.second);
        if (result.first) {
            extractedCursorThemes.append(theme.first);
        } else {
            qWarning() << "handleSync: failed to extract cursor theme" << theme.first << ":" << result.second;
        }
    }

    QStringList extractedKscreenFiles;
    const QString kscreenRoot = homeDir + QStringLiteral("/.local/share/kscreen");
    for (const auto &kscreen : payload.kscreenFiles) {
        const QString &relPath = kscreen.first;
        if (relPath.isEmpty() || relPath.startsWith(QLatin1Char('/')) || relPath.startsWith(QLatin1Char('\\')) || relPath.contains(QStringLiteral(".."))
            || relPath.contains(QLatin1Char('\0'))) {
            qWarning() << "handleSync: refusing to write suspicious kscreen path" << relPath;
            continue;
        }
        const QString fullPath = kscreenRoot + QLatin1Char('/') + relPath;
        const QString parentDir = QFileInfo(fullPath).absolutePath();
        if (!QDir().mkpath(parentDir)) {
            qWarning() << "handleSync: failed to create kscreen directory for" << relPath;
            continue;
        }
        QFile out(fullPath);
        if (!out.open(QFile::WriteOnly | QFile::Text | QFile::Truncate)) {
            qWarning() << "handleSync: failed to open kscreen file for writing" << fullPath << ":" << out.errorString();
            continue;
        }
        out.write(kscreen.second);
        out.close();
        extractedKscreenFiles.append(relPath);
        qInfo() << "handleSync: wrote kscreen file" << fullPath << "size=" << kscreen.second.size();
    }

    for (const QString &dir : {
             kscreenRoot,
             kscreenRoot + QStringLiteral("/outputs"),
             kscreenRoot + QStringLiteral("/control"),
             kscreenRoot + QStringLiteral("/control/configs"),
         }) {
        if (QFileInfo(dir).exists()) {
            chownPath(dir);
            chownRecursive(dir);
            qInfo() << "handleSync: chowned" << dir;
        }
    }

    if (!extractedKscreenFiles.isEmpty()) {
        if (QDir().mkpath(kscreenRoot)) {
            chownPath(kscreenRoot);
            const QString marker = kscreenRoot + QStringLiteral("/.soniclogin-synced");
            QFile f(marker);
            if (f.open(QFile::WriteOnly | QFile::Text | QFile::Truncate)) {
                f.write("synced from user session\n");
                f.close();
                chownPath(marker);
            }
        }
    }

    qInfo() << "handleSync:" << payload.files.size() << "files," << extractedCursorThemes.size() << "cursor themes," << extractedKscreenFiles.size()
            << "kscreen files for" << homeDir;
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

    // Remove any cursor theme directories we synced from the user's session.
    // We identify them by the marker file left behind during extraction so we
    // never delete a theme that the soniclogin user installed independently.
    QDir iconsDir(homeDir + QStringLiteral("/.local/share/icons"));
    if (iconsDir.exists()) {
        const QFileInfoList entries = iconsDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo &entry : entries) {
            if (QFile::exists(entry.absoluteFilePath() + QStringLiteral("/.soniclogin-synced"))) {
                QDir(entry.absoluteFilePath()).removeRecursively();
            }
        }
    }

    const QString kscreenRoot = homeDir + QStringLiteral("/.local/share/kscreen");
    if (QFile::exists(kscreenRoot + QStringLiteral("/.soniclogin-synced"))) {
        QDir(kscreenRoot).removeRecursively();
    }

    qInfo() << "handleReset:" << homeDir;
    return qMakePair(true, QString());
}

std::optional<SavePayload> KcmIpcServer::handleSaveRead(QDataStream &in)
{
    SavePayload payload;
    in >> payload.configText;

    quint32 nWallpapers = 0;
    in >> nWallpapers;

    payload.wallpapers.reserve(nWallpapers);
    for (quint32 i = 0; i < nWallpapers; ++i) {
        QString relPath;
        QByteArray content;
        in >> relPath >> content;
        payload.wallpapers.append(qMakePair(relPath, content));
    }

    if (in.status() != QDataStream::Ok) {
        return std::nullopt;
    }
    return payload;
}

QPair<bool, QString> KcmIpcServer::handleSaveWrite(const SavePayload &payload)
{
    QFile file(QString::fromLatin1(CONFIG_FILE));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate, standardPermissions)) {
        return qMakePair(false, QStringLiteral("cannot open config file for writing"));
    }

    QTextStream out(&file);
    out << payload.configText;
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
    if (wallpaperDir.exists()) {
        chownRecursive(wallpaperDir.path());
        if (!wallpaperDir.removeRecursively()) {
            qWarning() << "Could not clean old wallpaper directory";
        }
    }
    homeDir.mkdir(QStringLiteral("wallpapers"));

    int rootWallpaperFd = open(wallpaperDir.path().toUtf8().constData(), O_RDONLY | O_DIRECTORY);
    if (rootWallpaperFd < 0) {
        return qMakePair(false, QStringLiteral("could not open wallpaper directory"));
    }

    auto closeRoot = qScopeGuard([&]() {
        close(rootWallpaperFd);
    });

    for (const auto &wp : payload.wallpapers) {
        const QString &wallpaper = wp.first;
        const QByteArray &content = wp.second;

        if (wallpaper.isEmpty()) {
            qWarning() << "Skipping wallpaper with empty key";
            continue;
        }
        if (wallpaper.contains(QStringLiteral(".."))) {
            qWarning() << "Badly formed wallpaper name detected, aborting";
            return qMakePair(false, QStringLiteral("badly formed wallpaper name"));
        }

        const QString relativeFilePath = QStringLiteral("wallpapers/") + wallpaper;
        const QString relativeParentDirectory = relativeFilePath.left(relativeFilePath.lastIndexOf(QLatin1Char('/')));
        if (!homeDir.mkpath(relativeParentDirectory)) {
            qWarning() << "Could not create new wallpaper directory" << relativeParentDirectory;
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
            qWarning() << "Could not open wallpaper file" << wallpaper << "in" << wallpaperDir.path() << ":" << strerror(errno);
            return qMakePair(false, QStringLiteral("could not open wallpaper file"));
        }

        QFile outFile;
        if (!outFile.open(outFd, QIODevice::WriteOnly | QIODevice::Truncate, QFileDevice::AutoCloseHandle)) {
            qWarning() << "Could not open wallpaper file from FD.";
            return qMakePair(false, QStringLiteral("could not open wallpaper file from FD"));
        }

        outFile.write(content);
    }

    qInfo() << "handleSave:" << payload.wallpapers.size() << "wallpapers for" << homeDirPath;
    return qMakePair(true, QString());
}

} // namespace SONICLOGIN

#include "moc_KcmIpcServer.cpp"
