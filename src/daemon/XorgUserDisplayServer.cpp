/***************************************************************************
 * SPDX-FileCopyrightText: 2021 Pier Luigi Fiorini <pierluigi.fiorini@gmail.com>
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

#include "XorgUserDisplayServer.h"
#include "Configuration.h"
#include "Display.h"
#include "InitSystem.h"
#include "LogindDBusTypes.h"
#include "Seat.h"

namespace SONICLOGIN
{

QString XorgUserDisplayServer::command(Display *display)
{
    QStringList args;
    args << mainConfig.X11.ServerPath.get() << mainConfig.X11.ServerArguments.get().split(QLatin1Char(' '), Qt::SkipEmptyParts) << QStringLiteral("-background")
         << QStringLiteral("none") << QStringLiteral("-seat") << display->seat()->name() << QStringLiteral("-noreset") << QStringLiteral("-keeptty")
         << QStringLiteral("-novtswitch");

    // Only add -sharevts when using elogind (not systemd-logind)
    // With systemd-logind, VT management is handled by logind and -sharevts
    // can cause permission denied errors on modesetting
    // elogind requires -sharevts because it doesn't handle VT sharing the same way
    if (Logind::isAvailable() && Logind::isELogind()) {
        args << QStringLiteral("-sharevts");
    }

    args << QStringLiteral("-verbose") << QStringLiteral("3");

    return args.join(QLatin1Char(' '));
}

} // namespace SONICLOGIN
