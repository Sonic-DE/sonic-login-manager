/*
 * SPDX-FileCopyrightText: 2026 SonicDE Community
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "wallpaperapp.h"
#include "wallpaperwindow.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QProcess>
#include <QProcessEnvironment>
#include <QQmlEngine>
#include <QQmlExpression>
#include <QQuickItem>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

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

QByteArray picture()
{
    QImage image(64, 64, QImage::Format_RGB32);
    image.fill(QColor(28, 140, 220));
    QPainter painter(&image);
    painter.fillRect(0, 0, 32, 64, QColor(224, 70, 50));
    painter.end();
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return png;
}

QByteArray pluginQml(quint16 port)
{
    return get(QStringLiteral(WALLPAPER_TEST_DATA "/plugin.qml.in")).replace("@PORT@", QByteArray::number(port));
}

bool prepare(const QString &home, const QString &scenario)
{
    const auto png = picture();
    if (png.isEmpty() || !put(home + "/picture.png", png) || !put(home + "/broken.png", "not an image")) {
        return false;
    }
    const QString image = QUrl::fromLocalFile(home + "/picture.png").toString();
    for (const auto &id : {QStringLiteral("org.kde.image"), QStringLiteral("org.kde.potd"), QStringLiteral("org.kde.color")}) {
        if (scenario == QLatin1String("missingPotd") && id == QLatin1String("org.kde.potd")) {
            continue;
        }
        const QString package = home + "/data/plasma/wallpapers/" + id;
        const QJsonObject metadata{{"KPackageStructure", "Plasma/Wallpaper"}, {"KPlugin", QJsonObject{{"Id", id}, {"Name", id}}}};
        const QByteArray schema = get(QStringLiteral(WALLPAPER_TEST_DATA "/main.xml.in"))
                                      .replace("@IMAGE@", scenario == QLatin1String("missingPotd") ? image.toUtf8() : QByteArray());
        const QByteArray qml = id == QLatin1String("org.kde.color")
            ? QByteArray("import QtQuick\nRectangle { required property var configuration; required property string pluginName; color: configuration.Color }\n")
            : pluginQml(0);
        if (!put(package + "/metadata.json", QJsonDocument(metadata).toJson()) || !put(package + "/contents/config/main.xml", schema)
            || !put(package + "/contents/ui/main.qml", qml)) {
            return false;
        }
    }
    QByteArray config;
    if (scenario == QLatin1String("savedImage") || scenario == QLatin1String("brokenImage")) {
        const QString file = scenario == QLatin1String("brokenImage") ? home + "/broken.png" : home + "/picture.png";
        config = QStringLiteral("[Greeter][Wallpaper][org.kde.image][General]\nImage=%1\n").arg(QUrl::fromLocalFile(file).toString()).toUtf8();
    } else if (scenario.startsWith(QLatin1String("saved"))) {
        const QString provider = scenario.mid(5).toLower();
        config = QStringLiteral("[Greeter]\nWallpaperPluginId=org.kde.potd\n[Greeter][Wallpaper][org.kde.potd][General]\nProvider=%1\n").arg(provider).toUtf8();
    } else if (scenario == QLatin1String("configuredPotdSchemaDefault")) {
        config = "[Greeter]\nWallpaperPluginId=org.kde.potd\n[Greeter][Wallpaper][org.kde.potd][General]\nSavedSetting=true\n";
    } else if (scenario == QLatin1String("configuredColor")) {
        config = "[Greeter]\nWallpaperPluginId=org.kde.color\n[Greeter][Wallpaper][org.kde.color][General]\nColor=18,52,86\n";
    } else if (scenario == QLatin1String("invalidPlugin")) {
        config = "[Greeter]\nWallpaperPluginId=org.example.missing\n[Greeter][Wallpaper][org.example.missing][General]\nSavedSetting=true\n";
    }
    return put(home + "/config/wallpaper-test.conf", config);
}
}

class WallpaperScenario : public QObject {
    Q_OBJECT

public:
    WallpaperApp *app = nullptr;
    QString home;
    QString scenario;

private Q_SLOTS:
    void loadsWallpaper()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        QStringList requests;
        connect(&server, &QTcpServer::newConnection, &server, [&] {
            while (auto *socket = server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                    QByteArray request = socket->property("request").toByteArray() + socket->readAll();
                    socket->setProperty("request", request);
                    if (!request.contains("\r\n\r\n") || socket->property("answered").toBool()) {
                        return;
                    }
                    socket->setProperty("answered", true);
                    requests << QString::fromLatin1(request.split(' ').value(1));
                    const auto png = picture();
                    socket->write("HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nConnection: close\r\nContent-Length: " + QByteArray::number(png.size())
                                  + "\r\n\r\n" + png);
                    socket->disconnectFromHost();
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
        const QString potd = home + "/data/plasma/wallpapers/org.kde.potd/contents/ui/main.qml";
        if (QFile::exists(potd)) {
            QVERIFY(put(potd, pluginQml(server.serverPort())));
        }
        const auto originalConfig = get(home + "/config/wallpaper-test.conf");
        app->start(true);
        WallpaperWindow *window = nullptr;
        for (auto *candidate : QGuiApplication::topLevelWindows()) {
            if (auto *wallpaper = qobject_cast<WallpaperWindow *>(candidate)) {
                window = wallpaper;
                break;
            }
        }
        QVERIFY(window);
        QVERIFY(window->rootObject());
        // The wallpaper must classify the selected Window palette, not the
        // independent Complementary palette (which may have opposite tones).
        QQmlExpression usesWindow(qmlContext(window->rootObject()), window->rootObject(), QStringLiteral("Kirigami.Theme.colorSet === Kirigami.Theme.Window"));
        const auto windowPalette = usesWindow.evaluate();
        QVERIFY2(!usesWindow.hasError(), qPrintable(usesWindow.error().toString()));
        QVERIFY(windowPalette.toBool());
        auto *container = window->rootObject()->property("wallpaperContainer").value<QQuickItem *>();
        QVERIFY(container);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        if (scenario == QLatin1String("invalidPlugin")) {
            QCOMPARE(container->childItems().size(), 0);
        } else {
            QTRY_COMPARE_WITH_TIMEOUT(container->childItems().size(), 1, 5000);
            auto *item = container->childItems().first();
            const bool potdExpected = scenario == QLatin1String("unconfiguredBing")
                || (scenario.startsWith(QLatin1String("saved")) && scenario != QLatin1String("savedImage"))
                || scenario == QLatin1String("configuredPotdSchemaDefault");
            const QString expectedPlugin = scenario == QLatin1String("configuredColor") ? QStringLiteral("org.kde.color")
                : potdExpected                                                          ? QStringLiteral("org.kde.potd")
                                                                                        : QStringLiteral("org.kde.image");
            QCOMPARE(item->property("pluginName").toString(), expectedPlugin);
            QTRY_COMPARE(item->size(), container->size());
            if (potdExpected) {
                const QString provider = scenario == QLatin1String("unconfiguredBing") ? QStringLiteral("bing")
                    : scenario == QLatin1String("configuredPotdSchemaDefault")         ? QStringLiteral("apod")
                                                                                       : scenario.mid(5).toLower();
                QCOMPARE(item->property("providerAtCreation").toString(), provider);
                QTRY_COMPARE(item->property("imageStatus").toInt(), 1); // Image.Ready
                QCOMPARE(requests, QStringList{QLatin1Char('/') + provider + QStringLiteral(".png")});
            } else if (scenario != QLatin1String("configuredColor")) {
                QTRY_COMPARE(item->property("imageStatus").toInt(), scenario == QLatin1String("brokenImage") ? 3 : 1);
                QVERIFY(requests.isEmpty());
            }
            window->resize(480, 320);
            QTRY_COMPARE(item->size(), QSizeF(480, 320));
        }
        QImage rendered;
        QTRY_VERIFY_WITH_TIMEOUT(!(rendered = window->grabWindow()).isNull(), 5000);
        const bool black = scenario == QLatin1String("invalidPlugin") || scenario == QLatin1String("brokenImage");
        if (black || scenario == QLatin1String("configuredColor")) {
            const QColor expected = black ? QColor(Qt::black) : QColor(18, 52, 86);
            QTRY_COMPARE(window->grabWindow().pixelColor(window->width() / 2, window->height() / 2), expected);
        } else {
            QTRY_COMPARE(window->grabWindow().pixelColor(window->width() / 4, window->height() / 2), QColor(224, 70, 50));
            QTRY_COMPARE(window->grabWindow().pixelColor(3 * window->width() / 4, window->height() / 2), QColor(28, 140, 220));
        }
        // The automatic Bing choice must never overwrite saved configuration.
        QCOMPARE(get(home + "/config/wallpaper-test.conf"), originalConfig);
    }
};

class WallpaperAppTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void defaultsAndLoading_data()
    {
        QTest::addColumn<QString>("scenario");
        for (const auto &scenario : {"unconfiguredBing",
                                     "savedApod",
                                     "savedBing",
                                     "savedCustom",
                                     "configuredPotdSchemaDefault",
                                     "savedImage",
                                     "configuredColor",
                                     "missingPotd",
                                     "invalidPlugin",
                                     "brokenImage"}) {
            QTest::newRow(scenario) << QString::fromLatin1(scenario);
        }
    }

    void defaultsAndLoading()
    {
        QFETCH(QString, scenario);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        QProcess process;
        process.setProcessChannelMode(QProcess::MergedChannels);
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("WALLPAPER_TEST_HOME"), fixture.path());
        process.setProcessEnvironment(environment);
        process.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--scenario"), scenario});
        QVERIFY(process.waitForStarted());
        if (!process.waitForFinished(30000)) {
            process.kill();
            process.waitForFinished();
            QFAIL(qPrintable(QStringLiteral("Wallpaper scenario timed out: %1\n%2").arg(scenario, QString::fromLocal8Bit(process.readAll()))));
        }
        const auto output = process.readAll() + get(fixture.path() + "/wallpaper-test.log");
        QVERIFY2(
            process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0,
            qPrintable(QStringLiteral("Exit %1, status %2:\n%3").arg(process.exitCode()).arg(int(process.exitStatus())).arg(QString::fromLocal8Bit(output))));
    }
};

int main(int argc, char **argv)
{
    if (argc >= 3 && QByteArray(argv[1]) == "--scenario") {
        const QString home = qEnvironmentVariable("WALLPAPER_TEST_HOME");
        if (home.isEmpty() || !QFileInfo(home).isDir()) {
            return 1;
        }
        if (!QDir::setCurrent(home)) {
            return 1;
        }
        const QString scenario = QString::fromLocal8Bit(argv[2]);
        qputenv("HOME", home.toUtf8());
        qputenv("XDG_CONFIG_HOME", (home + "/config").toUtf8());
        qputenv("XDG_CONFIG_DIRS", (home + "/empty-config").toUtf8());
        qputenv("XDG_DATA_HOME", (home + "/data").toUtf8());
        qputenv("XDG_DATA_DIRS", (home + "/empty-data").toUtf8());
        qputenv("XDG_CACHE_HOME", (home + "/cache").toUtf8());
        for (const auto *name : {"http_proxy", "https_proxy", "HTTP_PROXY", "HTTPS_PROXY", "all_proxy", "ALL_PROXY"}) {
            qputenv(name, "");
        }
        qputenv("NO_PROXY", "127.0.0.1,localhost");
        qputenv("no_proxy", "127.0.0.1,localhost");
        if (!prepare(home, scenario)) {
            return 1;
        }
        WallpaperApp app(argc, argv);
        qInstallMessageHandler(nullptr);
        WallpaperScenario test;
        test.app = &app;
        test.home = home;
        test.scenario = scenario;
        return QTest::qExec(&test, QStringList{QString::fromLocal8Bit(argv[0])});
    }
    QCoreApplication app(argc, argv);
    WallpaperAppTest tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "WallpaperAppTest.moc"
