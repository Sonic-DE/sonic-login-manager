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

#ifndef PLASMALOGIN_SIGNALHANDLER_H
#define PLASMALOGIN_SIGNALHANDLER_H

#include <QObject>
#include <QString>
#include <QFile>
#include <QIODevice>
#include <unistd.h>

#ifdef Q_OS_FREEBSD
#include <sys/sysctl.h>
#include <sys/user.h>
#endif

class QSocketNotifier;

namespace PLASMALOGIN
{
// Utility function to get process name by PID
inline QString getProcessNameByPid(pid_t pid)
{
#ifdef Q_OS_FREEBSD
    int mib[4];
    mib[0] = CTL_KERN;
    mib[1] = KERN_PROC;
    mib[2] = KERN_PROC_PID;
    mib[3] = pid;

    struct kinfo_proc proc;
    size_t len = sizeof(proc);
    if (sysctl(mib, 4, &proc, &len, NULL, 0) == 0 && len > 0) {
        return QString::fromLocal8Bit(proc.ki_comm);
    }
#else
    // Linux: read /proc/PID/comm
    QFile commFile(QStringLiteral("/proc/%1/comm").arg(pid));
    if (commFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString::fromLocal8Bit(commFile.readAll()).trimmed();
    }
#endif
    return QStringLiteral("<unknown>");
}

class SignalHandler : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY(SignalHandler)
public:
    SignalHandler(QObject *parent = 0);

    void addCustomSignal(int signal);

signals:
    void sighupReceived();
    void sigintReceived();
    void sigtermReceived();
    void customSignalReceived(int signal);

private slots:
    void handleSigint();
    void handleSigterm();
    void handleSigCustom();

private:
    static void initialize();
    static void intSignalHandler(int unused);
    static void termSignalHandler(int unused);
    static void customSignalHandler(int unused);

    QSocketNotifier *snint{nullptr};
    QSocketNotifier *snterm{nullptr};
    QSocketNotifier *sncustom{nullptr};
};
}
#endif // PLASMALOGIN_SIGNALHANDLER_H
