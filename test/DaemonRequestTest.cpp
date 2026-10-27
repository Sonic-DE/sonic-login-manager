/*
 * SPDX-FileCopyrightText: 2026 SonicDE Community
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "daemonrequest.h"
#include "Messages.h"

#include <Sonic/Ipc/Frame>
#include <Sonic/Ipc/FrameReader>

#include <QDataStream>
#include <QFutureWatcher>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QtConcurrentRun>

#include <memory>

class DaemonRequestTest : public QObject {
    Q_OBJECT
private:
    // Today's reply bytes, framed as the daemon sends them.
    static QByteArray reply(bool success = true, const QString &error = {}, quint32 id = 1)
    {
        QByteArray bytes;
        QDataStream out(&bytes, QIODevice::WriteOnly);
        out.setVersion(QDataStream::Qt_6_0);
        out << quint32(100) << id << quint8(success) << error;
        return Sonic::Ipc::encodeFrame(100, bytes, SONICLOGIN::kcmProtocolVersion);
    }

private Q_SLOTS:
    void framedProtocolAndResponsiveEventLoop_data()
    {
        QTest::addColumn<QString>("operation");
        QTest::newRow("sync-v4") << QStringLiteral("sync");
        QTest::newRow("reset-v2") << QStringLiteral("reset");
        QTest::newRow("save-v3") << QStringLiteral("save");
    }

    void framedProtocolAndResponsiveEventLoop()
    {
        QFETCH(QString, operation);
        QTemporaryDir temporary;
        QLocalServer server;
        QVERIFY(server.listen(temporary.path() + "/ipc"));
        const QByteArray asset(2 * 1024 * 1024, 'x');
        const QJsonObject arguments = {
            {QStringLiteral("files"), QJsonObject{{QStringLiteral("kdeglobals"), QStringLiteral("[Icons]\nTheme=PrivateIcons\n")}}},
            {QStringLiteral("themes"), QJsonObject{{QStringLiteral("icons/PrivateIcons"), QString::fromLatin1(asset.toBase64())}}},
            {QStringLiteral("kscreen"), QJsonObject{{QStringLiteral("outputs/test"), QString::fromLatin1(QByteArray("screen").toBase64())}}},
            {QStringLiteral("config"), QStringLiteral("[Theme]\nWallpaper=PrivateWallpaper\n")},
            {QStringLiteral("wallpapers"), QJsonObject{{QStringLiteral("/test/wallpaper"), QString::fromLatin1(asset.toBase64())}}},
        };
        bool received = false;
        connect(&server, &QLocalServer::newConnection, this, [&]() {
            auto *peer = server.nextPendingConnection();
            connect(peer, &QLocalSocket::disconnected, peer, &QObject::deleteLater);
            auto reader = std::make_shared<Sonic::Ipc::FrameReader>(SONICLOGIN::kcmProtocolVersion, 512 * 1024 * 1024);
            auto decode = [&, peer, reader]() {
                if (received) {
                    return;
                }
                reader->feed(peer->readAll());
                const std::optional<Sonic::Ipc::Frame> frame = reader->next();
                if (!frame) {
                    QCOMPARE(reader->error(), Sonic::Ipc::FrameReader::Error::None);
                    return;
                }
                QDataStream in(frame->payload);
                in.setVersion(QDataStream::Qt_6_0);
                in.startTransaction();
                QString username, password, config;
                quint32 type = 0, id = 0;
                in >> username >> password >> type >> id;
                auto stringMap = [&]() {
                    QMap<QString, QString> values;
                    quint32 count = 0;
                    in >> count;
                    for (quint32 i = 0; i < count && in.status() == QDataStream::Ok; ++i) {
                        QString key, value;
                        in >> key >> value;
                        values.insert(key, value);
                    }
                    return values;
                };
                auto binaryMap = [&]() {
                    QMap<QString, QByteArray> values;
                    quint32 count = 0;
                    in >> count;
                    for (quint32 i = 0; i < count && in.status() == QDataStream::Ok; ++i) {
                        QString key;
                        QByteArray value;
                        in >> key >> value;
                        values.insert(key, value);
                    }
                    return values;
                };
                QMap<QString, QString> files;
                QMap<QString, QByteArray> themes, screens, wallpapers;
                if (type == 4) {
                    files = stringMap();
                    themes = binaryMap();
                    screens = binaryMap();
                } else if (type == 3) {
                    in >> config;
                    wallpapers = binaryMap();
                }
                if (!in.commitTransaction()) {
                    return;
                }
                received = true;
                QCOMPARE(frame->type, type);
                QCOMPARE(username, QStringLiteral("test-user"));
                QCOMPARE(password, QStringLiteral("test-password"));
                QCOMPARE(id, quint32(1));
                QCOMPARE(type, operation == QLatin1String("sync") ? quint32(4) : operation == QLatin1String("save") ? quint32(3) : quint32(2));
                if (type == 4) {
                    QCOMPARE(files.value(QStringLiteral("kdeglobals")), QStringLiteral("[Icons]\nTheme=PrivateIcons\n"));
                    QCOMPARE(themes.value(QStringLiteral("icons/PrivateIcons")), asset);
                    QCOMPARE(screens.value(QStringLiteral("outputs/test")), QByteArray("screen"));
                } else if (type == 3) {
                    QCOMPARE(config, arguments.value(QStringLiteral("config")).toString());
                    QCOMPARE(wallpapers.value(QStringLiteral("/test/wallpaper")), asset);
                }
                const auto response = reply();
                // Delay and split the reply while the GUI/test thread keeps
                // processing timers; never run the blocking helper on it.
                QTimer::singleShot(75, peer, [peer, response]() {
                    peer->write(response.first(5));
                });
                QTimer::singleShot(150, peer, [peer, response]() {
                    peer->write(response.mid(5));
                });
            };
            connect(peer, &QLocalSocket::readyRead, peer, decode);
            if (peer->bytesAvailable()) {
                decode();
            }
        });
        int ticks = 0;
        QTimer heartbeat;
        heartbeat.setInterval(10);
        connect(&heartbeat, &QTimer::timeout, this, [&]() {
            ++ticks;
        });
        heartbeat.start();
        QFutureWatcher<SONICLOGIN::DaemonReply> watcher;
        const QString name = server.fullServerName();
        watcher.setFuture(QtConcurrent::run([=]() {
            return SONICLOGIN::requestDaemon(operation, arguments, QStringLiteral("test-user"), QStringLiteral("test-password"), name, 3000);
        }));
        QTRY_VERIFY_WITH_TIMEOUT(watcher.isFinished(), 5000);
        const auto result = watcher.result();
        QVERIFY2(result.success, qPrintable(result.error));
        QVERIFY(!result.rejected);
        QVERIFY(received);
        QVERIFY(ticks >= 2);
    }

    void failure_data()
    {
        QTest::addColumn<QString>("kind");
        for (const auto &kind : {"missing-server", "timeout", "disconnect", "wrong-id", "rejected", "truncated"}) {
            QTest::newRow(kind) << QString::fromLatin1(kind);
        }
    }

    void failure()
    {
        QFETCH(QString, kind);
        QTemporaryDir temporary;
        QLocalServer server;
        const QString name = temporary.path() + "/ipc";
        if (kind != QLatin1String("missing-server")) {
            QVERIFY(server.listen(name));
        }
        connect(&server, &QLocalServer::newConnection, this, [&]() {
            auto *peer = server.nextPendingConnection();
            connect(peer, &QLocalSocket::disconnected, peer, &QObject::deleteLater);
            if (kind == QLatin1String("disconnect")) {
                peer->disconnectFromServer();
            } else if (kind == QLatin1String("wrong-id")) {
                peer->write(reply(true, {}, 2));
            } else if (kind == QLatin1String("rejected")) {
                peer->write(reply(false, QStringLiteral("auth: test rejection")));
            } else if (kind == QLatin1String("truncated")) {
                peer->write(reply().first(5));
                peer->disconnectFromServer();
            }
        });
        QFutureWatcher<SONICLOGIN::DaemonReply> watcher;
        watcher.setFuture(QtConcurrent::run([name]() {
            return SONICLOGIN::requestDaemon(QStringLiteral("reset"), {}, QStringLiteral("test-user"), QStringLiteral("test-password"), name, 150);
        }));
        QTRY_VERIFY_WITH_TIMEOUT(watcher.isFinished(), 3000);
        const auto result = watcher.result();
        QVERIFY(!result.success);
        QVERIFY(!result.error.isEmpty());
        QCOMPARE(result.rejected, kind == QLatin1String("rejected"));
    }
};

QTEST_GUILESS_MAIN(DaemonRequestTest)
#include "DaemonRequestTest.moc"
