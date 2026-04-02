/*
 * SPDX-FileCopyrightText: David Edmundson
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "MockGreeterProxy.h"

#include <QCoreApplication>
#include <QDebug>
#include <QTimer>

MockGreeterProxy::MockGreeterProxy()
{
    qDebug().noquote() << QStringLiteral("Mock backend in use, use password %1 for successful login on any user").arg(s_mockPassword);
}

void MockGreeterProxy::login(const QString &user, const QString &password, const PLASMALOGIN::SessionType sessionType, const QString &sessionFileName) const
{
    Q_UNUSED(sessionType);

    bool const success = (!user.isEmpty() && password == s_mockPassword);

    QString sessionTypeName = QStringLiteral("X11");

    qDebug().nospace() << "Login " << (success ? "success" : "failure") << " with user " << user << ", password " << password << ", session " << sessionTypeName
                       << " " << sessionFileName;

    if (success) {
        QTimer::singleShot(100, this, &MockGreeterProxy::loginSucceeded);
        QTimer::singleShot(800, []() {
            QCoreApplication::quit();
        });
    } else {
        QTimer::singleShot(100, this, &MockGreeterProxy::loginFailed);
    }
}

#include "moc_MockGreeterProxy.cpp"
