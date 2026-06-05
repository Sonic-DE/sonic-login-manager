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

[[maybe_unused]] static void messageHandler(QtMsgType type, const QString &category, const QString &msg)
{
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
