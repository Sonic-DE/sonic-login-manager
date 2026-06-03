/***************************************************************************
 * SPDX-FileCopyrightText: 2014 Pier Luigi Fiorini <pierluigi.fiorini@gmail.com>
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

#ifndef SONICLOGIN_MESSAGEHANDLER_H
#define SONICLOGIN_MESSAGEHANDLER_H

#include "Constants.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

#include <cerrno>
#include <cstring>
#include <stdio.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>

namespace SONICLOGIN
{
inline void ensureLogFileExists(const QString &s_logFilePath)
{
    // Only check/create the file, not the directory (directory is created at install time)
    QFile logFile(s_logFilePath);
    if (!logFile.exists()) {
        if (logFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            logFile.close();
        }
        chmod(s_logFilePath.toUtf8().constData(), 0666);
    }
}

// Helper to determine if stdout/stderr are actively being piped, socket-connected, or captured to a file
inline bool isStreamLogged()
{
    struct stat stdoutStat;
    struct stat stderrStat;

    // Fetch descriptor states for stdout (1) and stderr (2)
    if (fstat(1, &stdoutStat) < 0 || fstat(2, &stderrStat) < 0) {
        return false; // Error reading descriptors, fallback to local log file
    }

    // If it's an interactive terminal (TTY), it's not being captured by a supervisor daemon
    if (isatty(1) || isatty(2)) {
        return false;
    }

    // Verify if they are directed to a Pipe (S_ISFIFO) or a Socket (S_ISSOCK).
    // Runit, s6, and systemd use pipes/sockets to funnel data directly into their logging utilities.
    bool stdoutIsPipeOrSocket = S_ISFIFO(stdoutStat.st_mode) || S_ISSOCK(stdoutStat.st_mode);
    bool stderrIsPipeOrSocket = S_ISFIFO(stderrStat.st_mode) || S_ISSOCK(stderrStat.st_mode);

    // Verify if they are redirected to a regular file (S_ISREG).
    // OpenRC (via supervise-daemon) and Dinit point stdout directly to specified file paths.
    bool stdoutIsRegularFile = S_ISREG(stdoutStat.st_mode);
    bool stderrIsRegularFile = S_ISREG(stderrStat.st_mode);

    // If either stream is actively being piped, socketed, or written to a logfile by the supervisor, return true
    return (stdoutIsPipeOrSocket || stdoutIsRegularFile || stderrIsPipeOrSocket || stderrIsRegularFile);
}

[[maybe_unused]] static void messageHandler(QtMsgType type, const QString &category, const QString &msg)
{
    static bool loggingCapabilityChecked = false;
    static bool isStdoutLoggingValid = false;

    if (!loggingCapabilityChecked) {
        loggingCapabilityChecked = true;
        // Physically inspect file descriptors to see if any supervisor is collecting data
        isStdoutLoggingValid = isStreamLogged();
    }

    const QString timestamp = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
    QString systemdPrefix;
    QString logPriority;
    switch (type) {
    case QtInfoMsg:
        systemdPrefix = "<6> ";
        logPriority = QStringLiteral("(II) ");
        break;
    case QtWarningMsg:
        systemdPrefix = "<4> ";
        logPriority = QStringLiteral("(WW) ");
        break;
    case QtCriticalMsg:
        systemdPrefix = "<2> ";
        logPriority = QStringLiteral("(EE) ");
        break;
    case QtFatalMsg:
        systemdPrefix = "<1> ";
        logPriority = QStringLiteral("(EE) ");
        break;
    default:
        systemdPrefix = "<7> ";
        logPriority = QStringLiteral("(DD) ");
        break;
    }

    // If stdout/stderr are unhandled, try to use manual file
    ensureLogFileExists(QStringLiteral(LOG_FILE));
    static QFile logFile(QStringLiteral(LOG_FILE));
    if (logFile.open(QIODevice::Append | QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&logFile);

        out << timestamp << " " << logPriority << QStringLiteral("[%1] ").arg(category) << msg << "\n";
        out.flush();
        logFile.close();

        return;
    }

    QTextStream standardOut = QTextStream(stdout);

    standardOut << systemdPrefix << timestamp << " " << logPriority << ": " << QStringLiteral("[%1] ").arg(category) << msg << Qt::endl;
    standardOut.flush();
}
}

#endif // SONICLOGIN_MESSAGEHANDLER_H
