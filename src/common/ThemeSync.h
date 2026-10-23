/*
 * SPDX-FileCopyrightText: 2026 SonicDE Community
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QByteArray>
#include <QMap>
#include <QSet>
#include <QStringList>
#include <optional>
#include <sys/types.h>

namespace SONICLOGIN::ThemeSync {
// Sources are ordered from user overrides to system defaults. Symlinked assets
// are materialized in the bundle, never recreated by the privileged receiver.
bool bundle(const QStringList &sources, QByteArray *data, QString *error);
// Snapshot the selected settings, transferring only user-local assets and
// overrides. System themes and inherited system assets are referenced in place.
bool collectSettings(QMap<QString, QString> *configs, QMap<QString, QByteArray> *themes, QString *error);
bool install(const QString &home, const QString &relativeName, const QByteArray &data, QString *error, uid_t owner = uid_t(-1), gid_t group = gid_t(-1));
bool writeConfig(const QString &home,
                 const QString &relativeName,
                 const std::optional<QString> &text,
                 QString *error,
                 uid_t owner = uid_t(-1),
                 gid_t group = gid_t(-1));
bool reset(const QString &home, QString *error);
// Remove only copies marked as previously synchronized, keeping this request's
// user assets. This also stops stale copies from shadowing installed system themes.
bool prune(const QString &home, const QSet<QString> &retained, QString *error);
}
