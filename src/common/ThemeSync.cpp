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
#include <QRandomGenerator>
#include <QScopeGuard>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtEndian>

#include <KConfigGroup>
#include <KSharedConfig>

#include <cerrno>
#include <dirent.h>
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

void copyEffectiveGroup(const KConfigGroup &source, KConfigGroup &destination)
{
    // copyTo() clones internal default entries too. A standalone snapshot must
    // contain each resolved key once, not conflicting user/default records.
    const auto entries = source.entryMap();
    for (auto it = entries.cbegin(); it != entries.cend(); ++it) {
        destination.writeEntry(it.key(), it.value());
    }
    for (const auto &name : source.groupList()) {
        auto child = destination.group(name);
        copyEffectiveGroup(source.group(name), child);
    }
}

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

// Everything below a pinned directory works relative to its descriptor, so a
// directory the greeter swaps afterwards is never written through.
int openDirectoryAt(int directoryFd, const QByteArray &name)
{
    return openat(directoryFd, name.constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
}

bool isRegularFileAt(int directoryFd, const QByteArray &name)
{
    struct stat info;
    return fstatat(directoryFd, name.constData(), &info, AT_SYMLINK_NOFOLLOW) == 0 && S_ISREG(info.st_mode);
}

QList<QByteArray> entriesAt(int directoryFd)
{
    QList<QByteArray> names;
    // A descriptor of its own: closedir() closes it, and readdir() moves its offset.
    const int fd = openat(directoryFd, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    DIR *directory = fd >= 0 ? fdopendir(fd) : nullptr;
    if (!directory) {
        if (fd >= 0) {
            ::close(fd);
        }
        return names;
    }
    while (const dirent *entry = readdir(directory)) {
        const QByteArray name(entry->d_name);
        if (name != "." && name != "..") {
            names.append(name);
        }
    }
    closedir(directory);
    return names;
}

// Never follows symlinks: a symlink is removed itself.
bool removeAt(int directoryFd, const QByteArray &name)
{
    struct stat info;
    if (fstatat(directoryFd, name.constData(), &info, AT_SYMLINK_NOFOLLOW) != 0) {
        return errno == ENOENT;
    }
    if (!S_ISDIR(info.st_mode)) {
        return unlinkat(directoryFd, name.constData(), 0) == 0;
    }
    const int fd = openDirectoryAt(directoryFd, name);
    if (fd < 0) {
        return false;
    }
    bool removed = true;
    for (const auto &entry : entriesAt(fd)) {
        removed = removeAt(fd, entry) && removed;
    }
    ::close(fd);
    return removed && unlinkat(directoryFd, name.constData(), AT_REMOVEDIR) == 0;
}

// A new mode-0700 directory below the pinned parent; *name receives its entry name.
int makeStagingAt(int parentFd, const QString &prefix, QByteArray *name)
{
    for (int attempt = 0; attempt < 100; ++attempt) {
        *name = QFile::encodeName(prefix + QString::number(QRandomGenerator::system()->generate64(), 36));
        if (mkdirat(parentFd, name->constData(), 0700) == 0) {
            const int fd = openDirectoryAt(parentFd, *name);
            if (fd < 0) {
                unlinkat(parentFd, name->constData(), AT_REMOVEDIR);
            }
            return fd;
        }
        if (errno != EEXIST) {
            return -1;
        }
    }
    return -1;
}

// Empties the staging directory through its own descriptor, then removes its entry.
void removeStaging(int parentFd, int stagingFd, const QByteArray &name)
{
    for (const auto &entry : entriesAt(stagingFd)) {
        removeAt(stagingFd, entry);
    }
    ::close(stagingFd);
    unlinkat(parentFd, name.constData(), AT_REMOVEDIR);
}

bool writeFileAt(int directoryFd, const QByteArray &name, const QByteArray &bytes)
{
    const int fd = openat(directoryFd, name.constData(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) {
        return false;
    }
    qsizetype written = 0;
    while (written < bytes.size()) {
        const ssize_t count = ::write(fd, bytes.constData() + written, size_t(bytes.size() - written));
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            break;
        }
        written += count;
    }
    const bool complete = written == bytes.size() && fchmod(fd, 0644) == 0 && fsync(fd) == 0;
    return ::close(fd) == 0 && complete;
}

// Only used below the private, daemon-owned staging directory. Returns the
// deepest directory's descriptor.
int makeDirectoriesAt(int directoryFd, const QString &relative)
{
    int fd = openat(directoryFd, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    for (const auto &part : relative.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        if (fd < 0) {
            return -1;
        }
        const QByteArray name = QFile::encodeName(part);
        if (mkdirat(fd, name.constData(), 0700) != 0 && errno != EEXIST) {
            ::close(fd);
            return -1;
        }
        const int next = openDirectoryAt(fd, name);
        ::close(fd);
        if (next < 0 || fchmod(next, 0755) != 0) {
            if (next >= 0) {
                ::close(next);
            }
            return -1;
        }
        fd = next;
    }
    return fd;
}

// Called only inside a daemon-owned, mode-0700 staging directory, before the
// completed assets become visible to the greeter account.
bool setOwnerAt(int directoryFd, const QByteArray &name, uid_t owner, gid_t group, QString *error)
{
    if (owner == uid_t(-1)) {
        return true;
    }
    struct stat info;
    if (fstatat(directoryFd, name.constData(), &info, AT_SYMLINK_NOFOLLOW) != 0 || S_ISLNK(info.st_mode)) {
        return fail(error, QStringLiteral("Unexpected symlink in staged theme"));
    }
    if (S_ISDIR(info.st_mode)) {
        const int fd = openDirectoryAt(directoryFd, name);
        if (fd < 0) {
            return fail(error, QStringLiteral("Cannot set theme asset ownership"));
        }
        for (const auto &entry : entriesAt(fd)) {
            if (!setOwnerAt(fd, entry, owner, group, error)) {
                ::close(fd);
                return false;
            }
        }
        ::close(fd);
    }
    return fchownat(directoryFd, name.constData(), owner, group, AT_SYMLINK_NOFOLLOW) == 0 ? true
                                                                                           : fail(error, QStringLiteral("Cannot set theme asset ownership"));
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
            copyEffectiveGroup(effective->group(groupName), destination);
        }
        QStringList selected;
        if (name == QLatin1String("kdeglobals")) {
            const QString icons = effective->group(QStringLiteral("Icons")).readEntry("Theme", QStringLiteral("silver-dark"));
            const QString colors = effective->group(QStringLiteral("General")).readEntry("ColorScheme", QStringLiteral("SilverDark"));
            snapshot.group(QStringLiteral("Icons")).writeEntry("Theme", icons);
            snapshot.group(QStringLiteral("General")).writeEntry("ColorScheme", colors);
            selected << QStringLiteral("icons/") + icons << QStringLiteral("color-schemes/") + colors + QStringLiteral(".colors");
            // Keep the greeter's own fallback profile; apply the resolved
            // desktop categories without importing its global-profile policy.
            for (const auto &key : {"LookAndFeelPackage", "AutomaticLookAndFeel", "DefaultLightLookAndFeel", "DefaultDarkLookAndFeel"}) {
                snapshot.group("KDE").deleteEntry(key);
            }
        } else if (name == QLatin1String("plasmarc")) {
            const QString desktop = effective->group(QStringLiteral("Theme")).readEntry("name", QStringLiteral("silver-dark"));
            snapshot.group(QStringLiteral("Theme")).writeEntry("name", desktop);
            selected << QStringLiteral("plasma/desktoptheme/") + desktop;
            if (desktop != QLatin1String("default")) {
                selected << QStringLiteral("plasma/desktoptheme/default");
            }
        } else {
            const QString cursor = effective->group(QStringLiteral("Mouse")).readEntry("cursorTheme", QStringLiteral("silver_cursors_dark"));
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
    const QByteArray targetName = QFile::encodeName(relativeName.section(QLatin1Char('/'), -1));
    const QByteArray markerName = QFile::encodeName(marker);
    const QByteArray targetMarkerName = targetName + markerName;
    struct stat targetInfo;
    const bool targetExists = fstatat(parentFd, targetName.constData(), &targetInfo, AT_SYMLINK_NOFOLLOW) == 0;
    bool marked = false;
    if (targetExists && directory) {
        const int targetFd = openDirectoryAt(parentFd, targetName);
        marked = targetFd >= 0 && isRegularFileAt(targetFd, markerName);
        if (targetFd >= 0) {
            ::close(targetFd);
        }
    } else if (targetExists) {
        marked = !S_ISLNK(targetInfo.st_mode) && isRegularFileAt(parentFd, targetMarkerName);
    }
    if (targetExists && !marked) {
        return fail(error, QStringLiteral("Refusing to overwrite an independently installed theme: %1").arg(relativeName));
    }
    QByteArray stagingName;
    const int stagingFd = makeStagingAt(parentFd, QStringLiteral(".soniclogin-theme-"), &stagingName);
    if (stagingFd < 0) {
        return fail(error, QStringLiteral("Cannot stage theme assets"));
    }
    const auto removeStagingDirectory = qScopeGuard([parentFd, stagingFd, &stagingName] {
        removeStaging(parentFd, stagingFd, stagingName);
    });
    const QByteArray contentName("content");
    const QByteArray previousName("previous");
    int contentFd = -1;
    if (directory) {
        if (mkdirat(stagingFd, contentName.constData(), 0700) != 0 || (contentFd = openDirectoryAt(stagingFd, contentName)) < 0
            || fchmod(contentFd, 0755) != 0) {
            return fail(error, QStringLiteral("Cannot stage theme directory"));
        }
    }
    const auto closeContent = qScopeGuard([contentFd] {
        if (contentFd >= 0) {
            ::close(contentFd);
        }
    });
    for (auto it = files.cbegin(); it != files.cend(); ++it) {
        bool written = false;
        if (directory) {
            const int fd = makeDirectoriesAt(contentFd, it.key().section(QLatin1Char('/'), 0, -2));
            if (fd < 0) {
                return fail(error, QStringLiteral("Cannot create theme directory: %1").arg(it.key()));
            }
            written = writeFileAt(fd, QFile::encodeName(it.key().section(QLatin1Char('/'), -1)), it.value());
            ::close(fd);
        } else {
            written = writeFileAt(stagingFd, contentName, it.value());
        }
        if (!written) {
            return fail(error, QStringLiteral("Cannot write theme asset: %1").arg(it.key()));
        }
    }
    if (!writeFileAt(directory ? contentFd : stagingFd, markerName, QByteArrayLiteral("synced from user session\n"))) {
        return fail(error, QStringLiteral("Cannot mark synchronized theme"));
    }
    if (!setOwnerAt(stagingFd, contentName, owner, group, error) || (!directory && !setOwnerAt(stagingFd, markerName, owner, group, error))) {
        return false;
    }
    // Keep the previous copy until the replacement is complete and ready.
    if (targetExists && renameat(parentFd, targetName.constData(), stagingFd, previousName.constData()) != 0) {
        return fail(error, QStringLiteral("Cannot replace synchronized theme: %1").arg(relativeName));
    }
    if (renameat(stagingFd, contentName.constData(), parentFd, targetName.constData()) != 0) {
        if (targetExists) {
            renameat(stagingFd, previousName.constData(), parentFd, targetName.constData());
        }
        return fail(error, QStringLiteral("Cannot install theme: %1").arg(relativeName));
    }
    if (!directory) {
        unlinkat(parentFd, targetMarkerName.constData(), 0);
        if (renameat(stagingFd, markerName.constData(), parentFd, targetMarkerName.constData()) != 0) {
            unlinkat(parentFd, targetName.constData(), 0);
            if (targetExists) {
                renameat(stagingFd, previousName.constData(), parentFd, targetName.constData());
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
    QByteArray stagingName;
    const int stagingFd = makeStagingAt(parentFd, QStringLiteral(".soniclogin-config-"), &stagingName);
    if (stagingFd < 0) {
        return fail(error, QStringLiteral("Cannot stage synchronized configuration"));
    }
    const auto removeStagingDirectory = qScopeGuard([parentFd, stagingFd, &stagingName] {
        removeStaging(parentFd, stagingFd, stagingName);
    });
    const QByteArray configName("config");
    if (!writeFileAt(stagingFd, configName, text->toUtf8()) || !setOwnerAt(stagingFd, configName, owner, group, error)) {
        return fail(error, QStringLiteral("Cannot write synchronized configuration: %1").arg(name));
    }
    // Atomic replacement does not follow an existing greeter-owned symlink.
    return renameat(stagingFd, configName.constData(), parentFd, QFile::encodeName(name).constData()) == 0
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
        const QByteArray markerName = QFile::encodeName(marker);
        const QByteArray removedName("removed");
        for (const auto &name : entriesAt(parentFd)) {
            const QString path = root + QLatin1Char('/') + QFile::decodeName(name);
            struct stat info;
            if (fstatat(parentFd, name.constData(), &info, AT_SYMLINK_NOFOLLOW) != 0 || S_ISLNK(info.st_mode) || retained.contains(path)) {
                continue;
            }
            const bool isDirectory = S_ISDIR(info.st_mode);
            bool marked = false;
            if (isDirectory) {
                const int fd = openDirectoryAt(parentFd, name);
                marked = fd >= 0 && isRegularFileAt(fd, markerName);
                if (fd >= 0) {
                    ::close(fd);
                }
            } else {
                marked = isRegularFileAt(parentFd, name + markerName);
            }
            if (!marked) {
                continue;
            }
            QByteArray stagingName;
            const int stagingFd = makeStagingAt(parentFd, QStringLiteral(".soniclogin-reset-"), &stagingName);
            if (stagingFd < 0) {
                return fail(error, QStringLiteral("Cannot stage theme removal: %1").arg(path));
            }
            if (renameat(parentFd, name.constData(), stagingFd, removedName.constData()) != 0) {
                removeStaging(parentFd, stagingFd, stagingName);
                return fail(error, QStringLiteral("Cannot stage theme removal: %1").arg(path));
            }
            const bool removed = removeAt(stagingFd, removedName);
            removeStaging(parentFd, stagingFd, stagingName);
            if (!removed) {
                return fail(error, QStringLiteral("Cannot remove synchronized theme: %1").arg(path));
            }
            if (!isDirectory) {
                unlinkat(parentFd, (name + markerName).constData(), 0);
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
