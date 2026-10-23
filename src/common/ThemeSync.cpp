/*
 * SPDX-FileCopyrightText: 2026 SonicDE Community
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ThemeSync.h"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QSaveFile>
#include <QScopeGuard>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtEndian>

#include <KConfigGroup>
#include <KSharedConfig>

#include <cerrno>
#include <fcntl.h>
#include <functional>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

namespace SONICLOGIN::ThemeSync {
namespace {
constexpr quint32 magic = 0x53544831; // STH1
constexpr qsizetype maximumBytes = 256 * 1024 * 1024;
constexpr quint32 maximumFiles = 200000;
const QString marker = QStringLiteral(".soniclogin-synced");
const QStringList roots = {QStringLiteral("icons"),
                           QStringLiteral("plasma/desktoptheme"),
                           QStringLiteral("plasma/look-and-feel"),
                           QStringLiteral("color-schemes")};
constexpr QFile::Permissions filePermissions = QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther;
constexpr QFile::Permissions directoryPermissions = filePermissions | QFile::ExeOwner | QFile::ExeGroup | QFile::ExeOther;

bool fail(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
    return false;
}

bool safePath(const QString &path)
{
    if (path.isEmpty() || path.startsWith(QLatin1Char('/')) || path.contains(QLatin1Char('\\')) || path.contains(QChar::Null)) {
        return false;
    }
    const auto parts = path.split(QLatin1Char('/'));
    for (const auto &part : parts) {
        if (part.isEmpty() || part == QLatin1String(".") || part == QLatin1String("..") || part == marker) {
            return false;
        }
    }
    return true;
}

QString assetRoot(const QString &relativeName)
{
    for (const auto &root : roots) {
        const QString prefix = root + QLatin1Char('/');
        if (relativeName.startsWith(prefix) && safePath(relativeName.mid(prefix.size())) && !relativeName.mid(prefix.size()).contains(QLatin1Char('/'))) {
            return root;
        }
    }
    return {};
}

// Pin each ancestor by descriptor. Checking symlinks with QFileInfo alone is
// insufficient here: the greeter owns its home and could replace a directory
// between a privileged check and write.
int openDataDirectory(const QString &home, const QString &relative, bool create, uid_t owner, gid_t group, QString *error)
{
    int fd = ::open(QFile::encodeName(home).constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) {
        fail(error, QStringLiteral("Cannot open greeter home"));
        return -1;
    }
    for (const auto &part : relative.split(QLatin1Char('/'))) {
        const QByteArray name = QFile::encodeName(part);
        bool created = false;
        struct stat info;
        if (fstatat(fd, name.constData(), &info, AT_SYMLINK_NOFOLLOW) != 0) {
            if (errno == ENOENT && !create) {
                ::close(fd);
                return -2;
            }
            if (errno != ENOENT || mkdirat(fd, name.constData(), 0755) != 0) {
                ::close(fd);
                fail(error, QStringLiteral("Cannot create theme directory: %1").arg(part));
                return -1;
            }
            created = true;
        }
        const int next = openat(fd, name.constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        ::close(fd);
        if (next < 0) {
            fail(error, QStringLiteral("Unsafe theme directory: %1").arg(part));
            return -1;
        }
        fd = next;
        if (created && (fchmod(fd, 0755) != 0 || (owner != uid_t(-1) && fchown(fd, owner, group) != 0))) {
            ::close(fd);
            fail(error, QStringLiteral("Cannot set theme directory ownership"));
            return -1;
        }
    }
    return fd;
}

QString descriptorPath(int fd)
{
    return QStringLiteral("/dev/fd/") + QString::number(fd);
}

// Called only inside a daemon-owned, mode-0700 staging directory, before the
// completed assets become visible to the greeter account.
bool setOwner(const QString &path, uid_t owner, gid_t group, QString *error)
{
    if (owner == uid_t(-1)) {
        return true;
    }
    const QFileInfo info(path);
    if (info.isSymLink()) {
        return fail(error, QStringLiteral("Unexpected symlink in staged theme"));
    }
    if (info.isDir()) {
        const auto entries = QDir(path).entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);
        for (const auto &entry : entries) {
            if (!setOwner(entry.filePath(), owner, group, error)) {
                return false;
            }
        }
    }
    return lchown(QFile::encodeName(path).constData(), owner, group) == 0 ? true : fail(error, QStringLiteral("Cannot set theme asset ownership"));
}

// Only used below the private, daemon-owned staging directory.
bool makeDirectory(const QString &home, const QString &relative, QString *error)
{
    QString path = home;
    for (const auto &part : relative.split(QLatin1Char('/'))) {
        path += QLatin1Char('/') + part;
        const QFileInfo info(path);
        if (info.isSymLink() || (info.exists() && !info.isDir())) {
            return fail(error, QStringLiteral("Unsafe theme directory: %1").arg(path));
        }
        if (!info.exists() && (!QDir().mkdir(path) || !QFile::setPermissions(path, directoryPermissions))) {
            return fail(error, QStringLiteral("Cannot create theme directory: %1").arg(path));
        }
    }
    return true;
}

bool collect(const QString &source, const QString &relative, QMap<QString, QByteArray> &files, QSet<QString> &ancestors, qsizetype &size, QString *error)
{
    const QFileInfo info(source);
    if (info.isSymLink() && !info.exists()) {
        return true; // The theme loader also ignores broken optional links.
    }
    if (info.isDir()) {
        const QString canonical = info.canonicalFilePath();
        if (canonical.isEmpty() || ancestors.contains(canonical)) {
            return fail(error, QStringLiteral("Theme contains a directory cycle: %1").arg(source));
        }
        ancestors.insert(canonical);
        const auto entries = QDir(source).entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot, QDir::Name);
        for (const auto &entry : entries) {
            if (entry.fileName() == marker || entry.fileName() == QLatin1String(".git")) {
                continue;
            }
            const QString name = relative.isEmpty() ? entry.fileName() : relative + QLatin1Char('/') + entry.fileName();
            if (!safePath(name) || !collect(entry.filePath(), name, files, ancestors, size, error)) {
                return false;
            }
        }
        ancestors.remove(canonical);
        return true;
    }
    if (files.contains(relative)) {
        return true; // A higher-priority installation already supplied it.
    }
    if (!info.isFile() || info.size() > maximumBytes - size || files.size() >= maximumFiles) {
        return fail(error, QStringLiteral("Unreadable or oversized theme asset: %1").arg(source));
    }
    QFile file(source);
    if (!file.open(QIODevice::ReadOnly)) {
        return fail(error, file.errorString());
    }
    const QByteArray bytes = file.read(maximumBytes - size + 1);
    if (file.error() != QFile::NoError || !file.atEnd() || bytes.size() > maximumBytes - size) {
        return fail(error, QStringLiteral("Cannot read theme asset: %1").arg(source));
    }
    size += bytes.size();
    files.insert(relative, bytes);
    return true;
}
}

bool bundle(const QStringList &sources, QByteArray *data, QString *error)
{
    if (sources.isEmpty()) {
        return fail(error, QStringLiteral("Selected theme is not installed"));
    }
    const bool directory = QFileInfo(sources.first()).isDir();
    QMap<QString, QByteArray> files;
    QSet<QString> ancestors;
    qsizetype size = 0;
    for (const auto &source : sources) {
        if (QFileInfo(source).isDir() != directory || !collect(source, {}, files, ancestors, size, error)) {
            return false;
        }
    }
    data->clear();
    QDataStream out(data, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_6_0);
    out << magic << directory << quint32(files.size());
    for (auto it = files.cbegin(); it != files.cend(); ++it) {
        out << it.key() << it.value();
    }
    if (out.status() != QDataStream::Ok || data->size() > maximumBytes) {
        return fail(error, QStringLiteral("Theme bundle exceeds the size limit"));
    }
    out.device()->close();
    *data = qCompress(*data, 6);
    return !data->isEmpty();
}

bool collectSettings(QMap<QString, QString> *configs, QMap<QString, QByteArray> *themes, QString *error)
{
    QTemporaryDir temporary;
    if (!temporary.isValid()) {
        return fail(error, QStringLiteral("Cannot snapshot desktop theme settings"));
    }
    auto sourcesFor = [](const QString &relativeName) {
        const bool colorScheme = relativeName.startsWith(QLatin1String("color-schemes/"));
        auto sources = QStandardPaths::locateAll(QStandardPaths::GenericDataLocation,
                                                 relativeName,
                                                 colorScheme ? QStandardPaths::LocateFile : QStandardPaths::LocateDirectory);
        if (relativeName.startsWith(QLatin1String("icons/"))) {
            const QString legacy = QDir::homePath() + QStringLiteral("/.") + relativeName;
            if (QFileInfo(legacy).isDir() && !sources.contains(legacy)) {
                const QString userPrefix = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QLatin1Char('/');
                sources.insert(!sources.isEmpty() && sources.first().startsWith(userPrefix) ? 1 : 0, legacy);
            }
        }
        return sources;
    };
    // The greeter already searches the system XDG data directories. Copy only
    // private session assets, including legacy ~/.icons and user-specific data
    // directories added to XDG_DATA_DIRS. Do not merge entire system themes into
    // a small user override with the same ID.
    const QString homePrefix = QDir::cleanPath(QDir::homePath()) + QLatin1Char('/');
    const QString dataPrefix = QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)) + QLatin1Char('/');
    auto isUserSource = [&](const QString &source) {
        const QFileInfo info(source);
        for (const auto &path : {info.absoluteFilePath(), info.canonicalFilePath()}) {
            if (path.startsWith(homePrefix) || path.startsWith(dataPrefix)) {
                return true;
            }
        }
        return false;
    };
    QSet<QString> visited;
    qsizetype bundledBytes = 0;
    std::function<bool(const QString &)> addTheme;
    addTheme = [&](const QString &relativeName) {
        if (assetRoot(relativeName).isEmpty()) {
            return fail(error, QStringLiteral("Invalid selected theme name: %1").arg(relativeName));
        }
        if (visited.contains(relativeName)) {
            return true;
        }
        if (visited.size() >= 64) {
            return fail(error, QStringLiteral("Too many inherited themes"));
        }
        visited.insert(relativeName);
        auto sources = sourcesFor(relativeName);
        QStringList privateSources;
        for (const auto &source : sources) {
            if (isUserSource(source)) {
                privateSources << source;
            }
        }
        if (sources.isEmpty() && relativeName.startsWith(QLatin1String("color-schemes/"))
            && KSharedConfig::openConfig(QStringLiteral("kdeglobals"), KConfig::NoGlobals)->hasGroup(QStringLiteral("Colors:View"))) {
            // A customized palette may exist only in kdeglobals, with no
            // separately installed .colors file. Snapshot it under its ID.
            privateSources << temporary.path() + QStringLiteral("/kdeglobals");
            sources = privateSources;
        }
        if (sources.isEmpty()) {
            return fail(error, QStringLiteral("%1: Selected theme is not installed").arg(relativeName));
        }
        if (!privateSources.isEmpty()) {
            QByteArray bytes;
            if (!bundle(privateSources, &bytes, error)) {
                if (error) {
                    *error = QStringLiteral("%1: %2").arg(relativeName, *error);
                }
                return false;
            }
            bundledBytes += qFromBigEndian<quint32>(bytes.constData());
            if (bundledBytes > 2 * maximumBytes) {
                return fail(error, QStringLiteral("Selected themes exceed the total size limit"));
            }
            themes->insert(relativeName, bytes);
        }
        if (relativeName.startsWith(QLatin1String("icons/"))) {
            // Visit inherited themes too, but transfer only their user-local
            // assets. System parents remain available through normal lookup.
            for (const auto &source : sources) {
                if (!QFileInfo::exists(source + QStringLiteral("/index.theme"))) {
                    continue;
                }
                KConfig index(source + QStringLiteral("/index.theme"), KConfig::SimpleConfig);
                const auto inherited = index.group(QStringLiteral("Icon Theme")).readEntry("Inherits", QStringList());
                for (const auto &parent : inherited) {
                    const QString name = parent == QLatin1String("default") ? QStringLiteral("silver") : parent;
                    if (!name.isEmpty() && !addTheme(QStringLiteral("icons/") + name)) {
                        return false;
                    }
                }
                break;
            }
        } else if (relativeName.startsWith(QLatin1String("plasma/desktoptheme/"))) {
            // Match Plasma's plasmarc / legacy metadata.desktop fallback chain.
            for (const auto &file : {QStringLiteral("plasmarc"), QStringLiteral("metadata.desktop")}) {
                const QString path = QStandardPaths::locate(QStandardPaths::GenericDataLocation, relativeName + QLatin1Char('/') + file);
                if (!path.isEmpty()) {
                    KConfig metadata(path, KConfig::SimpleConfig);
                    const QString fallback = metadata.group(QStringLiteral("Settings")).readEntry("FallbackTheme", QString());
                    if (!fallback.isEmpty() && !addTheme(QStringLiteral("plasma/desktoptheme/") + fallback)) {
                        return false;
                    }
                    break;
                }
            }
        }
        return true;
    };

    for (const auto &name : {QStringLiteral("kdeglobals"), QStringLiteral("plasmarc"), QStringLiteral("kcminputrc")}) {
        const auto effective = KSharedConfig::openConfig(name, KConfig::NoGlobals);
        effective->reparseConfiguration();
        KConfig snapshot(temporary.path() + QLatin1Char('/') + name, KConfig::SimpleConfig);
        for (const auto &groupName : effective->groupList()) {
            auto destination = snapshot.group(groupName);
            effective->group(groupName).copyTo(&destination);
        }
        QStringList selected;
        if (name == QLatin1String("kdeglobals")) {
            const QString icons = effective->group(QStringLiteral("Icons")).readEntry("Theme", QStringLiteral("silver"));
            const QString colors = effective->group(QStringLiteral("General")).readEntry("ColorScheme", QStringLiteral("SilverLight"));
            snapshot.group(QStringLiteral("Icons")).writeEntry("Theme", icons);
            snapshot.group(QStringLiteral("General")).writeEntry("ColorScheme", colors);
            selected << QStringLiteral("icons/") + icons << QStringLiteral("color-schemes/") + colors + QStringLiteral(".colors");
            const QString lookAndFeel = effective->group(QStringLiteral("KDE")).readEntry("LookAndFeelPackage", QString());
            if (!lookAndFeel.isEmpty()) {
                snapshot.group(QStringLiteral("KDE")).writeEntry("LookAndFeelPackage", lookAndFeel);
                selected << QStringLiteral("plasma/look-and-feel/") + lookAndFeel;
            }
        } else if (name == QLatin1String("plasmarc")) {
            const QString desktop = effective->group(QStringLiteral("Theme")).readEntry("name", QStringLiteral("default"));
            snapshot.group(QStringLiteral("Theme")).writeEntry("name", desktop);
            selected << QStringLiteral("plasma/desktoptheme/") + desktop;
            if (desktop != QLatin1String("default")) {
                selected << QStringLiteral("plasma/desktoptheme/default");
            }
        } else {
            const QString cursor = effective->group(QStringLiteral("Mouse")).readEntry("cursorTheme", QStringLiteral("silver_cursors_light"));
            snapshot.group(QStringLiteral("Mouse")).writeEntry("cursorTheme", cursor);
            selected << QStringLiteral("icons/") + cursor;
        }
        if (!snapshot.sync()) {
            return fail(error, QStringLiteral("Cannot snapshot %1").arg(name));
        }
        QFile file(snapshot.name());
        if (!file.open(QIODevice::ReadOnly)) {
            return fail(error, file.errorString());
        }
        configs->insert(name, QString::fromUtf8(file.readAll()));
        for (const auto &theme : selected) {
            if (!addTheme(theme)) {
                return false;
            }
        }
    }
    return true;
}

bool install(const QString &home, const QString &relativeName, const QByteArray &data, QString *error, uid_t owner, gid_t group)
{
    const QString root = assetRoot(relativeName);
    if (root.isEmpty() || data.size() > maximumBytes) {
        return fail(error, QStringLiteral("Invalid theme destination or bundle size"));
    }
    if (data.size() < 5 || qFromBigEndian<quint32>(data.constData()) == 0 || qFromBigEndian<quint32>(data.constData()) > maximumBytes) {
        return fail(error, QStringLiteral("Invalid expanded theme bundle size"));
    }
    // The qCompress header is a size hint, not a decompression limit. Use a
    // fixed-size zlib buffer so a forged size cannot cause unbounded growth.
    QByteArray expanded(qFromBigEndian<quint32>(data.constData()), Qt::Uninitialized);
    uLongf expandedSize = expanded.size();
    uLong compressedSize = data.size() - 4;
    if (uncompress2(reinterpret_cast<Bytef *>(expanded.data()), &expandedSize, reinterpret_cast<const Bytef *>(data.constData() + 4), &compressedSize) != Z_OK
        || expandedSize != uLongf(expanded.size()) || compressedSize != uLong(data.size() - 4)) {
        return fail(error, QStringLiteral("Invalid compressed theme bundle"));
    }
    QDataStream in(expanded);
    in.setVersion(QDataStream::Qt_6_0);
    quint32 signature = 0, count = 0;
    bool directory = false;
    in >> signature >> directory >> count;
    if (signature != magic || count > maximumFiles || (root == QLatin1String("color-schemes")) == directory) {
        return fail(error, QStringLiteral("Invalid theme bundle header"));
    }
    if (!directory && (count != 1 || !relativeName.endsWith(QLatin1String(".colors")))) {
        return fail(error, QStringLiteral("Invalid color-scheme bundle"));
    }
    QMap<QString, QByteArray> files;
    for (quint32 i = 0; i < count; ++i) {
        QString name;
        QByteArray bytes;
        in >> name >> bytes;
        if (in.status() != QDataStream::Ok || (directory ? !safePath(name) : !name.isEmpty()) || files.contains(name)) {
            return fail(error, QStringLiteral("Invalid theme asset path or data"));
        }
        files.insert(name, bytes);
    }
    if (in.status() != QDataStream::Ok || !in.atEnd()) {
        return fail(error, QStringLiteral("Truncated or trailing theme data"));
    }
    const QString parent = QStringLiteral(".local/share/") + root;
    const int parentFd = openDataDirectory(home, parent, true, owner, group, error);
    if (parentFd < 0) {
        return false;
    }
    const auto closeParent = qScopeGuard([parentFd] {
        ::close(parentFd);
    });
    const QString parentPath = descriptorPath(parentFd);
    const QString target = parentPath + QLatin1Char('/') + relativeName.section(QLatin1Char('/'), -1);
    const QString targetMarker = directory ? target + QLatin1Char('/') + marker : target + marker;
    const QFileInfo targetInfo(target);
    if (targetInfo.isSymLink() || (targetInfo.exists() && (!QFileInfo(targetMarker).isFile() || QFileInfo(targetMarker).isSymLink()))) {
        return fail(error, QStringLiteral("Refusing to overwrite an independently installed theme: %1").arg(relativeName));
    }
    QTemporaryDir staging(parentPath + QStringLiteral("/.soniclogin-theme-XXXXXX"));
    if (!staging.isValid()) {
        return fail(error, QStringLiteral("Cannot stage theme assets"));
    }
    const QString content = staging.path() + QStringLiteral("/content");
    if (directory && (!QDir().mkdir(content) || !QFile::setPermissions(content, directoryPermissions))) {
        return fail(error, QStringLiteral("Cannot stage theme directory"));
    }
    for (auto it = files.cbegin(); it != files.cend(); ++it) {
        const QString destination = directory ? content + QLatin1Char('/') + it.key() : content;
        if (directory) {
            const QString relativeParent = it.key().section(QLatin1Char('/'), 0, -2);
            if (it.key().contains(QLatin1Char('/')) && !makeDirectory(content, relativeParent, error)) {
                return false;
            }
        }
        QSaveFile file(destination);
        if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(filePermissions) || file.write(it.value()) != it.value().size() || !file.commit()) {
            return fail(error, QStringLiteral("Cannot write theme asset: %1").arg(it.key()));
        }
    }
    const QString stagedMarker = directory ? content + QLatin1Char('/') + marker : staging.path() + QLatin1Char('/') + marker;
    QFile ownershipMarker(stagedMarker);
    if (!ownershipMarker.open(QIODevice::WriteOnly) || ownershipMarker.write("synced from user session\n") < 0) {
        return fail(error, QStringLiteral("Cannot mark synchronized theme"));
    }
    ownershipMarker.close();
    ownershipMarker.setPermissions(filePermissions);
    if (!setOwner(content, owner, group, error) || (!directory && !setOwner(stagedMarker, owner, group, error))) {
        return false;
    }
    // Keep the previous copy until the replacement is complete and ready.
    const QString backup = staging.path() + QStringLiteral("/previous");
    if (targetInfo.exists() && !QDir().rename(target, backup)) {
        return fail(error, QStringLiteral("Cannot replace synchronized theme: %1").arg(relativeName));
    }
    if (!QDir().rename(content, target)) {
        if (targetInfo.exists()) {
            QDir().rename(backup, target);
        }
        return fail(error, QStringLiteral("Cannot install theme: %1").arg(relativeName));
    }
    if (!directory) {
        QFile::remove(targetMarker);
        if (!QFile::rename(stagedMarker, targetMarker)) {
            QFile::remove(target);
            if (targetInfo.exists()) {
                QDir().rename(backup, target);
            }
            return fail(error, QStringLiteral("Cannot mark synchronized color scheme"));
        }
    }
    return true;
}

bool writeConfig(const QString &home, const QString &relativeName, const std::optional<QString> &text, QString *error, uid_t owner, gid_t group)
{
    const QStringList allowed = {QStringLiteral("kdeglobals"),
                                 QStringLiteral("plasmarc"),
                                 QStringLiteral("plasma-localerc"),
                                 QStringLiteral("kcminputrc"),
                                 QStringLiteral("kwinoutputconfig.json"),
                                 QStringLiteral("fontconfig/fonts.conf"),
                                 QStringLiteral("kxkbrc")};
    if (!allowed.contains(relativeName)) {
        return fail(error, QStringLiteral("Invalid synchronized configuration name"));
    }
    const QString parent = relativeName.contains(QLatin1Char('/')) ? QStringLiteral(".config/fontconfig") : QStringLiteral(".config");
    const int parentFd = openDataDirectory(home, parent, text.has_value(), owner, group, error);
    if (parentFd == -2 && !text) {
        return true;
    }
    if (parentFd < 0) {
        return false;
    }
    const auto closeParent = qScopeGuard([parentFd] {
        ::close(parentFd);
    });
    const QString name = relativeName.section(QLatin1Char('/'), -1);
    if (!text) {
        return unlinkat(parentFd, QFile::encodeName(name).constData(), 0) == 0 || errno == ENOENT
            ? true
            : fail(error, QStringLiteral("Cannot reset synchronized configuration: %1").arg(name));
    }
    const QString parentPath = descriptorPath(parentFd);
    QTemporaryDir staging(parentPath + QStringLiteral("/.soniclogin-config-XXXXXX"));
    if (!staging.isValid()) {
        return fail(error, QStringLiteral("Cannot stage synchronized configuration"));
    }
    const QString path = staging.path() + QStringLiteral("/config");
    QSaveFile file(path);
    const QByteArray bytes = text->toUtf8();
    if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(filePermissions) || file.write(bytes) != bytes.size() || !file.commit()
        || !setOwner(path, owner, group, error)) {
        return fail(error, QStringLiteral("Cannot write synchronized configuration: %1").arg(name));
    }
    // Atomic replacement does not follow an existing greeter-owned symlink.
    return rename(QFile::encodeName(path).constData(), QFile::encodeName(parentPath + QLatin1Char('/') + name).constData()) == 0
        ? true
        : fail(error, QStringLiteral("Cannot install synchronized configuration: %1").arg(name));
}

bool prune(const QString &home, const QSet<QString> &retained, QString *error)
{
    for (const auto &root : roots) {
        const QString relative = QStringLiteral(".local/share/") + root;
        const int parentFd = openDataDirectory(home, relative, false, uid_t(-1), gid_t(-1), error);
        if (parentFd == -2) {
            continue;
        }
        if (parentFd < 0) {
            return false;
        }
        const auto closeParent = qScopeGuard([parentFd] {
            ::close(parentFd);
        });
        const QString parentPath = descriptorPath(parentFd);
        const auto entries = QDir(parentPath).entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);
        for (const auto &entry : entries) {
            if (entry.isSymLink() || retained.contains(root + QLatin1Char('/') + entry.fileName())) {
                continue;
            }
            const QString path = entry.filePath();
            const QString ownershipMarker = entry.isDir() ? path + QLatin1Char('/') + marker : path + marker;
            if (QFileInfo(ownershipMarker).isFile() && !QFileInfo(ownershipMarker).isSymLink()) {
                QTemporaryDir staging(parentPath + QStringLiteral("/.soniclogin-reset-XXXXXX"));
                const QString removedPath = staging.path() + QStringLiteral("/removed");
                if (!staging.isValid() || !QDir().rename(path, removedPath)) {
                    return fail(error, QStringLiteral("Cannot stage theme removal: %1").arg(path));
                }
                const QFileInfo removedInfo(removedPath);
                const bool removed = removedInfo.isDir() && !removedInfo.isSymLink() ? QDir(removedPath).removeRecursively() : QFile::remove(removedPath);
                if (!removed) {
                    return fail(error, QStringLiteral("Cannot remove synchronized theme: %1").arg(path));
                }
                if (!entry.isDir()) {
                    QFile::remove(ownershipMarker);
                }
            }
        }
    }
    return true;
}

bool reset(const QString &home, QString *error)
{
    return prune(home, {}, error);
}
}
