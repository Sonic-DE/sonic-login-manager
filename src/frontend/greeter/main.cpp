/*
 * SPDX-FileCopyrightText: David Edmundson
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QGuiApplication>
#include <QObject>
#include <QQmlContext>
#include <QScreen>

#include <KLocalizedString>
#include <PlasmaQuick/QuickViewSharedEngine>
#include <kworkspace6/sessionmanagement.h>

#include "backend/GreeterProxy.h"
#include "mockbackend/MockGreeterProxy.h"

#include "MessageHandler.h"
#include "SignalHandler.h"

#include "blurscreenbridge.h"
#include "greetereventfilter.h"
#include "models/sessionmodel.h"
#include "models/usermodel.h"
#include "plasmaloginsettings.h"
#include "stateconfig.h"

#include <signal.h>

class LoginGreeter : public QObject
{
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
    void createWindowForScreen(QScreen *screen)
    {
        auto *window = new PlasmaQuick::QuickViewSharedEngine();
        window->QObject::setParent(this);
        window->setScreen(screen);
        window->setColor(s_testMode ? Qt::darkGray : Qt::transparent);

        connect(qApp, &QGuiApplication::screenRemoved, this, [window](QScreen *screenRemoved) {
            if (screenRemoved == window->screen()) {
                delete window;
            }
        });

        window->setGeometry(screen->geometry());
        connect(screen, &QScreen::geometryChanged, this, [window]() {
            window->setGeometry(window->screen()->geometry());
        });

        window->setResizeMode(PlasmaQuick::QuickViewSharedEngine::SizeRootObjectToView);

        window->setFlags(Qt::BypassWindowManagerHint);

        auto *greeterEventFilter = new GreeterEventFilter(this);
        window->installEventFilter(greeterEventFilter);
        window->rootContext()->setContextProperty(QStringLiteral("greeterEventFilter"), greeterEventFilter);

        window->setSource(QUrl("qrc:/qt/qml/org/kde/plasma/login/Main.qml"));
        window->show();

        // Raise greeter above wallpaper and ensure keyboard focus
        window->raise();
        window->requestActivate();
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

int main(int argc, char *argv[])
{
    // Install message handler to log to plasmalogin.log
    qInstallMessageHandler(PLASMALOGIN::GreeterMessageHandler);
    qDebug() << "Greeter main: Starting...";
    
    KLocalizedString::setApplicationDomain(QByteArrayLiteral("plasma-login"));

    QCommandLineParser parser;
    parser.addOption(QCommandLineOption(QStringLiteral("test"), QStringLiteral("Run in test mode")));
    parser.addHelpOption();

    qDebug() << "Greeter main: Creating QGuiApplication...";
    QGuiApplication app(argc, argv);
    qDebug() << "Greeter main: QGuiApplication created";

    PLASMALOGIN::SignalHandler signalHandler;
    QObject::connect(&signalHandler, &PLASMALOGIN::SignalHandler::sigtermReceived, &app, [] {
        pid_t ppid = getppid();
        QString parentName = PLASMALOGIN::getProcessNameByPid(ppid);
        qWarning() << "Greeter: Received SIGTERM - diagnostic information:"
                   << "parentProcess(PPID)=" << ppid << "=" << parentName
                   << "screens=" << QGuiApplication::screens().size()
                   << "testMode=" << LoginGreeter::testModeEnabled()
                   << "sonicloginSocket=" << qEnvironmentVariable("SONICLOGIN_SOCKET");
        QGuiApplication::instance()->exit(0);
    });
    signalHandler.addCustomSignal(SIGQUIT);
    QObject::connect(&signalHandler, &PLASMALOGIN::SignalHandler::customSignalReceived, &app, [](int signal) {
        if (signal == SIGQUIT) {
            pid_t ppid = getppid();
            QString parentName = PLASMALOGIN::getProcessNameByPid(ppid);
            qWarning() << "Greeter: Received SIGQUIT (signal 3) - diagnostic information:"
                       << "parentProcess(PPID)=" << ppid << "=" << parentName
                       << "screens=" << QGuiApplication::screens().size()
                       << "testMode=" << LoginGreeter::testModeEnabled()
                       << "sonicloginSocket=" << qEnvironmentVariable("SONICLOGIN_SOCKET");
            QGuiApplication::instance()->exit(0);
        }
    });
    
    parser.process(app);
    LoginGreeter::setTestModeEnabled(parser.isSet(QStringLiteral("test")));

    QQuickWindow::setDefaultAlphaBuffer(true);
    if (LoginGreeter::testModeEnabled()) {
        qDebug() << "Greeter main: Test mode enabled, using MockGreeterProxy";
        qmlRegisterSingletonInstance("org.kde.plasma.login", 0, 1, "Authenticator", new MockGreeterProxy);
    } else {
        qDebug() << "Greeter main: Using real GreeterProxy";
        qmlRegisterSingletonInstance("org.kde.plasma.login", 0, 1, "Authenticator", new PLASMALOGIN::GreeterProxy);
    }
    qmlRegisterSingletonInstance("org.kde.plasma.login", 0, 1, "SessionModel", new SessionModel);
    qmlRegisterSingletonInstance("org.kde.plasma.login", 0, 1, "UserModel", new UserModel);
    qmlRegisterSingletonInstance("org.kde.plasma.login", 0, 1, "SessionManagement", new SessionManagement());
    qmlRegisterSingletonInstance("org.kde.plasma.login", 0, 1, "Settings", &PlasmaLoginSettings::getInstance());
    qmlRegisterSingletonInstance("org.kde.plasma.login", 0, 1, "StateConfig", StateConfig::self());
    qmlRegisterSingletonInstance("org.kde.plasma.login", 0, 1, "BlurScreenBridge", new BlurScreenBridge);

    qDebug() << "Greeter main: Creating LoginGreeter...";
    LoginGreeter greeter;
    qDebug() << "Greeter main: Entering event loop...";
    return app.exec();
}

#include "main.moc"
