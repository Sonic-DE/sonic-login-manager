/*
 * Qt Authentication Library
 * SPDX-FileCopyrightText: 2013 Martin Bříza <mbriza@redhat.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#include "Auth.h"
#include "AuthMessages.h"
#include "Constants.h"
#include "LogindDBusTypes.h"
#include "Messages.h"

#include <Sonic/Ipc/Connection>
#include <Sonic/Ipc/Server>

#include <QtCore/QDebug>
#include <QtCore/QProcess>
#include <QtCore/QUuid>

#include <QtQml/QtQml>

#include <memory>

#include <unistd.h>

#ifdef Q_OS_LINUX
#include <linux/capability.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#endif

namespace SONICLOGIN
{
namespace {
// One message per frame: the frame type is the message, the payload the message's stream bytes.
template<typename... Fields>
void sendMessage(Sonic::Ipc::Connection *connection, Msg message, const Fields &...fields)
{
    if (!connection) {
        qWarning() << "Auth: no helper connection for message" << message;
        return;
    }
    QByteArray payload;
    QDataStream output(&payload, QIODevice::WriteOnly);
    output << message;
    ((output << fields), ...);
    connection->send(quint32(message), payload);
}
}

class Auth::SocketServer : public Sonic::Ipc::Server {
    Q_OBJECT
public slots:
    void handleNewConnection(Sonic::Ipc::Connection *connection);

public:
    static SocketServer *instance();

    QMap<qint64, Auth::Private *> helpers;

private:
    SocketServer();
};

class Auth::Private : public QObject
{
    Q_OBJECT
public:
    Private(Auth *parent);
    ~Private();
    void setConnection(Sonic::Ipc::Connection *connection);
public slots:
    void framePending(quint32 type, const QByteArray &payload);
    void childExited(int exitCode, QProcess::ExitStatus exitStatus);
    void childError(QProcess::ProcessError error);
    void requestFinished();

public:
    AuthRequest *request{nullptr};
    QProcess *child{nullptr};
    Sonic::Ipc::Connection *connection{nullptr};
    QString displayServerCmd;
    QString sessionPath{};
    QString user{};
    bool autologin{false};
    bool greeter{false};
    QProcessEnvironment environment{};
    qint64 id{0};
    static qint64 lastId;
};

qint64 Auth::Private::lastId = 1;

Auth::SocketServer::SocketServer()
    : Sonic::Ipc::Server()
{
    connect(this, &Sonic::Ipc::Server::newConnection, this, &Auth::SocketServer::handleNewConnection);
}

void Auth::SocketServer::handleNewConnection(Sonic::Ipc::Connection *connection)
{
    // The helper's first frame names the Auth it belongs to; later frames go to that Auth.
    connect(connection, &Sonic::Ipc::Connection::frameReceived, this, [this, connection](quint32 type, const QByteArray &payload) {
        disconnect(connection, &Sonic::Ipc::Connection::frameReceived, this, nullptr);
        Msg m = Msg::MSG_UNKNOWN;
        qint64 id = 0;
        QDataStream str(payload);
        str >> m >> id;
        if (type == quint32(Msg::HELLO) && m == Msg::HELLO && id && helpers.contains(id)) {
            helpers[id]->setConnection(connection);
        } else {
            qWarning() << "Auth: helper connection without a valid HELLO; closing it";
            connection->socket()->abort();
        }
    });
}

Auth::SocketServer *Auth::SocketServer::instance()
{
    static std::unique_ptr<Auth::SocketServer> self;
    if (!self) {
        self.reset(new SocketServer());
        self->listen(QStringLiteral("soniclogin-auth-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)),
                     Sonic::Ipc::Server::Access::User,
                     helperProtocolVersion);
    }
    return self.get();
}

Auth::Private::Private(Auth *parent)
    : QObject(parent)
    , request(new AuthRequest(parent))
    , child(new QProcess(this))
    , id(lastId++)
{
    SocketServer::instance()->helpers[id] = this;
    QProcessEnvironment env = child->processEnvironment();
    bool langEmpty = true;
    QFile localeFile(QStringLiteral("/etc/locale.conf"));
    if (localeFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&localeFile);
        while (!in.atEnd()) {
            QStringList parts = in.readLine().split(QLatin1Char('='));
            if (parts.size() >= 2) {
                env.insert(parts[0], parts[1]);
                if (parts[0] == QLatin1String("LANG")) {
                    langEmpty = false;
                }
            }
        }
        localeFile.close();
    }
    if (langEmpty) {
        env.insert(QStringLiteral("LANG"), QStringLiteral("C"));
    }
    child->setProcessEnvironment(env);
    connect(child, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &Auth::Private::childExited);
    connect(child, &QProcess::errorOccurred, this, &Auth::Private::childError);
    connect(request, &AuthRequest::finished, this, &Auth::Private::requestFinished);
    connect(request, &AuthRequest::promptsChanged, parent, &Auth::requestChanged);
}

Auth::Private::~Private()
{
    SocketServer::instance()->helpers.remove(id);
}

void Auth::Private::setConnection(Sonic::Ipc::Connection *connection)
{
    this->connection = connection;
    connect(connection, &Sonic::Ipc::Connection::frameReceived, this, &Auth::Private::framePending);
    connect(connection, &Sonic::Ipc::Connection::disconnected, this, [this, connection] {
        if (this->connection == connection) {
            this->connection = nullptr;
        }
    });
}

void Auth::Private::framePending(quint32 type, const QByteArray &payload)
{
    Auth *auth = qobject_cast<Auth *>(parent());
    Msg m = MSG_UNKNOWN;
    QDataStream str(payload);
    {
        str >> m;
        if (quint32(m) != type) {
            Q_EMIT auth->error(QStringLiteral("Auth: frame type %1 doesn't match its message %2").arg(type).arg(m), ERROR_INTERNAL);
            return;
        }
        switch (m) {
        case ERROR: {
            qWarning() << "Auth::dataPending: Got ERROR";
            QString message;
            Error type = ERROR_NONE;
            str >> message >> type;
            Q_EMIT auth->error(message, type);
            break;
        }
        case INFO: {
            QString message;
            Info type = INFO_NONE;
            str >> message >> type;
            Q_EMIT auth->info(message, type);
            break;
        }
        case REQUEST: {
            Request r;
            str >> r;
            request->setRequest(&r);
            break;
        }
        case AUTHENTICATED: {
            QString user;
            str >> user;
            if (!user.isEmpty()) {
                auth->setUser(user);
                Q_EMIT auth->authentication(user, true);
                sendMessage(connection, AUTHENTICATED, environment);
            } else {
                Q_EMIT auth->authentication(user, false);
            }
            break;
        }
        case SESSION_STATUS: {
            bool status;
            str >> status;
            Q_EMIT auth->sessionStarted(status);
            sendMessage(connection, SESSION_STATUS);
            break;
        }
        case DISPLAY_SERVER_STARTED: {
            QString displayName;
            str >> displayName;
            Q_EMIT auth->displayServerReady(displayName);
            sendMessage(connection, DISPLAY_SERVER_STARTED);
            break;
        }
        default: {
            Q_EMIT auth->error(QStringLiteral("Auth: Unexpected value received: %1").arg(m), ERROR_INTERNAL);
        }
        }
    }
}

void Auth::Private::childExited(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (exitStatus != QProcess::NormalExit) {
        qWarning("Auth: plasmalogin-helper (%s) crashed (signal %d)", qPrintable(child->arguments().join(QLatin1Char(' '))), exitCode);
        Q_EMIT qobject_cast<Auth *>(parent())->error(child->errorString(), ERROR_INTERNAL);
        // A crashed helper's exit code is the terminating signal number, which
        // can collide with HELPER_AUTH_ERROR; report a generic error instead.
        Q_EMIT qobject_cast<Auth *>(parent())->finished(HELPER_OTHER_ERROR);
        return;
    }

    if (exitCode == HELPER_SUCCESS) {
        qDebug() << "Auth: soniclogin-helper exited successfully";
    } else {
        qWarning("Auth: soniclogin-helper exited with %d", exitCode);
    }

    Q_EMIT qobject_cast<Auth *>(parent())->finished((Auth::HelperExitStatus)exitCode);
}

void Auth::Private::childError(QProcess::ProcessError error)
{
    Q_UNUSED(error);
    Q_EMIT qobject_cast<Auth *>(parent())->error(child->errorString(), ERROR_INTERNAL);
}

void Auth::Private::requestFinished()
{
    Request r = request->request();
    sendMessage(connection, REQUEST, r);
    request->setRequest();
}

Auth::Auth(const QString &user, const QString &session, bool autologin, QObject *parent, bool verbose)
    : QObject(parent)
    , d(new Private(this))
{
    setUser(user);
    setAutologin(autologin);
    setSession(session);
    setVerbose(verbose);
}

Auth::Auth(QObject *parent)
    : QObject(parent)
    , d(new Private(this))
{
}

Auth::~Auth()
{
    stop();
    delete d;
}

void Auth::registerTypes()
{
    qmlRegisterAnonymousType<AuthPrompt>("Auth", 1);
    qmlRegisterAnonymousType<AuthRequest>("Auth", 1);
    qmlRegisterType<Auth>("Auth", 1, 0, "Auth");
}

bool Auth::autologin() const
{
    return d->autologin;
}

bool Auth::isGreeter() const
{
    return d->greeter;
}

const QString &Auth::session() const
{
    return d->sessionPath;
}

const QString &Auth::user() const
{
    return d->user;
}

bool Auth::verbose() const
{
    return d->child->processChannelMode() == QProcess::ForwardedChannels;
}

AuthRequest *Auth::request()
{
    return d->request;
}

bool Auth::isActive() const
{
    return d->child->state() != QProcess::NotRunning;
}

void Auth::insertEnvironment(const QProcessEnvironment &env)
{
    d->environment.insert(env);
}

void Auth::insertEnvironment(const QString &key, const QString &value)
{
    d->environment.insert(key, value);
}

void Auth::setUser(const QString &user)
{
    if (user != d->user) {
        d->user = user;
        Q_EMIT userChanged();
    }
}

void Auth::setAutologin(bool on)
{
    if (on != d->autologin) {
        d->autologin = on;
        Q_EMIT autologinChanged();
    }
}

void Auth::setGreeter(bool on)
{
    if (on != d->greeter) {
        d->greeter = on;
        Q_EMIT greeterChanged();
    }
}

void Auth::setDisplayServerCommand(const QString &command)
{
    if (d->displayServerCmd != command) {
        d->displayServerCmd = command;
        Q_EMIT displayServerCommandChanged();
    }
}

void Auth::setSession(const QString &path)
{
    if (path != d->sessionPath) {
        d->sessionPath = path;
        Q_EMIT sessionChanged();
    }
}

void Auth::setVerbose(bool on)
{
    if (on != verbose()) {
        if (on) {
            d->child->setProcessChannelMode(QProcess::ForwardedChannels);
        } else {
            d->child->setProcessChannelMode(QProcess::SeparateChannels);
        }
        Q_EMIT verboseChanged();
    }
}

void Auth::start()
{
    QStringList args;
    args << QStringLiteral("--socket") << SocketServer::instance()->fullServerName();
    args << QStringLiteral("--id") << QString::number(d->id);
    if (!d->sessionPath.isEmpty()) {
        args << QStringLiteral("--start") << d->sessionPath;
    }
    if (!d->user.isEmpty()) {
        args << QStringLiteral("--user") << d->user;
    }
    if (d->autologin) {
        args << QStringLiteral("--autologin");
    }
    if (!d->displayServerCmd.isEmpty()) {
        args << QStringLiteral("--display-server") << d->displayServerCmd;
    }
    if (d->greeter) {
        args << QStringLiteral("--greeter");
    }

#ifdef Q_OS_LINUX
    if (Logind::isAvailable() && Logind::isELogind()) {
        // Ensure CAP_SYS_TTY_CONFIG propagates to soniclogin-helper so Xorg can do VT ioctls.
        // prctl(PR_CAP_AMBIENT_RAISE) requires the capability in both permitted and inheritable.
        d->child->setChildProcessModifier([]() {
            struct __user_cap_header_struct capHeader = {_LINUX_CAPABILITY_VERSION_3, 0};
            struct __user_cap_data_struct capData[2] = {};
            if (syscall(SYS_capget, &capHeader, capData) == 0) {
                capData[0].inheritable |= (1U << CAP_SYS_TTY_CONFIG);
                capData[0].inheritable |= (1U << CAP_SETPCAP);
                if (syscall(SYS_capset, &capHeader, capData) == 0) {
                    if (prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, CAP_SYS_TTY_CONFIG, 0, 0) < 0) {
                        qWarning() << "Auth::start: Failed to set CAP_SYS_TTY_CONFIG ambient capability";
                    }
                    if (prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, CAP_SETPCAP, 0, 0) < 0) {
                        qWarning() << "Auth::start: Failed to set CAP_SETPCAP ambient capability";
                    }
                } else {
                    qWarning() << "Auth::start: Failed to add capabilities to inheritable set";
                }
            } else {
                qWarning() << "Auth::start: capget failed";
            }
        });
    }
#endif

    d->child->start(QStringLiteral("%1/soniclogin-helper").arg(QStringLiteral(LIBEXEC_INSTALL_DIR)), args);

    d->child->waitForStarted(3000);
}

void Auth::stop()
{
    qWarning() << "Auth::stop() CALLED - TRACE:"
               << "child_state=" << (d->child ? d->child->state() : QProcess::NotRunning) << "child_pid=" << (d->child ? d->child->processId() : 0)
               << "user=" << d->user << "greeter=" << d->greeter << "autologin=" << d->autologin << "sessionPath=" << d->sessionPath;
    if (d->child->state() == QProcess::NotRunning) {
        qWarning() << "Auth::stop() child already not running, returning";
        return;
    }

    d->child->terminate();

    // wait for finished
    if (!d->child->waitForFinished(5000)) {
        qWarning() << "Auth::stop: child process did not terminate gracefully after 5s, sending SIGKILL to pid" << d->child->processId();
        d->child->kill();
    }
}
}

#include "Auth.moc"

#include "moc_Auth.cpp"
