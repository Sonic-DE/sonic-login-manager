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
#include <QStandardPaths>

#include "Configuration.h"
#include "LogindDBusTypes.h"
#include "xorguserhelper.h"

#include <fcntl.h>
#ifdef Q_OS_LINUX
#include <linux/capability.h>
#include <sys/capability.h>
#include <sys/prctl.h>
#endif
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace SONICLOGIN
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
    qInfo() << "XOrgUserHelper::start: Starting XOrgUserHelper";

    // Create xauthority
    QString xdgRuntimeDir = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (xdgRuntimeDir.isEmpty()) {
        xdgRuntimeDir = QStringLiteral("/tmp/xauth_%1").arg(::getuid());
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

bool XOrgUserHelper::startProcess(const QString &cmd, const QProcessEnvironment &env, QProcess **p, bool quitOnFinish)
{
    auto args = QProcess::splitCommand(cmd);
    const auto program = args.takeFirst();

    // Make sure to forward the input of this process into the Xorg
    // server, otherwise it will complain that only console users are allowed
    auto *process = new QProcess(this);
    process->setProcessEnvironment(env);
    process->setInputChannelMode(QProcess::ForwardedInputChannel);

#ifdef Q_OS_LINUX
    if (Logind::isAvailable() && Logind::isELogind()) {
        // Give the Xorg process CAP_SYS_TTY_CONFIG so it can perform VT ioctls.
        // We have CAP_SETPCAP ambient, so we can modify our own capability set.
        process->setChildProcessModifier([]() {
            auto dbg = [](const char *label, int ok) {
                ::write(STDERR_FILENO, label, strlen(label));
                const char *yn = ok ? "YES\n" : "NO\n";
                ::write(STDERR_FILENO, yn, strlen(yn));
            };
            int before_tty = prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_IS_SET, CAP_SYS_TTY_CONFIG, 0, 0);
            int before_pcap = prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_IS_SET, CAP_SETPCAP, 0, 0);
            dbg("XOrgUserHelper child: before capset ambient CAP_SYS_TTY_CONFIG=", before_tty == 1);
            dbg("XOrgUserHelper child: before capset ambient CAP_SETPCAP=", before_pcap == 1);
            struct __user_cap_header_struct capHeader = {_LINUX_CAPABILITY_VERSION_3, 0};
            struct __user_cap_data_struct capData[2] = {};
            if (capget(&capHeader, capData) == 0) {
                dbg("XOrgUserHelper child: capget eff=", capData[0].effective & (1U << CAP_SYS_TTY_CONFIG));
                dbg("XOrgUserHelper child: capget prm=", capData[0].permitted & (1U << CAP_SYS_TTY_CONFIG));
                dbg("XOrgUserHelper child: capget inh=", capData[0].inheritable & (1U << CAP_SYS_TTY_CONFIG));
                capData[0].effective |= (1U << CAP_SYS_TTY_CONFIG);
                capData[0].permitted |= (1U << CAP_SYS_TTY_CONFIG);
                capData[0].inheritable |= (1U << CAP_SYS_TTY_CONFIG);
                if (capset(&capHeader, capData) == 0) {
                    if (prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, CAP_SYS_TTY_CONFIG, 0, 0) < 0) {
                        const char msg[] = "XOrgUserHelper: Failed to raise CAP_SYS_TTY_CONFIG ambient\n";
                        ::write(STDERR_FILENO, msg, sizeof(msg) - 1);
                    }
                    int after_tty = prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_IS_SET, CAP_SYS_TTY_CONFIG, 0, 0);
                    dbg("XOrgUserHelper child: after ambient raise CAP_SYS_TTY_CONFIG=", after_tty == 1);
                } else {
                    const char msg[] = "XOrgUserHelper: capset failed\n";
                    ::write(STDERR_FILENO, msg, sizeof(msg) - 1);
                }
            } else {
                const char msg[] = "XOrgUserHelper: capget failed\n";
                ::write(STDERR_FILENO, msg, sizeof(msg) - 1);
            }
        });
    }
