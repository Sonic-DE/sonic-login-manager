/***************************************************************************
 * SPDX-FileCopyrightText: 2020 David Edmundson <davidedmundson@kde.org>
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

#include "LogindDBusTypes.h"

#include <QDBusMetaType>

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDebug>
#include <QFile>
#include <QTextStream>

class LogindPathInternal
{
public:
    LogindPathInternal();
    bool available = false;
    QString serviceName;
    QString managerPath;
    QString managerIfaceName;
    QString sessionIfaceName;
    QString seatIfaceName;
    QString userIfaceName;
};

LogindPathInternal::LogindPathInternal()
{
    qRegisterMetaType<NamedSeatPath>("NamedSeatPath");
    qDBusRegisterMetaType<NamedSeatPath>();

    qRegisterMetaType<NamedSeatPathList>("NamedSeatPathList");
    qDBusRegisterMetaType<NamedSeatPathList>();

    qRegisterMetaType<NamedSessionPath>("NamedSessionPath");
    qDBusRegisterMetaType<NamedSessionPath>();

    qRegisterMetaType<NamedSessionPathList>("NamedSessionPathList");
    qDBusRegisterMetaType<NamedSessionPathList>();

    qRegisterMetaType<SessionInfo>("SessionInfo");
    qDBusRegisterMetaType<SessionInfo>();

    qRegisterMetaType<SessionInfoList>("SessionInfoList");
    qDBusRegisterMetaType<SessionInfoList>();

    qRegisterMetaType<UserInfo>("UserInfo");
    qDBusRegisterMetaType<UserInfo>();

    qRegisterMetaType<UserInfoList>("UserInfoList");
    qDBusRegisterMetaType<UserInfoList>();

    if (QDBusConnection::systemBus().interface()->isServiceRegistered(QStringLiteral("org.freedesktop.login1"))) {
        if (Logind::isELogind()) {
            qDebug() << "elogind login interface found";
        } else {
            qDebug() << "logind login interface found";
        }
        available = true;
        serviceName = QStringLiteral("org.freedesktop.login1");
        managerPath = QStringLiteral("/org/freedesktop/login1");
        managerIfaceName = QStringLiteral("org.freedesktop.login1.Manager");
        seatIfaceName = QStringLiteral("org.freedesktop.login1.Seat");
        sessionIfaceName = QStringLiteral("org.freedesktop.login1.Session");
        userIfaceName = QStringLiteral("org.freedesktop.login1.User");
        return;
    }

    if (QDBusConnection::systemBus().interface()->isServiceRegistered(QStringLiteral("org.freedesktop.ConsoleKit"))) {
        qDebug() << "Console Kit login interface found";
        available = true;
        serviceName = QStringLiteral("org.freedesktop.ConsoleKit");
        managerPath = QStringLiteral("/org/freedesktop/ConsoleKit/Manager");
        managerIfaceName = QStringLiteral("org.freedesktop.ConsoleKit.Manager"); // note this doesn't match logind
        seatIfaceName = QStringLiteral("org.freedesktop.ConsoleKit.Seat");
        sessionIfaceName = QStringLiteral("org.freedesktop.ConsoleKit.Session");
        userIfaceName = QStringLiteral("org.freedesktop.ConsoleKit.User");
        return;
    }
    qDebug() << "No session manager found";
}

Q_GLOBAL_STATIC(LogindPathInternal, s_instance);

bool Logind::isAvailable()
{
    return s_instance->available;
}

QString Logind::serviceName()
{
    return s_instance->serviceName;
}

QString Logind::managerPath()
{
    return s_instance->managerPath;
}

QString Logind::managerIfaceName()
{
    return s_instance->managerIfaceName;
}

QString Logind::seatIfaceName()
{
    return s_instance->seatIfaceName;
}

QString Logind::sessionIfaceName()
{
    return s_instance->sessionIfaceName;
}

QString Logind::userIfaceName()
{
    return s_instance->userIfaceName;
}

bool Logind::isELogind()
{
    // systemd-logind uses /lib/systemd/systemd-logind
    // elogind uses /lib/elogind/elogind
    QString serviceFile = QStringLiteral("/usr/share/dbus-1/system-services/org.freedesktop.login1.service");

    // Read the service file to determine which backend is in use
    QFile file(serviceFile);
    if (file.open(QIODevice::ReadOnly)) {
        QTextStream stream(&file);
        while (!stream.atEnd()) {
            QString line = stream.readLine().trimmed();
            if (line.startsWith(QStringLiteral("Exec="))) {
                QString execPath = line.mid(5).trimmed();
                if (execPath.contains(QStringLiteral("/elogind/"))) {
                    return true; // elogind
                }
                return false; // systemd-logind
            }
        }
        file.close();
    }

    // Fallback: assume systemd-logind (most common)
    return false;
}
