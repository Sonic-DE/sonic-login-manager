/*
 * SPDX-FileCopyrightText: David Edmundson
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#pragma once
#include <QObject>

#include "Messages.h"

// this class needs the same QML interface as GreeterProxy
class MockGreeterProxy : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool canReboot READ canReboot NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool canShutdown READ canShutdown NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool canSuspend READ canSuspend NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool canHibernate READ canHibernate NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool canLogout READ canLogout NOTIFY capabilitiesChanged)

public:
    MockGreeterProxy();

    // Capability getters
    bool canReboot() const { return true; }
    bool canShutdown() const { return true; }
    bool canSuspend() const { return true; }
    bool canHibernate() const { return true; }
    bool canLogout() const { return true; }

public Q_SLOTS:
    void login(const QString &user, const QString &password, const PLASMALOGIN::SessionType sessionType, const QString &sessionFileName) const;
    void shutdown();
    void reboot();
    void suspend();
    void hibernate();

Q_SIGNALS:
    void informationMessage(const QString &message);
    void capabilitiesChanged();

    void socketDisconnected();
    void loginFailed();
    void loginSucceeded();

private:
    static constexpr QLatin1String s_mockPassword = QLatin1String("mypassword");
};
