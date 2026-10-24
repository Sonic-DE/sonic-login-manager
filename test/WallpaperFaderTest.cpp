/*
 * SPDX-FileCopyrightText: 2026 SonicDE Community
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <QGuiApplication>
#include <QImage>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickView>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <cmath>

class WallpaperFaderTest : public QObject {
    Q_OBJECT

    static double edgeEnergy(const QImage &image, const QRect &area)
    {
        double sum = 0;
        qsizetype count = 0;
        for (int y = area.top(); y <= area.bottom(); ++y) {
            for (int x = area.left(); x <= area.right(); ++x) {
                const int pixel = qGray(image.pixel(x, y));
                if (x < area.right()) {
                    sum += std::abs(pixel - qGray(image.pixel(x + 1, y)));
                    ++count;
                }
                if (y < area.bottom()) {
                    sum += std::abs(pixel - qGray(image.pixel(x, y + 1)));
                    ++count;
                }
            }
        }
        return count ? sum / count : 0;
    }

private Q_SLOTS:
    void tintFollowsTheme_data()
    {
        QTest::addColumn<QColor>("background");
        QTest::addColumn<QColor>("sourceColor");
        const QColor neutral(128, 128, 128);
        QTest::newRow("dark-silver") << QColor("#2a2e32") << neutral;
        QTest::newRow("light-silver") << QColor("#eff0f1") << neutral;
        QTest::newRow("dark-saturated-blue") << QColor("#2020c0") << neutral;
        QTest::newRow("warm-theme") << QColor("#c06020") << neutral;
        QTest::newRow("translucent-theme") << QColor(32, 64, 192, 128) << neutral;
        QTest::newRow("colored-wallpaper") << QColor("#2a2e32") << QColor(64, 160, 208);
    }

    void tintFollowsTheme()
    {
        if (qgetenv("SONICLOGIN_BLUR_TEST_ISOLATED") != "1") {
            QSKIP("Rendered blur test requires the private Xvfb CTest wrapper");
        }
        QFETCH(QColor, background);
        QFETCH(QColor, sourceColor);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        QImage solid(320, 240, QImage::Format_RGB32);
        solid.fill(sourceColor);
        const auto path = fixture.path() + QStringLiteral("/neutral.png");
        QVERIFY(solid.save(path));
        QQuickView view;
        view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.resize(solid.size());
        view.setInitialProperties({{QStringLiteral("testImage"), QUrl::fromLocalFile(path)}, {QStringLiteral("themeBackground"), background}});
        view.setSource(QUrl::fromLocalFile(QStringLiteral(WALLPAPER_FADER_TEST_QML)));
        QVERIFY2(view.status() == QQuickView::Ready, qPrintable(view.errors().isEmpty() ? QString() : view.errors().first().toString()));
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QTRY_COMPARE(view.rootObject()->property("imageStatus").toInt(), 1);
        auto *fader = view.rootObject()->property("fader").value<QQuickItem *>();
        QVERIFY(fader);
        const auto center = [&view]() {
            const auto image = view.grabWindow();
            return image.isNull() ? QColor() : image.pixelColor(image.width() / 2, image.height() / 2);
        };
        const auto matchesTint = [&center, sourceColor](const QColor &tint, double factor) {
            const auto actual = center();
            if (!actual.isValid()) {
                return false;
            }
            const double amount = 0.35 * factor * tint.alphaF();
            const auto expected = [amount](int source, int channel) {
                return qRound(source * (1 - amount) + channel * amount);
            };
            return std::abs(actual.red() - expected(sourceColor.red(), tint.red())) <= 2
                && std::abs(actual.green() - expected(sourceColor.green(), tint.green())) <= 2
                && std::abs(actual.blue() - expected(sourceColor.blue(), tint.blue())) <= 2 && actual.alpha() == 255;
        };
        // No tint is applied while the wallpaper is unblurred.
        QTRY_COMPARE(center(), sourceColor);
        QVERIFY(view.rootObject()->setProperty("blurred", true));
        QTRY_COMPARE_WITH_TIMEOUT(fader->property("factor").toDouble(), 1.0, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(matchesTint(background, 1), 5000);

        // Tint strength follows the blur transition, not a light/dark threshold.
        QVERIFY(view.rootObject()->setProperty("blurAmount", 0.5));
        QTRY_COMPARE_WITH_TIMEOUT(fader->property("factor").toDouble(), 0.5, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(matchesTint(background, 0.5), 5000);

        // Theme changes must update the tint without re-creating the fader.
        const QColor alternate("#883355");
        QVERIFY(view.rootObject()->setProperty("themeBackground", alternate));
        QTRY_VERIFY_WITH_TIMEOUT(matchesTint(alternate, 0.5), 5000);
        QVERIFY(view.rootObject()->setProperty("blurred", false));
        QTRY_COMPARE_WITH_TIMEOUT(fader->property("factor").toDouble(), 0.0, 5000);
        QTRY_COMPARE(center(), sourceColor);
    }

    void blurCoversEveryEdge_data()
    {
        QTest::addColumn<QSize>("size");
        QTest::addColumn<bool>("layered");
        QTest::newRow("layered-1920x1080") << QSize(1920, 1080) << true;
        QTest::newRow("layered-non-power-of-two") << QSize(901, 507) << true;
        QTest::newRow("texture-provider-1920x1080") << QSize(1920, 1080) << false;
    }

    void blurCoversEveryEdge()
    {
        if (qgetenv("SONICLOGIN_BLUR_TEST_ISOLATED") != "1") {
            QSKIP("Rendered blur test requires the private Xvfb CTest wrapper");
        }
        QFETCH(QSize, size);
        QFETCH(bool, layered);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        QImage checkerboard(size, QImage::Format_RGB32);
        for (int y = 0; y < size.height(); ++y) {
            for (int x = 0; x < size.width(); ++x) {
                checkerboard.setPixel(x, y, ((x / 4 + y / 4) % 2) ? qRgb(240, 240, 240) : qRgb(16, 16, 16));
            }
        }
        const QString path = fixture.path() + QStringLiteral("/checkerboard.png");
        QVERIFY(checkerboard.save(path));
        QQuickView view;
        view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.resize(size);
        view.setInitialProperties({{QStringLiteral("testImage"), QUrl::fromLocalFile(path)}, {QStringLiteral("sourceLayered"), layered}});
        view.setSource(QUrl::fromLocalFile(QStringLiteral(WALLPAPER_FADER_TEST_QML)));
        QVERIFY2(view.status() == QQuickView::Ready, qPrintable(view.errors().isEmpty() ? QString() : view.errors().first().toString()));
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QTRY_COMPARE(view.rootObject()->property("imageStatus").toInt(), 1); // Image.Ready
        auto *fader = view.rootObject()->property("fader").value<QQuickItem *>();
        QVERIFY(fader);
        QTRY_COMPARE(fader->property("factor").toDouble(), 0.0);
        QImage clear;
        QTRY_VERIFY(!(clear = view.grabWindow()).isNull());
        QCOMPARE(clear.size(), size);
        const QList<QRect> regions = {
            QRect(0, 0, size.width(), 24),
            QRect(0, size.height() - 24, size.width(), 24),
            QRect(0, 0, 24, size.height()),
            QRect(size.width() - 24, 0, 24, size.height()),
            QRect(size.width() / 2 - 40, size.height() / 2 - 40, 80, 80),
        };
        for (const auto &region : regions) {
            QVERIFY2(edgeEnergy(clear, region) > 30, "Clear reference must contain sharp checkerboard detail");
        }
        QVERIFY(view.rootObject()->setProperty("blurred", true));
        QTRY_COMPARE_WITH_TIMEOUT(fader->property("factor").toDouble(), 1.0, 5000);
        for (qsizetype i = 0; i < regions.size(); ++i) {
            const double maximum = edgeEnergy(clear, regions[i]) * 0.25;
            QTRY_VERIFY2_WITH_TIMEOUT(edgeEnergy(view.grabWindow(), regions[i]) < maximum,
                                      qPrintable(QStringLiteral("Unblurred detail remains in region %1").arg(i)),
                                      5000);
        }

        // A flat source also catches a faded/dark border: edge-energy alone
        // would consider an incorrectly black edge to be successfully blurred.
        QImage solid(size, QImage::Format_RGB32);
        solid.fill(QColor(64, 160, 208));
        const QString solidPath = fixture.path() + QStringLiteral("/solid.png");
        QVERIFY(solid.save(solidPath));
        QVERIFY(view.rootObject()->setProperty("testImage", QUrl::fromLocalFile(solidPath)));
        QTRY_COMPARE(view.rootObject()->property("imageStatus").toInt(), 1);
        QTRY_VERIFY(view.grabWindow().pixelColor(size.width() / 2, size.height() / 2).blue()
                    > 1.5 * view.grabWindow().pixelColor(size.width() / 2, size.height() / 2).red());
        const auto largestEdgeDifference = [&] {
            const QImage rendered = view.grabWindow();
            const QColor center = rendered.pixelColor(size.width() / 2, size.height() / 2);
            int difference = 0;
            for (qsizetype i = 0; i < regions.size() - 1; ++i) {
                const QRect region = regions[i];
                for (int y = region.top(); y <= region.bottom(); ++y) {
                    for (int x = region.left(); x <= region.right(); ++x) {
                        const QColor pixel = rendered.pixelColor(x, y);
                        difference = std::max({difference,
                                               std::abs(pixel.red() - center.red()),
                                               std::abs(pixel.green() - center.green()),
                                               std::abs(pixel.blue() - center.blue()),
                                               std::abs(pixel.alpha() - center.alpha())});
                    }
                }
            }
            return difference;
        };
        QTRY_VERIFY2_WITH_TIMEOUT(largestEdgeDifference() <= 2, "Blur introduces a faded or discolored wallpaper border", 5000);
    }
};

int main(int argc, char **argv)
{
    QTemporaryDir home;
    if (!home.isValid()) {
        return 1;
    }
    qputenv("HOME", home.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", (home.path() + QStringLiteral("/config")).toUtf8());
    qputenv("XDG_CONFIG_DIRS", (home.path() + QStringLiteral("/empty-config")).toUtf8());
    qputenv("XDG_CACHE_HOME", (home.path() + QStringLiteral("/cache")).toUtf8());
    qputenv("QT_QPA_PLATFORMTHEME", "generic");
    qputenv("QT_NO_XDG_DESKTOP_PORTAL", "1");
    qputenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent/soniclogin-blur-test-bus");
    qputenv("DBUS_SYSTEM_BUS_ADDRESS", "unix:path=/nonexistent/soniclogin-blur-test-system-bus");
    qputenv("QT_SCALE_FACTOR", "1");
    qunsetenv("QT_SCREEN_SCALE_FACTORS");
    qunsetenv("QT_QUICK_BACKEND");
    QGuiApplication app(argc, argv);
    WallpaperFaderTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "WallpaperFaderTest.moc"
