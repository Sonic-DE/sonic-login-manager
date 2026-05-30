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

#ifndef SONICLOGIN_POWER_MANAGER_H
#define SONICLOGIN_POWER_MANAGER_H

#include <QObject>
#include <QVector>

#include "Messages.h"

namespace SONICLOGIN
{

// Forward declaration for private backend implementation
class PowerManagerBackend;

/**
 * @brief Power manager that queries system services (logind, ConsoleKit2, UPower)
 *
 * This class runs in the daemon (as root) and determines what power operations
 * are available on the system. It sends capabilities to the greeter via socket.
 *
 * Based on SDDM's PowerManager implementation.
 */
class PowerManager : public QObject
{
    Q_OBJECT

public:
    explicit PowerManager(QObject *parent = nullptr);
    ~PowerManager() override;

    Capabilities capabilities() const;

    void powerOff() const;
    void reboot() const;
    void suspend() const;
    void hibernate() const;
    void hybridSleep() const;

private:
    QVector<PowerManagerBackend *> m_backends;
};

} // namespace SONICLOGIN

#endif // SONICLOGIN_POWER_MANAGER_H
