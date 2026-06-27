/***************************************************************************
 * SPDX-FileCopyrightText: 2013 Abdurrahman AVCI <abdurrahmanavci@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the
 * Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 ***************************************************************************/

#include "DaemonApp.h"

#include "DisplayManager.h"
#include "KcmIpcServer.h"
#include "LogindDBusTypes.h"
#include "PowerManager.h"
#include "SeatManager.h"
#include "SelfProvisioner.h"
#include "SignalHandler.h"

#include "MessageHandler.h"

#include <QDBusConnectionInterface>
#include <QDebug>
#include <QHostInfo>
#include <QMetaObject>
#include <QTimer>

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QTextStream>
#include <csignal>
#include <cstdlib>
#include <iostream>

namespace SONICLOGIN
{
DaemonApp *DaemonApp::self = nullptr;

void DaemonMessageHandler(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    messageHandler(type, QStringLiteral("SONICLOGIN DAEMON"), msg);
}
DaemonApp::DaemonApp(int &argc, char **argv)
    : QCoreApplication(argc, argv)
{
    // point instance to this
    self = this;

    qInstallMessageHandler(SONICLOGIN::DaemonMessageHandler);

    // log message
    qDebug() << "Initializing...";

    // set testing parameter
    m_testing = (arguments().indexOf(QStringLiteral("--test-mode")) != -1);

    // If ConsoleKit isn't started by the OS init system (FreeBSD, for instance),
    // we start it ourselves during the soniclogin startup
    if (!Logind::isAvailable()) {
        bool consoleKitServiceActivatable = false;
        QDBusReply<QStringList> activatableNamesReply = QDBusConnection::systemBus().interface()->activatableServiceNames();
        if (activatableNamesReply.isValid()) {
            consoleKitServiceActivatable = activatableNamesReply.value().contains(QStringLiteral("org.freedesktop.ConsoleKit"));
        }

        if (consoleKitServiceActivatable) {
            QDBusReply<bool> registeredReply = QDBusConnection::systemBus().interface()->isServiceRegistered(QStringLiteral("org.freedesktop.ConsoleKit"));
            if (registeredReply.isValid() && registeredReply.value() == false) {
                qDebug() << "Starting ConsoleKit as it is not yet running...";
                QDBusConnection::systemBus().interface()->startService(QStringLiteral("org.freedesktop.ConsoleKit"));
            }
        }
    }

    // create display manager
    m_displayManager = new DisplayManager(this);

    // create power manager
    m_powerManager = new PowerManager(this);

    // create seat manager
    m_seatManager = new SeatManager(this);

    // connect with display manager
    connect(m_seatManager, &SeatManager::seatCreated, m_displayManager, &DisplayManager::AddSeat);
    connect(m_seatManager, &SeatManager::seatRemoved, m_displayManager, &DisplayManager::RemoveSeat);

    // create signal handler
    m_signalHandler = new SignalHandler(this);

    // KCM IPC server (replaces the old KAuth/D-Bus helper). Root-only
    // socket; the setuid helper connects on the KCM's behalf after PAM auth.
    m_kcmIpcServer = new KcmIpcServer(this);
    if (!m_kcmIpcServer->start()) {
        qWarning() << "DaemonApp: KCM IPC server failed to start";
    }

    // Write PID file so the setuid helper can locate the daemon.
    QDir().mkpath(QStringLiteral(RUNTIME_DIR));
    {
        QFile pidFile(QStringLiteral(RUNTIME_DIR "/daemon.pid"));
        if (pidFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            pidFile.write(QByteArray::number(QCoreApplication::applicationPid()));
            pidFile.close();
        }
    }

    // quit when SIGINT, SIGTERM received, with diagnostic logging
    connect(m_signalHandler, &SignalHandler::sigintReceived, this, [this] {
        pid_t ppid = getppid();
        QString parentName = getProcessNameByPid(ppid);
        qWarning() << "DaemonApp: SIGINT received - diagnostic information:"
                   << "parentProcess(PPID)=" << ppid << "=" << parentName << "hostName=" << hostName() << "testing=" << m_testing
                   << "firstLoginLock=" << m_firstloginLock << "lastSessionId=" << m_lastSessionId << "displayManager=" << (void *)m_displayManager
                   << "seatManager=" << (void *)m_seatManager;
        quit();
    });
    connect(m_signalHandler, &SignalHandler::sigtermReceived, this, [this] {
        pid_t ppid = getppid();
        QString parentName = getProcessNameByPid(ppid);
        qWarning() << "DaemonApp: SIGTERM received - diagnostic information:"
                   << "parentProcess(PPID)=" << ppid << "=" << parentName << "hostName=" << hostName() << "testing=" << m_testing
                   << "firstLoginLock=" << m_firstloginLock << "lastSessionId=" << m_lastSessionId << "displayManager=" << (void *)m_displayManager
                   << "seatManager=" << (void *)m_seatManager;
        quit();
    });
    connect(this, &QCoreApplication::aboutToQuit, this, [] {
        pid_t ppid = getppid();
        QString parentName = getProcessNameByPid(ppid);
        qWarning() << "DaemonApp: aboutToQuit emitted - parent process (PPID=" << ppid << ") is" << parentName;
        QFile::remove(QStringLiteral(RUNTIME_DIR "/daemon.pid"));
    });

    // Add atexit handler to log when the application exits
    std::atexit([]() {
        // Note: This runs after main() exits, so we can't use Qt logging
        // Write to stderr to ensure it's captured
        fprintf(stderr, "DaemonApp: atexit handler called - application is exiting\n");
    });

    // Also add signal handlers for crash signals
    signal(SIGSEGV, [](int sig) {
        fprintf(stderr, "DaemonApp: Caught signal %d (SIGSEGV) - crashing!\n", sig);
        signal(sig, SIG_DFL);
        raise(sig);
    });
    signal(SIGABRT, [](int sig) {
        fprintf(stderr, "DaemonApp: Caught signal %d (SIGABRT) - crashing!\n", sig);
        signal(sig, SIG_DFL);
        raise(sig);
    });
    signal(SIGFPE, [](int sig) {
        fprintf(stderr, "DaemonApp: Caught signal %d (SIGFPE) - crashing!\n", sig);
        signal(sig, SIG_DFL);
        raise(sig);
    });
    signal(SIGQUIT, [](int sig) {
        fprintf(stderr, "DaemonApp: Caught signal %d (SIGQUIT) - initiating graceful shutdown\n", sig);
        // Use Qt's meta-object system to quit from the main thread
        QMetaObject::invokeMethod(QCoreApplication::instance(), "quit", Qt::QueuedConnection);
    });

    // log message
    qDebug() << "DaemonApp: Starting...";

    // initialize seats only after signals are connected
    m_seatManager->initialize();
}

QString DaemonApp::hostName() const
{
    return QHostInfo::localHostName();
}

DisplayManager *DaemonApp::displayManager() const
{
    return m_displayManager;
}

PowerManager *DaemonApp::powerManager() const
{
    return m_powerManager;
}

SeatManager *DaemonApp::seatManager() const
{
    return m_seatManager;
}

SignalHandler *DaemonApp::signalHandler() const
{
    return m_signalHandler;
}

KcmIpcServer *DaemonApp::kcmIpcServer() const
{
    return m_kcmIpcServer;
}

int DaemonApp::newSessionId()
{
    return m_lastSessionId++;
}

bool DaemonApp::tryLockFirstLogin()
{
    if (m_firstloginLock) {
        return false;
    }
    m_firstloginLock = true;

    QDBusMessage msg = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.systemd1"),
                                                      QStringLiteral("/org/freedesktop/systemd1"),
                                                      QStringLiteral("org.freedesktop.DBus.Properties"),
                                                      QStringLiteral("Get"));

