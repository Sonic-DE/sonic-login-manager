/*
 * SPDX-FileCopyrightText: 2026 SonicDE Community
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef SONICLOGIN_KCMIPCSERVER_H
#define SONICLOGIN_KCMIPCSERVER_H

#include <QLocalServer>
#include <QLocalSocket>
#include <QObject>

namespace SONICLOGIN
{

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

    QPair<bool, QString> handleSync(QDataStream &in);
    QPair<bool, QString> handleReset(QDataStream &in);
    QPair<bool, QString> handleSave(QDataStream &in);
};

} // namespace SONICLOGIN

#endif // SONICLOGIN_KCMIPCSERVER_H
