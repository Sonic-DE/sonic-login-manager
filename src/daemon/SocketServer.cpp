/***************************************************************************
 * SPDX-FileCopyrightText: 2015 Pier Luigi Fiorini <pierluigi.fiorini@gmail.com>
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

#include "SocketServer.h"

#include "DaemonApp.h"
#include "Messages.h"
#include "Utils.h"

#include <Sonic/Ipc/Connection>
#include <Sonic/Ipc/Server>

#include <QDataStream>
#include <QDebug>
#include <QLocalSocket>

namespace SONICLOGIN
{
namespace {
// The greeter's connection owns its socket, so the socket the login signal carries leads back to it.
Sonic::Ipc::Connection *connectionOf(QLocalSocket *socket)
{
    return socket ? qobject_cast<Sonic::Ipc::Connection *>(socket->parent()) : nullptr;
}

// One message per frame: the frame type is the message, the payload the message's stream bytes.
template<typename... Fields>
void sendMessage(QLocalSocket *socket, DaemonMessages message, const Fields &...fields)
{
    Sonic::Ipc::Connection *connection = connectionOf(socket);
    if (!connection) {
        qWarning() << "SocketServer: no greeter connection for message" << quint32(message);
        return;
    }
    QByteArray payload;
    QDataStream output(&payload, QIODevice::WriteOnly);
    output << quint32(message);
    ((output << fields), ...);
    connection->send(quint32(message), payload);
}
}

SocketServer::SocketServer(QObject *parent)
    : QObject(parent)
{
}

QString SocketServer::socketAddress() const
{
    if (m_server) {
        return m_server->fullServerName();
    }
    return QString();
}

bool SocketServer::start(const QString &displayName)
{
    // check if the server has been created already
    if (m_server) {
        return false;
    }

    QString socketName = QStringLiteral("soniclogin-%1-%2").arg(displayName).arg(generateName(6));

    // log message
    qDebug() << "Socket server starting...";

    // create server
    m_server = new Sonic::Ipc::Server(this);

    // start listening, owner-only until Display hands the socket to the greeter user
    if (!m_server->listen(socketName, Sonic::Ipc::Server::Access::User, greeterProtocolVersion)) {
        // log message
        qCritical() << "Failed to start socket server:" << m_server->errorString();

        delete m_server;
        m_server = nullptr;

        // return fail
        return false;
    }

    // log message
    qDebug() << "Socket server started.";

    // connect signals
    connect(m_server, &Sonic::Ipc::Server::newConnection, this, &SocketServer::newConnection);

    // return success
    return true;
}

void SocketServer::stop()
{
    // check flag
    if (!m_server) {
        return;
    }

    // log message
    qDebug() << "Socket server stopping...";

    // delete server
    m_server->deleteLater();
    m_server = nullptr;

    // log message
    qDebug() << "Socket server stopped.";
}

void SocketServer::newConnection(Sonic::Ipc::Connection *connection)
{
    QLocalSocket *socket = connection->socket();

    // connect signals
    connect(connection, &Sonic::Ipc::Connection::frameReceived, this, [this, socket](quint32 type, const QByteArray &payload) {
        frameReceived(socket, type, payload);
    });
    connect(socket, &QLocalSocket::errorOccurred, this, [socket](QLocalSocket::LocalSocketError error) {
        qWarning() << "SocketServer: socket error occurred, serverName=" << socket->serverName() << " error=" << error << "=" << socket->errorString();
    });
    connect(connection, &Sonic::Ipc::Connection::disconnected, this, [socket] {
        qWarning() << "SocketServer: client disconnected, serverName=" << socket->serverName() << "socket ptr=" << (void *)socket << "state=" << socket->state()
                   << "error=" << socket->errorString();
    });
}

void SocketServer::frameReceived(QLocalSocket *socket, quint32 type, const QByteArray &payload)
{
    // input stream
    QDataStream input(payload);

    {
        // read message
        quint32 message;
        input >> message;
        if (message != type) {
            qWarning() << "SocketServer: frame type" << type << "doesn't match its message" << message;
            return;
        }

        switch (GreeterMessages(message)) {
        case GreeterMessages::Connect: {
            // log message
            // send host name
            sendMessage(socket, DaemonMessages::HostName, daemonApp->hostName());

            // send session capabilities
            if (daemonApp->powerManager()) {
                Capabilities caps = daemonApp->powerManager()->capabilities();
                sendMessage(socket, DaemonMessages::SessionCapabilities, quint32(caps));
            } else {
                qWarning() << "SocketServer::Connect: no PowerManager available, sending zero capabilities";
                sendMessage(socket, DaemonMessages::SessionCapabilities, quint32(Capability::None));
            }

            // emit signal
            emit connected();
        } break;
        case GreeterMessages::Login: {
            // read username, pasword etc.
            QString user, password, filename;
            Session session;
            input >> user >> password >> session;
            if (!socket || user.isEmpty() || !session.isValid()) {
                qWarning() << "SocketServer::Login: validation failed, not emitting signal";
                return;
            }
            // emit signal
            emit login(socket, user, password, session);
        } break;
        case GreeterMessages::PowerOff: {
            powerOff();
        } break;
        case GreeterMessages::Reboot: {
            reboot();
        } break;
        case GreeterMessages::Suspend: {
            suspend();
        } break;
        case GreeterMessages::Hibernate: {
            hibernate();
        } break;
        default: {
            // log message
            qWarning() << "SocketServer::frameReceived: GreeterMessages: Unknown message" << message;
        }
        }
    }
}

void SocketServer::loginFailed(QLocalSocket *socket)
{
    sendMessage(socket, DaemonMessages::LoginFailed);
}

void SocketServer::loginSucceeded(QLocalSocket *socket)
{
    sendMessage(socket, DaemonMessages::LoginSucceeded);
}

void SocketServer::informationMessage(QLocalSocket *socket, const QString &message)
{
    sendMessage(socket, DaemonMessages::InformationMessage, message);
}

void SocketServer::sendSessionCapabilities(QLocalSocket *socket, Capabilities caps)
{
    if (socket) {
        sendMessage(socket, DaemonMessages::SessionCapabilities, quint32(caps));
    }
}

void SocketServer::powerOff()
{
    if (daemonApp->powerManager()) {
        daemonApp->powerManager()->powerOff();
    } else {
        qWarning() << "SocketServer::powerOff: no PowerManager available!";
    }
}

void SocketServer::reboot()
{
    if (daemonApp->powerManager()) {
        daemonApp->powerManager()->reboot();
    } else {
        qWarning() << "SocketServer::reboot: no PowerManager available!";
    }
}

void SocketServer::suspend()
{
    if (daemonApp->powerManager()) {
        daemonApp->powerManager()->suspend();
    } else {
        qWarning() << "SocketServer::suspend: no PowerManager available!";
    }
}

void SocketServer::hibernate()
{
    if (daemonApp->powerManager()) {
        daemonApp->powerManager()->hibernate();
    } else {
        qWarning() << "SocketServer::hibernate: no PowerManager available!";
    }
}
}

#include "moc_SocketServer.cpp"
