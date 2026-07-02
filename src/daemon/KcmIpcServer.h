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

struct SyncPayload {
    QList<QPair<QString, QString>> files;
    // Cursor theme name -> raw tar archive of the theme directory.
    QList<QPair<QString, QByteArray>> cursorThemes;
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
    void onNewConnection();
    void onReadyRead();
    void onDisconnected();

private:
    QLocalServer *m_server{nullptr};

    void sendResponse(QLocalSocket *socket, quint32 messageId, bool ok, const QString &error);

    bool authenticate(const QString &username, const QString &password, QString *errorOut);

    std::optional<SyncPayload> handleSyncRead(QDataStream &in);
    QPair<bool, QString> handleSyncWrite(const SyncPayload &payload);

    std::optional<SavePayload> handleSaveRead(QDataStream &in);
    QPair<bool, QString> handleSaveWrite(const SavePayload &payload);

    QPair<bool, QString> handleReset(QDataStream &in);
};

} // namespace SONICLOGIN

#endif // SONICLOGIN_KCMIPCSERVER_H
