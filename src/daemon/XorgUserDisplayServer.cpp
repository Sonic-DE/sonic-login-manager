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
 **************************************************************************/

#include "XorgUserDisplayServer.h"
#include "Configuration.h"
#include "Display.h"
#include "InitSystem.h"
#include "LogindDBusTypes.h"
#include "Seat.h"

#include "Constants.h"

#include <pwd.h>
#include <unistd.h>

namespace SONICLOGIN
{

QString XorgUserDisplayServer::command(Display *display, const QString &userName)
{
    QStringList args;
    QString xorgLogFile;

    if (userName.isEmpty()) {
        xorgLogFile = QStringLiteral(STATE_DIR) + QStringLiteral("/.local/state/Xorg.0.log");
    } else {
        QString userHome;
        struct passwd *pw = getpwnam(userName.toLocal8Bit().constData());
        if (pw && pw->pw_dir && pw->pw_dir[0] != '\0') {
            userHome = QString::fromLocal8Bit(pw->pw_dir);
        } else {
            userHome = QStringLiteral("/tmp");
            qWarning() << "XorgUserDisplayServer::command: user home for" << userName << "missing/empty, falling back to:" << userHome;
        }

        xorgLogFile = userHome + QStringLiteral("/.local/state/Xorg.0.log");
    }

    args << mainConfig.X11.ServerPath.get() << mainConfig.X11.ServerArguments.get().split(QLatin1Char(' '), Qt::SkipEmptyParts) << QStringLiteral("-background")
         << QStringLiteral("none") << QStringLiteral("-seat") << display->seat()->name() << QStringLiteral("-noreset") << QStringLiteral("-keeptty")
         << QStringLiteral("-novtswitch") << QStringLiteral("-verbose") << QStringLiteral("3") << QStringLiteral("-logfile") << xorgLogFile;

    return args.join(QLatin1Char(' '));
}

} // namespace SONICLOGIN
