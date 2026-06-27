/*
 * SPDX-FileCopyrightText: 2026 SonicDE Community
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef SONICLOGIN_KCMIPCSERVER_H
#define SONICLOGIN_KCMIPCSERVER_H

#include <QByteArray>
#include <QList>
#include <QLocalServer>
#include <QLocalSocket>
#include <QObject>
#include <QPair>
#include <QString>
#include <optional>

namespace SONICLOGIN
{

struct SavePayload {
    QString configText;
    QList<QPair<QString, QByteArray>> wallpapers;
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
    void onNewConnection();
    void onReadyRead();
    void onDisconnected();

private:
    QLocalServer *m_server{nullptr};

    void sendResponse(QLocalSocket *socket, quint32 messageId, bool ok, const QString &error);

    bool authenticate(const QString &username, const QString &password, QString *errorOut);

    std::optional<QList<QPair<QString, QString>>> handleSyncRead(QDataStream &in);
    QPair<bool, QString> handleSyncWrite(const QList<QPair<QString, QString>> &files);

    std::optional<SavePayload> handleSaveRead(QDataStream &in);
    QPair<bool, QString> handleSaveWrite(const SavePayload &payload);

    QPair<bool, QString> handleReset(QDataStream &in);
};

} // namespace SONICLOGIN

#endif // SONICLOGIN_KCMIPCSERVER_H
