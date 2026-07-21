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

#include "Seat.h"

#include "Configuration.h"
#include "DaemonApp.h"
#include "InitSystem.h"
#include "LogindDBusTypes.h"
#include "VirtualTerminal.h"

#include <QDebug>
#include <QTimer>

#include <Login1Manager.h>
#include <Login1Seat.h>
#include <Login1Session.h>
#include <errno.h>
#include <functional>
#include <optional>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace SONICLOGIN {
Seat::Seat(const QString &name, QObject *parent)
    : QObject(parent)
    , m_name(name)
{
    createDisplay();
}

const QString &Seat::name() const
{
    return m_name;
}

VirtualTerminal::Terminal Seat::availableVt() const
{
    const int initialVt = getInitialVt();
    // Use the correct TTY device path for the platform.
    const QString ttyPath = VirtualTerminal::path(initialVt);

    // Log VT device ownership and permissions for diagnostic purposes.
    struct stat st;
    if (::stat(qPrintable(ttyPath), &st) == 0) {
        qInfo() << "Seat::availableVt: VT" << initialVt << "device" << ttyPath << "uid=" << st.st_uid << "gid=" << st.st_gid
                << "mode=" << QString::number(st.st_mode & 0777, 8);
    } else {
        qWarning() << "Seat::availableVt: cannot stat" << ttyPath << ":" << strerror(errno);
    }

    // Keep the opened device as the reservation instead of closing an access probe.
    auto terminal = VirtualTerminal::openVt(initialVt);
    if (terminal.isValid()) {
        return terminal;
    }

    const auto vt = VirtualTerminal::currentVt();
    if (vt > 0) {
        terminal = VirtualTerminal::openVt(vt);
        if (terminal.isValid()) {
            return terminal;
        }
    }

    return VirtualTerminal::setUpNewVt();
}

QString Seat::reusableSessionId(const QString &user) const
{
    if (!Logind::isAvailable()) {
        return {};
    }

    OrgFreedesktopLogin1ManagerInterface manager(Logind::serviceName(), Logind::managerPath(), QDBusConnection::systemBus());
    auto reply = manager.ListSessions();
    reply.waitForFinished();

    for (const SessionInfo &sessionInfo : reply.value()) {
        if (sessionInfo.userName != user) {
            continue;
        }

        OrgFreedesktopLogin1SessionInterface session(Logind::serviceName(), sessionInfo.sessionPath.path(), QDBusConnection::systemBus());
        if (session.service() == QLatin1String("soniclogin") && session.state() == QLatin1String("online")) {
            return sessionInfo.sessionId;
        }
    }

    return {};
}

void Seat::activateSession(const QString &sessionId) const
{
    if (sessionId.isNull()) {
        return;
    }

    if (!Logind::isAvailable()) {
        qDebug() << "Session reuse requested but logind is not available";
        return;
    }

    OrgFreedesktopLogin1ManagerInterface manager(Logind::serviceName(), Logind::managerPath(), QDBusConnection::systemBus());
    manager.UnlockSession(sessionId);
    manager.ActivateSession(sessionId);
}

std::optional<int> Seat::vtForSession(const QString &sessionId) const
{
    if (!Logind::isAvailable() || sessionId.isEmpty()) {
        return std::nullopt;
    }

    OrgFreedesktopLogin1ManagerInterface manager(Logind::serviceName(), Logind::managerPath(), QDBusConnection::systemBus());
    if (!manager.isValid()) {
        return std::nullopt;
    }

    auto sessionPath = manager.GetSession(sessionId);
    OrgFreedesktopLogin1SessionInterface session(Logind::serviceName(), sessionPath.value().path(), QDBusConnection::systemBus());
    return QStringView(session.tTY()).mid(3).toInt(); // we need to convert ttyN to N
}

void Seat::createDisplay()
{
    // reload config if needed
    mainConfig.load();

    // create a new display
    qDebug() << "Adding new display...";
    Display *display = new Display(this);

    // restart display on stop
    connect(display, &Display::stopped, this, &Seat::displayStopped);
    connect(display, &Display::displayServerFailed, this, [this, display] {
        removeDisplay(display);
        qWarning() << "Failed to launch a display server";
        QCoreApplication::instance()->exit(12);
    });

    // add display to the list
    m_displays << display;

    // start the display
    startDisplay(display);
}

void Seat::startDisplay(Display *display, int tryNr)
{
    if (display->start()) {
        return;
    }

    // It's possible that the system isn't ready yet (driver not loaded,
    // device not enumerated, ...). It's not possible to tell when that changes,
    // so try a few times with a delay in between.
    qWarning() << "Attempt" << tryNr << "starting the Display server on vt" << display->terminalId() << "failed";

    if (tryNr >= 3) {
        qCritical() << "Could not start Display server on vt" << display->terminalId();
        // Use the same cleanup and daemon-exit path as an X11 helper failure.
        Q_EMIT display->displayServerFailed();
        return;
    }

    QTimer::singleShot(2000, display, [this, display, tryNr] {
        startDisplay(display, tryNr + 1);
    });
}

void Seat::removeDisplay(Display *display)
{
    qDebug() << "Removing display" << display << "...";

    // remove display from list
    m_displays.removeAll(display);

    // stop the display
    display->blockSignals(true);
    display->stop();
    display->blockSignals(false);

    // delete display
    display->deleteLater();
}

void Seat::displayStopped()
{
    Display *display = qobject_cast<Display *>(sender());
    std::optional<int> nextVt = vtForSession(display->reuseSessionId());

    // remove display
    removeDisplay(display);

    // restart otherwise
    if (m_displays.isEmpty()) {
        createDisplay();
    }
    // If there is still a session running on some display,
    // switch to last display in display vector.
    // Set vt_auto to true, so let the kernel handle the
    // vt switch automatically (VT_AUTO).
    else if (!nextVt) {
        int disp = m_displays.last()->terminalId();
        if (disp != -1) {
            nextVt = disp;
        }
    }

    if (nextVt) {
        VirtualTerminal::jumpToVt(*nextVt, true);
    }
}

bool Seat::canTTY()
{
    OrgFreedesktopLogin1ManagerInterface manager(Logind::serviceName(), Logind::managerPath(), QDBusConnection::systemBus());
    if (manager.isValid()) {
        auto seatPath = manager.GetSeat(m_name);
        OrgFreedesktopLogin1SeatInterface seatIface(Logind::serviceName(), seatPath.value().path(), QDBusConnection::systemBus());
        if (seatIface.property("CanTTY").isValid()) {
            return seatIface.canTTY();
        }
    }

    return m_name.compare(QStringLiteral("seat0"), Qt::CaseInsensitive) == 0 && access(VirtualTerminal::defaultVtPath, F_OK) == 0;
}

bool Seat::tryLockFirstLogin()
{
    // One first-login token per seat, so each seat independently gets its
    // first-boot login; the machine-global soft-reboot check stays on the
    // daemon (isFirstBoot).
    if (m_firstLoginLock) {
        return false;
    }
    m_firstLoginLock = true;
    return daemonApp->isFirstBoot();
}
}

#include "moc_Seat.cpp"
