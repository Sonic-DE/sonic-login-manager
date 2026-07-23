/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "../src/frontend/settings/models/sessionmodel.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class SessionModelTest : public QObject {
    Q_OBJECT

    QByteArray m_dataHome;
    QByteArray m_dataDirs;
    bool m_hadDataHome;
    bool m_hadDataDirs;

    void useDirectory(const QTemporaryDir &directory)
    {
        QVERIFY(directory.isValid());
        qputenv("XDG_DATA_HOME", directory.path().toUtf8());
        qputenv("XDG_DATA_DIRS", (directory.path() + QStringLiteral("/empty")).toUtf8());
    }

    void addSession(const QTemporaryDir &directory, const QString &folder, const QString &fileName, const QString &name)
    {
        const QString path = directory.path() + QLatin1Char('/') + folder;
        QVERIFY(QDir().mkpath(path));
        QFile file(path + QLatin1Char('/') + fileName);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write((QStringLiteral("[Desktop Entry]\nName=%1\nExec=test-session\nType=Application\n").arg(name)).toUtf8());
    }

private Q_SLOTS:
    void initTestCase()
    {
        m_hadDataHome = qEnvironmentVariableIsSet("XDG_DATA_HOME");
        m_hadDataDirs = qEnvironmentVariableIsSet("XDG_DATA_DIRS");
        m_dataHome = qgetenv("XDG_DATA_HOME");
        m_dataDirs = qgetenv("XDG_DATA_DIRS");
    }

    void cleanupTestCase()
    {
        if (m_hadDataHome) {
            qputenv("XDG_DATA_HOME", m_dataHome);
        } else {
            qunsetenv("XDG_DATA_HOME");
        }
        if (m_hadDataDirs) {
            qputenv("XDG_DATA_DIRS", m_dataDirs);
        } else {
            qunsetenv("XDG_DATA_DIRS");
        }
    }

    void preferredOrderAndExplicitLookup()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        useDirectory(directory);
        addSession(directory, "xsessions", "sonicde.desktop", "SonicDE");
        addSession(directory, "xsessions", "plasma.desktop", "Plasma X11");
        addSession(directory, "xsessions", "aero.desktop", "Aero");
        addSession(directory, "xsessions", "same-b.desktop", "Same");
        addSession(directory, "xsessions", "same-a.desktop", "Same");
        addSession(directory, "wayland-sessions", "plasma.desktop", "Plasma Wayland");
        addSession(directory, "wayland-sessions", "sonicde.desktop", "SonicDE Wayland");
        addSession(directory, "wayland-sessions", "aaa.desktop", "A Wayland");
        SessionModel model;
        QCOMPARE(model.rowCount(), 8);
        const QStringList expected =
            {"sonicde.desktop", "plasma.desktop", "aero.desktop", "plasma.desktop", "same-a.desktop", "same-b.desktop", "aaa.desktop", "sonicde.desktop"};
        for (int row = 0; row < expected.size(); ++row) {
            QCOMPARE(model.data(model.index(row, 0), SessionModel::FileNameRole).toString(), expected[row]);
        }
        QCOMPARE(model.data(model.index(0, 0), SessionModel::TypeRole).toInt(), int(Session::X11));
        QCOMPARE(model.data(model.index(1, 0), SessionModel::TypeRole).toInt(), int(Session::Wayland));
        QCOMPARE(model.data(model.index(3, 0), SessionModel::TypeRole).toInt(), int(Session::X11));
        QCOMPARE(model.indexOfData("aero.desktop", SessionModel::FileNameRole), 2);
        QCOMPARE(model.indexOfData("aaa.desktop", SessionModel::FileNameRole), 6);
    }

    void plasmaWaylandFallback()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        useDirectory(directory);
        addSession(directory, "xsessions", "aero.desktop", "Aero");
        addSession(directory, "wayland-sessions", "plasma.desktop", "Plasma");
        addSession(directory, "wayland-sessions", "sonicde.desktop", "SonicDE");
        SessionModel model;
        QCOMPARE(model.rowCount(), 3);
        QCOMPARE(model.data(model.index(0, 0), SessionModel::FileNameRole).toString(), QStringLiteral("plasma.desktop"));
        QCOMPARE(model.data(model.index(0, 0), SessionModel::TypeRole).toInt(), int(Session::Wayland));
        QCOMPARE(model.data(model.index(1, 0), SessionModel::TypeRole).toInt(), int(Session::X11));
    }

    void otherX11Fallback()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        useDirectory(directory);
        addSession(directory, "xsessions", "z.desktop", "Z X11");
        addSession(directory, "wayland-sessions", "a.desktop", "A Wayland");
        SessionModel model;
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.data(model.index(0, 0), SessionModel::TypeRole).toInt(), int(Session::X11));
    }
};

QTEST_GUILESS_MAIN(SessionModelTest)

#include "SessionModelTest.moc"
