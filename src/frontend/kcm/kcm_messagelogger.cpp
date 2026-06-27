/*
 * SPDX-FileCopyrightText: 2026 SonicDE Community
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <QAtomicInt>
#include <QDateTime>
#include <QFile>
#include <QLatin1StringView>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>
#include <QtGlobal>
#include <QtLogging>

static QtMessageHandler s_previousHandler = nullptr;

static void kcmMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    const QLatin1StringView cat(context.category ? context.category : "");

    if (type == QtDebugMsg
        && (cat.startsWith(QLatin1StringView("qt.")) || cat.startsWith(QLatin1StringView("kf.")) || cat.startsWith(QLatin1StringView("libkwin."))
            || cat.startsWith(QLatin1StringView("kwin_")) || cat == QLatin1StringView("kwin") || cat == QLatin1StringView("heap") || cat.isEmpty())) {
        return;
    }

    static QBasicMutex mutex;
    QMutexLocker locker(&mutex);

    const QString formatted = qFormatLogMessage(type, context, msg);
    const QString line = QDateTime::currentDateTime().toString(Qt::ISODateWithMs) + QLatin1Char(' ') + formatted + QLatin1Char('\n');

    QFile file(QStringLiteral("/var/log/sonic/desktop-interface.log"));
    if (file.open(QIODevice::Append | QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(line.toLocal8Bit());
        file.flush();
        file.close();
    }

    if (s_previousHandler) {
        s_previousHandler(type, context, msg);
    }
}

void installKcmMessageHandler()
{
    static QAtomicInt s_installed(0);
    if (s_installed.fetchAndStoreRelaxed(1) != 0) {
        return;
    }
    s_previousHandler = qInstallMessageHandler(kcmMessageHandler);
}