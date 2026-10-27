/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "GreeterProxy.h"
#include "Messages.h"

#include <Sonic/Ipc/Frame>
#include <Sonic/Ipc/FrameReader>

#include <QDataStream>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>
#include <optional>

using namespace SONICLOGIN;

class GreeterProtocolTest : public QObject {
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> m_directory;
    std::unique_ptr<QLocalServer> m_server;
    std::unique_ptr<GreeterProxy> m_proxy;
    std::unique_ptr<Sonic::Ipc::FrameReader> m_reader;
    QLocalSocket *m_peer = nullptr;
    QByteArray m_oldSocket;
    bool m_hadSocket;

    // The next frame the proxy wrote, decoded as the daemon does.
    std::optional<Sonic::Ipc::Frame> nextFrame()
    {
        std::optional<Sonic::Ipc::Frame> frame = m_reader->next();
        QElapsedTimer timer;
        timer.start();
        while (!frame && m_reader->error() == Sonic::Ipc::FrameReader::Error::None && timer.elapsed() < 3000) {
            QTest::qWait(10);
            m_reader->feed(m_peer->readAll());
            frame = m_reader->next();
        }
        return frame;
    }

    // Today's message bytes, framed as the daemon sends them: one message per frame.
    template<typename... Fields>
    static QByteArray daemonFrame(DaemonMessages message, const Fields &...fields)
    {
        QByteArray payload;
        QDataStream output(&payload, QIODevice::WriteOnly);
        output << quint32(message);
        ((output << fields), ...);
        return Sonic::Ipc::encodeFrame(quint32(message), payload, greeterProtocolVersion);
    }

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
        m_reader = std::make_unique<Sonic::Ipc::FrameReader>(greeterProtocolVersion);
        const std::optional<Sonic::Ipc::Frame> frame = nextFrame();
        QVERIFY(frame);
        QCOMPARE(frame->type, quint32(GreeterMessages::Connect));
        QDataStream input(frame->payload);
        quint32 connectMessage;
        input >> connectMessage;
        QCOMPARE(connectMessage, quint32(GreeterMessages::Connect));
        QCOMPARE(input.status(), QDataStream::Ok);
    }

    void cleanup()
    {
        m_proxy.reset();
        m_reader.reset();
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
        const std::optional<Sonic::Ipc::Frame> frame = nextFrame();
        QVERIFY(frame);
        QCOMPARE(frame->type, quint32(GreeterMessages::Login));
        QCOMPARE(frame->payload, expected);
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
        const std::optional<Sonic::Ipc::Frame> frame = nextFrame();
        QVERIFY(frame);
        QCOMPARE(frame->type, quint32(message));
        QDataStream input(frame->payload);
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
        packet += daemonFrame(DaemonMessages::HostName, QStringLiteral("test-host"));
        packet += daemonFrame(DaemonMessages::SessionCapabilities, caps);
        packet += daemonFrame(DaemonMessages::InformationMessage, text);
        packet += daemonFrame(DaemonMessages::LoginSucceeded);
        packet += daemonFrame(DaemonMessages::LoginFailed);
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
