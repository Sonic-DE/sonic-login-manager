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

#include "GreeterProxy.h"
#include "Session.h"

#include <Sonic/Ipc/Client>
#include <Sonic/Ipc/Connection>

#include <QDataStream>
#include <QDebug>

namespace SONICLOGIN
{
class GreeterProxyPrivate
{
public:
    SessionModel *sessionModel{nullptr};
    Sonic::Ipc::Client *client{nullptr};
    Sonic::Ipc::Connection *connection{nullptr};

    // One message per frame: the frame type is the message, the payload the message's stream bytes.
    template<typename... Fields>
    bool send(GreeterMessages message, const Fields &...fields) const
    {
        if (!connection) {
            qWarning() << "GreeterProxy: not connected to the daemon; dropping message" << quint32(message);
            return false;
        }
        QByteArray payload;
        QDataStream output(&payload, QIODevice::WriteOnly);
        output << quint32(message);
        ((output << fields), ...);
        return connection->send(quint32(message), payload);
    }
};

GreeterProxy::GreeterProxy(QObject *parent)
    : QObject(parent)
    , d(new GreeterProxyPrivate())
{
    d->client = new Sonic::Ipc::Client(greeterProtocolVersion, Sonic::Ipc::defaultMaxPayload, this);
    connect(d->client, &Sonic::Ipc::Client::connected, this, [this](Sonic::Ipc::Connection *connection) {
        d->connection = connection;
        connect(connection, &Sonic::Ipc::Connection::frameReceived, this, &GreeterProxy::frameReceived);
        connect(connection, &Sonic::Ipc::Connection::disconnected, this, [this, connection]() {
            if (d->connection == connection) {
                d->connection = nullptr;
            }
            Q_EMIT socketDisconnected();
        });
        d->send(GreeterMessages::Connect);
    });
    connect(d->client, &Sonic::Ipc::Client::connectionFailed, this, [](const QString &reason) {
        qWarning() << "GreeterProxy: cannot connect to the daemon:" << reason;
    });
    const QString socket = qEnvironmentVariable("SONICLOGIN_SOCKET");
    if (socket.isEmpty()) {
        qCritical() << "GreeterProxy: SONICLOGIN_SOCKET environment variable is empty!";
    }
    // connect to server
    d->client->connectToServer(socket);
}

GreeterProxy::~GreeterProxy()
{
    delete d;
}
void GreeterProxy::setSessionModel(SessionModel *model)
{
    d->sessionModel = model;
}

void GreeterProxy::login(const QString &user, const QString &password, const SONICLOGIN::SessionType sessionType, const QString &sessionFileName) const
{
    if (!d->send(GreeterMessages::Login, user, password, static_cast<quint32>(sessionType), sessionFileName)) {
        qWarning() << "GreeterProxy::login: sending failed";
    }
}

void GreeterProxy::shutdown()
{
    if (!d->send(GreeterMessages::PowerOff)) {
        qWarning() << "GreeterProxy::shutdown: sending failed";
    }
}

void GreeterProxy::reboot()
{
    if (!d->send(GreeterMessages::Reboot)) {
        qWarning() << "GreeterProxy::reboot: sending failed";
    }
}

void GreeterProxy::suspend()
{
    if (!d->send(GreeterMessages::Suspend)) {
        qWarning() << "GreeterProxy::suspend: sending failed";
    }
}

void GreeterProxy::hibernate()
{
    if (!d->send(GreeterMessages::Hibernate)) {
        qWarning() << "GreeterProxy::hibernate: sending failed";
    }
}

void GreeterProxy::frameReceived(quint32 type, const QByteArray &payload)
{
    // input stream
    QDataStream input(payload);

    {
        // read message
        quint32 message;
        input >> message;
        if (message != type) {
            qWarning() << "GreeterProxy: frame type" << type << "doesn't match its message" << message;
            return;
        }

        switch (DaemonMessages(message)) {
        case DaemonMessages::HostName: {
            // Host name is read but not used in the greeter currently
            QString hostName;
            input >> hostName;
            qDebug() << "Message received from daemon: HostName =" << hostName;
        } break;
        case DaemonMessages::LoginSucceeded: {
            // log message
            qDebug() << "Message received from daemon: LoginSucceeded";

            // emit signal
            emit loginSucceeded();
        } break;
        case DaemonMessages::LoginFailed: {
            // log message
            qDebug() << "Message received from daemon: LoginFailed";

            // emit signal
            emit loginFailed();
        } break;
        case DaemonMessages::SessionCapabilities: {
            // Read capabilities from daemon
            quint32 capsValue;
            input >> capsValue;
            m_capabilities = static_cast<Capabilities>(capsValue);
            emit capabilitiesChanged();
        } break;
        case DaemonMessages::InformationMessage: {
            QString message;
            input >> message;

            qDebug() << "Information Message received from daemon: " << message;
            emit informationMessage(message);
        } break;
        default: {
            // log message
            qWarning() << "Unknown message received from daemon, message type:" << message;
        }
        }
    }
}
}

#include "moc_GreeterProxy.cpp"
