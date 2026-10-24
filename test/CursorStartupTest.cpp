/*
 * SPDX-FileCopyrightText: 2026 SonicDE Community
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QScopeGuard>
#include <QSettings>
#include <QTest>

#include <algorithm>

// Keep Xlib's macros after the Qt headers.
#include <X11/Xatom.h>
#include <X11/Xcursor/Xcursor.h>
#include <X11/extensions/Xfixes.h>

namespace {
const QString stateDir = QStringLiteral(CURSOR_STARTUP_STATE_DIR);
const QString binDir = QStringLiteral(CURSOR_STARTUP_BIN_DIR);

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

bool putCursor(const QString &theme, int size, XcursorPixel color)
{
    const QString path = stateDir + QStringLiteral("/.local/share/icons/") + theme;
    if (!put(path + QStringLiteral("/index.theme"), "[Icon Theme]\nName=CursorStartupTest\n")) {
        return false;
    }
    if (!QDir().mkpath(path + QStringLiteral("/cursors"))) {
        return false;
    }
    auto *images = XcursorImagesCreate(1);
    if (!images) {
        return false;
    }
    const auto destroy = qScopeGuard([images] {
        XcursorImagesDestroy(images);
    });
    auto *image = XcursorImageCreate(size, size);
    if (!image) {
        return false;
    }
    images->images[0] = image;
    images->nimage = 1;
    image->size = size;
    image->xhot = image->yhot = 0;
    std::fill_n(image->pixels, size * size, color);
    return XcursorFilenameSaveImages(QFile::encodeName(path + QStringLiteral("/cursors/left_ptr")).constData(), images);
}
}

class CursorStartupTest : public QObject {
    Q_OBJECT

    Display *m_display = nullptr;

    QByteArray resources() const
    {
        Atom type;
        int format;
        unsigned long count = 0, after = 0;
        unsigned char *data = nullptr;
        const int status =
            XGetWindowProperty(m_display, DefaultRootWindow(m_display), XA_RESOURCE_MANAGER, 0, 8192, False, XA_STRING, &type, &format, &count, &after, &data);
        QByteArray result;
        if (status == Success && format == 8 && data) {
            result = QByteArray(reinterpret_cast<const char *>(data), count);
        }
        if (data) {
            XFree(data);
        }
        return result;
    }

private Q_SLOTS:
    void initTestCase()
    {
        // Never connect this mutating test to the developer's display or bus.
        if (qgetenv("SONICLOGIN_CURSOR_TEST_ISOLATED") != "1") {
            QSKIP("CursorStartup requires the private Xvfb/dbus-run-session CTest wrapper");
        }
        m_display = XOpenDisplay(nullptr);
        QVERIFY(m_display);
        QVERIFY(QDBusConnection::sessionBus().isConnected());
        QVERIFY(QDBusConnection::sessionBus().registerService(QStringLiteral("org.kde.KWin")));
    }

    void cleanupTestCase()
    {
        if (m_display) {
            QDBusConnection::sessionBus().unregisterService(QStringLiteral("org.kde.KWin"));
            XCloseDisplay(m_display);
        }
        QDir(stateDir).removeRecursively();
        QDir(binDir).removeRecursively();
    }

    void appliesCursorBeforeGreeter_data()
    {
        QTest::addColumn<bool>("oneshot");
        QTest::addColumn<bool>("configured");
        QTest::newRow("systemd-oneshot-configured") << true << true;
        QTest::newRow("direct-startup-configured") << false << true;
        QTest::newRow("systemd-oneshot-default") << true << false;
        QTest::newRow("direct-startup-default") << false << false;
    }

    void appliesCursorBeforeGreeter()
    {
        QFETCH(bool, oneshot);
        QFETCH(bool, configured);
        QVERIFY(QDir(stateDir).removeRecursively());
        QVERIFY(QDir().mkpath(stateDir + QStringLiteral("/runtime")));
        QVERIFY(QFile::setPermissions(stateDir + QStringLiteral("/runtime"), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        const QString theme = configured ? QStringLiteral("UserCursor") : QStringLiteral("silver_cursors_dark");
        const int size = configured ? 48 : 24;
        const XcursorPixel color = configured ? 0xffd04090 : 0xff20c080;
        QVERIFY(putCursor(theme, size, color));
        const QByteArray input = configured ? "[Mouse]\ncursorTheme=UserCursor\ncursorSize=48\n" : QByteArray();
        QVERIFY(put(stateDir + QStringLiteral("/.config/kcminputrc"), input));
        // Catch regressions that only set environment variables, overwrite
        // unrelated Xresources, or leave a previous session's cursor entries.
        const QByteArray original = "Xft.dpi:\t144\nXcursor.theme:\tOldTheme\nXcursor.size:\t16\n";
        XChangeProperty(m_display,
                        DefaultRootWindow(m_display),
                        XA_RESOURCE_MANAGER,
                        XA_STRING,
                        8,
                        PropModeReplace,
                        reinterpret_cast<const unsigned char *>(original.constData()),
                        original.size());
        XSync(m_display, False);

        for (const auto &name : {QStringLiteral("sonic-win"), QStringLiteral("soniclogin-greeter"), QStringLiteral("soniclogin-wallpaper")}) {
            const QString path = binDir + QLatin1Char('/') + name;
            QVERIFY(put(path, "#!/bin/sh\nprintf '%s\\n' \"$XCURSOR_THEME\" \"$XCURSOR_SIZE\" \"$XCURSOR_PATH\" > \"$HOME/${0##*/}.env\"\n"));
            QVERIFY(QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        }
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("HOME"), stateDir);
        environment.insert(QStringLiteral("XDG_CONFIG_HOME"), stateDir + QStringLiteral("/.config"));
        environment.insert(QStringLiteral("XDG_CONFIG_DIRS"), stateDir + QStringLiteral("/empty-config"));
        environment.insert(QStringLiteral("XDG_DATA_HOME"), stateDir + QStringLiteral("/.local/share"));
        environment.insert(QStringLiteral("XDG_CACHE_HOME"), stateDir + QStringLiteral("/.cache"));
        environment.insert(QStringLiteral("XDG_RUNTIME_DIR"), stateDir + QStringLiteral("/runtime"));
        environment.insert(QStringLiteral("DBUS_SYSTEM_BUS_ADDRESS"), QStringLiteral("unix:path=/nonexistent/soniclogin-cursor-test-system-bus"));
        environment.insert(QStringLiteral("XCURSOR_THEME"), QStringLiteral("WrongInheritedTheme"));
        environment.insert(QStringLiteral("XCURSOR_SIZE"), QStringLiteral("16"));
        environment.insert(QStringLiteral("XCURSOR_PATH"), QStringLiteral("/nonexistent/inherited-cursor-path"));
        environment.insert(QStringLiteral("SONICLOGIN_SOCKET"), stateDir + QStringLiteral("/no-authentication-socket"));
        QProcess process;
        process.setProcessEnvironment(environment);
        process.setProcessChannelMode(QProcess::MergedChannels);
        const auto stop = qScopeGuard([&] {
            if (process.state() != QProcess::NotRunning) {
                process.terminate();
                if (!process.waitForFinished(5000)) {
                    process.kill();
                    process.waitForFinished();
                }
            }
        });
        process.start(QStringLiteral(CURSOR_STARTUP_HELPER), oneshot ? QStringList{QStringLiteral("--apply-kscreen")} : QStringList());
        QVERIFY(process.waitForStarted());
        if (oneshot) {
            QVERIFY(process.waitForFinished(10000));
            QVERIFY2(process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0,
                     (process.readAll() + get(stateDir + QStringLiteral("/startup.log"))).constData());
            QVERIFY(!QFile::exists(stateDir + QStringLiteral("/soniclogin-greeter.env")));
        } else {
            QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(stateDir + QStringLiteral("/soniclogin-greeter.env")), 10000);
            QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(stateDir + QStringLiteral("/soniclogin-wallpaper.env")), 10000);
            for (const auto &name : {"sonic-win", "soniclogin-greeter", "soniclogin-wallpaper"}) {
                const auto inherited = get(stateDir + QLatin1Char('/') + QLatin1String(name) + QStringLiteral(".env")).split('\n');
                QCOMPARE(inherited.value(0), theme.toLocal8Bit());
                QCOMPARE(inherited.value(1), QByteArray::number(size));
                QVERIFY(inherited.value(2).startsWith((stateDir + QStringLiteral("/.local/share/icons:")).toLocal8Bit()));
                QVERIFY(inherited.value(2).contains("/nonexistent/inherited-cursor-path"));
            }
        }
        const QByteArray updated = resources();
        QVERIFY2(updated.contains("Xft.dpi:\t144"), updated.constData());
        QVERIFY2(updated.contains("Xcursor.theme:\t" + theme.toUtf8()), updated.constData());
        QVERIFY2(updated.contains("Xcursor.size:\t" + QByteArray::number(size)), updated.constData());
        QVERIFY(!updated.contains("OldTheme"));
        QCOMPARE(updated.count("Xcursor.theme:"), 1);
        QCOMPARE(updated.count("Xcursor.size:"), 1);
        auto *cursor = XFixesGetCursorImage(m_display);
        QVERIFY(cursor);
        const auto freeCursor = qScopeGuard([cursor] {
            XFree(cursor);
        });
        QCOMPARE(cursor->width, size);
        QCOMPARE(cursor->height, size);
        QCOMPARE(cursor->pixels[0] & 0xffffffffUL, static_cast<unsigned long>(color));
        QCOMPARE(get(stateDir + QStringLiteral("/.config/kcminputrc")), input);
    }

    void systemdReadiness_data()
    {
        QTest::addColumn<bool>("appears");
        QTest::addColumn<bool>("connected");
        QTest::newRow("delayed-compositor") << true << true;
        QTest::newRow("missing-compositor") << false << true;
        QTest::newRow("disconnected-bus") << false << false;
    }

    void systemdReadiness()
    {
        QFETCH(bool, appears);
        QFETCH(bool, connected);
        QVERIFY(QDBusConnection::sessionBus().unregisterService(QStringLiteral("org.kde.KWin")));
        const auto restoreBusName = qScopeGuard([] {
            QDBusConnection::sessionBus().registerService(QStringLiteral("org.kde.KWin"));
        });
        QVERIFY(QDir(stateDir).removeRecursively());
        QVERIFY(QDir().mkpath(stateDir + QStringLiteral("/runtime")));
        QVERIFY(putCursor(QStringLiteral("ReadinessCursor"), 24, 0xff20c080));
        QVERIFY(put(stateDir + QStringLiteral("/.config/kcminputrc"), "[Mouse]\ncursorTheme=ReadinessCursor\ncursorSize=24\n"));

        const QByteArray original = "Xft.dpi:\t96\nXcursor.theme:\tNotReady\nXcursor.size:\t16\n";
        XChangeProperty(m_display,
                        DefaultRootWindow(m_display),
                        XA_RESOURCE_MANAGER,
                        XA_STRING,
                        8,
                        PropModeReplace,
                        reinterpret_cast<const unsigned char *>(original.constData()),
                        original.size());
        XSync(m_display, False);

        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("HOME"), stateDir);
        environment.insert(QStringLiteral("XDG_CONFIG_HOME"), stateDir + QStringLiteral("/.config"));
        environment.insert(QStringLiteral("XDG_CONFIG_DIRS"), stateDir + QStringLiteral("/empty-config"));
        environment.insert(QStringLiteral("XDG_DATA_HOME"), stateDir + QStringLiteral("/.local/share"));
        environment.insert(QStringLiteral("XDG_CACHE_HOME"), stateDir + QStringLiteral("/.cache"));
        environment.insert(QStringLiteral("XDG_RUNTIME_DIR"), stateDir + QStringLiteral("/runtime"));
        environment.insert(QStringLiteral("DBUS_SYSTEM_BUS_ADDRESS"), QStringLiteral("unix:path=/nonexistent/soniclogin-cursor-test-system-bus"));
        if (!connected) {
            environment.insert(QStringLiteral("DBUS_SESSION_BUS_ADDRESS"), QStringLiteral("unix:path=/nonexistent/soniclogin-cursor-test-session-bus"));
        }
        QProcess process;
        process.setProcessEnvironment(environment);
        process.setProcessChannelMode(QProcess::MergedChannels);
        const auto stop = qScopeGuard([&] {
            if (process.state() != QProcess::NotRunning) {
                process.kill();
                process.waitForFinished();
            }
        });
        process.start(QStringLiteral(CURSOR_STARTUP_HELPER), {QStringLiteral("--apply-kscreen")});
        QVERIFY(process.waitForStarted());
        if (connected) {
            QTRY_VERIFY_WITH_TIMEOUT(get(stateDir + QStringLiteral("/startup.log")).contains("Waiting for window manager readiness"), 5000);
            QTest::qWait(200);
            QCOMPARE(process.state(), QProcess::Running);
            QCOMPARE(resources(), original);
            if (appears) {
                QVERIFY(QDBusConnection::sessionBus().registerService(QStringLiteral("org.kde.KWin")));
            }
        }
        QTRY_COMPARE_WITH_TIMEOUT(process.state(), QProcess::NotRunning, appears ? 10000 : 35000);
        const auto diagnostics = process.readAll() + get(stateDir + QStringLiteral("/startup.log"));
        QVERIFY2(process.exitStatus() == QProcess::NormalExit, diagnostics.constData());
        QCOMPARE(process.exitCode(), appears ? 0 : 1);
        if (appears) {
            QVERIFY2(resources().contains("Xcursor.theme:\tReadinessCursor"), diagnostics.constData());
        } else {
            QCOMPARE(resources(), original);
            QVERIFY2(diagnostics.contains(connected ? "Timed out waiting for org.kde.KWin" : "Cannot query window manager readiness"), diagnostics.constData());
        }
        QVERIFY(!QFile::exists(stateDir + QStringLiteral("/soniclogin-greeter.env")));
        QVERIFY(!QFile::exists(stateDir + QStringLiteral("/soniclogin-wallpaper.env")));
    }
};

QTEST_GUILESS_MAIN(CursorStartupTest)

#include "CursorStartupTest.moc"
