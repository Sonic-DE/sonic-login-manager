/***************************************************************************
 * SPDX-FileCopyrightText: 2013 Abdurrahman AVCI <abdurrahmanavci@gmail.com>
 * SPDX-FileCopyrightText: 2026 Sonic Login Manager contributors
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

#include "PowerManager.h"

#include "Configuration.h"
#include "DaemonApp.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDebug>
#include <QFileInfo>
#include <QProcess>

namespace PLASMALOGIN
{

/**
 * @brief Abstract base class for power manager backends
 *
 * Provides a common interface for different power management systems
 * such as systemd-logind, ConsoleKit2, and UPower.
 */
class PowerManagerBackend
{
public:
    PowerManagerBackend()
    {
    }

    virtual ~PowerManagerBackend()
    {
    }

    virtual Capabilities capabilities() const = 0;

    virtual void powerOff() const = 0;
    virtual void reboot() const = 0;
    virtual void suspend() const = 0;
    virtual void hibernate() const = 0;
    virtual void hybridSleep() const = 0;
};

/**
 * @brief UPower backend implementation
 *
 * Provides power management capabilities through the UPower D-Bus interface.
 * Handles suspend and hibernate operations, with fallback to system commands
 * for power off and reboot.
 */
const QString UPOWER_PATH = QStringLiteral("/org/freedesktop/UPower");
const QString UPOWER_SERVICE = QStringLiteral("org.freedesktop.UPower");
const QString UPOWER_OBJECT = QStringLiteral("org.freedesktop.UPower");

class UPowerBackend : public PowerManagerBackend
{
public:
    UPowerBackend(const QString &service, const QString &path, const QString &interface)
    {
        m_interface = new QDBusInterface(service, path, interface, QDBusConnection::systemBus());
        qWarning() << "PowerManager: UPowerBackend created for service:" << service;
    }

    ~UPowerBackend()
    {
        delete m_interface;
    }

    Capabilities capabilities() const override
    {
        Capabilities caps = Capability::PowerOff | Capability::Reboot;

        QDBusReply<bool> reply;

        // suspend
        reply = m_interface->call(QStringLiteral("SuspendAllowed"));
        if (reply.isValid() && reply.value()) {
            caps |= Capability::Suspend;
            qWarning() << "PowerManager: UPower SuspendAllowed = true";
        } else {
            qWarning() << "PowerManager: UPower SuspendAllowed failed or false:" << reply.error().message();
        }

        // hibernate
        reply = m_interface->call(QStringLiteral("HibernateAllowed"));
        if (reply.isValid() && reply.value()) {
            caps |= Capability::Hibernate;
            qWarning() << "PowerManager: UPower HibernateAllowed = true";
        } else {
            qWarning() << "PowerManager: UPower HibernateAllowed failed or false:" << reply.error().message();
        }

        // return capabilities
        qWarning() << "PowerManager: UPowerBackend capabilities:" << caps;
        return caps;
    }

    /**
     * @brief Execute power off operation with systemd detection
     *
     * Attempts to use systemctl if systemd is detected, otherwise falls back
     * to traditional shutdown command.
     */
    void powerOff() const override
    {
        if (QFileInfo::exists("/usr/bin/systemctl")) {
            QProcess::execute("/usr/bin/systemctl", QStringList() << "poweroff");
        } else {
            QProcess::execute("/sbin/shutdown", QStringList() << "-p" << "now");
        }
    }

    /**
     * @brief Execute reboot operation with systemd detection
     *
     * Attempts to use systemctl if systemd is detected, otherwise falls back
     * to traditional shutdown command.
     */
    void reboot() const override
    {
        if (QFileInfo::exists("/usr/bin/systemctl")) {
            QProcess::execute("/usr/bin/systemctl", QStringList() << "reboot");
        } else {
            QProcess::execute("/sbin/shutdown", QStringList() << "-r" << "now");
        }
    }

    void suspend() const override
    {
        m_interface->call(QStringLiteral("Suspend"));
    }

    void hibernate() const override
    {
        m_interface->call(QStringLiteral("Hibernate"));
    }

