/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "GreeterProxy.h"
#include "Messages.h"

#include <QDataStream>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

using namespace SONICLOGIN;

class GreeterProtocolTest : public QObject {
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> m_directory;
    std::unique_ptr<QLocalServer> m_server;
    std::unique_ptr<GreeterProxy> m_proxy;
    QLocalSocket *m_peer = nullptr;
    QByteArray m_oldSocket;
    bool m_hadSocket;

private Q_SLOTS:
    void initTestCase()
    {
        m_hadSocket = qEnvironmentVariableIsSet("SONICLOGIN_SOCKET");
        m_oldSocket = qgetenv("SONICLOGIN_SOCKET");
    }

    void init()
    {
        m_directory = std::make_unique<QTemporaryDir>();
        QVERIFY(m_directory->isValid());
        m_server = std::make_unique<QLocalServer>();
        const QString path = m_directory->path() + QStringLiteral("/greeter-test.sock");
        QVERIFY(m_server->listen(path));
        qputenv("SONICLOGIN_SOCKET", path.toUtf8());
        m_proxy = std::make_unique<GreeterProxy>();
        QTRY_VERIFY_WITH_TIMEOUT(m_server->hasPendingConnections(), 3000);
        m_peer = m_server->nextPendingConnection();
        QVERIFY(m_peer);
        QTRY_VERIFY_WITH_TIMEOUT(m_peer->bytesAvailable() >= 4, 3000);
        QDataStream input(m_peer);
        quint32 connectMessage;
        input >> connectMessage;
        QCOMPARE(connectMessage, quint32(GreeterMessages::Connect));
        QCOMPARE(input.status(), QDataStream::Ok);
    }

    void cleanup()
    {
        m_proxy.reset();
        m_peer = nullptr;
        m_server.reset();
        m_directory.reset();
    }

    void cleanupTestCase()
    {
        if (m_hadSocket) {
            qputenv("SONICLOGIN_SOCKET", m_oldSocket);
        } else {
            qunsetenv("SONICLOGIN_SOCKET");
        }
    }

    void stableMessageIds()
    {
        QCOMPARE(quint32(GreeterMessages::Connect), 0u);
        QCOMPARE(quint32(GreeterMessages::Login), 1u);
        QCOMPARE(quint32(GreeterMessages::PowerOff), 2u);
        QCOMPARE(quint32(GreeterMessages::Reboot), 3u);
        QCOMPARE(quint32(GreeterMessages::Suspend), 4u);
        QCOMPARE(quint32(GreeterMessages::Hibernate), 5u);
        QCOMPARE(quint32(DaemonMessages::HostName), 0u);
        QCOMPARE(quint32(DaemonMessages::LoginSucceeded), 1u);
        QCOMPARE(quint32(DaemonMessages::LoginFailed), 2u);
        QCOMPARE(quint32(DaemonMessages::InformationMessage), 3u);
        QCOMPARE(quint32(DaemonMessages::SessionCapabilities), 4u);
        QCOMPARE(quint32(SessionType::X11), 0u);
        QCOMPARE(quint32(SessionType::Wayland), 1u);
    }

    void loginPacket_data()
    {
        QTest::addColumn<int>("type");
        QTest::newRow("x11") << int(SessionType::X11);
        QTest::newRow("wayland") << int(SessionType::Wayland);
    }

    void loginPacket()
    {
        QFETCH(int, type);
        const QString user = QStringLiteral("test-user");
        const QString password = QString::fromUtf8("test-only-pässword");
        const QString filename = QStringLiteral("sonicde.desktop");
        QByteArray expected;
        QDataStream output(&expected, QIODevice::WriteOnly);
        output << quint32(GreeterMessages::Login) << user << password << quint32(type) << filename;
        m_proxy->login(user, password, SessionType(type), filename);
        QTRY_VERIFY_WITH_TIMEOUT(m_peer->bytesAvailable() >= expected.size(), 3000);
        QCOMPARE(m_peer->readAll(), expected);
    }

    void powerPacket_data()
    {
        QTest::addColumn<int>("message");
        QTest::newRow("shutdown") << int(GreeterMessages::PowerOff);
        QTest::newRow("reboot") << int(GreeterMessages::Reboot);
        QTest::newRow("suspend") << int(GreeterMessages::Suspend);
        QTest::newRow("hibernate") << int(GreeterMessages::Hibernate);
    }

    void powerPacket()
    {
        QFETCH(int, message);
        switch (GreeterMessages(message)) {
        case GreeterMessages::PowerOff:
            m_proxy->shutdown();
            break;
        case GreeterMessages::Reboot:
            m_proxy->reboot();
            break;
        case GreeterMessages::Suspend:
            m_proxy->suspend();
            break;
        case GreeterMessages::Hibernate:
            m_proxy->hibernate();
            break;
        default:
            QFAIL("Unexpected test power message");
        }
        QTRY_VERIFY_WITH_TIMEOUT(m_peer->bytesAvailable() >= 4, 3000);
        QDataStream input(m_peer);
        quint32 actual;
        input >> actual;
        QCOMPARE(actual, quint32(message));
        QCOMPARE(input.status(), QDataStream::Ok);
    }

    void daemonRepliesAndCapabilities()
    {
        QSignalSpy succeeded(m_proxy.get(), &GreeterProxy::loginSucceeded);
        QSignalSpy failed(m_proxy.get(), &GreeterProxy::loginFailed);
        QSignalSpy information(m_proxy.get(), &GreeterProxy::informationMessage);
        QSignalSpy capabilities(m_proxy.get(), &GreeterProxy::capabilitiesChanged);
        const QString text = QString::fromUtf8("test information: Grüße");
        const quint32 caps = PowerOff | Reboot | Suspend | Hibernate;
        QByteArray packet;
        QDataStream output(&packet, QIODevice::WriteOnly);
        output << quint32(DaemonMessages::HostName) << QStringLiteral("test-host") << quint32(DaemonMessages::SessionCapabilities) << caps
               << quint32(DaemonMessages::InformationMessage) << text << quint32(DaemonMessages::LoginSucceeded) << quint32(DaemonMessages::LoginFailed);
        QCOMPARE(m_peer->write(packet), qint64(packet.size()));
        m_peer->flush();
        QTRY_COMPARE_WITH_TIMEOUT(succeeded.count(), 1, 3000);
        QCOMPARE(failed.count(), 1);
        QCOMPARE(information.count(), 1);
        QCOMPARE(information.at(0).at(0).toString(), text);
        QCOMPARE(capabilities.count(), 1);
        QVERIFY(m_proxy->canShutdown());
        QVERIFY(m_proxy->canReboot());
        QVERIFY(m_proxy->canSuspend());
        QVERIFY(m_proxy->canHibernate());
        QVERIFY(m_proxy->canLogout());
    }
};

QTEST_GUILESS_MAIN(GreeterProtocolTest)

#include "GreeterProtocolTest.moc"
