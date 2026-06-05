/*
 * Session process wrapper
 * SPDX-FileCopyrightText: 2015 Pier Luigi Fiorini <pierluigi.fiorini@gmail.com>
 * SPDX-FileCopyrightText: 2014 Martin Bříza <mbriza@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#include <QDir>
#include <QFileInfo>
#include <QSocketNotifier>

#include "Configuration.h"
#include "Constants.h"
#include "HelperApp.h"
#include "UserSession.h"
#include "VirtualTerminal.h"

#include <errno.h>
#include <fcntl.h>
#include <functional>
#include <grp.h>
#ifdef Q_OS_LINUX
#include <linux/capability.h>
#include <linux/securebits.h>
#include <sys/capability.h>
#include <sys/prctl.h>
#endif
#include <pwd.h>
#include <sched.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>
#ifdef Q_OS_FREEBSD
#include <libutil.h>
#include <login_cap.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#endif

namespace SONICLOGIN
{

UserSession::UserSession(HelperApp *parent)
    : QProcess(parent)
{
    connect(this, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &UserSession::finished);
    connect(this, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &UserSession::onProcessFinished);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    setChildProcessModifier(std::bind(&UserSession::childModifier, this));
#endif
}

void UserSession::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (exitStatus == QProcess::CrashExit) {
        // A CrashExit means the process was killed by a signal.
        // This is expected when Auth::stop() sends SIGTERM to terminate the helper.
        // Only log as "crashed" if the exit code suggests an abnormal condition.
        pid_t pid = processId();
        qDebug() << "UserSession: process exited with signal"
                 << "pid=" << pid << "exitCode=" << exitCode << "program=" << program() << "arguments=" << arguments() << "state=" << state();
    }
    Q_EMIT finished(exitCode);
}

bool UserSession::start()
{
    QProcessEnvironment env = processEnvironment();
    qDebug() << "UserSession::start: DEBUG - entering start()";
    qDebug() << "UserSession::start: DEBUG - XDG_SESSION_TYPE=" << env.value(QStringLiteral("XDG_SESSION_TYPE"));
    qDebug() << "UserSession::start: DEBUG - XDG_SESSION_CLASS=" << env.value(QStringLiteral("XDG_SESSION_CLASS"));
    qDebug() << "UserSession::start: DEBUG - m_path=" << m_path;
    qDebug() << "UserSession::start: DEBUG - m_displayServerCmd=" << m_displayServerCmd;
    qDebug() << "UserSession::start: DEBUG - SESSION_COMMAND=" << SESSION_COMMAND;

    bool isWaylandGreeter = false;

    if (env.value(QStringLiteral("XDG_SESSION_TYPE")) == QLatin1String("x11")) {
        QString command;
        if (env.value(QStringLiteral("XDG_SESSION_CLASS")) == QLatin1String("greeter")) {
            command = m_path;
        } else {
            command = QStringLiteral("%1 \"%2\"").arg(SESSION_COMMAND).arg(m_path);
        }

        qInfo() << "Starting X11 session:" << m_displayServerCmd << command;
        if (m_displayServerCmd.isEmpty()) {
            auto args = QProcess::splitCommand(command);
            setProgram(args.takeFirst());
            setArguments(args);
            qDebug() << "UserSession::start: DEBUG - starting directly with program=" << args.first();
        } else {
            QString helperPath = QStringLiteral(LIBEXEC_INSTALL_DIR "/soniclogin-helper-start-x11user");
            setProgram(helperPath);
            setArguments({m_displayServerCmd, command});
            qDebug() << "UserSession::start: DEBUG - starting helper=" << helperPath << "with args=" << m_displayServerCmd << command;
        }
        QString homeDir = processEnvironment().value(QStringLiteral("HOME"));
        if (!homeDir.isEmpty()) {
            if (QDir(homeDir).exists()) {
                setWorkingDirectory(homeDir);
            } else {
                qWarning() << "UserSession::start: HOME directory does not exist, keeping default working directory:" << homeDir;
            }
        }
        QProcess::start();
        qDebug() << "UserSession::start: DEBUG - QProcess::start() called, state=" << state() << "error=" << error();
    } else if (env.value(QStringLiteral("XDG_SESSION_TYPE")) == QLatin1String("wayland")) {
        if (env.value(QStringLiteral("XDG_SESSION_CLASS")) == QLatin1String("greeter")) {
            isWaylandGreeter = true;
        }
        setProgram(WAYLAND_SESSION_COMMAND);
        setArguments(QStringList{m_path});
        qInfo() << "Starting Wayland user session:" << program() << m_path;
        QProcess::start();
        closeWriteChannel();
        closeReadChannel(QProcess::StandardOutput);
    } else {
        qCritical() << "Unable to run user session: unknown session type";
    }

    const bool started = waitForStarted();
    if (started) {
        return true;
    } else if (isWaylandGreeter) {
        // This is probably fine, we need the compositor to start first
        return true;
    }

    qCritical() << "UserSession::start: session process failed to start! error:" << error() << "-" << errorString();
    return false;
}

void UserSession::stop()
{
    qWarning() << "UserSession::stop() CALLED - TRACE:"
               << "program=" << program() << "pid=" << processId() << "state=" << state()
               << "sessionClass=" << processEnvironment().value(QStringLiteral("XDG_SESSION_CLASS"))
               << "sessionType=" << processEnvironment().value(QStringLiteral("XDG_SESSION_TYPE"))
               << "user=" << processEnvironment().value(QStringLiteral("USER"));
    if (state() != QProcess::NotRunning) {
        terminate();
        const bool isGreeter = processEnvironment().value(QStringLiteral("XDG_SESSION_CLASS")) == QLatin1String("greeter");

        // Wait longer for a session than a greeter
        if (!waitForFinished(isGreeter ? 5000 : 60000)) {
            qWarning() << "UserSession::stop() process did not finish in time, sending SIGKILL to pid" << processId();
            kill();
            if (!waitForFinished(5000)) {
                qWarning() << "Could not fully finish the process" << program();
            }
        }
    } else {
        qWarning() << "UserSession::stop() process not running, emitting finished(HELPER_OTHER_ERROR)";
        Q_EMIT finished(Auth::HELPER_OTHER_ERROR);
    }
}

QString UserSession::displayServerCommand() const
{
    return m_displayServerCmd;
}

void UserSession::setDisplayServerCommand(const QString &command)
{
    m_displayServerCmd = command;
}

void UserSession::setPath(const QString &path)
{
    m_path = path;
}

QString UserSession::path() const
{
    return m_path;
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
void UserSession::childModifier()
{
#else
void UserSession::setupChildProcess()
{
#endif
    // Session type
    QString sessionType = processEnvironment().value(QStringLiteral("XDG_SESSION_TYPE"));
    QString sessionClass = processEnvironment().value(QStringLiteral("XDG_SESSION_CLASS"));
    const bool x11Session = sessionType == QLatin1String("x11");

    // set this process as session leader first
    if (setsid() < 0) {
        qCritical("Failed to set pid %lld as leader of the new session and process group: %s", QCoreApplication::applicationPid(), strerror(errno));
        _exit(Auth::HELPER_OTHER_ERROR);
    }

    // open VT and get the fd
    int vtNumber = processEnvironment().value(QStringLiteral("XDG_VTNR")).toInt();
    QString ttyString = VirtualTerminal::path(vtNumber);

    if (vtNumber > 0) {
        // Open TTY WITHOUT O_NOCTTY so it automatically becomes the controlling terminal
        int vtFd = ::open(qPrintable(ttyString), O_RDWR);
        if (vtFd > 0) {
            dup2(vtFd, STDIN_FILENO);
            ::close(vtFd);
        } else {
            qWarning() << "Failed to open" << ttyString << ":" << strerror(errno);
            int stdinFd = ::open("/dev/null", O_RDWR);
            dup2(stdinFd, STDIN_FILENO);
            ::close(stdinFd);
        }
    } else {
        int stdinFd = ::open("/dev/null", O_RDWR);
        dup2(stdinFd, STDIN_FILENO);
        ::close(stdinFd);
    }

    if (vtNumber > 0 && VirtualTerminal::currentVt() != vtNumber) {
        VirtualTerminal::jumpToVt(vtNumber, x11Session);
    }

#ifdef Q_OS_LINUX
    // enter Linux namespaces
    for (const QString &ns : mainConfig.Namespaces.get()) {
        qInfo() << "Entering namespace" << ns;
        int fd = ::open(qPrintable(ns), O_RDONLY);
        if (fd < 0) {
            qCritical("open(%s) failed: %s", qPrintable(ns), strerror(errno));
            exit(Auth::HELPER_OTHER_ERROR);
        }
        if (setns(fd, 0) != 0) {
            qCritical("setns(open(%s), 0) failed: %s", qPrintable(ns), strerror(errno));
            exit(Auth::HELPER_OTHER_ERROR);
        }
        ::close(fd);
    }
#endif

    // switch user
    const QByteArray username = qobject_cast<HelperApp *>(parent())->user().toLocal8Bit();
    struct passwd pw;
    struct passwd *rpw;
    long bufsize = sysconf(_SC_GETPW_R_SIZE_MAX);
    if (bufsize == -1) {
        bufsize = 16384;
    }
    QScopedPointer<char, QScopedPointerPodDeleter> buffer(static_cast<char *>(malloc(bufsize)));
    if (buffer.isNull()) {
        qCritical() << "Could not allocate buffer of size" << bufsize;
        exit(Auth::HELPER_OTHER_ERROR);
    }
    int err = getpwnam_r(username.constData(), &pw, buffer.data(), bufsize, &rpw);
    if (rpw == NULL) {
        if (err == 0) {
            qCritical() << "getpwnam_r(" << username << ") username not found!";
        } else {
            qCritical() << "getpwnam_r(" << username << ") failed with error: " << strerror(err);
        }
        exit(Auth::HELPER_OTHER_ERROR);
    }

#if defined(Q_OS_FREEBSD)
    // execve() uses the environment prepared in Backend::openSession()
    if (setusercontext(NULL, &pw, pw.pw_uid, LOGIN_SETALL) != 0) {
        qCritical() << "setusercontext(NULL, *, " << pw.pw_uid << ", LOGIN_SETALL) failed for user: " << username;
        exit(Auth::HELPER_OTHER_ERROR);
    }
#else
    if (setgid(pw.pw_gid) != 0) {
        qCritical() << "setgid(" << pw.pw_gid << ") failed for user: " << username;
        exit(Auth::HELPER_OTHER_ERROR);
    }

    // fetch ambient groups from PAM's environment;
    // these are set by modules such as pam_groups.so
    int n_pam_groups = getgroups(0, NULL);
    gid_t *pam_groups = NULL;
    if (n_pam_groups > 0) {
        pam_groups = new gid_t[n_pam_groups];
        if ((n_pam_groups = getgroups(n_pam_groups, pam_groups)) == -1) {
            qCritical() << "getgroups() failed to fetch supplemental"
                        << "PAM groups for user:" << username;
            exit(Auth::HELPER_OTHER_ERROR);
        }
    } else {
        n_pam_groups = 0;
    }

    // fetch session's user's groups
    int n_user_groups = 0;
    gid_t *user_groups = NULL;
    if (-1 == getgrouplist(pw.pw_name, pw.pw_gid, NULL, &n_user_groups)) {
        user_groups = new gid_t[n_user_groups];
        if ((n_user_groups = getgrouplist(pw.pw_name, pw.pw_gid, user_groups, &n_user_groups)) == -1) {
            qCritical() << "getgrouplist(" << pw.pw_name << ", " << pw.pw_gid << ") failed";
            exit(Auth::HELPER_OTHER_ERROR);
        }
    }

    // set groups to concatenation of PAM's ambient
    // groups and the session's user's groups
    int n_groups = n_pam_groups + n_user_groups;
    if (n_groups > 0) {
        gid_t *groups = new gid_t[n_groups];
        memcpy(groups, pam_groups, (n_pam_groups * sizeof(gid_t)));
        memcpy((groups + n_pam_groups), user_groups, (n_user_groups * sizeof(gid_t)));

        // setgroups(2) handles duplicate groups
        if (setgroups(n_groups, groups) != 0) {
            qCritical() << "setgroups() failed for user: " << username;
            exit(Auth::HELPER_OTHER_ERROR);
        }
        delete[] groups;
    }
    delete[] pam_groups;
    delete[] user_groups;

#ifdef Q_OS_LINUX
    // Set CAP_SYS_TTY_CONFIG and CAP_SETPCAP as ambient capabilities so they survive setuid().
    // CAP_SETPCAP lets child processes modify their own capability sets before execve().
    // prctl(PR_CAP_AMBIENT_RAISE) requires the capability to be in both permitted and inheritable.
    struct __user_cap_header_struct capHeader = {_LINUX_CAPABILITY_VERSION_3, 0};
    struct __user_cap_data_struct capData[2] = {};
    if (capget(&capHeader, capData) == 0) {
        capData[0].inheritable |= (1U << CAP_SYS_TTY_CONFIG);
        capData[0].inheritable |= (1U << CAP_SETPCAP);
        if (capset(&capHeader, capData) == 0) {
            if (prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, CAP_SYS_TTY_CONFIG, 0, 0) < 0) {
                qWarning() << "UserSession::childModifier: Failed to set CAP_SYS_TTY_CONFIG ambient capability:" << strerror(errno);
            } else {
                qInfo() << "UserSession::childModifier: Set CAP_SYS_TTY_CONFIG as ambient capability";
            }
            if (prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, CAP_SETPCAP, 0, 0) < 0) {
                qWarning() << "UserSession::childModifier: Failed to set CAP_SETPCAP ambient capability:" << strerror(errno);
            } else {
                qInfo() << "UserSession::childModifier: Set CAP_SETPCAP as ambient capability";
            }
            // Prevent setuid() from dropping ambient capabilities
            if (prctl(PR_SET_SECUREBITS, SECBIT_NO_SETUID_FIXUP) < 0) {
                qWarning() << "UserSession::childModifier: Failed to set SECBIT_NO_SETUID_FIXUP:" << strerror(errno);
            }
        } else {
            qWarning() << "UserSession::childModifier: Failed to add capabilities to inheritable set:" << strerror(errno);
        }
    } else {
        qWarning() << "UserSession::childModifier: capget failed:" << strerror(errno);
    }
#endif

    if (setuid(pw.pw_uid) != 0) {
        qCritical() << "setuid(" << pw.pw_uid << ") failed for user: " << username;
        exit(Auth::HELPER_OTHER_ERROR);
    }
    qInfo() << "UserSession::childModifier: setuid complete, uid=" << getuid() << "gid=" << getgid() << "euid=" << geteuid();
#ifdef Q_OS_LINUX
    {
        int a_tty = prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_IS_SET, CAP_SYS_TTY_CONFIG, 0, 0);
        int a_pcap = prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_IS_SET, CAP_SETPCAP, 0, 0);
        qInfo() << "UserSession::childModifier: AFTER setuid ambient CAP_SYS_TTY_CONFIG=" << a_tty << "CAP_SETPCAP=" << a_pcap;
    }
#endif

    if (chdir(pw.pw_dir) != 0) {
        qCritical() << "chdir(" << pw.pw_dir << ") failed for user: " << username;
        qCritical() << "verify directory exist and has sufficient permissions";
        exit(Auth::HELPER_OTHER_ERROR);
    }
#endif

    if (sessionClass != QLatin1String("greeter")) {
        // we cannot use setStandardError file as this code is run in the child process
        // we want to redirect after we setuid so that the log file is owned by the user

        // determine stderr log file based on session type
        QString sessionLog = QStringLiteral("%1/%2")
                                 .arg(QString::fromLocal8Bit(pw.pw_dir))
                                 .arg(sessionType == QLatin1String("x11") ? mainConfig.X11.SessionLogFile.get() : mainConfig.Wayland.SessionLogFile.get());

        // create the path
        QFileInfo finfo(sessionLog);
        QDir().mkpath(finfo.absolutePath());

        // swap the stderr pipe of this subprcess into a file
        int fd = ::open(qPrintable(sessionLog), O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (fd >= 0) {
            dup2(fd, STDERR_FILENO);
            ::close(fd);
        } else {
            qWarning() << "Could not open stderr to" << sessionLog;
        }

        // redirect any stdout to /dev/null
        fd = ::open("/dev/null", O_WRONLY);
        if (fd >= 0) {
            dup2(fd, STDOUT_FILENO);
            ::close(fd);
        } else {
            qWarning() << "Could not redirect stdout";
        }
    }
}

}

#include "moc_UserSession.cpp"
