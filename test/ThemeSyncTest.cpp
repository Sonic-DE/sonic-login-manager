/*
 * SPDX-FileCopyrightText: 2026 SonicDE Community
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ThemeSync.h"

#include <QCoreApplication>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>

#include <KConfig>
#include <KConfigGroup>

#include <X11/Xcursor/Xcursor.h>
#include <algorithm>
#include <unistd.h>

using namespace SONICLOGIN;

namespace {
bool put(const QString &path, const QByteArray &bytes)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        return false;
    }
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QByteArray get(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QByteArray archive(const QList<QPair<QString, QByteArray>> &files, bool directory = true)
{
    QByteArray bytes;
    QDataStream out(&bytes, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_6_0);
    out << quint32(0x53544831) << directory << quint32(files.size());
    for (const auto &file : files) {
        out << file.first << file.second;
    }
    return qCompress(bytes);
}

bool putCursor(const QString &path)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        return false;
    }
    XcursorImages *images = XcursorImagesCreate(1);
    if (!images) {
        return false;
    }
    const auto destroy = qScopeGuard([images] {
        XcursorImagesDestroy(images);
    });
    XcursorImage *image = XcursorImageCreate(24, 24);
    if (!image) {
        return false;
    }
    images->images[0] = image;
    images->nimage = 1;
    image->size = 24;
    image->xhot = image->yhot = 0;
    std::fill_n(image->pixels, 24 * 24, XcursorPixel(0xff20c080));
    return XcursorFilenameSaveImages(QFile::encodeName(path).constData(), images);
}
}

class ThemeSyncTest : public QObject {
    Q_OBJECT

public:
    explicit ThemeSyncTest(const QString &fixture)
        : m_fixture(fixture)
    {
    }

private:
    QString m_fixture;

private Q_SLOTS:
    void init()
    {
        for (const auto &name : {"desktop", "system", "config-defaults", "synced-greeter"}) {
            const QString path = m_fixture + QLatin1Char('/') + QLatin1String(name);
            QVERIFY(QDir(path).removeRecursively());
            QVERIFY(QDir().mkpath(path));
        }
    }

    void materializesSymlinksAndMergesUserOverrides()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString user = temp.path() + "/user", system = temp.path() + "/system", home = temp.path() + "/greeter";
        QVERIFY(QDir().mkpath(home));
        QVERIFY(put(user + "/index.theme", "user metadata"));
        QVERIFY(put(user + "/icon.svg", "user icon"));
        QVERIFY(put(system + "/icon.svg", "system icon"));
        QVERIFY(put(system + "/inherited.svg", "system-only asset"));
        QVERIFY(QFile::link(user + "/icon.svg", user + "/alias.svg"));
        QByteArray data;
        QString error;
        QVERIFY2(ThemeSync::bundle({user, system}, &data, &error), qPrintable(error));
        QVERIFY2(ThemeSync::install(home, "icons/UserTheme", data, &error, getuid(), getgid()), qPrintable(error));
        const QString installed = home + "/.local/share/icons/UserTheme";
        QCOMPARE(get(installed + "/icon.svg"), "user icon");
        QCOMPARE(get(installed + "/inherited.svg"), "system-only asset");
        QCOMPARE(get(installed + "/alias.svg"), "user icon");
        QVERIFY(!QFileInfo(installed + "/alias.svg").isSymLink());
        QCOMPARE(QFileInfo(installed + "/alias.svg").ownerId(), uint(getuid()));
        QVERIFY(QFile::remove(user + "/alias.svg"));
        QVERIFY(QFile::remove(system + "/inherited.svg"));
        QVERIFY2(ThemeSync::bundle({user, system}, &data, &error), qPrintable(error));
        QVERIFY2(ThemeSync::install(home, "icons/UserTheme", data, &error), qPrintable(error));
        QVERIFY(!QFile::exists(installed + "/alias.svg"));
        QVERIFY(!QFile::exists(installed + "/inherited.svg"));
    }

    void rejectsTraversal_data()
    {
        QTest::addColumn<QString>("path");
        for (const auto &path : {"../escaped", "/absolute", "a/../../escaped", "a\\escaped", ".soniclogin-synced", "a//b", "./a"}) {
            QTest::newRow(path) << QString::fromLatin1(path);
        }
    }

    void rejectsTraversal()
    {
        QFETCH(QString, path);
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QString error;
        QVERIFY(!ThemeSync::install(temp.path(), "icons/Theme", archive({{path, "hostile"}}), &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!QFile::exists(temp.path() + "/escaped"));
    }

    void rejectsMalformedBundles()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QString error;
        const auto valid = archive({{"safe", "content"}});
        QVERIFY(!ThemeSync::install(temp.path(), "../../escaped", valid, &error));
        QVERIFY(!ThemeSync::install(temp.path(), "icons/Theme", QByteArray("truncated"), &error));
        QVERIFY(!ThemeSync::install(temp.path(), "icons/Theme", archive({{"same", "a"}, {"same", "b"}}), &error));
        QVERIFY(!ThemeSync::install(temp.path(), "icons/Theme", archive({{"", "file"}}, false), &error));
        auto falseSize = valid;
        falseSize[0] = falseSize[1] = falseSize[2] = 0;
        falseSize[3] = 1;
        QVERIFY(!ThemeSync::install(temp.path(), "icons/Theme", falseSize, &error));
        auto trailing = qUncompress(valid);
        trailing.append("trailing");
        QVERIFY(!ThemeSync::install(temp.path(), "icons/Theme", qCompress(trailing), &error));
        QVERIFY(!QFile::exists(temp.path() + "/.local/share/icons/Theme"));
    }

    void refusesSymlinkAncestorsAndPreservesIndependentThemes()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString home = temp.path() + "/greeter", outside = temp.path() + "/outside";
        QVERIFY(QDir().mkpath(home));
        QVERIFY(QDir().mkpath(outside));
        QVERIFY(QFile::link(outside, home + "/.local"));
        QString error;
        const auto data = archive({{"safe", "content"}});
        QVERIFY(!ThemeSync::install(home, "icons/Theme", data, &error));
        QVERIFY(!ThemeSync::prune(home, {}, &error));
        QVERIFY(QDir(outside).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());
        QVERIFY(QFile::remove(home + "/.local"));
        const QString independent = home + "/.local/share/icons/Theme/icon.svg";
        QVERIFY(put(independent, "preserve"));
        QVERIFY(!ThemeSync::install(home, "icons/Theme", data, &error));
        QCOMPARE(get(independent), "preserve");
        QVERIFY2(ThemeSync::reset(home, &error), qPrintable(error));
        QCOMPARE(get(independent), "preserve");
    }

    void rejectsDirectoryCycles()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QVERIFY(put(temp.path() + "/theme/index.theme", "metadata"));
        QVERIFY(QFile::link(temp.path() + "/theme", temp.path() + "/theme/loop"));
        QByteArray data;
        QString error;
        QVERIFY(!ThemeSync::bundle({temp.path() + "/theme"}, &data, &error));
        QVERIFY(!error.isEmpty());
    }

    void atomicConfigCopyDoesNotFollowSymlinks()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString home = temp.path() + "/greeter", external = temp.path() + "/outside";
        QVERIFY(QDir().mkpath(home + "/.config"));
        QVERIFY(put(external, "preserve external data"));
        QVERIFY(QFile::link(external, home + "/.config/kdeglobals"));
        QString error;
        QVERIFY2(ThemeSync::writeConfig(home, "kdeglobals", QString("[Icons]\nTheme=UserTheme\n"), &error, getuid(), getgid()), qPrintable(error));
        QCOMPARE(get(external), "preserve external data");
        QCOMPARE(get(home + "/.config/kdeglobals"), "[Icons]\nTheme=UserTheme\n");
        QVERIFY(!QFileInfo(home + "/.config/kdeglobals").isSymLink());
        QVERIFY(!ThemeSync::writeConfig(home, "../outside", QString("bad"), &error));
        QVERIFY2(ThemeSync::writeConfig(home, "kdeglobals", std::nullopt, &error), qPrintable(error));
        QVERIFY(QDir().rmdir(home + "/.config"));
        QVERIFY(QDir().mkpath(temp.path() + "/outside-dir"));
        QVERIFY(QFile::link(temp.path() + "/outside-dir", home + "/.config"));
        QVERIFY(!ThemeSync::writeConfig(home, "kdeglobals", QString("bad"), &error));
        QVERIFY(!QFile::exists(temp.path() + "/outside-dir/kdeglobals"));
    }

    void collectsThemesAndLoadsCopiedCursor_data()
    {
        QTest::addColumn<QString>("themeRoot");
        QTest::newRow("user-installed") << m_fixture + "/desktop/.local/share";
        QTest::newRow("system-installed") << m_fixture + "/system";
        QTest::newRow("legacy-user-icons") << m_fixture + "/desktop/.icons";
    }

    void collectsThemesAndLoadsCopiedCursor()
    {
        QFETCH(QString, themeRoot);
        const QString home = m_fixture + "/desktop", user = home + "/.local/share", system = m_fixture + "/system";
        const QString config = home + "/.config";
        const QString defaults = m_fixture + "/config-defaults";
        const bool legacy = themeRoot.endsWith(QLatin1String("/.icons"));
        const QString icons = legacy ? themeRoot : themeRoot + "/icons";
        const QString data = legacy ? user : themeRoot;
        // Match a real desktop: selected values override kdedefaults, while
        // some settings (such as the scheme name/font) exist only in defaults.
        QVERIFY(put(defaults + "/kdeglobals", "[Icons]\nTheme=SystemParent\n[General]\nColorScheme=ChosenColors\nfont=FixtureFont,10,-1,5,50,0,0,0,0,0\n"));
        QVERIFY(put(defaults + "/plasmarc", "[Theme]\nname=SystemDesktopParent\n"));
        QVERIFY(
            put(defaults + "/kcminputrc", "[Mouse]\ncursorTheme=SystemCursorParent\ncursorSize=48\n[Libinput][Touchpad]\nTapToClick=true\nLeftHanded=false\n"));
        QVERIFY(put(config + "/kdeglobals",
                    "[Icons]\nTheme=ChosenIcons\n[KDE]\nLookAndFeelPackage=ChosenGlobal\nAutomaticLookAndFeel=true\nDefaultLightLookAndFeel="
                    "ChosenGlobal\nDefaultDarkLookAndFeel=ChosenGlobal\n"));
        QVERIFY(put(config + "/plasmarc", "[Theme]\nname=ChosenDesktop\n"));
        QVERIFY(put(config + "/kcminputrc", "[Mouse]\ncursorTheme=ChosenCursor\ncursorSize=24\n[Libinput][Touchpad]\nLeftHanded=true\n"));
        QVERIFY(put(icons + "/ChosenIcons/index.theme", "[Icon Theme]\nName=ChosenIcons\nInherits=SystemParent\n"));
        QVERIFY(put(icons + "/ChosenIcons/icon.svg", "selected icon"));
        QVERIFY(put(system + "/icons/SystemParent/index.theme", "[Icon Theme]\nName=SystemParent\nInherits=ChosenIcons\n"));
        QVERIFY(put(system + "/icons/SystemParent/parent.svg", "inherited icon"));
        QVERIFY(put(icons + "/ChosenCursor/index.theme", "[Icon Theme]\nName=ChosenCursor\nInherits=SystemCursorParent\n"));
        QVERIFY(put(system + "/icons/SystemCursorParent/index.theme", "[Icon Theme]\nName=SystemCursorParent\n"));
        QVERIFY(putCursor(system + "/icons/SystemCursorParent/cursors/left_ptr"));
        QVERIFY(put(data + "/plasma/desktoptheme/ChosenDesktop/metadata.json", "selected desktop"));
        QVERIFY(put(data + "/plasma/desktoptheme/ChosenDesktop/plasmarc", "[Settings]\nFallbackTheme=SystemDesktopParent\n"));
        QVERIFY(put(system + "/plasma/desktoptheme/SystemDesktopParent/metadata.json", "inherited desktop"));
        QVERIFY(put(system + "/plasma/desktoptheme/default/metadata.json", "fallback desktop"));
        QVERIFY(put(data + "/plasma/look-and-feel/ChosenGlobal/metadata.json", "selected global theme"));
        QVERIFY(put(data + "/color-schemes/ChosenColors.colors", "[Colors:View]\nForegroundNormal=250,250,250\n"));
        // A local override of a system theme must not pull in its system
        // assets. This sparse file exceeds the bundle limit if it is read.
        const QString systemOnly = system + "/icons/ChosenIcons/system-only.svg";
        QVERIFY(put(systemOnly, "system asset"));
        QFile largeSystemAsset(systemOnly);
        QVERIFY(largeSystemAsset.open(QIODevice::ReadWrite));
        QVERIFY(largeSystemAsset.resize(257 * 1024 * 1024));
        largeSystemAsset.close();
        QVERIFY(put(user + "/icons/NotSelected/index.theme", "unrelated user theme"));
        QVERIFY(put(system + "/icons/NotSelected/index.theme", "unrelated system theme"));
        QMap<QString, QString> configs;
        QMap<QString, QByteArray> themes;
        QString error;
        QVERIFY2(ThemeSync::collectSettings(&configs, &themes, &error), qPrintable(error));
        if (themeRoot == system) {
            QVERIFY(themes.isEmpty()); // Keep only references to system themes.
        } else {
            const QSet<QString> expected = {QStringLiteral("icons/ChosenIcons"),
                                            QStringLiteral("icons/ChosenCursor"),
                                            QStringLiteral("plasma/desktoptheme/ChosenDesktop"),
                                            QStringLiteral("color-schemes/ChosenColors.colors")};
            const auto names = themes.keys();
            QCOMPARE(QSet<QString>(names.cbegin(), names.cend()), expected);
        }
        // libXcursor caches XCURSOR_PATH after its first load. Keep the path
        // stable across data rows, with a fresh directory supplied by init().
        const QString destination = m_fixture + "/synced-greeter";
        for (auto it = themes.cbegin(); it != themes.cend(); ++it) {
            QVERIFY2(ThemeSync::install(destination, it.key(), it.value(), &error), qPrintable(it.key() + ": " + error));
        }
        for (auto it = configs.cbegin(); it != configs.cend(); ++it) {
            QVERIFY2(ThemeSync::writeConfig(destination, it.key(), it.value(), &error), qPrintable(error));
        }
        KConfig copied(destination + "/.config/kdeglobals", KConfig::SimpleConfig);
        QCOMPARE(copied.group("Icons").readEntry("Theme", QString()), "ChosenIcons");
        QCOMPARE(copied.group("General").readEntry("ColorScheme", QString()), "ChosenColors");
        QCOMPARE(copied.group("General").readEntry("font", QString()), "FixtureFont,10,-1,5,50,0,0,0,0,0");
        for (const auto &key : {"LookAndFeelPackage", "AutomaticLookAndFeel", "DefaultLightLookAndFeel", "DefaultDarkLookAndFeel"}) {
            QVERIFY(!copied.group("KDE").hasKey(key));
        }
        if (themeRoot != system) {
            QCOMPARE(get(destination + "/.local/share/icons/ChosenIcons/icon.svg"), "selected icon");
        } else {
            QVERIFY(!QFile::exists(destination + "/.local/share/icons/ChosenIcons"));
        }
        QVERIFY(!QFile::exists(destination + "/.local/share/icons/ChosenIcons/system-only.svg"));
        QVERIFY(!QFile::exists(destination + "/.local/share/icons/SystemParent"));
        QVERIFY(!QFile::exists(destination + "/.local/share/icons/SystemCursorParent"));
        QVERIFY(!QFile::exists(destination + "/.local/share/plasma/desktoptheme/default"));
        QCOMPARE(get(system + "/icons/SystemParent/parent.svg"), "inherited icon");
        KConfig copiedInput(destination + "/.config/kcminputrc", KConfig::SimpleConfig);
        QCOMPARE(copiedInput.group("Mouse").readEntry("cursorTheme", QString()), "ChosenCursor");
        QCOMPARE(copiedInput.group("Mouse").readEntry("cursorSize", 0), 24);
        QVERIFY(copiedInput.group("Libinput").group("Touchpad").readEntry("TapToClick", false));
        QVERIFY(copiedInput.group("Libinput").group("Touchpad").readEntry("LeftHanded", false));
        KConfig copiedPlasma(destination + "/.config/plasmarc", KConfig::SimpleConfig);
        QCOMPARE(copiedPlasma.group("Theme").readEntry("name", QString()), "ChosenDesktop");
        QCOMPARE(get(destination + "/.config/kdeglobals").count("\nTheme="), 1);
        QCOMPARE(get(destination + "/.config/kdeglobals").count("\nColorScheme="), 1);
        QCOMPARE(get(destination + "/.config/kdeglobals").count("\nfont="), 1);
        QCOMPARE(get(destination + "/.config/plasmarc").count("\nname="), 1);
        QCOMPARE(get(destination + "/.config/kcminputrc").count("\ncursorTheme="), 1);
        QCOMPARE(get(destination + "/.config/kcminputrc").count("\nLeftHanded="), 1);
        const QByteArray oldPath = qgetenv("XCURSOR_PATH");
        const bool hadPath = qEnvironmentVariableIsSet("XCURSOR_PATH");
        const auto restorePath = qScopeGuard([&] {
            hadPath ? qputenv("XCURSOR_PATH", oldPath) : qunsetenv("XCURSOR_PATH");
        });
        qputenv("XCURSOR_PATH", (destination + "/.local/share/icons:" + system + "/icons").toUtf8());
        XcursorImages *cursor = XcursorLibraryLoadImages("left_ptr", "ChosenCursor", 24);
        QVERIFY(cursor);
        const auto destroyCursor = qScopeGuard([cursor] {
            XcursorImagesDestroy(cursor);
        });
        QVERIFY(cursor->nimage > 0);
        QCOMPARE(cursor->images[0]->size, XcursorDim(24));
        QCOMPARE(cursor->images[0]->pixels[0], XcursorPixel(0xff20c080));
        QVERIFY2(ThemeSync::reset(destination, &error), qPrintable(error));
        for (auto it = themes.cbegin(); it != themes.cend(); ++it) {
            QVERIFY(!QFile::exists(destination + "/.local/share/" + it.key()));
        }
        QVERIFY(!QFile::exists(destination + "/.local/share/color-schemes/ChosenColors.colors.soniclogin-synced"));
    }

    void fallbackSelectionsUseSilverDark()
    {
        const QString system = m_fixture + "/system";
        QVERIFY(put(system + "/icons/silver-dark/index.theme", "[Icon Theme]\nName=Silver Dark\n"));
        QVERIFY(put(system + "/icons/silver_cursors_dark/index.theme", "[Icon Theme]\nName=Silver Cursor Dark\n"));
        QVERIFY(put(system + "/plasma/desktoptheme/silver-dark/metadata.json", "{}"));
        QVERIFY(put(system + "/plasma/desktoptheme/default/metadata.json", "{}"));
        QVERIFY(put(system + "/color-schemes/SilverDark.colors", "[Colors:Window]\nForegroundNormal=252,252,252\n"));
        QMap<QString, QString> configs;
        QMap<QString, QByteArray> themes;
        QString error;
        QVERIFY2(ThemeSync::collectSettings(&configs, &themes, &error), qPrintable(error));
        QVERIFY(themes.isEmpty());
        const QString home = m_fixture + "/synced-greeter";
        for (auto it = configs.cbegin(); it != configs.cend(); ++it) {
            QVERIFY2(ThemeSync::writeConfig(home, it.key(), it.value(), &error), qPrintable(error));
        }
        KConfig globals(home + "/.config/kdeglobals", KConfig::SimpleConfig);
        KConfig plasma(home + "/.config/plasmarc", KConfig::SimpleConfig);
        KConfig input(home + "/.config/kcminputrc", KConfig::SimpleConfig);
        QCOMPARE(globals.group("Icons").readEntry("Theme", QString()), "silver-dark");
        QCOMPARE(globals.group("General").readEntry("ColorScheme", QString()), "SilverDark");
        QCOMPARE(plasma.group("Theme").readEntry("name", QString()), "silver-dark");
        QCOMPARE(input.group("Mouse").readEntry("cursorTheme", QString()), "silver_cursors_dark");
        QVERIFY(!globals.group("KDE").hasKey("LookAndFeelPackage"));
    }

    void missingSelectedThemeIsAnError()
    {
        QVERIFY(put(m_fixture + "/desktop/.config/kdeglobals", "[Icons]\nTheme=NotInstalled\n"));
        QMap<QString, QString> configs;
        QMap<QString, QByteArray> themes;
        QString error;
        QVERIFY(!ThemeSync::collectSettings(&configs, &themes, &error));
        QVERIFY2(error.contains(QLatin1String("NotInstalled")), qPrintable(error));
    }

    void prunesStaleManagedCopiesButKeepsCurrentAndIndependentThemes()
    {
        QTemporaryDir home;
        QVERIFY(home.isValid());
        QString error;
        const auto directory = archive({{"asset.svg", "theme asset"}});
        QVERIFY2(ThemeSync::install(home.path(), "icons/OldSystemCopy", directory, &error), qPrintable(error));
        QVERIFY2(ThemeSync::install(home.path(), "icons/CurrentUserTheme", directory, &error), qPrintable(error));
        QVERIFY2(ThemeSync::install(home.path(), "color-schemes/OldSystem.colors", archive({{"", "old colors"}}, false), &error), qPrintable(error));
        const QString data = home.path() + "/.local/share/";
        QVERIFY(put(data + "icons/Independent/asset.svg", "preserve independent theme"));
        QVERIFY2(ThemeSync::prune(home.path(), {QStringLiteral("icons/CurrentUserTheme")}, &error), qPrintable(error));
        QVERIFY(!QFile::exists(data + "icons/OldSystemCopy"));
        QVERIFY(!QFile::exists(data + "color-schemes/OldSystem.colors"));
        QVERIFY(!QFile::exists(data + "color-schemes/OldSystem.colors.soniclogin-synced"));
        QCOMPARE(get(data + "icons/CurrentUserTheme/asset.svg"), "theme asset");
        QCOMPARE(get(data + "icons/Independent/asset.svg"), "preserve independent theme");
        QVERIFY2(ThemeSync::reset(home.path(), &error), qPrintable(error));
        QVERIFY(!QFile::exists(data + "icons/CurrentUserTheme"));
        QCOMPARE(get(data + "icons/Independent/asset.svg"), "preserve independent theme");
    }

    void transfersSparseUserOverridesAndInlinePaletteOnly()
    {
        const QString user = m_fixture + "/desktop/.local/share", system = m_fixture + "/system";
        const QString config = m_fixture + "/desktop/.config";
        QVERIFY(put(config + "/kdeglobals", "[Icons]\nTheme=Shared\n[General]\nColorScheme=PrivatePalette\n[Colors:View]\nForegroundNormal=250,240,230\n"));
        QVERIFY(put(config + "/plasmarc", "[Theme]\nname=default\n"));
        QVERIFY(put(config + "/kcminputrc", "[Mouse]\ncursorTheme=SystemCursor\n"));
        QVERIFY(put(system + "/icons/Shared/index.theme", "[Icon Theme]\nName=Shared\nInherits=PrivateParent,SystemParent\n"));
        QVERIFY(put(system + "/icons/Shared/icon.svg", "system icon"));
        QVERIFY(put(system + "/icons/Shared/system-only.svg", "system asset"));
        QVERIFY(put(system + "/icons/SystemParent/index.theme", "[Icon Theme]\nName=SystemParent\n"));
        QVERIFY(put(system + "/icons/SystemCursor/index.theme", "[Icon Theme]\nName=SystemCursor\n"));
        QVERIFY(put(system + "/plasma/desktoptheme/default/metadata.json", "system desktop theme"));
        // No local index.theme: retain the system metadata and transfer just
        // this override. A private inherited parent still needs transferring.
        QVERIFY(put(user + "/icons/Shared/icon.svg", "user override"));
        QVERIFY(put(user + "/icons/PrivateParent/index.theme", "[Icon Theme]\nName=PrivateParent\n"));
        QVERIFY(put(user + "/icons/PrivateParent/parent.svg", "private parent"));
        QMap<QString, QString> configs;
        QMap<QString, QByteArray> themes;
        QString error;
        QVERIFY2(ThemeSync::collectSettings(&configs, &themes, &error), qPrintable(error));
        const QSet<QString> expected = {QStringLiteral("icons/Shared"),
                                        QStringLiteral("icons/PrivateParent"),
                                        QStringLiteral("color-schemes/PrivatePalette.colors")};
        const auto names = themes.keys();
        QCOMPARE(QSet<QString>(names.cbegin(), names.cend()), expected);
        const QString home = m_fixture + "/synced-greeter";
        for (auto it = themes.cbegin(); it != themes.cend(); ++it) {
            QVERIFY2(ThemeSync::install(home, it.key(), it.value(), &error), qPrintable(error));
        }
        const QString destination = home + "/.local/share/";
        QCOMPARE(get(destination + "icons/Shared/icon.svg"), "user override");
        QVERIFY(!QFile::exists(destination + "icons/Shared/system-only.svg"));
        QVERIFY(!QFile::exists(destination + "icons/Shared/index.theme"));
        QCOMPARE(get(destination + "icons/PrivateParent/parent.svg"), "private parent");
        QVERIFY(get(destination + "color-schemes/PrivatePalette.colors").contains("ForegroundNormal=250,240,230"));
        QCOMPARE(get(system + "/icons/Shared/icon.svg"), "system icon");
    }
};

int main(int argc, char **argv)
{
    QTemporaryDir fixture;
    if (!fixture.isValid()) {
        return 1;
    }
    qputenv("HOME", (fixture.path() + "/desktop").toUtf8());
    qputenv("XDG_CONFIG_HOME", (fixture.path() + "/desktop/.config").toUtf8());
    qputenv("XDG_CONFIG_DIRS", (fixture.path() + "/config-defaults").toUtf8());
    qputenv("XDG_CACHE_HOME", (fixture.path() + "/cache").toUtf8());
    qputenv("XDG_DATA_HOME", (fixture.path() + "/desktop/.local/share").toUtf8());
    qputenv("XDG_DATA_DIRS", (fixture.path() + "/system").toUtf8());
    QCoreApplication app(argc, argv);
    ThemeSyncTest tests(fixture.path());
    return QTest::qExec(&tests, argc, argv);
}

#include "ThemeSyncTest.moc"
