/***************************************************************************
 * SPDX-FileCopyrightText: 2021 Pier Luigi Fiorini <pierluigi.fiorini@gmail.com>
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

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#include <QEventLoop>
#include <QStandardPaths>
#include <QTimer>
#include <QVariantMap>

#include "Configuration.h"
#include "InitSystem.h"
#include "VirtualTerminal.h"

#include "xorguserhelper.h"

#include <fcntl.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace PLASMALOGIN
{

XOrgUserHelper::XOrgUserHelper(QObject *parent)
    : QObject(parent)
{
}

QProcessEnvironment XOrgUserHelper::sessionEnvironment() const
{
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("DISPLAY"), m_display);
    env.insert(QStringLiteral("XAUTHORITY"), m_xauth.authPath());
    env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("xcb"));
    return env;
}

QString XOrgUserHelper::display() const
{
    return m_display;
}

bool XOrgUserHelper::start(const QString &cmd)
{
    // Create xauthority
    QString xdgRuntimeDir = qEnvironmentVariable("XDG_RUNTIME_DIR");
    qDebug() << "XOrgUserHelper::start: XDG_RUNTIME_DIR=" << xdgRuntimeDir;
    // Fall back to /tmp/xauth_<uid> if XDG_RUNTIME_DIR is not set
    if (xdgRuntimeDir.isEmpty()) {
        xdgRuntimeDir = QStringLiteral("/tmp/xauth_%1").arg(::getuid());
        qDebug() << "XOrgUserHelper::start: XDG_RUNTIME_DIR empty, using fallback:" << xdgRuntimeDir;
    }
    m_xauth.setAuthDirectory(xdgRuntimeDir);
    if (!m_xauth.setup()) {
        qCritical() << "XOrgUserHelper::start: XAuth setup failed!";
        return false;
    }

    // Start server process
    if (!startServer(cmd)) {
        qCritical() << "XOrgUserHelper::start: X server start failed!";
        return false;
    }

    // Setup display
    startDisplayCommand();

    return true;
}

void XOrgUserHelper::stop()
{
    if (m_serverProcess) {
        qInfo("Stopping server...");
        m_serverProcess->terminate();
        if (!m_serverProcess->waitForFinished(5000)) {
            m_serverProcess->kill();
            m_serverProcess->waitForFinished(25000);
        }
        m_serverProcess->deleteLater();
        m_serverProcess = nullptr;

        displayFinished();
    }
}

bool XOrgUserHelper::startProcess(const QString &cmd, const QProcessEnvironment &env, QProcess **p)
{
    auto args = QProcess::splitCommand(cmd);
    const auto program = args.takeFirst();

    // Make sure to forward the input of this process into the Xorg
    // server, otherwise it will complain that only console users are allowed
    auto *process = new QProcess(this);
    process->setProcessEnvironment(env);
    process->setInputChannelMode(QProcess::ForwardedInputChannel);
    connect(process, &QProcess::readyReadStandardError, this, [process] {
        qWarning() << process->readAllStandardError();
    });
    connect(process, &QProcess::readyReadStandardOutput, this, [process] {
        qInfo() << process->readAllStandardOutput();
    });
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), process, [program](int exitCode, QProcess::ExitStatus exitStatus) {
        if (exitCode != 0 || exitStatus != QProcess::NormalExit) {
            qCritical() << "XOrgUserHelper: process" << program << "exited abnormally with exitCode:" << exitCode << ", quitting helper";
            QCoreApplication::instance()->quit();
        }
    });

    process->start(program, args);
    if (!process->waitForStarted(10000)) {
        qWarning("Failed to start \"%s\": %s", qPrintable(cmd), qPrintable(process->errorString()));
        return false;
    }

    if (p) {
        *p = process;
    }

    return true;
}

bool XOrgUserHelper::startServer(const QString &cmd)
{
    QString serverCmd = cmd;

    // Create pipe for communicating with X server
    // 0 == read from X, 1 == write to X
    int pipeFds[2];
    if (::pipe(pipeFds) != 0) {
        qCritical("Could not create pipe to start X server");
        return false;
    }

    // Do not leak the read endpoint to the X server process
    fcntl(pipeFds[0], F_SETFD, FD_CLOEXEC);

    // IMPORTANT: The write end of the pipe (pipeFds[1]) must NOT have O_CLOEXEC set,
    // otherwise Xorg will lose access to it the moment it execs.
    // QProcess passes the fd number via -displayfd, and Xorg writes to it.
    // We explicitly clear FD_CLOEXEC on the write end to ensure it survives exec.
    int writeFlags = fcntl(pipeFds[1], F_GETFD);
    if (writeFlags >= 0) {
        fcntl(pipeFds[1], F_SETFD, writeFlags & ~FD_CLOEXEC);
    }

    // Diagnostic: Log pipe FDs and their cloexec status
    int readFlags = fcntl(pipeFds[0], F_GETFD);
    int writeFlagsAfter = fcntl(pipeFds[1], F_GETFD);
    qInfo() << "XOrgUserHelper::startServer: pipe fds: read=" << pipeFds[0] << "write=" << pipeFds[1] << "read_cloexec=" << (readFlags & FD_CLOEXEC)
            << "write_cloexec=" << (writeFlagsAfter & FD_CLOEXEC);

    // Server environment
    auto serverEnv = QProcessEnvironment::systemEnvironment();

    // Diagnostic: Log critical environment variables for logind/elogind session inheritance
    qInfo() << "XOrgUserHelper::startServer: Environment diagnostics:"
            << "XDG_RUNTIME_DIR=" << serverEnv.value(QStringLiteral("XDG_RUNTIME_DIR")) << "XDG_SEAT=" << serverEnv.value(QStringLiteral("XDG_SEAT"))
            << "XDG_SESSION_ID=" << serverEnv.value(QStringLiteral("XDG_SESSION_ID")) << "XDG_VTNR=" << serverEnv.value(QStringLiteral("XDG_VTNR"))
            << "DBUS_SESSION_BUS_ADDRESS=" << serverEnv.value(QStringLiteral("DBUS_SESSION_BUS_ADDRESS"))
            << "ELOGIND_SESSION_ID=" << serverEnv.value(QStringLiteral("ELOGIND_SESSION_ID"))
            << "XDG_SESSION_TYPE=" << serverEnv.value(QStringLiteral("XDG_SESSION_TYPE"))
            << "XDG_SESSION_CLASS=" << serverEnv.value(QStringLiteral("XDG_SESSION_CLASS"));
    // Diagnostic: Log current uid/gid and groups
    qInfo() << "XOrgUserHelper::startServer: Process diagnostics:"
            << "uid=" << ::getuid() << "euid=" << ::geteuid() << "gid=" << ::getgid();

    // Detect if we're using elogind (not systemd-logind)
    bool isElogind = false;
    QProcess loginctlProcess;
    loginctlProcess.start(QStringLiteral("loginctl"), {QStringLiteral("--version")});
    if (loginctlProcess.waitForFinished(2000)) {
        QString output = QString::fromLocal8Bit(loginctlProcess.readAllStandardOutput());
        if (output.contains(QLatin1String("elogind"), Qt::CaseInsensitive)) {
            isElogind = true;
            qInfo() << "XOrgUserHelper::startServer: Detected elogind via loginctl --version";
        }
    }

    // For elogind systems, we need to set up the VT properly before Xorg starts.
    // Xorg's xf86OpenConsole() calls VT_ACTIVATE which requires CAP_SYS_ADMIN.
    // Since we're running as an unprivileged user, we can't do VT_ACTIVATE directly.
    // Instead, we set the VT to KD_GRAPHICS mode and VT_PROCESS mode, which tells
    // the kernel that we're managing VT switches ourselves. This avoids the need
    // for VT_ACTIVATE.
    if (isElogind) {
        qInfo() << "XOrgUserHelper::startServer: Setting up VT for elogind";
        QString vtNr = serverEnv.value(QStringLiteral("XDG_VTNR"));
        if (!vtNr.isEmpty()) {
            QString ttyPath = QStringLiteral("/dev/tty%1").arg(vtNr);
            int vtFd = ::open(qPrintable(ttyPath), O_RDWR | O_NOCTTY);
            if (vtFd >= 0) {
                // Set VT to KD_GRAPHICS mode
                if (ioctl(vtFd, KDSETMODE, KD_GRAPHICS) == 0) {
                    qInfo() << "XOrgUserHelper::startServer: Set" << ttyPath << "to KD_GRAPHICS mode";
                } else {
                    qWarning() << "XOrgUserHelper::startServer: Failed to set KD_GRAPHICS mode on" << ttyPath << ":" << strerror(errno);
                }

                // Set VT to VT_PROCESS mode with dummy signal handlers
                // This tells the kernel we're managing VT switches
                vt_mode mode = {};
                mode.mode = VT_PROCESS;
                mode.relsig = SIGUSR1;
                mode.acqsig = SIGUSR1;
                if (ioctl(vtFd, VT_SETMODE, &mode) == 0) {
                    qInfo() << "XOrgUserHelper::startServer: Set" << ttyPath << "to VT_PROCESS mode";
                } else {
                    qWarning() << "XOrgUserHelper::startServer: Failed to set VT_PROCESS mode on" << ttyPath << ":" << strerror(errno);
                }

                ::close(vtFd);
            } else {
                qWarning() << "XOrgUserHelper::startServer: Failed to open" << ttyPath << ":" << strerror(errno);
            }
        }
        // Don't set XORG_RUN_AS_USER_OK=1 - let xorg-wrap handle VT operations
    } else {
        serverEnv.insert(QStringLiteral("XORG_RUN_AS_USER_OK"), QStringLiteral("1"));
    }

    // Append xauth and display fd to the command
    auto args = QStringList() << QStringLiteral("-auth") << m_xauth.authPath() << QStringLiteral("-displayfd") << QString::number(pipeFds[1]);

    // Append VT from environment
    // Xorg needs to know which VT to open, regardless of init system.
    QString vtNr = serverEnv.value(QStringLiteral("XDG_VTNR"));
    if (!vtNr.isEmpty()) {
        args << QStringLiteral("vt%1").arg(vtNr);
    }

    // Command string
    serverCmd += QLatin1Char(' ') + args.join(QLatin1Char(' '));

    // Start the server process
    qInfo("Running server: %s", qPrintable(serverCmd));
    if (!startProcess(serverCmd, serverEnv, &m_serverProcess)) {
        qCritical() << "XOrgUserHelper::startServer: startProcess failed, closing pipe";
        ::close(pipeFds[0]);
        ::close(pipeFds[1]);
        return false;
    }

    // Close the other side of pipe in our process, otherwise reading
    // from it may stuck even X server exit
    ::close(pipeFds[1]);

    // Read the display number from the pipe
    QFile readPipe;
    if (!readPipe.open(pipeFds[0], QIODevice::ReadOnly)) {
        qCritical("Failed to open pipe to start X Server");
        ::close(pipeFds[0]);
        return false;
    }
    QByteArray displayNumber = readPipe.readLine();
    if (displayNumber.size() < 2) {
        // X server gave nothing (or a whitespace)
        qCritical("Failed to read display number from pipe");
        ::close(pipeFds[0]);
        return false;
    }
    displayNumber.prepend(QByteArray(":"));
    displayNumber.remove(displayNumber.size() - 1, 1); // trim trailing whitespace
    m_display = QString::fromLocal8Bit(displayNumber);
    qDebug("X11 display: %s", qPrintable(m_display));
    Q_EMIT displayChanged(m_display);

    // Generate xauthority file
    // For the X server's copy, the display number doesn't matter.
    // An empty file would result in no access control!
    if (!m_xauth.addCookie(m_display)) {
        qCritical("Failed to write xauth file");
        return false;
    }

    // Close our pipe
    ::close(pipeFds[0]);

    return true;
}

void XOrgUserHelper::startDisplayCommand()
{
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("DISPLAY"), m_display);
    env.insert(QStringLiteral("XAUTHORITY"), m_xauth.authPath());

    // Display setup script
    auto cmd = DATA_INSTALL_DIR "/scripts/Xsetup";
    qInfo("Running display setup script: %s", cmd);
    QProcess *displayScript = nullptr;
    if (startProcess(cmd, env, &displayScript)) {
        if (!displayScript->waitForFinished(30000)) {
            displayScript->kill();
        }
        displayScript->deleteLater();
    }
}

void XOrgUserHelper::displayFinished()
{
    auto cmd = DATA_INSTALL_DIR "/scripts/Xstop";
    qInfo("Running display stop script: %s", cmd);
    QProcess *displayStopScript = nullptr;
    if (startProcess(cmd, sessionEnvironment(), &displayStopScript)) {
        if (!displayStopScript->waitForFinished(5000)) {
            displayStopScript->kill();
        }
        displayStopScript->deleteLater();
    }
}

void XOrgUserHelper::onSessionPropertiesChanged(const QString &interfaceName, const QVariantMap &changedProperties, const QStringList &invalidatedProperties)
{
    Q_UNUSED(invalidatedProperties);
    if (interfaceName != QStringLiteral("org.freedesktop.login1.Session")) {
        return;
    }
    auto it = changedProperties.constFind(QStringLiteral("Active"));
    if (it != changedProperties.constEnd()) {
        bool active = it.value().toBool();
        qInfo() << "XOrgUserHelper::onSessionPropertiesChanged: Active changed to" << active;
        if (active) {
            // Session is now active, quit the event loop if it's running
            // The event loop is stored in a member variable during waitForSessionActive()
            if (m_waitLoop) {
                m_waitLoop->quit();
            }
        }
    }
}

} // namespace PLASMALOGIN

#include "moc_xorguserhelper.cpp"
