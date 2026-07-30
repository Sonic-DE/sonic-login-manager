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

#include <QFile>
#include <QFileInfo>

#include <pwd.h>
#include <unistd.h>

namespace SONICLOGIN
{

static QString nativeXServerPath(const QString &configuredPath)
{
    const QFileInfo serverInfo(configuredPath);
    const QString canonicalPath = serverInfo.canonicalFilePath();
    if (canonicalPath.isEmpty() || !QFileInfo(canonicalPath).isExecutable()) {
        qCritical() << "XorgUserDisplayServer: X server is missing or not executable:" << configuredPath;
        return QString();
    }

    QFile server(canonicalPath);
    if (!server.open(QIODevice::ReadOnly)
        || server.read(4)
            != QByteArrayLiteral("\x7f"
                                 "ELF")) {
        qCritical() << "XorgUserDisplayServer: refusing non-native X server wrapper:" << configuredPath << "resolved to" << canonicalPath;
        return QString();
    }

    return canonicalPath;
}

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

    QString serverPath = nativeXServerPath(mainConfig.X11.ServerPath.get());
    if (serverPath.isEmpty()) {
        serverPath = nativeXServerPath(QStringLiteral(X_SERVER_EXECUTABLE));
    }
    if (serverPath.isEmpty()) {
        // Keep the display-server helper path active so an invalid override
        // cannot bypass Xorg startup and launch the greeter directly.
        serverPath = QStringLiteral("/nonexistent/soniclogin-invalid-xorg");
    }

    args << serverPath << mainConfig.X11.ServerArguments.get().split(QLatin1Char(' '), Qt::SkipEmptyParts) << QStringLiteral("-background")
         << QStringLiteral("none") << QStringLiteral("-seat") << display->seat()->name() << QStringLiteral("-noreset") << QStringLiteral("-keeptty")
         << QStringLiteral("-novtswitch") << QStringLiteral("-verbose") << QStringLiteral("3") << QStringLiteral("-logfile") << xorgLogFile;

    return args.join(QLatin1Char(' '));
}

} // namespace SONICLOGIN
