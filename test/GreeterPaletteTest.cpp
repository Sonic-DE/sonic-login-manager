/*
 * SPDX-FileCopyrightText: 2026 SonicDE Community
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QProcess>
#include <QProcessEnvironment>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickView>
#include <QSignalSpy>
#include <QStringListModel>
#include <QTemporaryDir>
#include <QTest>

#include "lookandfeelsettings.h"

#include <KIconTheme>
#include <KLocalizedQmlContext>

#include <cmath>

class PaletteGreeterState : public QObject {
    Q_OBJECT
    Q_PROPERTY(int sessionIndex MEMBER sessionIndex NOTIFY sessionIndexChanged)
public:
    int sessionIndex = 0;
    Q_INVOKABLE void setSessionButtonPopupVisible(QObject *, bool)
    {
    }
Q_SIGNALS:
    void sessionIndexChanged();
};

class GreeterPaletteTest : public QObject {
    Q_OBJECT

    QQuickView m_view;
    QStringListModel m_sessions;
    PaletteGreeterState m_state;

    static double luminance(const QColor &color)
    {
        const auto channel = [](double v) {
            return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
        };
        return 0.2126 * channel(color.redF()) + 0.7152 * channel(color.greenF()) + 0.0722 * channel(color.blueF());
    }

    static int matchingPixels(const QImage &image, const QRect &area, const QColor &expected)
    {
        int count = 0;
        for (int y = area.top(); y <= area.bottom(); ++y) {
            for (int x = area.left(); x <= area.right(); ++x) {
                const QColor actual = image.pixelColor(x, y);
                if (std::abs(actual.red() - expected.red()) <= 2 && std::abs(actual.green() - expected.green()) <= 2
                    && std::abs(actual.blue() - expected.blue()) <= 2) {
                    ++count;
                }
            }
        }
        return count;
    }

private Q_SLOTS:
    void initTestCase()
    {
        m_sessions.setStringList({QStringLiteral("Test X11"), QStringLiteral("Test Wayland")});
        qmlRegisterSingletonInstance("org.kde.sonic.login", 0, 1, "SessionModel", &m_sessions);
        qmlRegisterSingletonInstance("org.kde.sonic.login", 0, 1, "GreeterState", &m_state);
        KLocalization::setupLocalizedContext(m_view.engine());
        m_view.setSource(QUrl::fromLocalFile(QStringLiteral(GREETER_PALETTE_QML)));
        QVERIFY2(m_view.status() == QQuickView::Ready, qPrintable(m_view.errors().isEmpty() ? QString() : m_view.errors().first().toString()));
        m_view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&m_view));
    }

    void embeddedColorAwareStyleIsSelected()
    {
        QCOMPARE(QQuickStyle::name(), QStringLiteral("org.kde.breeze"));
        QFile config(QStringLiteral(":/qtquickcontrols2.conf"));
        QVERIFY(config.open(QIODevice::ReadOnly));
        QVERIFY(config.readAll().contains("Style=org.kde.breeze"));
    }

    void darkSilverIsTheStartupDefault()
    {
        LookAndFeelSettings settings;
        QCOMPARE(settings.lookAndFeelPackage(), QStringLiteral("org.kde.silverdarkbottompanel.desktop"));
        QCOMPARE(settings.defaultDarkLookAndFeel(), QStringLiteral("org.kde.silverdarkbottompanel.desktop"));
        QCOMPARE(settings.defaultLightLookAndFeel(), QStringLiteral("org.kde.silverlightbottompanel.desktop"));
    }

    void selectorAndIconsFollowPalette_data()
    {
        QTest::addColumn<QColor>("foreground");
        QTest::addColumn<QColor>("background");
        QTest::newRow("dark-theme") << QColor("#beffe6") << QColor("#202428");
        QTest::newRow("light-theme") << QColor("#183d56") << QColor("#f5f5f5");
    }

    void unmodifiedSelectorFollowsSelectedScheme_data()
    {
        QTest::addColumn<QColor>("foreground");
        QTest::addColumn<QColor>("background");
        QTest::newRow("dark-silver-light-text") << QColor(252, 252, 252) << QColor(42, 46, 50);
        QTest::newRow("light-silver-dark-text") << QColor(35, 38, 41) << QColor(239, 240, 241);
    }

    void unmodifiedSelectorFollowsSelectedScheme()
    {
        QFETCH(QColor, foreground);
        QFETCH(QColor, background);
        // Initialize the real KDE/Kirigami palette from private kdeglobals in
        // fresh processes. QGuiApplication::setPalette() alone does not change
        // the Kirigami platform theme's KColorScheme.
        if (!qEnvironmentVariableIsSet("SONICLOGIN_PALETTE_TEST")) {
            QProcess child;
            auto env = QProcessEnvironment::systemEnvironment();
            env.insert(QStringLiteral("SONICLOGIN_PALETTE_TEST"), foreground.name());
            env.insert(QStringLiteral("SONICLOGIN_PALETTE_BACKGROUND"), background.name());
            child.setProcessEnvironment(env);
            child.start(QCoreApplication::applicationFilePath(),
                        {QStringLiteral("unmodifiedSelectorFollowsSelectedScheme:") + QString::fromLatin1(QTest::currentDataTag())});
            QVERIFY(child.waitForFinished(10000));
            QCOMPARE(child.exitStatus(), QProcess::NormalExit);
            QVERIFY2(child.exitCode() == 0, (child.readAllStandardOutput() + child.readAllStandardError()).constData());
            return;
        }
        QCOMPARE(m_view.rootObject()->property("unmodifiedForeground").value<QColor>(), foreground);
        QCOMPARE(m_view.rootObject()->property("unmodifiedBackground").value<QColor>(), background);
    }

    void selectorAndIconsFollowPalette()
    {
        QFETCH(QColor, foreground);
        QFETCH(QColor, background);
        auto *root = m_view.rootObject();
        QVERIFY(root->setProperty("testForeground", foreground));
        QVERIFY(root->setProperty("testBackground", background));
        auto *selector = root->property("selector").value<QObject *>();
        QVERIFY(selector);
        auto *label = selector->property("contentItem").value<QObject *>();
        QVERIFY(label);
        QTRY_COMPARE(label->property("color").value<QColor>(), foreground);
        // The selected scheme's Window color set, not a Plasma SVG surface with
        // unrelated colors. Rendered checks catch the original white-on-white.
        QVERIFY(root->property("selectorUsesWindow").toBool());
        QImage screenshot;
        QTRY_VERIFY_WITH_TIMEOUT(!(screenshot = m_view.grabWindow()).isNull(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(matchingPixels(m_view.grabWindow(), QRect(20, 130, 32, 32), foreground) > 100, 5000);
        screenshot = m_view.grabWindow();
        QCOMPARE(screenshot.pixelColor(380, 20 + selector->property("height").toInt() / 2), background);
        QVERIFY(matchingPixels(screenshot, QRect(20, 20, 300, 40), foreground) > 10);
    }

    void popupHasReadableOwnPalette()
    {
        auto *selector = m_view.rootObject()->property("selector").value<QObject *>();
        auto *popup = selector->property("popup").value<QObject *>();
        QVERIFY(popup);
        QVERIFY(QMetaObject::invokeMethod(popup, "open"));
        QTRY_VERIFY(popup->property("visible").toBool());
        const double first = luminance(m_view.rootObject()->property("popupForeground").value<QColor>());
        const double second = luminance(m_view.rootObject()->property("popupBackground").value<QColor>());
        QVERIFY((std::max(first, second) + 0.05) / (std::min(first, second) + 0.05) >= 4.5);
        QVERIFY(QMetaObject::invokeMethod(popup, "close"));
    }
};

int main(int argc, char **argv)
{
    QTemporaryDir fixture;
    if (!fixture.isValid()) {
        return 1;
    }
    const QString data = fixture.path() + QStringLiteral("/data");
    const QString config = fixture.path() + QStringLiteral("/config");
    const QString icons = data + QStringLiteral("/icons/PaletteTest/32x32/actions");
    if (!QDir().mkpath(icons) || !QDir().mkpath(config)) {
        return 1;
    }
    auto write = [](const QString &path, const QByteArray &bytes) {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    };
    QByteArray globals = "[Icons]\nTheme=PaletteTest\n";
    if (qEnvironmentVariableIsSet("SONICLOGIN_PALETTE_TEST")) {
        auto rgb = [](const QString &name) {
            const QColor color(name);
            return QByteArray::number(color.red()) + ',' + QByteArray::number(color.green()) + ',' + QByteArray::number(color.blue());
        };
        const auto foreground = rgb(qEnvironmentVariable("SONICLOGIN_PALETTE_TEST"));
        const auto background = rgb(qEnvironmentVariable("SONICLOGIN_PALETTE_BACKGROUND"));
        globals += "[Colors:Window]\nForegroundNormal=" + foreground + "\nBackgroundNormal=" + background + '\n';
        // Deliberately opposite: using Complementary must fail these tests.
        globals += "[Colors:Complementary]\nForegroundNormal=" + background + "\nBackgroundNormal=" + foreground + '\n';
    }
    if (!write(config + QStringLiteral("/kdeglobals"), globals)
        || !write(data + QStringLiteral("/icons/PaletteTest/index.theme"),
                  "[Icon Theme]\nName=PaletteTest\nFollowsColorScheme=true\nDirectories=32x32/actions\n[32x32/actions]\nSize=32\nType=Fixed\nContext=Actions\n")
        || !write(
            icons + QStringLiteral("/regression-symbolic.svg"),
            "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"32\" height=\"32\"><defs><style type=\"text/css\" id=\"current-color-scheme\">.ColorScheme-Text "
            "{color:#232629;}</style></defs><path class=\"ColorScheme-Text\" style=\"fill:currentColor\" d=\"M4 4h24v24H4z\"/></svg>")) {
        return 1;
    }
    qputenv("HOME", fixture.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", config.toUtf8());
    qputenv("XDG_CONFIG_DIRS", (fixture.path() + QStringLiteral("/empty-config")).toUtf8());
    qputenv("XDG_DATA_HOME", data.toUtf8());
    qputenv("XDG_CACHE_HOME", (fixture.path() + QStringLiteral("/cache")).toUtf8());
    qputenv("QT_QPA_PLATFORMTHEME", qEnvironmentVariableIsSet("SONICLOGIN_PALETTE_TEST") ? "kde" : "generic");
    qunsetenv("QT_QUICK_CONTROLS_STYLE");
    qunsetenv("QT_QUICK_CONTROLS_CONF");
    KIconTheme::initTheme();
    QGuiApplication app(argc, argv);
    GreeterPaletteTest tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "GreeterPaletteTest.moc"
