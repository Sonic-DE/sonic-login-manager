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
#include "SocketWriter.h"

#include <QDebug>
#include <QLocalSocket>

namespace PLASMALOGIN
{
class GreeterProxyPrivate
{
public:
    SessionModel *sessionModel{nullptr};
    QLocalSocket *socket{nullptr};
};

GreeterProxy::GreeterProxy(QObject *parent)
    : QObject(parent)
    , d(new GreeterProxyPrivate())
{
    d->socket = new QLocalSocket(this);
    // connect signals
    connect(d->socket, &QLocalSocket::connected, this, [this]() {
        SocketWriter(d->socket) << quint32(GreeterMessages::Connect);
    });
    connect(d->socket, &QLocalSocket::disconnected, this, [this]() {
        Q_EMIT socketDisconnected();
    });
    connect(d->socket, &QLocalSocket::readyRead, this, &GreeterProxy::readyRead);
    connect(d->socket, &QLocalSocket::errorOccurred, this, [this]() {
        qWarning() << "GreeterProxy::errorOccurred: Socket error:" << d->socket->errorString()
                   << "state=" << d->socket->state()
                   << "serverName=" << d->socket->serverName()
                   << "fullServerName=" << d->socket->fullServerName()
                   << "bytesAvailable=" << d->socket->bytesAvailable();
    });

    const QString socket = qEnvironmentVariable("SONICLOGIN_SOCKET");
    if (socket.isEmpty()) {
        qCritical() << "GreeterProxy: SONICLOGIN_SOCKET environment variable is empty!";
    }
    // connect to server
    d->socket->connectToServer(socket);
}

GreeterProxy::~GreeterProxy()
{
    delete d;
}
void GreeterProxy::setSessionModel(SessionModel *model)
{
    d->sessionModel = model;
}

void GreeterProxy::login(const QString &user, const QString &password, const PLASMALOGIN::SessionType sessionType, const QString &sessionFileName) const
{
    SocketWriter(d->socket) << quint32(GreeterMessages::Login) << user << password << static_cast<uint32_t>(sessionType) << sessionFileName;
    qDebug() << "GreeterProxy::login: Message written, waiting for flush...";
    const bool flushOk = d->socket->flush();
    if (!flushOk) {
        qWarning() << "GreeterProxy::login: flush failed";
    }
}

void GreeterProxy::shutdown()
{
    SocketWriter(d->socket) << quint32(GreeterMessages::PowerOff);
    const bool flushOk = d->socket->flush();
    if (!flushOk) {
        qWarning() << "GreeterProxy::shutdown: flush failed";
    }
}

void GreeterProxy::reboot()
{
    SocketWriter(d->socket) << quint32(GreeterMessages::Reboot);
    const bool flushOk = d->socket->flush();
    if (!flushOk) {
        qWarning() << "GreeterProxy::reboot: flush failed";
    }
}

void GreeterProxy::suspend()
{
    SocketWriter(d->socket) << quint32(GreeterMessages::Suspend);
    const bool flushOk = d->socket->flush();
    if (!flushOk) {
        qWarning() << "GreeterProxy::suspend: flush failed";
    }
}

void GreeterProxy::hibernate()
{
    SocketWriter(d->socket) << quint32(GreeterMessages::Hibernate);
    const bool flushOk = d->socket->flush();
    if (!flushOk) {
        qWarning() << "GreeterProxy::hibernate: flush failed";
    }
}

void GreeterProxy::readyRead()
{
    // input stream
    QDataStream input(d->socket);

    while (input.device()->bytesAvailable()) {
        // read message
        quint32 message;
        input >> message;

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
            qWarning() << "Unknown message received from daemon.";
        }
        }
    }
}
}

#include "moc_GreeterProxy.cpp"
