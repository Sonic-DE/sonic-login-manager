/*
 * SPDX-FileCopyrightText: 2026 SonicDE Community
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <QCryptographicHash>
#include <QDeadlineTimer>
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
#include <QScopeGuard>
#include <QScreen>
#include <QSet>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QStringListModel>
#include <QTemporaryDir>
#include <QTest>
#include <QtGui/qguiapplication_platform.h>

#include <xcb/shape.h>

#include "lookandfeelsettings.h"

#include <KIconTheme>
#include <KLocalizedQmlContext>
#include <Plasma/Theme>

#include <cmath>
#include <memory>

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

    QStringListModel m_sessions;
    PaletteGreeterState m_state;
    QQuickView m_view;

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

    static QSet<xcb_window_t> mappedWindows()
    {
        auto *connection = qGuiApp->nativeInterface<QNativeInterface::QX11Application>()->connection();
        const auto root = xcb_setup_roots_iterator(xcb_get_setup(connection)).data->root;
        auto *tree = xcb_query_tree_reply(connection, xcb_query_tree(connection, root), nullptr);
        QSet<xcb_window_t> result;
        if (tree) {
            const auto windows = xcb_query_tree_children(tree);
            for (int i = 0; i < xcb_query_tree_children_length(tree); ++i) {
                auto *attributes = xcb_get_window_attributes_reply(connection, xcb_get_window_attributes(connection, windows[i]), nullptr);
                if (attributes && attributes->map_state == XCB_MAP_STATE_VIEWABLE) {
                    result.insert(windows[i]);
                }
                free(attributes);
            }
            free(tree);
        }
        return result;
    }

    static bool clickScreenPoint(QPoint point)
    {
        auto *connection = qGuiApp->nativeInterface<QNativeInterface::QX11Application>()->connection();
        const auto root = xcb_setup_roots_iterator(xcb_get_setup(connection)).data->root;
        xcb_warp_pointer(connection, XCB_NONE, root, 0, 0, 0, 0, point.x(), point.y());
        auto *pointer = xcb_query_pointer_reply(connection, xcb_query_pointer(connection, root), nullptr);
        const auto target = pointer ? pointer->child : XCB_NONE;
        free(pointer);
        if (target == XCB_NONE) {
            return false;
        }
        xcb_button_press_event_t event{};
        event.response_type = XCB_BUTTON_PRESS;
        event.detail = 1;
        event.root = root;
        event.event = target;
        event.root_x = event.event_x = point.x();
        event.root_y = event.event_y = point.y();
        event.same_screen = true;
        xcb_send_event(connection, false, target, XCB_EVENT_MASK_BUTTON_PRESS, reinterpret_cast<const char *>(&event));
        event.response_type = XCB_BUTTON_RELEASE;
        event.state = XCB_BUTTON_MASK_1;
        xcb_send_event(connection, false, target, XCB_EVENT_MASK_BUTTON_RELEASE, reinterpret_cast<const char *>(&event));
        xcb_flush(connection);
        return true;
    }

    static QImage newlyShownKeyboard(const QSet<xcb_window_t> &before)
    {
        auto *connection = qGuiApp->nativeInterface<QNativeInterface::QX11Application>()->connection();
        auto *screen = QGuiApplication::primaryScreen();
        for (const auto window : mappedWindows() - before) {
            auto *geometry = xcb_get_geometry_reply(connection, xcb_get_geometry(connection, window), nullptr);
            const bool fullSize = geometry && QSize(geometry->width, geometry->height) == screen->size();
            free(geometry);
            if (!fullSize) {
                continue;
            }
            auto *shape = xcb_shape_get_rectangles_reply(connection, xcb_shape_get_rectangles(connection, window, XCB_SHAPE_SK_BOUNDING), nullptr);
            QRect bounds;
            if (shape) {
                const auto rectangles = xcb_shape_get_rectangles_rectangles(shape);
                for (int i = 0; i < xcb_shape_get_rectangles_rectangles_length(shape); ++i) {
                    const auto rect = rectangles[i];
                    bounds |= QRect(rect.x, rect.y, rect.width, rect.height);
                }
                free(shape);
            }
            if (!bounds.isEmpty() && QRect(QPoint(), screen->size()).contains(bounds) && bounds.top() >= screen->size().height() / 2) {
                return screen->grabWindow(window, bounds.x(), bounds.y(), bounds.width(), bounds.height()).toImage();
            }
        }
        return {};
    }

private Q_SLOTS:
    void initTestCase()
    {
        if (QGuiApplication::platformName() != QLatin1String("xcb")) {
            QSKIP("The Plasma SVG consumer test requires a private X11 display (xvfb-run)");
        }
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

    void selectedDesktopThemeBackgroundIsRendered()
    {
        auto *selector = m_view.rootObject()->property("selector").value<QObject *>();
        QVERIFY(selector);
        QVERIFY(selector->setProperty("flat", false));
        const auto restore = qScopeGuard([selector]() {
            selector->setProperty("flat", true);
        });
        auto orangePixels = [this]() {
            const auto image = m_view.grabWindow().convertToFormat(QImage::Format_ARGB32);
            return matchingPixels(image, image.rect(), QColor(240, 120, 0));
        };
        // This color exists only in the selected desktop theme's button SVG,
        // not in an attached-property override or an application palette.
        QTRY_VERIFY_WITH_TIMEOUT(orangePixels() > 500, 3000);
    }

    void selectedAppearanceIsRenderedByGreeter_data()
    {
        QTest::addColumn<bool>("customized");
        QTest::newRow("scheme-reference-with-cached-hash") << false;
        QTest::newRow("explicit-user-overrides-win") << true;
    }

    void selectedAppearanceIsRenderedByGreeter()
    {
        if (!qEnvironmentVariableIntValue("SONICLOGIN_GREETER_TEST_ISOLATED")) {
            QSKIP("Launching the real greeter requires the private xvfb-run test display");
        }
        QFETCH(bool, customized);
        const auto keyboardSuffix = customized ? QStringLiteral("-override.png") : QStringLiteral("-reference.png");
        const auto keyboardScreenshot = QStringLiteral(GREETER_KEYBOARD_SCREENSHOT) + keyboardSuffix;
        for (const auto &file : {QStringLiteral(GREETER_RENDERED_SCREENSHOT), keyboardScreenshot}) {
            if (QFile::exists(file)) {
                QVERIFY(QFile::remove(file));
            }
        }
        QFile globals(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/kdeglobals"));
        QVERIFY(globals.open(QIODevice::ReadOnly));
        const auto original = globals.readAll();
        globals.close();
        const auto restoreConfig = qScopeGuard([&]() {
            if (globals.open(QIODevice::WriteOnly)) {
                globals.write(original);
                globals.close();
            }
        });
        if (customized) {
            QVERIFY(globals.open(QIODevice::Append));
            globals.write("[Colors:Window]\nForegroundNormal=255,0,255\nBackgroundNormal=245,245,245\n[Icons]\nTheme=PaletteAlternate\n");
            globals.close();
        }
        const QColor foreground = customized ? QColor(255, 0, 255) : QColor(0, 255, 255);
        m_view.hide();
        QQuickWindow backdrop;
        backdrop.setColor(Qt::black);
        backdrop.setFlags(Qt::BypassWindowManagerHint);
        backdrop.setGeometry(QGuiApplication::primaryScreen()->geometry());
        backdrop.show();
        const auto restore = qScopeGuard([this]() {
            m_view.show();
        });
        QVERIFY(QTest::qWaitForWindowExposed(&backdrop));
        auto orangePixels = []() {
            const auto image = QGuiApplication::primaryScreen()->grabWindow(0).toImage();
            return matchingPixels(image, image.rect(), QColor(240, 120, 0));
        };
        QTRY_COMPARE_WITH_TIMEOUT(orangePixels(), 0, 3000);
        QProcess greeter;
        greeter.setProcessChannelMode(QProcess::ForwardedChannels);
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("QT_QPA_PLATFORMTHEME"), QStringLiteral("kde"));
        environment.remove(QStringLiteral("QT_VIRTUALKEYBOARD_STYLE"));
        environment.remove(QStringLiteral("QT_VIRTUALKEYBOARD_LAYOUT_PATH"));
        greeter.setProcessEnvironment(environment);
        const auto stop = qScopeGuard([&greeter]() {
            greeter.terminate();
            if (!greeter.waitForFinished(3000)) {
                greeter.kill();
                greeter.waitForFinished();
            }
        });
        greeter.start(QStringLiteral(GREETER_BINARY), {QStringLiteral("--test")});
        QVERIFY(greeter.waitForStarted());
        QTRY_VERIFY_WITH_TIMEOUT(greeter.state() == QProcess::Running && orangePixels() > 500, 5000);
        auto clockPixels = [foreground]() {
            const auto image = QGuiApplication::primaryScreen()->grabWindow(0).toImage();
            return matchingPixels(image, QRect(0, 0, image.width(), image.height() / 3), foreground);
        };
        QTRY_VERIFY_WITH_TIMEOUT(clockPixels() > 100, 3000);
        QVERIFY(QGuiApplication::primaryScreen()->grabWindow(0).save(QStringLiteral(GREETER_RENDERED_SCREENSHOT)));
        auto shutdownIconPixels = []() {
            const auto image = QGuiApplication::primaryScreen()->grabWindow(0).toImage();
            int count = 0;
            for (int y = image.height() * 2 / 3; y < image.height(); ++y) {
                for (int x = 0; x < image.width(); ++x) {
                    const auto pixel = image.pixelColor(x, y);
                    // ActionButton uses 85% opacity when unfocused. Only the
                    // selected icon is green; palette text is cyan/magenta.
                    if (pixel.green() > 150 && pixel.green() > pixel.red() + 100 && pixel.green() > pixel.blue() + 100) {
                        ++count;
                    }
                }
            }
            return count;
        };
        QTRY_VERIFY_WITH_TIMEOUT(shutdownIconPixels() > 100, 3000);
        QVERIFY(QGuiApplication::primaryScreen()->grabWindow(0).save(QStringLiteral(GREETER_RENDERED_SCREENSHOT)));

        const auto windowsBeforeKeyboard = mappedWindows();
        const QColor background = customized ? QColor(245, 245, 245) : QColor(80, 0, 80);
        const QColor keyBackground = background.lighter(130);
        QVERIFY(clickScreenPoint(QPoint(80, QGuiApplication::primaryScreen()->size().height() - 20)));
        QImage keyboard;
        int backgroundPixels = 0, keyPixels = 0, textPixels = 0;
        int redPixels = 0, bluePixels = 0;
        qint64 redX = 0, redY = 0, blueX = 0, blueY = 0;
        auto themedKeyboard = [&]() {
            keyboard = newlyShownKeyboard(windowsBeforeKeyboard);
            backgroundPixels = matchingPixels(keyboard, keyboard.rect(), background);
            keyPixels = matchingPixels(keyboard, keyboard.rect(), keyBackground);
            textPixels = matchingPixels(keyboard, keyboard.rect(), foreground);
            redPixels = bluePixels = 0;
            redX = redY = blueX = blueY = 0;
            for (int y = 0; y < keyboard.height(); ++y) {
                for (int x = 0; x < keyboard.width(); ++x) {
                    const auto pixel = keyboard.pixelColor(x, y);
                    if (pixel.red() > 252 && pixel.green() < 3 && pixel.blue() < 3) {
                        ++redPixels;
                        redX += x;
                        redY += y;
                    } else if (pixel.blue() > 252 && pixel.red() < 3 && pixel.green() < 3) {
                        ++bluePixels;
                        blueX += x;
                        blueY += y;
                    }
                }
            }
            // The selected sets have different globe assets: left/right halves
            // in PaletteTest and top/bottom halves in PaletteAlternate.
            const bool selectedGlobe = customized ? redY * bluePixels > blueY * redPixels : redX * bluePixels > blueX * redPixels;
            const bool paletteMatches = backgroundPixels > 1000 && keyPixels > 10000 && textPixels > 50;
            return !keyboard.isNull() && paletteMatches && redPixels > 100 && bluePixels > 100 && selectedGlobe;
        };
        const bool themed = QTest::qWaitFor(themedKeyboard, QDeadlineTimer(5000));
        qInfo() << "Keyboard image" << keyboard.size();
        qInfo() << "Keyboard background/key/text pixels" << backgroundPixels << keyPixels << textPixels;
        qInfo() << "Selected globe red/blue pixels" << redPixels << bluePixels << "alternate set" << customized;
        if (!keyboard.isNull()) {
            QVERIFY(keyboard.save(keyboardScreenshot));
        }
        QVERIFY(themed);
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
        // The surface belongs to the selected desktop SVG, not a forced solid
        // application-palette background. Text and icons still follow palette.
        QCOMPARE(screenshot.pixelColor(380, 20 + selector->property("height").toInt() / 2), QColor(240, 120, 0));
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
    const QString alternateIcons = data + QStringLiteral("/icons/PaletteAlternate/32x32/actions");
    const QString desktop = data + QStringLiteral("/plasma/desktoptheme/PaletteDesktop");
    if (!QDir().mkpath(icons) || !QDir().mkpath(alternateIcons) || !QDir().mkpath(config) || !QDir().mkpath(desktop + QStringLiteral("/widgets"))
        || !QDir().mkpath(data + QStringLiteral("/color-schemes"))) {
        return 1;
    }
    auto write = [](const QString &path, const QByteArray &bytes) {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    };
    const QByteArray palette = R"([Colors:Window]
ForegroundNormal=0,255,255
BackgroundNormal=80,0,80
[Colors:View]
ForegroundNormal=0,255,255
BackgroundNormal=80,0,80
[Colors:Button]
ForegroundNormal=0,255,255
BackgroundNormal=80,0,80
)";
    QByteArray globals = "[Icons]\nTheme=PaletteTest\n[General]\nColorScheme=PaletteColors\nColorSchemeHash="
        + QCryptographicHash::hash(palette, QCryptographicHash::Sha1).toHex() + '\n';
    if (!write(data + QStringLiteral("/color-schemes/PaletteColors.colors"), palette)) {
        return 1;
    }
    QFile desktopSvg(QStringLiteral(GREETER_SVG_FIXTURE));
    if (!desktopSvg.open(QIODevice::ReadOnly)
        || !write(desktop + QStringLiteral("/metadata.json"), "{\"KPlugin\":{\"Id\":\"PaletteDesktop\",\"Name\":\"Palette desktop\"}}")
        || !write(desktop + QStringLiteral("/widgets/button.svg"), desktopSvg.readAll())
        || !write(config + QStringLiteral("/plasmarc"), "[Theme]\nname=PaletteDesktop\n")) {
        return 1;
    }
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
        || !write(icons + QStringLiteral("/system-shutdown.svg"),
                  "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"32\" height=\"32\"><path fill=\"#00ff00\" d=\"M16 2L30 30H2z\"/></svg>")
        || !write(icons + QStringLiteral("/globe.svg"),
                  "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"32\" height=\"32\"><path fill=\"#0000ff\" d=\"M0 0h16v32H0z\"/>"
                  "<path fill=\"#ff0000\" d=\"M16 0h16v32H16z\"/></svg>")
        || !write(data + QStringLiteral("/icons/PaletteAlternate/index.theme"),
                  "[Icon Theme]\nName=PaletteAlternate\nInherits=PaletteTest\nFollowsColorScheme=true\nDirectories=32x32/actions\n"
                  "[32x32/actions]\nSize=32\nType=Fixed\nContext=Actions\n")
        || !write(alternateIcons + QStringLiteral("/globe.svg"),
                  "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"32\" height=\"32\"><path fill=\"#0000ff\" d=\"M0 0h32v16H0z\"/>"
                  "<path fill=\"#ff0000\" d=\"M0 16h32v16H0z\"/></svg>")
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
    std::unique_ptr<Plasma::Theme> desktopTheme;
    if (QGuiApplication::platformName() == QLatin1String("xcb")) {
        desktopTheme = std::make_unique<Plasma::Theme>();
    }
    GreeterPaletteTest tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "GreeterPaletteTest.moc"
