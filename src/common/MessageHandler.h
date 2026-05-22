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

#ifndef PLASMALOGIN_MESSAGEHANDLER_H
#define PLASMALOGIN_MESSAGEHANDLER_H

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

namespace PLASMALOGIN
{

static void standardLogger(QtMsgType type, const QString &msg)
{
    static QFile file(QStringLiteral(LOG_FILE));
    static bool s_openFailureLogged = false;
    static bool s_journalctlChecked = false;
    static bool s_hasJournalctl = false;

    // Detect journalctl availability once at first call
    if (!s_journalctlChecked) {
        s_journalctlChecked = true;
        // Check if journalctl exists on the system
        if (QFile::exists(QStringLiteral("/usr/bin/journalctl")) || QFile::exists(QStringLiteral("/bin/journalctl"))) {
            s_hasJournalctl = true;
        }
    }

    // Open syslog if not already opened
    openlog("plasmalogin", LOG_PID | LOG_CONS, LOG_AUTH);

    // Only use log file when journalctl is NOT available
    // When journalctl is present, syslog output goes to the journal
    if (!s_hasJournalctl && !file.isOpen()) {
        file.setFileName(QStringLiteral(LOG_FILE));
        // Ensure directory exists for login manager (runs as root before user session)
        QFileInfo info(QStringLiteral(LOG_FILE));
        QDir().mkpath(info.path());
        if (!file.open(QIODevice::Append | QIODevice::WriteOnly) && !s_openFailureLogged) {
            s_openFailureLogged = true;
            const int savedErrno = errno;

            QByteArray details;
            details += "[plasmalogin logger] failed to open log file";
            details += " path=";
            details += QFile::encodeName(QStringLiteral(LOG_FILE));
            details += " uid=" + QByteArray::number(getuid());
            details += " euid=" + QByteArray::number(geteuid());
            details += " gid=" + QByteArray::number(getgid());
            details += " egid=" + QByteArray::number(getegid());
            details += " qtError=\"" + file.errorString().toLocal8Bit() + "\"";
            details += " errno=" + QByteArray::number(savedErrno);
            details += " strerror=\"" + QByteArray(strerror(savedErrno)) + "\"";

            struct stat st;
            if (::stat(QFile::encodeName(QStringLiteral(LOG_FILE)).constData(), &st) == 0) {
                details += " fileMode(oct)=" + QByteArray::number(st.st_mode & 07777, 8);
                details += " fileUid=" + QByteArray::number(st.st_uid);
                details += " fileGid=" + QByteArray::number(st.st_gid);
            } else {
                details += " statErrno=" + QByteArray::number(errno);
            }

            details += "\n";
            fputs(details.constData(), stderr);
            fflush(stderr);
        }
    }

    // Convert Qt message type to syslog priority
    int syslogPriority = LOG_INFO;
    switch (type) {
    case QtDebugMsg:
        syslogPriority = LOG_DEBUG;
        break;
    case QtInfoMsg:
        syslogPriority = LOG_INFO;
        break;
    case QtWarningMsg:
        syslogPriority = LOG_WARNING;
        break;
    case QtCriticalMsg:
        syslogPriority = LOG_CRIT;
        break;
    case QtFatalMsg:
        syslogPriority = LOG_ALERT;
        break;
    default:
        break;
    }

    // Log to syslog (journalctl picks this up on systemd systems)
    syslog(syslogPriority, "%s", qPrintable(msg));

    // Only write to log file when journalctl is NOT available
    if (s_hasJournalctl) {
        return;
    }

    // create timestamp
    QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss.zzz"));

    // set log priority
    QString logPriority = QStringLiteral("(II)");
    switch (type) {
    case QtDebugMsg:
        break;
    case QtWarningMsg:
        logPriority = QStringLiteral("(WW)");
        break;
    case QtCriticalMsg:
    case QtFatalMsg:
        logPriority = QStringLiteral("(EE)");
        break;
    default:
        break;
    }

    // prepare log message
    QString logMessage = QStringLiteral("[%1] %2 %3\n").arg(timestamp).arg(logPriority).arg(msg);

    // log message
    if (file.isOpen()) {
        file.write(logMessage.toLocal8Bit());
        file.flush();
    } else {
        fputs(qPrintable(logMessage), stderr);
        fflush(stderr);
    }
}

static void messageHandler(QtMsgType type, const QString &prefix, const QString &msg)
{
    // prepend program name
    QString logMessage = prefix + msg;

    // log to file or stderr
    standardLogger(type, logMessage);
}

void DaemonMessageHandler(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    messageHandler(type, QStringLiteral("DAEMON: "), msg);
}

void HelperMessageHandler(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    messageHandler(type, QStringLiteral("HELPER: "), msg);
}

void GreeterMessageHandler(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    messageHandler(type, QStringLiteral("GREETER: "), msg);
}

void StartPlasmaMessageHandler(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    messageHandler(type, QStringLiteral("STARTPLASMA: "), msg);
}

void WallpaperMessageHandler(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    messageHandler(type, QStringLiteral("WALLPAPER: "), msg);
}
}

#endif // PLASMALOGIN_MESSAGEHANDLER_H
