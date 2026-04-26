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
#include <QTimer>

#include <iostream>
#include <cstdlib>
#include <csignal>
#include <QFile>
#include <QTextStream>
#include <QDir>

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

    // quit when SIGINT, SIGTERM received
    connect(m_signalHandler, &SignalHandler::sigintReceived, this, [] {
        qWarning() << "DaemonApp: SIGINT received, quitting";
        quit();
    });
    connect(m_signalHandler, &SignalHandler::sigtermReceived, this, [] {
        qWarning() << "DaemonApp: SIGTERM received, quitting";
        quit();
    });
    connect(this, &QCoreApplication::aboutToQuit, this, [] {
        qWarning() << "DaemonApp: aboutToQuit emitted";
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
