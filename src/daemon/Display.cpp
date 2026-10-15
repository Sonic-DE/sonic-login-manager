/***************************************************************************
 * SPDX-FileCopyrightText: 2014-2015 Pier Luigi Fiorini <pierluigi.fiorini@gmail.com>
 * SPDX-FileCopyrightText: 2014 Martin Bříza <mbriza@redhat.com>
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

#include "Display.h"

#include "Configuration.h"
#include "DaemonApp.h"
#include "DisplayManager.h"
#include "Greeter.h"
#include "InitSystem.h"
#include "Seat.h"
#include "SocketServer.h"
#include "XorgUserDisplayServer.h"

#include <QDebug>
#include <QLocalSocket>
#include <QTimer>

#include <grp.h>
#include <pwd.h>
#include <sys/time.h>
#include <unistd.h>

#include "VirtualTerminal.h"

static int s_ttyFailures = 0;

namespace SONICLOGIN {
Display::Display(Seat *parent)
    : QObject(parent)
    , m_auth(new Auth(this))
    , m_seat(parent)
    , m_socketServer(new SocketServer(this))
    , m_greeter(new Greeter(this))
{
    if (seat()->canTTY()) {
        m_terminalId = seat()->availableVt();
    }
    qDebug("Using VT %d", m_terminalId.tty());

    // respond to authentication requests
    m_auth->setVerbose(true);
    connect(m_auth, &Auth::requestChanged, this, &Display::slotRequestChanged);
    connect(m_auth, &Auth::authentication, this, &Display::slotAuthenticationFinished);
    connect(m_auth, &Auth::sessionStarted, this, &Display::slotSessionStarted);
    connect(m_auth, &Auth::finished, this, &Display::slotHelperFinished);
    connect(m_auth, &Auth::info, this, &Display::slotAuthInfo);
    connect(m_auth, &Auth::error, this, &Display::slotAuthError);

    // connect login signal
    connect(m_socketServer, &SocketServer::login, this, &Display::login);

    // connect login result signals
    connect(this, &Display::loginFailed, m_socketServer, &SocketServer::loginFailed);
    connect(this, &Display::loginSucceeded, m_socketServer, &SocketServer::loginSucceeded);

    connect(m_greeter, &Greeter::failed, this, &Display::stop);
    connect(m_greeter, &Greeter::ttyFailed, this, [this] {
        ++s_ttyFailures;
        if (s_ttyFailures > 5) {
            QCoreApplication::exit(23);
        }
        // It might be the case that we are trying a tty that has been taken over by a
        // different process. In such a case, switch back to the initial one and try again.
        VirtualTerminal::jumpToVt(getInitialVt(), true);
        stop();
    });
    connect(m_greeter, &Greeter::displayServerFailed, this, &Display::displayServerFailed);
}

Display::~Display()
{
    disconnect(m_auth, &Auth::finished, this, &Display::slotHelperFinished);
    stop();
}

int Display::terminalId() const
{
    return (m_auth->isActive() ? m_sessionTerminalId : m_terminalId).tty();
}

QString Display::sessionType() const
{
    return "x11";
}

Seat *Display::seat() const
{
    return m_seat;
}

void Display::setAutoLogin(const QString &user, const QString &session)
{
    m_autologinUser = user;
    m_autologinSession = Session();
    if (user.isEmpty()) {
        return;
    }

    m_autologinSession = Session::create(Session::X11Session, session);
    if (!m_autologinSession.isValid()) {
        m_autologinSession = Session::create(Session::WaylandSession, session);
    }
    if (!m_autologinSession.isValid()) {
        qCritical() << "Unable to find autologin session entry" << session << "for user" << user << "on seat" << seat()->name()
                    << "— falling back to the greeter";
    }
}

bool Display::start()
{
    if (m_started) {
        return true;
    }

    if (seat()->canTTY() && !m_terminalId.isValid()) {
        // A device may have appeared since construction or an earlier retry.
        m_terminalId = seat()->availableVt();
        if (!m_terminalId.isValid()) {
            qWarning() << "Display::start: cannot reserve a VT for the greeter";
            return false;
        }
    }

    m_started = true;

    // Handle autologin early, unless it needs the display server to be up
    // (rootful X + X11 autologin session).
    if (m_autologinSession.isValid()) {
        m_auth->setAutologin(true);
        if (startAuth(m_autologinUser, QString(), m_autologinSession)) {
            return true;
        } else {
            return handleAutologinFailure();
        }
    }

    // no reason for this to be queued, other than porting
    QMetaObject::invokeMethod(this, &Display::displayServerStarted, Qt::QueuedConnection);
    return true;
}

void Display::startSocketServerAndGreeter()
{
    // start socket server
    m_socketServer->start(QString());

    // change the owner and group of the socket to avoid permission denied errors
    struct passwd *pw = getpwnam("soniclogin");
    if (pw) {
        if (chown(qPrintable(m_socketServer->socketAddress()), pw->pw_uid, pw->pw_gid) == -1) {
            qWarning() << "Failed to change owner of the socket";
            return;
        }
    }

    m_greeter->setSocket(m_socketServer->socketAddress());

    // Set up display server for the greeter
    m_greeter->setDisplayServerCommand(XorgUserDisplayServer::command(this, QString()));

    // start greeter
    m_greeter->start();

    // Track that the greeter was started, even if it later crashes (e.g. OOM).
    // This is needed so that startAuth() knows to allocate a new VT for the user
    // session, since the greeter's VT may still be locked after a crash.
    m_greeterWasStarted = true;
}

bool Display::handleAutologinFailure()
{
    qWarning() << "Autologin failed!";
    m_auth->setAutologin(false);
    // For late autologin handling only the greeter needs to be started.

    QMetaObject::invokeMethod(this, &Display::displayServerStarted, Qt::QueuedConnection);
    return true;
}

void Display::displayServerStarted()
{
    // log message
    qDebug() << "Display server started.";

    startSocketServerAndGreeter();
}

void Display::stop()
{
    // check flag
    if (!m_started) {
        qWarning() << "Display::stop() called but m_started=false, returning";
        return;
    }

    qWarning() << "Display::stop() CALLED - TRACE:"
               << "auth_user=" << m_auth->user() << "auth_isGreeter=" << m_auth->isGreeter() << "greeter_running=" << m_greeter->isRunning();

    // stop the greeter
    qWarning() << "Display::stop() stopping greeter";
    m_greeter->stop();

    qWarning() << "Display::stop() stopping auth";
    m_auth->stop();

    // stop socket server
    qWarning() << "Display::stop() stopping socket server";
    m_socketServer->stop();

    // reset flag before emitting stopped() so that a concurrent start()
    // triggered by the stopped() signal does not race with cleanup.
    m_started = false;

    // emit signal
    qWarning() << "Display::stop() emitting stopped()";
    emit stopped();
}

void Display::login(QLocalSocket *socket, const QString &user, const QString &password, const Session &session)
{
    m_socket = socket;

    // the SONICLOGIN user has special privileges that skip password checking so that we can load the greeter
    // block ever trying to log in as the SONICLOGIN user
    if (user == QLatin1String("soniclogin")) {
        qWarning() << "Display::login: Rejecting login for 'soniclogin' user";
        emit loginFailed(m_socket);
        return;
    }

    // authenticate
    if (!startAuth(user, password, session)) {
        qWarning() << "Display::login: startAuth failed, notifying greeter with LoginFailed";
        if (m_socket) {
            emit loginFailed(m_socket);
            m_socket = nullptr;
        }
    } else {
        qDebug() << "Display::login: startAuth started successfully for user" << user;
    }
}

bool Display::startAuth(const QString &user, const QString &password, const Session &session)
{
    qDebug() << "start auth" << "user" << session.isValid() << session.exec();

    if (m_auth->isActive()) {
        qWarning() << "Display::startAuth: Existing authentication ongoing, aborting";
        return false;
    }

    m_passPhrase = password;

    // sanity check
    if (!session.isValid()) {
        qCritical() << "Invalid session" << session.fileName();
        return false;
    }

    if (session.xdgSessionType().isEmpty()) {
        qCritical() << "Failed to find XDG session type for session" << session.fileName();
        return false;
    }
    if (session.exec().isEmpty()) {
        qCritical() << "Failed to find command for session" << session.fileName();
        return false;
    }

    m_reuseSessionId = seat()->reusableSessionId(user);

    // save session desktop file name, we'll use it to set the
    // last session later, in slotAuthenticationFinished()
    m_sessionName = session.fileName();

    // Release any reservation from an earlier failed attempt before querying.
    m_sessionTerminalId = {};
    const bool canTTY = seat()->canTTY();

    qDebug() << "Display::startAuth: VT allocation check:"
             << "m_greeter->isRunning()" << m_greeter->isRunning() << "m_greeterWasStarted" << m_greeterWasStarted << "seat()->canTTY()" << canTTY
             << "m_terminalId" << m_terminalId.tty();

    if (canTTY && (m_greeter->isRunning() || m_greeterWasStarted)) {
        // Create a new VT when we need to have another compositor running.
        // Also allocate a new VT if the greeter was previously started but is
        // no longer running (e.g. it was killed by the OOM killer). In that case
        // the greeter's VT may still be locked and cannot be reused.
        m_sessionTerminalId = VirtualTerminal::setUpNewVt();
        qDebug() << "Display::startAuth: setUpNewVt() returned VT" << m_sessionTerminalId.tty();
    } else {
        m_sessionTerminalId = {m_terminalId.tty(), m_terminalId.ttyFd().duplicate()};
        if (m_greeter->isRunning() || m_greeterWasStarted) {
            qWarning() << "Display::startAuth: seat()->canTTY() is false, cannot allocate new VT";
        } else {
            qDebug() << "Display::startAuth: greeter not running and was never started, reusing VT" << m_terminalId.tty();
        }
    }

    if (canTTY && !m_sessionTerminalId.isValid()) {
        qCritical() << "Display::startAuth: cannot reserve a VT for the session";
        return false;
    }

    // some information
    qDebug() << "Display::startAuth: Session" << m_sessionName << "selected, command:" << session.exec() << "for VT" << m_sessionTerminalId.tty()
             << "type=" << session.xdgSessionType() << "reuseSessionId=" << m_reuseSessionId;

    QProcessEnvironment env;
    env.insert(QStringLiteral("PATH"), mainConfig.Users.DefaultPath.get());
    env.insert(QStringLiteral("XDG_SEAT_PATH"), daemonApp->displayManager()->seatPath(seat()->name()));
    env.insert(QStringLiteral("XDG_SESSION_PATH"), daemonApp->displayManager()->sessionPath(QStringLiteral("Session%1").arg(daemonApp->newSessionId())));
    env.insert(QStringLiteral("DESKTOP_SESSION"), session.desktopSession());
    if (!session.desktopNames().isEmpty()) {
        env.insert(QStringLiteral("XDG_CURRENT_DESKTOP"), session.desktopNames());
    }
    env.insert(QStringLiteral("XDG_SESSION_CLASS"), QStringLiteral("user"));
    env.insert(QStringLiteral("XDG_SESSION_TYPE"), session.xdgSessionType());
    env.insert(QStringLiteral("XDG_SEAT"), seat()->name());
    if (m_sessionTerminalId.isValid()) {
        env.insert(QStringLiteral("XDG_VTNR"), QString::number(m_sessionTerminalId.tty()));
    }
    env.insert(QStringLiteral("XDG_SESSION_DESKTOP"), session.desktopNames());

    if (session.xdgSessionType() == QLatin1String("x11")) {
        m_auth->setDisplayServerCommand(XorgUserDisplayServer::command(this, user));
    } else {
        m_auth->setDisplayServerCommand(QStringLiteral());
    }
    m_auth->setUser(user);
    if (m_reuseSessionId.isNull()) {
        m_auth->setSession(session.exec());
    }
    m_auth->insertEnvironment(env);
    m_auth->start();

    return true;
}

void Display::slotAuthenticationFinished(const QString &user, bool success)
{
    if (m_auth->autologin() && !success) {
        handleAutologinFailure();
        return;
    }

    if (success) {
        qDebug() << "Authentication for user " << user << " successful";

        if (!m_reuseSessionId.isNull()) {
            seat()->activateSession(m_reuseSessionId);
        }

        if (m_socket) {
            emit loginSucceeded(m_socket);
        }
    } else if (m_socket) {
        qDebug() << "Authentication for user " << user << " failed";
        emit loginFailed(m_socket);
    }
    m_socket = nullptr;
}

void Display::slotAuthInfo(const QString &message, Auth::Info info)
{
    qWarning() << "Authentication information:" << info << message;

    if (!m_socket) {
        return;
    }

    m_socketServer->informationMessage(m_socket, message);
}

void Display::slotAuthError(const QString &message, Auth::Error error)
{
    qWarning() << "Authentication error:" << error << message;

    if (!m_socket) {
        return;
    }

    m_socketServer->informationMessage(m_socket, message);
    if (error == Auth::ERROR_AUTHENTICATION) {
        emit loginFailed(m_socket);
    }
}

void Display::slotHelperFinished(Auth::HelperExitStatus status)
{
    const bool isGreeterBootstrapHelper = m_auth->isGreeter() && m_auth->user() == QLatin1String("soniclogin");
    if (isGreeterBootstrapHelper && status == Auth::HELPER_SUCCESS) {
        qWarning() << "Display::slotHelperFinished: greeter bootstrap helper exited successfully; keeping display/socket server alive";
        return;
    }

    // Don't restart greeter and display server unless soniclogin-helper exited
    // with an internal error or the user session finished successfully,
    // we want to avoid greeter from restarting when an authentication
    // error happens (in this case we want to show the message from the
    // greeter
    if (status != Auth::HELPER_AUTH_ERROR) {
        qWarning() << "Display::slotHelperFinished: invoking stop() because status != HELPER_AUTH_ERROR";
        stop();
    }
}

void Display::slotRequestChanged()
{
    if (m_auth->request()->prompts().length() == 1) {
        m_auth->request()->prompts()[0]->setResponse(qPrintable(m_passPhrase));
        m_auth->request()->done();
    } else if (m_auth->request()->prompts().length() == 2) {
        m_auth->request()->prompts()[0]->setResponse(qPrintable(m_auth->user()));
        m_auth->request()->prompts()[1]->setResponse(qPrintable(m_passPhrase));
        m_auth->request()->done();
    }
}

void Display::slotSessionStarted(bool success)
{
    qDebug() << "Session started" << success;
    if (success) {
        // Stop the greeter after a short delay once the user session has started.
        // The delay gives the user session time to fully initialize before we
        // tear down the greeter's X server.
        qDebug() << "slotSessionStarted: User session started, scheduling greeter stop in 5 seconds";
        QTimer::singleShot(5000, m_greeter, &Greeter::stop);
    }
}
}

#include "moc_Display.cpp"
