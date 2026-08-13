/*
    SPDX-FileCopyrightText: 2010 Ivan Cukic <ivan.cukic(at)kde.org>
    SPDX-FileCopyrightText: 2013 Martin Klapetek <mklapetek(at)kde.org>
    SPDX-FileCopyrightText: 2025 Oliver Beard <olib141@outlook.com

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <QFile>
#include <QFileInfo>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickItem>
#include <QUrl>

#include <memory>

#include <QDBusConnection>
#include <QDBusError>

#include <KConfig>
#include <KConfigGroup>
#include <KConfigLoader>
#include <KConfigPropertyMap>
#include <KPackage/PackageLoader>
#include <KWindowSystem>

#include "sonicloginsettings.h"

#include "wallpaperwindow.h"

#include "wallpaperapp.h"

#include "MessageHandler.h"

void WallpaperAppMessageHandler(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    SONICLOGIN::messageHandler(type, QStringLiteral("SONICLOGIN WALLPAPERAPP"), msg);
}

WallpaperApp::WallpaperApp(int &argc, char **argv)
    : QGuiApplication(argc, argv)
{
    // Install message handler to log to soniclogin.log
    qInstallMessageHandler(WallpaperAppMessageHandler);

    qInfo() << "WallpaperApp: initialized"
            << "platform=" << platformName() << "screens=" << screens().size();

    m_wallpaperPackage = KPackage::PackageLoader::self()->loadPackage(QStringLiteral("Plasma/Wallpaper"));
    m_wallpaperPackage.setPath(SonicLoginSettings::getInstance().wallpaperPluginId());

    qInfo() << "WallpaperApp: wallpaper plugin package"
            << "path=" << m_wallpaperPackage.path() << "valid=" << m_wallpaperPackage.isValid() << "pluginId=" << m_wallpaperPackage.metadata().pluginId();
}

WallpaperApp::~WallpaperApp()
{
    qDeleteAll(m_windows);
}

void WallpaperApp::start(bool testMode)
{
    m_testMode = testMode;

    if (m_testMode) {
        qInfo() << "WallpaperApp: starting standalone test window";
        if (QScreen *screen = primaryScreen()) {
            adoptScreen(screen);
        } else {
            qWarning() << "WallpaperApp: no primary screen available for test window";
        }
        return;
    }

    for (const auto screenList{screens()}; QScreen *screen : screenList) {
        adoptScreen(screen);
    }

    connect(this, &QGuiApplication::screenAdded, this, &WallpaperApp::adoptScreen);
    connect(this, &QGuiApplication::screenRemoved, this, &WallpaperApp::removeScreen);

    auto bus = QDBusConnection::sessionBus();
    bus.registerObject(QStringLiteral("/Wallpaper"), this, QDBusConnection::ExportScriptableSlots);
    if (!bus.registerService(QStringLiteral("org.kde.plasma.wallpaper"))) {
        qWarning() << "Failed to register DBus service org.kde.plasma.wallpaper:" << bus.lastError().message();
    }
}

void WallpaperApp::adoptScreen(QScreen *screen)
{
    qInfo() << "WallpaperApp::adoptScreen:"
            << "name=" << screen->name() << "geometry=" << screen->geometry() << "devicePixelRatio=" << screen->devicePixelRatio();

    if (screen->geometry().isNull()) {
        qWarning() << "adoptScreen: Screen" << screen->name() << "has null geometry, deferring.";
        connect(screen, &QScreen::geometryChanged, this, [this, screen]() {
            if (!screen->geometry().isNull()) {
                QObject::disconnect(sender());
                adoptScreen(screen);
            }
        });
        return;
    }

    for (WallpaperWindow *window : std::as_const(m_windows)) {
        if (window->screen() == screen) {
            if (window->geometry() != screen->geometry()) {
                window->setGeometry(screen->geometry());
                window->raise();
                window->show();
            }
            return;
        }
    }

    WallpaperWindow *window = new WallpaperWindow(screen, m_testMode);
    if (m_testMode) {
        const QRect availableGeometry = screen->availableGeometry();
        const QSize windowSize = QSize(1280, 720).boundedTo(availableGeometry.size());
        QRect windowGeometry(QPoint(), windowSize);
        windowGeometry.moveCenter(availableGeometry.center());
        window->setGeometry(windowGeometry);
    } else {
        window->setGeometry(screen->geometry());
    }
    m_windows << window;

    connect(screen, &QObject::destroyed, window, [this, window]() {
        m_windows.removeAll(window);
        window->deleteLater();
    });

    window->setSource(QUrl(QStringLiteral("qrc:/qt/qml/org/kde/sonic/login/wallpaper/main.qml")));
    if (window->status() == QQmlComponent::Error) {
        qWarning() << "Failed to load wallpaper host QML:" << window->errors();
        return;
    }

    QQuickItem *wallpaperContainer = window->rootObject()->property("wallpaperContainer").value<QQuickItem *>();
    if (!wallpaperContainer) {
        qWarning() << "Failed to find wallpaper container in wallpaper host QML";
        return;
    }

    auto initialized = std::make_shared<bool>(false);
    auto setupWhenLaidOut = [this, window, wallpaperContainer, initialized]() {
        if (*initialized || wallpaperContainer->width() <= 0 || wallpaperContainer->height() <= 0) {
            return;
        }
        *initialized = true;
        setupWallpaperPlugin(window, wallpaperContainer);
    };
    connect(wallpaperContainer, &QQuickItem::widthChanged, window, setupWhenLaidOut);
    connect(wallpaperContainer, &QQuickItem::heightChanged, window, setupWhenLaidOut);

    window->setVisible(true);
    setupWhenLaidOut();
}

void WallpaperApp::removeScreen(QScreen *screen)
{
    for (int i = m_windows.size() - 1; i >= 0; --i) {
        WallpaperWindow *window = m_windows.at(i);
        if (window->screen() == screen) {
            m_windows.removeAt(i);
            window->deleteLater();
        }
    }
}

void WallpaperApp::setupWallpaperPlugin(WallpaperWindow *window, QQuickItem *wallpaperContainer)
{
    if (!m_wallpaperPackage.isValid()) {
        qWarning() << "Error loading the wallpaper, not a valid package";
        return;
    }

    QString xmlPath = m_wallpaperPackage.filePath(QByteArrayLiteral("config"), QStringLiteral("main.xml"));

    auto sharedConfig = SonicLoginSettings::getInstance().sharedConfig();
    sharedConfig->reparseConfiguration();

    const auto wallpaperGroup = sharedConfig->group(QStringLiteral("Greeter")).group(QStringLiteral("Wallpaper"));
    const bool hasWallpaperConfig = !wallpaperGroup.keyList().isEmpty() || !wallpaperGroup.groupList().isEmpty();

    const QString pluginId = SonicLoginSettings::getInstance().wallpaperPluginId();
    KConfigGroup cfg = wallpaperGroup.group(pluginId);

    qInfo() << "setupWallpaperPlugin: pluginId=" << pluginId << "hasWallpaperConfig=" << hasWallpaperConfig << "configFile=" << sharedConfig->name()
            << "cfg group=" << cfg.name() << "cfg keys=" << cfg.keyList() << "cfg groupList=" << cfg.groupList();

    // Fall back to POTD if the current wallpaper plugin has no images configured
    if (!hasWallpaperConfig) {
        m_wallpaperPackage.setPath(QStringLiteral("org.kde.potd"));
        if (!m_wallpaperPackage.isValid()) {
            qWarning() << "Error loading POTD wallpaper, falling back to black background";
            m_wallpaperPackage.setPath(pluginId);
        } else {
            xmlPath = m_wallpaperPackage.filePath(QByteArrayLiteral("config"), QStringLiteral("main.xml"));

            // Use config for the POTD plugin
            cfg = sharedConfig->group(QStringLiteral("Greeter")).group(QStringLiteral("Wallpaper")).group(m_wallpaperPackage.path());
        }
    }

    KConfigLoader *configLoader;
    if (xmlPath.isEmpty()) {
        configLoader = new KConfigLoader(cfg, nullptr, this);
    } else {
        QFile file(xmlPath);
        configLoader = new KConfigLoader(cfg, &file, this);
    }

    KConfigPropertyMap *config = new KConfigPropertyMap(configLoader, this);
    // potd (picture of the day) is using a kded to monitor changes and
    // cache data for the lockscreen. Let's notify it.
    config->setNotify(true);

    const QUrl sourceUrl = QUrl::fromLocalFile(m_wallpaperPackage.filePath("mainscript"));

    const QString configuredImage = cfg.group(QStringLiteral("General")).readEntry("Image", QString());
    const QUrl configuredImageUrl(configuredImage);
    const QFileInfo configuredImageInfo(configuredImageUrl.toLocalFile());
    qInfo() << "setupWallpaperPlugin: mainscript=" << sourceUrl << "xmlPath=" << xmlPath << "Image entry=" << configuredImage
            << "localPath=" << configuredImageUrl.toLocalFile() << "exists=" << configuredImageInfo.exists() << "isFile=" << configuredImageInfo.isFile()
            << "isDir=" << configuredImageInfo.isDir() << "readable=" << configuredImageInfo.isReadable();

    auto *component = new QQmlComponent(window->engine().get(), sourceUrl, window);
    if (component->isError()) {
        qWarning() << "Failed to load wallpaper component:" << component->errors();
        return;
    }

    const QVariantMap properties = {{QStringLiteral("configuration"), QVariant::fromValue(config)},
                                    {QStringLiteral("pluginName"), m_wallpaperPackage.metadata().pluginId()}};
    QObject *wallpaperObject = component->createWithInitialProperties(properties, window->rootContext());
    auto wallpaperItem = qobject_cast<QQuickItem *>(wallpaperObject);
    if (!wallpaperItem) {
        qWarning() << "Failed to create wallpaper root object:" << component->errors();
        return;
    }
    qInfo() << "setupWallpaperPlugin: wallpaper object created successfully"
            << "class=" << wallpaperObject->metaObject()->className();

    wallpaperItem->setParentItem(wallpaperContainer);
    wallpaperItem->setWidth(wallpaperContainer->width());
    wallpaperItem->setHeight(wallpaperContainer->height());
    qInfo() << "setupWallpaperPlugin: wallpaper item attached"
            << "containerSize=" << wallpaperContainer->size() << "itemSize=" << wallpaperItem->size() << "visible=" << wallpaperItem->isVisible();
    connect(wallpaperContainer, &QQuickItem::widthChanged, wallpaperItem, [wallpaperContainer, wallpaperItem]() {
        wallpaperItem->setWidth(wallpaperContainer->width());
    });
    connect(wallpaperContainer, &QQuickItem::heightChanged, wallpaperItem, [wallpaperContainer, wallpaperItem]() {
        wallpaperItem->setHeight(wallpaperContainer->height());
    });
}

void WallpaperApp::blurScreen(const QString &screenName)
{
    for (WallpaperWindow *window : std::as_const(m_windows)) {
        if (window->screen()->name() == screenName) {
            window->setBlur(true);
        } else {
            window->setBlur(false);
        }
    }
}

#include "moc_wallpaperapp.cpp"
