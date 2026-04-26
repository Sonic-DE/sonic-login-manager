/***************************************************************************
 * SPDX-FileCopyrightText: 2015 Pier Luigi Fiorini <pierluigi.fiorini@gmail.com>
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

#include <QDebug>

#include "VirtualTerminal.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#ifdef __FreeBSD__
#include <sys/consio.h>
#else
#include <linux/kd.h>
#include <linux/vt.h>
#endif
#include <QFileInfo>
#include <qscopeguard.h>
#include <sys/ioctl.h>

#define RELEASE_DISPLAY_SIGNAL (SIGRTMAX)
#define ACQUIRE_DISPLAY_SIGNAL (SIGRTMAX - 1)

namespace PLASMALOGIN
{
namespace VirtualTerminal
{
#ifdef __FreeBSD__
const char *defaultVtPath = "/dev/ttyv0";

QString path(int vt)
{
    char c = (vt <= 10 ? '0' : 'a') + (vt - 1);
    return QStringLiteral("/dev/ttyv%1").arg(c);
}

int getVtActive(int fd)
{
    int vtActive = 0;
    if (ioctl(fd, VT_GETACTIVE, &vtActive) < 0) {
        qCritical() << "Failed to get current VT:" << strerror(errno);
        return -1;
    }
    return vtActive;
}
#else
const char *defaultVtPath = "/dev/tty0";

QString path(int vt)
{
    return QStringLiteral("/dev/tty%1").arg(vt);
}

int getVtActive(int fd)
{
    vt_stat vtState{};
    if (ioctl(fd, VT_GETSTATE, &vtState) < 0) {
        qCritical() << "Failed to get current VT:" << strerror(errno);
        return -1;
    }
    return vtState.v_active;
}
#endif

static void onAcquireDisplay([[maybe_unused]] int signal)
{
    int fd = open(defaultVtPath, O_RDWR | O_NOCTTY);
    ioctl(fd, VT_RELDISP, VT_ACKACQ);
    close(fd);
}

static void onReleaseDisplay([[maybe_unused]] int signal)
{
    int fd = open(defaultVtPath, O_RDWR | O_NOCTTY);
    ioctl(fd, VT_RELDISP, 1);
    close(fd);
}

static bool handleVtSwitches(int fd)
{
    vt_mode setModeRequest{};
    bool ok = true;

    setModeRequest.mode = VT_PROCESS;
    setModeRequest.relsig = RELEASE_DISPLAY_SIGNAL;
    setModeRequest.acqsig = ACQUIRE_DISPLAY_SIGNAL;

    if (ioctl(fd, VT_SETMODE, &setModeRequest) < 0) {
        qDebug() << "Failed to manage VT manually:" << strerror(errno);
        ok = false;
    }

    signal(RELEASE_DISPLAY_SIGNAL, onReleaseDisplay);
    signal(ACQUIRE_DISPLAY_SIGNAL, onAcquireDisplay);

    return ok;
}

static void fixVtMode(int fd, bool vt_auto)
{
    vt_mode getmodeReply{};
    int kernelDisplayMode = 0;
    bool modeFixed = false;
    bool ok = true;

    if (ioctl(fd, VT_GETMODE, &getmodeReply) < 0) {
        qWarning() << "Failed to query VT mode:" << strerror(errno);
        ok = false;
    }

    if (getmodeReply.mode != VT_AUTO) {
        goto out;
    }

    if (ioctl(fd, KDGETMODE, &kernelDisplayMode) < 0) {
        qWarning() << "Failed to query kernel display mode:" << strerror(errno);
        ok = false;
    }

    if (kernelDisplayMode == KD_TEXT) {
        goto out;
    }

    // VT is in the VT_AUTO + KD_GRAPHICS state, fix it
    if (vt_auto) {
        // If vt_auto is true, the controlling process is already gone, so there is no
        // process which could send the VT_RELDISP 1 ioctl to release the vt.
        // Switch to KD_TEXT and let the kernel switch vts automatically
        if (ioctl(fd, KDSETMODE, KD_TEXT) < 0) {
            qWarning("Failed to set text mode for current VT: %s", strerror(errno));
            ok = false;
        }
    } else {
        ok = handleVtSwitches(fd);
        modeFixed = true;
    }
out:
    if (!ok) {
        qCritical() << "Failed to set up VT mode";
        return;
    }

    if (modeFixed) {
        qDebug() << "VT mode fixed";
    } else {
        qDebug() << "VT mode didn't need to be fixed";
    }
}

int currentVt()
{
    int fd = open(defaultVtPath, O_RDWR | O_NOCTTY);
    if (fd < 0) {
        qCritical() << "Failed to open VT master:" << strerror(errno);
        return -1;
    }
    auto closeFd = qScopeGuard([fd] {
        close(fd);
    });

    return getVtActive(fd);
}

int setUpNewVt()
{
    // open VT master
    int fd = open(defaultVtPath, O_RDWR | O_NOCTTY);
    if (fd < 0) {
        qCritical() << "Failed to open VT master:" << strerror(errno);
        return -1;
    }
    auto closeFd = qScopeGuard([fd] {
        close(fd);
    });

    int vt = 0;
    if (ioctl(fd, VT_OPENQRY, &vt) < 0) {
        qCritical() << "Failed to open new VT:" << strerror(errno);
        return -1;
    }

    qDebug() << "VT_OPENQRY returned vt=" << vt;

#ifdef __FreeBSD__
    // On FreeBSD, VT_OPENQRY may return a VT that appears available but is
    // actually in use (e.g., VT 2 used by the greeter). We need to verify
    // the returned VT is actually usable, and if not, find an alternative.
    //
    // FreeBSD VT numbering: ttyv0=VT1, ttyv1=VT2, ttyv2=VT3, ... ttyv8=VT9
    // The greeter typically uses VT2 (ttyv1), so we should use a different VT.
    // VT8 (ttyv7) or VT9 (ttyv8) are commonly used for X sessions.
    {
        int vtActive = getVtActive(fd);
        qWarning() << "FreeBSD: VT_OPENQRY returned" << vt << ", active VT is" << vtActive;

        // Helper lambda to test if a VT is available
        auto testVtAvailable = [](int testVt) -> bool {
            QString vtPath = path(testVt);
            int testFd = open(qPrintable(vtPath), O_RDWR | O_NOCTTY | O_NONBLOCK);
            if (testFd >= 0) {
                close(testFd);
                return true;
            }
            qDebug() << "VT" << testVt << "path:" << vtPath << "not available:" << strerror(errno);
            return false;
        };

        // First, check if the VT_OPENQRY result is actually usable
        if (vt > 0 && vt != vtActive) {
            if (testVtAvailable(vt)) {
                qWarning() << "FreeBSD: VT_OPENQRY result" << vt << "is available, using it";
                return vt;
            }
            qWarning() << "FreeBSD: VT_OPENQRY result" << vt << "is not actually available, searching for alternative";
        }

        // Last resort: scan VTs 3-12, skipping active VT
        qWarning() << "FreeBSD: preferred VT not available, scanning for any available VT";
        for (int tryVt = 3; tryVt <= 12; tryVt++) {
            if (tryVt == vtActive) {
                continue;
            }
            if (testVtAvailable(tryVt)) {
                qWarning() << "FreeBSD: found available VT:" << tryVt;
                return tryVt;
            }
        }
        qCritical() << "FreeBSD: no available VT found - all VTs appear to be in use";
        return vtActive;
    }
#endif

    // VT_OPENQRY returned an invalid VT number (non-FreeBSD path)
    if (vt <= 0) {
        int vtActive = getVtActive(fd);
        qWarning() << "VT_OPENQRY returned" << vt << "(invalid), fall back to active VT" << vtActive;
        return vtActive;
    }

    return vt;
}

void jumpToVt(int vt, bool vt_auto)
{
    qDebug() << "Jumping to VT" << vt;

#ifdef __FreeBSD__
    // On FreeBSD, simply opening the target VT device causes the kernel to
    // switch to that VT.
    Q_UNUSED(vt_auto);
    QString ttyString = path(vt);
    int vtFd = open(qPrintable(ttyString), O_RDWR | O_NOCTTY);
    if (vtFd >= 0) {
        qDebug() << "Successfully switched to VT" << vt << "by opening" << ttyString;
        close(vtFd);
    } else {
        qWarning("Failed to open %s for VT switching: %s", qPrintable(ttyString), strerror(errno));
    }
#else
    int fd;

    int activeVtFd = open(defaultVtPath, O_RDWR | O_NOCTTY);

    QString ttyString = path(vt);
    int vtFd = open(qPrintable(ttyString), O_RDWR | O_NOCTTY);
    if (vtFd != -1) {
        fd = vtFd;

        // Clear VT
        static const char *clearEscapeSequence = "\33[H\33[2J";
        if (write(vtFd, clearEscapeSequence, sizeof(clearEscapeSequence)) == -1) {
            qWarning("Failed to clear VT %d: %s", vt, strerror(errno));
        }

        // set graphics mode to prevent flickering
        if (ioctl(fd, KDSETMODE, KD_GRAPHICS) < 0) {
            qWarning("Failed to set graphics mode for VT %d: %s", vt, strerror(errno));
        }

        // it's possible that the current VT was left in a broken
        // combination of states (KD_GRAPHICS with VT_AUTO) that we
        // cannot switch from, so make sure things are in a way that
        // will make VT_ACTIVATE work without hanging VT_WAITACTIVE
        fixVtMode(activeVtFd, vt_auto);
    } else {
        qWarning("Failed to open %s: %s", qPrintable(ttyString), strerror(errno));
        qDebug("Using %s instead of %s!", defaultVtPath, qPrintable(ttyString));
        fd = activeVtFd;
    }

    // If vt_auto is true, the controlling process is already gone, so there is no
    // process which could send the VT_RELDISP 1 ioctl to release the vt.
    // Let the kernel switch vts automatically
    if (!vt_auto) {
        handleVtSwitches(fd);
    }

    do {
        errno = 0;

        if (ioctl(fd, VT_ACTIVATE, vt) < 0) {
            if (errno == EINTR) {
                continue;
            }

            qWarning("Couldn't initiate jump to VT %d: %s", vt, strerror(errno));
            break;
        }

        if (ioctl(fd, VT_WAITACTIVE, vt) < 0 && errno != EINTR) {
            qWarning("Couldn't finalize jump to VT %d: %s", vt, strerror(errno));
        }

    } while (errno == EINTR);
    close(activeVtFd);
    if (vtFd != -1) {
        close(vtFd);
    }
#endif
}
}
}
