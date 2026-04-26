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
#include "LogindDBusTypes.h"
#include "SeatManager.h"
#include "SignalHandler.h"

#include "MessageHandler.h"

#include <QDBusConnectionInterface>
#include <QDebug>
#include <QHostInfo>
#include <QMetaObject>
#include <QTimer>

#include <iostream>
#include <cstdlib>
#include <csignal>
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QProcess>
#include <QRegularExpression>

namespace PLASMALOGIN
{
DaemonApp *DaemonApp::self = nullptr;

DaemonApp::DaemonApp(int &argc, char **argv)
    : QCoreApplication(argc, argv)
{
    // point instance to this
    self = this;

    qInstallMessageHandler(PLASMALOGIN::DaemonMessageHandler);

    // log message
    qDebug() << "Initializing...";

    // set testing parameter
    m_testing = (arguments().indexOf(QStringLiteral("--test-mode")) != -1);

    // If ConsoleKit isn't started by the OS init system (FreeBSD, for instance),
    // we start it ourselves during the plasmalogin startup
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

    // create seat manager
    m_seatManager = new SeatManager(this);

    // connect with display manager
    connect(m_seatManager, &SeatManager::seatCreated, m_displayManager, &DisplayManager::AddSeat);
    connect(m_seatManager, &SeatManager::seatRemoved, m_displayManager, &DisplayManager::RemoveSeat);

    // create signal handler
    m_signalHandler = new SignalHandler(this);

    // quit when SIGINT, SIGTERM received, with diagnostic logging
    connect(m_signalHandler, &SignalHandler::sigintReceived, this, [this] {
        pid_t ppid = getppid();
        QString parentName = getProcessNameByPid(ppid);
        qWarning() << "DaemonApp: SIGINT received - diagnostic information:"
                   << "parentProcess(PPID)=" << ppid << "=" << parentName
                   << "hostName=" << hostName()
                   << "testing=" << m_testing
                   << "first=" << first
                   << "lastSessionId=" << m_lastSessionId
                   << "displayManager=" << (void *)m_displayManager
                   << "seatManager=" << (void *)m_seatManager;
        quit();
    });
    connect(m_signalHandler, &SignalHandler::sigtermReceived, this, [this] {
        pid_t ppid = getppid();
        QString parentName = getProcessNameByPid(ppid);
        qWarning() << "DaemonApp: SIGTERM received - diagnostic information:"
                   << "parentProcess(PPID)=" << ppid << "=" << parentName
                   << "hostName=" << hostName()
                   << "testing=" << m_testing
                   << "first=" << first
                   << "lastSessionId=" << m_lastSessionId
                   << "displayManager=" << (void *)m_displayManager
                   << "seatManager=" << (void *)m_seatManager;
        quit();
    });
    connect(this, &QCoreApplication::aboutToQuit, this, [] {
        pid_t ppid = getppid();
        QString parentName = getProcessNameByPid(ppid);
        qWarning() << "DaemonApp: aboutToQuit emitted - parent process (PPID=" << ppid << ") is" << parentName;
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
#ifdef Q_OS_FREEBSD
    // On FreeBSD, the parent shell process (sh) sends SIGQUIT to the daemon
    // during normal startup transitions. Ignore this signal to prevent
    // premature shutdown.
    signal(SIGQUIT, [](int sig) {
        pid_t pid = getpid();
        pid_t ppid = getppid();
        uid_t uid = getuid();
        fprintf(stderr, "DaemonApp: Ignoring signal %d (SIGQUIT) - PID=%d PPID=%d UID=%d (parent shell may send this during startup)\n", sig, pid, ppid, uid);
        // Do NOT quit - ignore the signal as it's typically from parent shell
    });
#else
    signal(SIGQUIT, [](int sig) {
        fprintf(stderr, "DaemonApp: Caught signal %d (SIGQUIT) - initiating graceful shutdown\n", sig);
        // Use Qt's meta-object system to quit from the main thread
        QMetaObject::invokeMethod(QCoreApplication::instance(), "quit", Qt::QueuedConnection);
    });
#endif

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

SeatManager *DaemonApp::seatManager() const
{
    return m_seatManager;
}

SignalHandler *DaemonApp::signalHandler() const
{
    return m_signalHandler;
}

int DaemonApp::newSessionId()
{
    return m_lastSessionId++;
}
}

int main(int argc, char **argv)
{
    QStringList arguments;

    for (int i = 0; i < argc; i++) {
        arguments << QString::fromLocal8Bit(argv[i]);
    }

    if (arguments.contains(QStringLiteral("--help")) || arguments.contains(QStringLiteral("-h"))) {
        std::cout << "Usage: plasmalogin [options]\n"
                  << "Options: \n"
                  << "  --test-mode         Start daemon in test mode" << std::endl;

        return EXIT_FAILURE;
    }

    // create application
    PLASMALOGIN::DaemonApp app(argc, argv);

    // run application
    return app.exec();
}

#include "moc_DaemonApp.cpp"
