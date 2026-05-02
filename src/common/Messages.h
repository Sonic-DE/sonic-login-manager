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

#ifndef PLASMALOGIN_MESSAGES_H
#define PLASMALOGIN_MESSAGES_H

#include <QFlags>

namespace PLASMALOGIN
{

/**
 * @brief Capability flags for power management operations
 *
 * These flags represent the available power management operations
 * that can be performed on the system.
 */
enum Capability {
    None = 0x00,
    PowerOff = 0x01,
    Reboot = 0x02,
    Suspend = 0x04,
    Hibernate = 0x08,
    HybridSleep = 0x10,
};
Q_DECLARE_FLAGS(Capabilities, Capability)

enum class GreeterMessages {
    Connect = 0,
    Login,
    PowerOff,
    Reboot,
    Suspend,
    Hibernate,
};

enum class DaemonMessages {
    HostName,
    LoginSucceeded,
    LoginFailed,
    InformationMessage,
    SessionCapabilities,  // Power management capabilities (sent when greeter connects)
};

enum class SessionType {
    X11 = 0,
    Wayland
};
}

Q_DECLARE_OPERATORS_FOR_FLAGS(PLASMALOGIN::Capabilities)

#endif // PLASMALOGIN_MESSAGES_H
