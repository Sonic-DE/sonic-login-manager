/*
 * SPDX-FileCopyrightText: David Edmundson
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QGuiApplication>
#include <QHash>
#include <QIcon>
#include <QObject>
#include <QProcess>
#include <QQmlContext>
#include <QScreen>
#include <QTimer>

#include <KIconTheme>
#include <KLocalizedString>
#include <PlasmaQuick/QuickViewSharedEngine>

#include "backend/GreeterProxy.h"
#include "mockbackend/MockGreeterProxy.h"

#include "MessageHandler.h"
#include "SignalHandler.h"

#include "blurscreenbridge.h"
#include "greetereventfilter.h"
#include "models/sessionmodel.h"
#include "models/usermodel.h"
#include "sonicloginsettings.h"
#include "stateconfig.h"

#include <memory>
#include <signal.h>

#include <X11/Xcursor/Xcursor.h>
#include <X11/extensions/Xfixes.h>
#include <private/qtx11extras_p.h>

class LoginGreeter : public QObject {
    Q_OBJECT
public:
    explicit LoginGreeter(QObject *parent = nullptr)
        : QObject(parent)
    {
        connect(qApp, &QGuiApplication::screenAdded, this, [this](QScreen *screen) {
            createWindowForScreen(screen);
        });
        for (QScreen *screen : qApp->screens()) {
            createWindowForScreen(screen);
        }
    }
    static void setTestModeEnabled(bool testModeEnabled);
    static bool testModeEnabled();

private:
    // qmlRegisterSingletonInstance objects must stay on their original engine,
    // including while there are temporarily no screen windows.
    std::shared_ptr<QQmlEngine> m_engine;
    QHash<QScreen *, PlasmaQuick::QuickViewSharedEngine *> m_windows;

    void createWindowForScreen(QScreen *screen)
    {
        if (screen->geometry().isNull()) {
            qWarning() << "createWindowForScreen: Screen" << screen->name() << "has null geometry, deferring.";
            connect(screen, &QScreen::geometryChanged, this, [this, screen]() {
                if (!screen->geometry().isNull()) {
                    QObject::disconnect(sender());
                    createWindowForScreen(screen);
                }
            });
            return;
        }

        auto *window = m_windows.value(screen, nullptr);
        if (window) {
            if (window->geometry() != screen->geometry()) {
                window->setGeometry(screen->geometry());
                window->raise();
                window->show();
            }
            return;
        }

        window = new PlasmaQuick::QuickViewSharedEngine();
        if (!m_engine) {
            m_engine = window->engine();
        }
        window->QObject::setParent(this);
        window->setScreen(screen);
        window->setColor(s_testMode ? Qt::darkGray : Qt::transparent);

        window->setGeometry(screen->geometry());
        window->setResizeMode(PlasmaQuick::QuickViewSharedEngine::SizeRootObjectToView);
        window->setFlags(Qt::BypassWindowManagerHint);

        window->setSource(QUrl("qrc:/qt/qml/org/kde/sonic/login/Main.qml"));
        window->show();

        // Raise greeter above wallpaper and ensure keyboard focus
        window->raise();
        window->requestActivate();

        m_windows.insert(screen, window);

        connect(qApp, &QGuiApplication::screenRemoved, window, [this, window, screen](QScreen *screenRemoved) {
            if (screenRemoved == screen) {
                m_windows.remove(screen);
                delete window;
            }
        });

        connect(screen, &QScreen::geometryChanged, window, [window]() {
            if (auto *s = window->screen()) {
                window->setGeometry(s->geometry());
            }
        });
    }

    static bool s_testMode;
};

bool LoginGreeter::s_testMode = false;

void LoginGreeter::setTestModeEnabled(bool testModeEnabled)
{
    s_testMode = testModeEnabled;
}

bool LoginGreeter::testModeEnabled()
{
    return s_testMode;
}

void GreeterMessageHandler(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    SONICLOGIN::messageHandler(type, QStringLiteral("SONICLOGIN GREETER"), msg);
}

int main(int argc, char *argv[])
{
    // Install message handler to log to soniclogin.log
    qInstallMessageHandler(GreeterMessageHandler);
    qDebug() << "Greeter main: Starting...";

    KLocalizedString::setApplicationDomain(QByteArrayLiteral("sonic_login"));

    QCommandLineParser parser;
    parser.addOption(QCommandLineOption(QStringLiteral("test"), QStringLiteral("Run in test mode")));
    parser.addHelpOption();

    // The shared workspace footer owns the keyboard backend. Qt requires its
    // input-method plugin to be selected before constructing the application.
    qputenv("QT_IM_MODULE", "sonicvirtualkeyboard");
    qunsetenv("QT_VIRTUALKEYBOARD_DESKTOP_DISABLE");
    // Keep symbolic icons color-aware even in the standalone test UI, without
    // replacing a configured icon theme or relying on session defaults.
    QIcon::setFallbackThemeName(QStringLiteral("silver"));
    KIconTheme::initTheme();
    QGuiApplication app(argc, argv);

    SONICLOGIN::SignalHandler signalHandler;
    QObject::connect(&signalHandler, &SONICLOGIN::SignalHandler::sigtermReceived, &app, [] {
        pid_t ppid = getppid();
        QString parentName = SONICLOGIN::getProcessNameByPid(ppid);
        qWarning() << "Greeter: Received SIGTERM - diagnostic information:"
                   << "parentProcess(PPID)=" << ppid << "=" << parentName << "screens=" << QGuiApplication::screens().size()
                   << "testMode=" << LoginGreeter::testModeEnabled() << "sonicloginSocket=" << qEnvironmentVariable("SONICLOGIN_SOCKET");
        QGuiApplication::instance()->exit(0);
    });
    signalHandler.addCustomSignal(SIGQUIT);
    QObject::connect(&signalHandler, &SONICLOGIN::SignalHandler::customSignalReceived, &app, [](int signal) {
        if (signal == SIGQUIT) {
            pid_t ppid = getppid();
            QString parentName = SONICLOGIN::getProcessNameByPid(ppid);
            qWarning() << "Greeter: Received SIGQUIT (signal 3) - diagnostic information:"
                       << "parentProcess(PPID)=" << ppid << "=" << parentName << "screens=" << QGuiApplication::screens().size()
                       << "testMode=" << LoginGreeter::testModeEnabled() << "sonicloginSocket=" << qEnvironmentVariable("SONICLOGIN_SOCKET");
            QGuiApplication::instance()->exit(0);
        }
    });

    parser.process(app);
    LoginGreeter::setTestModeEnabled(parser.isSet(QStringLiteral("test")));
    // Service mode must survive monitor removal; standalone tests still close normally.
    app.setQuitOnLastWindowClosed(LoginGreeter::testModeEnabled());

    QQuickWindow::setDefaultAlphaBuffer(true);
    SONICLOGIN::GreeterProxy *authenticator = nullptr;
    if (LoginGreeter::testModeEnabled()) {
        qDebug() << "Greeter main: Test mode enabled, using MockGreeterProxy";
        qmlRegisterSingletonInstance("org.kde.sonic.login", 0, 1, "Authenticator", new MockGreeterProxy);
    } else {
        authenticator = new SONICLOGIN::GreeterProxy();
        qmlRegisterSingletonInstance("org.kde.sonic.login", 0, 1, "Authenticator", authenticator);
    }
    qmlRegisterSingletonInstance("org.kde.sonic.login", 0, 1, "SessionModel", new SessionModel);
    qmlRegisterSingletonInstance("org.kde.sonic.login", 0, 1, "UserModel", new UserModel);

    qmlRegisterSingletonInstance("org.kde.sonic.login", 0, 1, "Settings", &SonicLoginSettings::getInstance());
    qmlRegisterSingletonInstance("org.kde.sonic.login", 0, 1, "StateConfig", StateConfig::self());
    qmlRegisterSingletonInstance("org.kde.sonic.login", 0, 1, "BlurScreenBridge", new BlurScreenBridge);
    qmlRegisterType<GreeterEventFilter>("org.kde.sonic.login", 0, 1, "GreeterEventFilter");

    LoginGreeter greeter;

    return app.exec();
}

#include "main.moc"