#endif

    // Helper lambda to filter and log Xorg output line by line
    // Only log errors (EE), warnings (WW), and key events - skip verbose informational output
    auto filterAndLogOutput = [process, program](QProcess::ProcessChannel channel) {
        QByteArray output = channel == QProcess::StandardError ? process->readAllStandardError() : process->readAllStandardOutput();
        QList<QByteArray> lines = output.split('\n');
        for (const QByteArray &line : lines) {
            if (line.isEmpty()) {
                continue;
            }
            // Only log lines that contain relevant information:
            // (EE) errors, (WW) warnings, (!!) notices, or lines about module loading failures
            if (line.contains("(EE)") || line.contains("(WW)") || line.contains("(!!)") || line.contains("Failed to load") || line.contains("exited")
                || line.contains("error") || line.contains("Error")) {
                qWarning("[%s] %s", qPrintable(program), line.constData());
            }
        }
    };

    connect(process, &QProcess::readyReadStandardError, this, [filterAndLogOutput] {
        filterAndLogOutput(QProcess::StandardError);
    });
    connect(process,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            process,
            [program, process, quitOnFinish](int exitCode, QProcess::ExitStatus exitStatus) {
                qCritical() << "XOrgUserHelper: process" << program << "finished with exitCode:" << exitCode << "exitStatus:" << exitStatus
                            << "error:" << process->error() << process->errorString();
                if (quitOnFinish) {
                    if (exitCode != 0 || exitStatus != QProcess::NormalExit) {
                        qCritical() << "XOrgUserHelper::startProcess: ABNORMAL EXIT - process" << program << "exited with exitCode:" << exitCode
                                    << "- calling QCoreApplication::quit()";
                        QCoreApplication::instance()->quit();
                    } else {
                        qWarning() << "XOrgUserHelper: process" << program << "exited normally with exitCode:" << exitCode
                                   << "- calling QCoreApplication::quit()";
                        QCoreApplication::instance()->quit();
                    }
                }
            });

    process->start(program, args);
    if (!process->waitForStarted(10000)) {
        qWarning("XOrgUserHelper::startProcess: Failed to start \"%s\": %s", qPrintable(cmd), qPrintable(process->errorString()));
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
        qCritical("XOrgUserHelper::startServer: Could not create pipe to start X server");
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

    // Do not leak the read endpoint to the X server process
    fcntl(pipeFds[0], F_SETFD, FD_CLOEXEC);

    // Server environment
    auto serverEnv = QProcessEnvironment::systemEnvironment();
    // Explicitly preserve XDG_SESSION_ID so it survives privilege drops and Qt filtering
    const QByteArray xdgSessionId = qgetenv("XDG_SESSION_ID");
    if (!xdgSessionId.isEmpty()) {
        serverEnv.insert(QStringLiteral("XDG_SESSION_ID"), QString::fromLocal8Bit(xdgSessionId));
    }
    // Set XORG_RUN_AS_USER_OK=1 to allow Xorg to run as an unprivileged user.
    serverEnv.insert(QStringLiteral("XORG_RUN_AS_USER_OK"), QStringLiteral("1"));

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
    qInfo("XOrgUserHelper::startServer: Running server: %s", qPrintable(serverCmd));
    if (!startProcess(serverCmd, serverEnv, &m_serverProcess)) {
        qCritical() << "XOrgUserHelper::startServer: startProcess failed, closing pipe";
        ::close(pipeFds[0]);
        ::close(pipeFds[1]);
        return false;
    }

    // THEORY 2: Check if m_serverProcess is null after startProcess
    qInfo("XOrgUserHelper::startServer: m_serverProcess=%p after startProcess", (void *)m_serverProcess);

    // Close the other side of pipe in our process, otherwise reading
    // from it may stuck even X server exit
    ::close(pipeFds[1]);

    // Read the display number from the pipe
    QFile readPipe;
    if (!readPipe.open(pipeFds[0], QIODevice::ReadOnly)) {
        qCritical("XOrgUserHelper::startServer: Failed to open pipe to start X Server");
        ::close(pipeFds[0]);
        return false;
    }
    QByteArray displayNumber = readPipe.readLine();
    if (displayNumber.size() < 2) {
        // X server gave nothing (or a whitespace)
        qCritical("XOrgUserHelper::startServer: Failed to read display number from pipe");
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
        qCritical("XOrgUserHelper::startServer: Failed to write xauth file");
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
    if (startProcess(cmd, env, &displayScript, false)) {
        // THEORY 1: Check if Xsetup script exit code causes helper to quit
        qInfo("XOrgUserHelper::startDisplayCommand: Xsetup finished with exitCode=%d", displayScript->exitCode());
        if (!displayScript->waitForFinished(30000)) {
            qWarning("XOrgUserHelper::startDisplayCommand: Xsetup timed out, killing");
            displayScript->kill();
        }
        displayScript->deleteLater();
    } else {
        qCritical("XOrgUserHelper::startDisplayCommand: startProcess for Xsetup failed!");
    }
}

void XOrgUserHelper::displayFinished()
{
    auto cmd = DATA_INSTALL_DIR "/scripts/Xstop";
    qInfo("XOrgUserHelper::displayFinished: Running display stop script: %s", cmd);
    QProcess *displayStopScript = nullptr;
    if (startProcess(cmd, sessionEnvironment(), &displayStopScript)) {
        if (!displayStopScript->waitForFinished(5000)) {
            displayStopScript->kill();
        }
        displayStopScript->deleteLater();
    }
}

} // namespace SONICLOGIN

#include "moc_xorguserhelper.cpp"
