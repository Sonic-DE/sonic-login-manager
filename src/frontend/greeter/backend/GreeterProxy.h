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

#ifndef SDDM_GREETERPROXY_H
#define SDDM_GREETERPROXY_H

#include <QObject>

#include "Messages.h"

class QLocalSocket;

namespace SONICLOGIN
{
class SessionModel;

class GreeterProxyPrivate;
class GreeterProxy : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY(GreeterProxy)

    // Session capabilities properties
    Q_PROPERTY(bool canReboot READ canReboot NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool canShutdown READ canShutdown NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool canSuspend READ canSuspend NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool canHibernate READ canHibernate NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool canLogout READ canLogout NOTIFY capabilitiesChanged)

public:
    explicit GreeterProxy(QObject *parent = 0);
    ~GreeterProxy();

    void setSessionModel(SessionModel *model);

    // Capability getters
    bool canReboot() const
    {
        return m_capabilities & Capability::Reboot;
    }
    bool canShutdown() const
    {
        return m_capabilities & Capability::PowerOff;
    }
    bool canSuspend() const
    {
        return m_capabilities & Capability::Suspend;
    }
    bool canHibernate() const
    {
        return m_capabilities & Capability::Hibernate;
    }
    bool canLogout() const
    {
        return true;
    } // Always true for greeter

public slots:
    void login(const QString &user, const QString &password, const SONICLOGIN::SessionType sessionType, const QString &sessionFileName) const;
    void shutdown();
    void reboot();
    void suspend();
    void hibernate();

private slots:
    void readyRead();

signals:
    void informationMessage(const QString &message);
    void capabilitiesChanged();

    void socketDisconnected();
    void loginFailed();
    void loginSucceeded();

private:
    GreeterProxyPrivate *d{nullptr};
    Capabilities m_capabilities = Capability::None;
};
}

#endif // SDDM_GREETERPROXY_H