    msg << QStringLiteral("org.freedesktop.systemd1.Manager") << QStringLiteral("SoftRebootsCount");

    const QDBusMessage reply = QDBusConnection::systemBus().call(msg);

    if (reply.type() == QDBusMessage::ErrorMessage) {
        qWarning() << "DBus error:" << reply.errorName() << "-" << reply.errorMessage();
        return false;
    }

    const QVariant soft_reboot_count = qvariant_cast<QDBusVariant>(reply.arguments().at(0)).variant();
    if (!soft_reboot_count.isValid()) {
        qWarning() << "DBus variant is invalid:" << reply;
        return false;
    }

    return soft_reboot_count.toUInt() == 0;
};
}

int main(int argc, char **argv)
{
    QStringList arguments;

    for (int i = 0; i < argc; i++) {
        arguments << QString::fromLocal8Bit(argv[i]);
    }

    if (arguments.contains(QStringLiteral("--help")) || arguments.contains(QStringLiteral("-h"))) {
        std::cout << "Usage: soniclogin [options]\n"
                  << "Options: \n"
                  << "  --test-mode         Start daemon in test mode" << std::endl;

        return EXIT_FAILURE;
    }

    // Self-provision: create user, directories, setup logging, clean up stale sockets
    // This MUST be done before DaemonApp construction since it needs root privileges
    {
        SONICLOGIN::SelfProvisioner provisioner;
        if (!provisioner.provision()) {
            std::cerr << "FATAL: Self-provisioning failed. Cannot continue." << std::endl;
            return EXIT_FAILURE;
        }
    }

    // create application
    SONICLOGIN::DaemonApp app(argc, argv);

    // run application
    return app.exec();
}

#include "moc_DaemonApp.cpp"
