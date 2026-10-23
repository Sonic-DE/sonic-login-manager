/*
 * SPDX-FileCopyrightText: 2026 SonicDE Community
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QJsonObject>
#include <QString>

namespace SONICLOGIN {
struct DaemonReply {
    bool success = false;
    bool rejected = false;
    QString error;
};

// Blocking transport, independent of KCM/QML objects. Run on a worker thread,
// including serialization of large private theme bundles. Explicit socket and
// deadline arguments permit isolated transport tests without a real daemon.
DaemonReply requestDaemon(const QString &operation,
                          const QJsonObject &arguments,
                          const QString &username,
                          const QString &password,
                          QString socketName = {},
                          int timeoutMs = -1);
}
