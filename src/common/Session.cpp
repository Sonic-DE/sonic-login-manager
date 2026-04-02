/***************************************************************************
 * SPDX-FileCopyrightText: 2025 David Edmundson <davidedmundson@kde.org>
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

#include "Session.h"

#include <KConfig>
#include <KConfigGroup>
#include <QFile>

namespace PLASMALOGIN
{

Session Session::create(const QString &name)
{
    QString fileName = name;
    if (!name.endsWith(".desktop")) {
        fileName = name + QStringLiteral(".desktop");
    }

    QString filePath = QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("/xsessions/") + fileName);
    if (!filePath.isEmpty() && QFile::exists(filePath)) {
        auto config = KSharedConfig::openConfig(filePath, KConfig::SimpleConfig);
        return Session(config);
    } else {
        return Session();
    }
}

Session::Session()
    : Session(KSharedConfigPtr())
{
}

Session::Session(KSharedConfigPtr desktopFile)
    : m_desktopFile(desktopFile)
{
}

bool Session::isValid() const
{
    return m_desktopFile;
}

QString Session::name() const
{
    Q_ASSERT(isValid());
    if (!isValid()) {
        return QString();
    }
    return m_desktopFile->group(QStringLiteral("Desktop Entry")).readEntry(QStringLiteral("Name"));
}

QString Session::fileName() const
{
    Q_ASSERT(isValid());
    if (!isValid()) {
        return QString();
    }
    return m_desktopFile->name();
}

QString Session::desktopSession() const
{
    Q_ASSERT(isValid());
    if (!isValid()) {
        return QString();
    }
    return m_desktopFile->name();
}

QString Session::exec() const
{
    Q_ASSERT(isValid());
    if (!isValid()) {
        return QString();
    }
    return m_desktopFile->group(QStringLiteral("Desktop Entry")).readEntry(QStringLiteral("Exec"));
}

QString Session::desktopNames() const
{
    Q_ASSERT(isValid());
    if (!isValid()) {
        return QString();
    }
    return m_desktopFile->group(QStringLiteral("Desktop Entry")).readEntry(QStringLiteral("DesktopNames"));
}

} // namespace PLASMALOGIN
