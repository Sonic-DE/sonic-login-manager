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
#include "SocketWriter.h"
#include "Utils.h"

#include <QDebug>
#include <QLocalServer>
#include <QLocalSocket>

namespace PLASMALOGIN
{
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

    QString socketName = QStringLiteral("plasmalogin-%1-%2").arg(displayName).arg(generateName(6));

    // log message
    qDebug() << "Socket server starting...";

    // create server
    m_server = new QLocalServer(this);

    // set server options
    m_server->setSocketOptions(QLocalServer::UserAccessOption);

    // start listening
    if (!m_server->listen(socketName)) {
        // log message
        qCritical() << "Failed to start socket server.";

        // return fail
        return false;
    }

    // log message
    qDebug() << "Socket server started.";

    // connect signals
    connect(m_server, &QLocalServer::newConnection, this, &SocketServer::newConnection);

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

void SocketServer::newConnection()
{
    // get pending connection
    QLocalSocket *socket = m_server->nextPendingConnection();

    // connect signals
    connect(socket, &QLocalSocket::readyRead, this, &SocketServer::readyRead);
    connect(socket, &QLocalSocket::errorOccurred, this, [socket](QLocalSocket::LocalSocketError error) {
        qWarning() << "SocketServer: socket error occurred, serverName=" << socket->serverName() << " error=" << error << "=" << socket->errorString();
    });
    connect(socket, &QObject::destroyed, this, [](QObject *obj) {
        qWarning() << "SocketServer: socket object destroyed, ptr=" << obj;
    });
    connect(socket, &QLocalSocket::disconnected, this, [socket] {
        qWarning() << "SocketServer: client disconnected, serverName=" << socket->serverName()
                   << "socket ptr=" << (void *)socket
                   << "state=" << socket->state()
                   << "error=" << socket->errorString();
    });
    connect(socket, &QLocalSocket::disconnected, socket, &QLocalSocket::deleteLater);
}

void SocketServer::readyRead()
{
    QLocalSocket *socket = qobject_cast<QLocalSocket *>(sender());

    // check socket
    if (!socket) {
        qWarning() << "SocketServer::readyRead: socket is null!";
        return;
    }

    // input stream
    QDataStream input(socket);

    // Qt's QLocalSocket::readyRead is not designed to be called at every socket.write(),
    // so we need to use a loop to read all the signals.
    while (socket->bytesAvailable()) {
        // read message
        quint32 message;
        input >> message;

        switch (GreeterMessages(message)) {
        case GreeterMessages::Connect: {
            // log message
            // send host name
            SocketWriter(socket) << quint32(DaemonMessages::HostName) << daemonApp->hostName();

            // send session capabilities
            if (daemonApp->powerManager()) {
                Capabilities caps = daemonApp->powerManager()->capabilities();
                SocketWriter(socket) << quint32(DaemonMessages::SessionCapabilities) << quint32(caps);
            } else {
                qWarning() << "SocketServer::Connect: no PowerManager available, sending zero capabilities";
                SocketWriter(socket) << quint32(DaemonMessages::SessionCapabilities) << quint32(Capability::None);
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
            qWarning() << "SocketServer::readyRead: GreeterMessages: Unknown message" << message;
        }
        }
    }
}

void SocketServer::loginFailed(QLocalSocket *socket)
{
    SocketWriter(socket) << quint32(DaemonMessages::LoginFailed);
}

void SocketServer::loginSucceeded(QLocalSocket *socket)
{
    SocketWriter(socket) << quint32(DaemonMessages::LoginSucceeded);
}

void SocketServer::informationMessage(QLocalSocket *socket, const QString &message)
{
    SocketWriter(socket) << quint32(DaemonMessages::InformationMessage) << message;
}

void SocketServer::sendSessionCapabilities(QLocalSocket *socket, Capabilities caps)
{
    if (socket) {
        SocketWriter(socket) << quint32(DaemonMessages::SessionCapabilities) << quint32(caps);
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