    void hybridSleep() const override
    {
    }

private:
    QDBusInterface *m_interface{nullptr};
};

/**
 * @brief Seat manager backend implementation for login1 and ConsoleKit2
 *
 * Provides power management capabilities through either systemd-logind
 * or ConsoleKit2 D-Bus interfaces. Both services expose similar APIs
 * for power operations, making them interchangeable.
 */
const QString LOGIN1_SERVICE = QStringLiteral("org.freedesktop.login1");
const QString LOGIN1_PATH = QStringLiteral("/org/freedesktop/login1");
const QString LOGIN1_OBJECT = QStringLiteral("org.freedesktop.login1.Manager");

const QString CK2_SERVICE = QStringLiteral("org.freedesktop.ConsoleKit");
const QString CK2_PATH = QStringLiteral("/org/freedesktop/ConsoleKit/Manager");
const QString CK2_OBJECT = QStringLiteral("org.freedesktop.ConsoleKit.Manager");

class SeatManagerBackend : public PowerManagerBackend
{
public:
    SeatManagerBackend(const QString &service, const QString &path, const QString &interface)
    {
        m_interface = new QDBusInterface(service, path, interface, QDBusConnection::systemBus());
        qWarning() << "PowerManager: SeatManagerBackend created for service:" << service;
    }

    ~SeatManagerBackend()
    {
        delete m_interface;
    }

    Capabilities capabilities() const override
    {
        Capabilities caps = Capability::None;

        QDBusReply<QString> reply;

        // power off
        reply = m_interface->call(QStringLiteral("CanPowerOff"));
        if (reply.isValid() && (reply.value() == QLatin1String("yes"))) {
            caps |= Capability::PowerOff;
            qWarning() << "PowerManager:" << m_interface->service() << "CanPowerOff = yes";
        } else {
            qWarning() << "PowerManager:" << m_interface->service() << "CanPowerOff failed or no:" << reply.error().message() << "value:" << reply.value();
        }

        // reboot
        reply = m_interface->call(QStringLiteral("CanReboot"));
        if (reply.isValid() && (reply.value() == QLatin1String("yes"))) {
            caps |= Capability::Reboot;
            qWarning() << "PowerManager:" << m_interface->service() << "CanReboot = yes";
        } else {
            qWarning() << "PowerManager:" << m_interface->service() << "CanReboot failed or no:" << reply.error().message() << "value:" << reply.value();
        }

        // suspend
        reply = m_interface->call(QStringLiteral("CanSuspend"));
        if (reply.isValid() && (reply.value() == QLatin1String("yes"))) {
            caps |= Capability::Suspend;
            qWarning() << "PowerManager:" << m_interface->service() << "CanSuspend = yes";
        } else {
            qWarning() << "PowerManager:" << m_interface->service() << "CanSuspend failed or no:" << reply.error().message() << "value:" << reply.value();
        }

        // hibernate
        reply = m_interface->call(QStringLiteral("CanHibernate"));
        if (reply.isValid() && (reply.value() == QLatin1String("yes"))) {
            caps |= Capability::Hibernate;
            qWarning() << "PowerManager:" << m_interface->service() << "CanHibernate = yes";
        } else {
            qWarning() << "PowerManager:" << m_interface->service() << "CanHibernate failed or no:" << reply.error().message() << "value:" << reply.value();
        }

        // hybrid sleep
        reply = m_interface->call(QStringLiteral("CanHybridSleep"));
        if (reply.isValid() && (reply.value() == QLatin1String("yes"))) {
            caps |= Capability::HybridSleep;
            qWarning() << "PowerManager:" << m_interface->service() << "CanHybridSleep = yes";
        } else {
            qWarning() << "PowerManager:" << m_interface->service() << "CanHybridSleep failed or no:" << reply.error().message() << "value:" << reply.value();
        }

        // return capabilities
        qWarning() << "PowerManager: SeatManagerBackend (" << m_interface->service() << ") capabilities:" << caps;
        return caps;
    }

    void powerOff() const override
    {
        m_interface->call(QStringLiteral("PowerOff"), true);
    }

    void reboot() const override
    {
        m_interface->call(QStringLiteral("Reboot"), true);
    }

