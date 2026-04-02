/*
 *  SPDX-FileCopyrightText: 2025 Oliver Beard <olib141@outlook.com>
 *
 *  SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#include <QDir>
#include <QFileSystemWatcher>
#include <QStandardPaths>

#include <KDesktopFile>
#include <KLocalizedString>

#include <utility>

#include "sessionmodel.h"

SessionModel::SessionModel(QObject *parent)
    : QAbstractListModel(parent)
{
    // NOTE: /usr/local/share is listed first, then /usr/share, so sessions in the former take precedence
    const QStringList xSessionsDirs =
        QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, QStringLiteral("xsessions"), QStandardPaths::LocateDirectory);
}

int SessionModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_sessions.count();
}

QVariant SessionModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= m_sessions.count()) {
        return {};
    }

    Session session = m_sessions[index.row()];

    auto getDisplay = [this, session]() {
        // Here we want to handle gracefully any sessions with the same display name by disambiguating using
        // the session type and if not enough, an index (which will be as consistent as the installed files)

        const bool shouldAppendType = std::any_of(m_sessions.cbegin(), m_sessions.cend(), [session](const Session &other) {
            return session.path != other.path // Don't compare to ourselves
                && session.displayName == other.displayName; // Display names are the same...
        });

        int index = 1;
        const bool shouldAppendIndex = std::any_of(m_sessions.cbegin(), m_sessions.cend(), [session, &index](const Session &other) {
            const bool match = session.path != other.path // Don't compare to ourselves
                && session.displayName == other.displayName; // Display names are the same...

            if (match && other.path < session.path) {
                ++index;
            }

            return match;
        });

        if (shouldAppendType && shouldAppendIndex) {
                return i18nc("@item:inmenu %1 is the localised name of a desktop session, %2 is the index of the session",
                             "%1 (X11) (%2)",
                             session.displayName,
                             index);
        } else if (shouldAppendType) {
                return i18nc("@item:inmenu %1 is the localised name of a desktop session", "%1 (X11)", session.displayName);
        } else if (shouldAppendIndex) {
            return i18nc("@item:inmenu %1 is the localised name of a desktop session, %2 is the index of the session", "%1 (%2)", session.displayName, index);
        }

        return session.displayName;
    };

    switch (role) {
    case Qt::DisplayRole:
        return getDisplay();
    case SessionModel::DisplayNameRole:
        return session.displayName;
    case SessionModel::FileNameRole:
        return QFileInfo(session.path).fileName();
    case SessionModel::CommentRole:
        return session.comment;
    default:
        break;
    }

    return {};
}

QHash<int, QByteArray> SessionModel::roleNames() const
{
    QHash<int, QByteArray> roles = QAbstractItemModel::roleNames();
    roles[TypeRole] = "type";
    roles[DisplayNameRole] = "displayName";
    roles[CommentRole] = "comment";
    roles[FileNameRole] = "fileName";
    return roles;
}

int SessionModel::indexOfData(const QVariant &data, int role) const
{
    if (data.isNull() || data.toString().isEmpty()) {
        return -1;
    }

    for (int i = 0; i < m_sessions.count(); ++i) {
        if (SessionModel::data(index(i, 0), role) == data) {
            return i;
        }
    }

    return -1;
}

QStringList SessionModel::getSessionsPaths(const QStringList &sessionsDirs) const
{
    QStringList sessionsPaths;

    for (QDir sessionsDir : sessionsDirs) {
        sessionsDir.setNameFilters({QStringLiteral("*.desktop")});
        sessionsDir.setFilter(QDir::Files);

        for (const auto &sessionPath : sessionsDir.entryList()) {
            const QString sessionFileName = QFileInfo(sessionPath).fileName();

            // Ignore duplicate sessions, already added ones take precedence
            bool isDuplicate = std::any_of(sessionsPaths.begin(), sessionsPaths.end(), [&](const auto &existingSessionPath) {
                return QFileInfo(existingSessionPath).fileName() == sessionFileName;
            });

            if (!isDuplicate) {
                sessionsPaths.append(sessionsDir.absoluteFilePath(sessionFileName));
            }
        }
    }

    return sessionsPaths;
}

Session SessionModel::getSession(const QString path) const
{
    qDebug().nospace() << "Reading session from " << path;

    KDesktopFile desktop(path);
    return Session(path, desktop.readName(), desktop.readComment());
}

#include "moc_sessionmodel.cpp"
