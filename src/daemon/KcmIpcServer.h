/*
 * SPDX-FileCopyrightText: 2026 SonicDE Community
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef SONICLOGIN_KCMIPCSERVER_H
#define SONICLOGIN_KCMIPCSERVER_H

#include <QByteArray>
#include <QDataStream>
#include <QList>
#include <QObject>
#include <QPair>
#include <QString>
#include <optional>

namespace Sonic::Ipc {
class Connection;
class Server;
}

namespace SONICLOGIN
{

struct SavePayload {
    QString configText;
    QList<QPair<QString, QByteArray>> wallpapers;
};

struct SyncPayload {
    QList<QPair<QString, QString>> files;
    // Relative XDG data path -> materialized theme bundle. Bare names retain
    // compatibility with cursor tarballs sent by older KCM clients.
    QList<QPair<QString, QByteArray>> themes;
    // KScreen per-output and combined config JSONs, keyed by their path
    // relative to the user's $XDG_DATA_HOME/kscreen/ directory.
    QList<QPair<QString, QByteArray>> kscreenFiles;
};

class KcmIpcServer : public QObject
{
    Q_OBJECT
public:
    explicit KcmIpcServer(QObject *parent = nullptr);
    ~KcmIpcServer() override;

    bool start();
    void stop();

    QString boundSocketName() const;

private Q_SLOTS:
    void onNewConnection(Sonic::Ipc::Connection *connection);

private:
    Sonic::Ipc::Server *m_server{nullptr};

    void onRequest(Sonic::Ipc::Connection *connection, quint32 frameType, const QByteArray &payload);
    void sendResponse(Sonic::Ipc::Connection *connection, quint32 messageId, bool ok, const QString &error);

    bool authenticate(const QString &username, const QString &password, QString *errorOut);

    std::optional<SyncPayload> handleSyncRead(QDataStream &in);
    QPair<bool, QString> handleSyncWrite(const SyncPayload &payload);

    std::optional<SavePayload> handleSaveRead(QDataStream &in);
    QPair<bool, QString> handleSaveWrite(const SavePayload &payload);

    QPair<bool, QString> handleReset(QDataStream &in);
};

} // namespace SONICLOGIN

#endif // SONICLOGIN_KCMIPCSERVER_H
