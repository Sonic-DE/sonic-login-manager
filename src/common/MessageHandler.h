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

static void standardLogger(QtMsgType type, const QString &msg)
{
    static bool journalctlChecked = false;
    static bool hasJournalctl = false;

    // Detect journalctl availability once at first call
    if (!journalctlChecked) {
        journalctlChecked = true;
        // Check if journalctl exists on the system
        if (!QStandardPaths::findExecutable(QStringLiteral("journalctl")).isEmpty()) {
            hasJournalctl = true;
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

    // Only write to syslog when journalctl is available
    if (hasJournalctl) {
        openlog("plasmalogin", LOG_PID | LOG_CONS, LOG_AUTH);
        syslog(syslogPriority, "%s", qPrintable(msg));
        return;
    }

    ensureLogFileExists(QStringLiteral(LOG_FILE));
    static QFile logFile(QStringLiteral(LOG_FILE));
    if (logFile.open(QIODevice::Append | QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&logFile);
        const QString timestamp = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
        QString logPriority;
        switch (type) {
        case QtInfoMsg:
            logPriority = QStringLiteral("(II) ");
            break;
        case QtDebugMsg:
            logPriority = QStringLiteral("(DD) ");
            break;
        case QtWarningMsg:
            logPriority = QStringLiteral("(WW) ");
            break;
        case QtCriticalMsg:
        case QtFatalMsg:
            logPriority = QStringLiteral("(EE) ");
            break;
        default:
            logPriority = QStringLiteral("");
            break;
        }

        // prepare log message
        out << timestamp << " " << logPriority << msg << "\n";
        out.flush();
        logFile.close();
    }
}

static void messageHandler(QtMsgType type, const QString &prefix, const QString &msg)
{
    // prepend program name
    QString logMessage = QStringLiteral("[") + prefix + QStringLiteral("] ") + msg;

    // log to file or stderr
    standardLogger(type, logMessage);
}

void DaemonMessageHandler(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    messageHandler(type, QStringLiteral("PLASMALOGIN DAEMON"), msg);
}

void HelperMessageHandler(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    messageHandler(type, QStringLiteral("PLASMALOGIN HELPER"), msg);
}

void GreeterMessageHandler(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    messageHandler(type, QStringLiteral("PLASMALOGIN GREETER"), msg);
}

void StartPlasmaMessageHandler(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    messageHandler(type, QStringLiteral("PLASMALOGIN STARTPLASMA"), msg);
}

void WallpaperMessageHandler(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    messageHandler(type, QStringLiteral("PLASMALOGIN WALLPAPER"), msg);
}

void X11UserHelperMessageHandler(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    PLASMALOGIN::messageHandler(type, QStringLiteral("X11 USER HELPER"), msg);
}
}

#endif // PLASMALOGIN_MESSAGEHANDLER_H