    void suspend() const override
    {
        m_interface->call(QStringLiteral("Suspend"), true);
    }

    void hibernate() const override
    {
        m_interface->call(QStringLiteral("Hibernate"), true);
    }

    void hybridSleep() const override
    {
        m_interface->call(QStringLiteral("HybridSleep"), true);
    }

private:
    QDBusInterface *m_interface{nullptr};
};

/**
 * @brief PowerManager constructor
 *
 * Initializes the power manager by detecting available D-Bus services
 * and creating appropriate backend instances for each available service.
 */
PowerManager::PowerManager(QObject *parent)
    : QObject(parent)
{
    qWarning() << "PowerManager: Initializing PowerManager...";

    QDBusConnectionInterface *interface = QDBusConnection::systemBus().interface();
    if (!interface) {
        qWarning() << "PowerManager: No system bus interface available!";
        return;
    }

    if (!interface->isServiceRegistered(LOGIN1_SERVICE)) {
        qWarning() << "PowerManager: Service" << LOGIN1_SERVICE << "is NOT registered on system bus";
    }
    if (!interface->isServiceRegistered(CK2_SERVICE)) {
        qWarning() << "PowerManager: Service" << CK2_SERVICE << "is NOT registered on system bus";
    }
    if (!interface->isServiceRegistered(UPOWER_SERVICE)) {
        qWarning() << "PowerManager: Service" << UPOWER_SERVICE << "is NOT registered on system bus";
    }

    // check if login1 interface exists
    if (interface->isServiceRegistered(LOGIN1_SERVICE)) {
        qWarning() << "PowerManager: Found login1 service, creating backend";
        m_backends << new SeatManagerBackend(LOGIN1_SERVICE, LOGIN1_PATH, LOGIN1_OBJECT);
    }

    // check if ConsoleKit2 interface exists
    if (interface->isServiceRegistered(CK2_SERVICE)) {
        qWarning() << "PowerManager: Found ConsoleKit2 service, creating backend";
        m_backends << new SeatManagerBackend(CK2_SERVICE, CK2_PATH, CK2_OBJECT);
    }

    // check if upower interface exists
    if (interface->isServiceRegistered(UPOWER_SERVICE)) {
        qWarning() << "PowerManager: Found UPower service, creating backend";
        m_backends << new UPowerBackend(UPOWER_SERVICE, UPOWER_PATH, UPOWER_OBJECT);
    }

    qWarning() << "PowerManager: Total backends created:" << m_backends.size();
}

PowerManager::~PowerManager()
{
    while (!m_backends.empty())
        delete m_backends.takeFirst();
}

Capabilities PowerManager::capabilities() const
{
    Capabilities caps = Capability::None;

    for (PowerManagerBackend *backend : m_backends) {
        Capabilities backendCaps = backend->capabilities();
        caps |= backendCaps;
        qWarning() << "PowerManager: Backend capabilities:" << backendCaps << "total now:" << caps;
    }

    qWarning() << "PowerManager: Final capabilities:" << caps;
    return caps;
}

void PowerManager::powerOff() const
{
    for (PowerManagerBackend *backend : m_backends) {
        if (backend->capabilities() & Capability::PowerOff) {
            backend->powerOff();
            break;
        }
    }
}

void PowerManager::reboot() const
{
    for (PowerManagerBackend *backend : m_backends) {
        if (backend->capabilities() & Capability::Reboot) {
            backend->reboot();
            break;
        }
    }
}

void PowerManager::suspend() const
{
    for (PowerManagerBackend *backend : m_backends) {
        if (backend->capabilities() & Capability::Suspend) {
            backend->suspend();
            break;
        }
    }
}

void PowerManager::hibernate() const
{
    for (PowerManagerBackend *backend : m_backends) {
        if (backend->capabilities() & Capability::Hibernate) {
            backend->hibernate();
            break;
        }
    }
}

void PowerManager::hybridSleep() const
{
    for (PowerManagerBackend *backend : m_backends) {
        if (backend->capabilities() & Capability::HybridSleep) {
            backend->hybridSleep();
            break;
        }
    }
}

} // namespace PLASMALOGIN
