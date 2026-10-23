/*
 * SPDX-FileCopyrightText: 2026 SonicDE Community
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "daemonrequest.h"

#include <QDataStream>
#include <QDeadlineTimer>
#include <QFile>
#include <QLocalSocket>

#include <KLocalizedString>

namespace SONICLOGIN {
DaemonReply
requestDaemon(const QString &operation, const QJsonObject &arguments, const QString &username, const QString &password, QString socketName, int timeoutMs)
{
    constexpr quint32 sync = 4, reset = 2, save = 3, response = 100, requestId = 1;
    const quint32 type = operation == QLatin1String("sync") ? sync
        : operation == QLatin1String("reset")               ? reset
        : operation == QLatin1String("save")                ? save
                                                            : 0;
    if (!type) {
        return {false, false, kxi18n("Unknown operation: %1").subs(operation).toString()};
    }
    if (socketName.isEmpty()) {
        QFile pidFile(QStringLiteral("/run/soniclogin/daemon.pid"));
        bool ok = false;
        const qint64 pid = pidFile.open(QIODevice::ReadOnly | QIODevice::Text) ? pidFile.readAll().trimmed().toLongLong(&ok) : 0;
        if (!ok || pid <= 0) {
            return {false, false, kxi18n("Sonic Login daemon is not running. Start it and try again.").toString()};
        }
        socketName = QStringLiteral("/run/soniclogin/kcm-ipc-") + QString::number(pid);
    }

    QLocalSocket socket;
    socket.connectToServer(socketName);
    if (!socket.waitForConnected(5000)) {
        return {false, false, kxi18n("Cannot connect to the Sonic Login daemon: %1").subs(socket.errorString()).toString()};
    }

    QByteArray block;
    QDataStream out(&block, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_6_0);
    out << username << password << type << requestId;
    auto writeBinaryMap = [&out](const QJsonObject &values) {
        out << quint32(values.size());
        for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
            out << it.key() << QByteArray::fromBase64(it.value().toString().toLatin1());
        }
    };
    if (type == sync) {
        const auto files = arguments.value(QStringLiteral("files")).toObject();
        out << quint32(files.size());
        for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
            out << it.key() << it.value().toString();
        }
        writeBinaryMap(arguments.value(QStringLiteral("themes")).toObject());
        writeBinaryMap(arguments.value(QStringLiteral("kscreen")).toObject());
    } else if (type == save) {
        out << arguments.value(QStringLiteral("config")).toString();
        writeBinaryMap(arguments.value(QStringLiteral("wallpapers")).toObject());
    }
    if (out.status() != QDataStream::Ok || socket.write(block) != block.size()) {
        return {false, false, kxi18n("Cannot send settings to the Sonic Login daemon: %1").subs(socket.errorString()).toString()};
    }
    socket.flush();

    QDeadlineTimer deadline(timeoutMs >= 0 ? timeoutMs : type == sync ? 120000 : 15000);
    QDataStream in(&socket);
    in.setVersion(QDataStream::Qt_6_0);
    quint32 responseType = 0, responseId = 0;
    quint8 success = 0;
    QString error;
    for (;;) {
        in.startTransaction();
        in >> responseType >> responseId >> success >> error;
        if (in.commitTransaction()) {
            break;
        }
        if (deadline.hasExpired() || !socket.waitForReadyRead(int(deadline.remainingTime()))) {
            return {false, false, kxi18n("Daemon did not respond: %1").subs(socket.errorString()).toString()};
        }
    }
    if (responseType != response || responseId != requestId || success > 1) {
        return {false, false, kxi18n("Unexpected response from daemon: %1").subs(responseType).toString()};
    }
    return {success == 1, success == 0, error.isEmpty() && !success ? kxi18n("Operation failed.").toString() : error};
}
}
