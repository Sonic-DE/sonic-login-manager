/*
 *  SPDX-FileCopyrightText: 2014 Martin Gräßlin <mgraesslin@kde.org>
 *  SPDX-FileCopyrightText: 2014 Marco Martin <mart@kde.org>
 *  SPDX-FileCopyrightText: 2019 Kevin Ottens <kevin.ottens@enioka.com>
 *  SPDX-FileCopyrightText: 2020 David Redondo <kde@david-redondo.de>
 *  SPDX-FileCopyrightText: 2025 Oliver Beard <olib141@outlook.com>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <KConfigPropertyMap>
#include <KQuickManagedConfigModule>

#include "sonicloginsettings.h"
#include "wallpaperintegration.h"

#include <functional>

class UserModel;
class SessionModel;

class SonicLoginKcm : public KQuickManagedConfigModule
{
    Q_OBJECT
public:
    explicit SonicLoginKcm(QObject *parent, const KPluginMetaData &data);

    Q_PROPERTY(SonicLoginSettings *settings READ settings CONSTANT)
    Q_PROPERTY(KConfigPropertyMap *wallpaperConfiguration READ wallpaperConfiguration NOTIFY currentWallpaperChanged)
    Q_PROPERTY(QUrl wallpaperConfigFile READ wallpaperConfigFile NOTIFY currentWallpaperChanged)
    Q_PROPERTY(WallpaperIntegration *wallpaperIntegration READ wallpaperIntegration NOTIFY currentWallpaperChanged)
    Q_PROPERTY(QString currentWallpaper READ currentWallpaper NOTIFY currentWallpaperChanged)
    Q_PROPERTY(bool isDefaultsWallpaper READ isDefaultsWallpaper NOTIFY isDefaultsWallpaperChanged)
    Q_PROPERTY(UserModel *userModel READ userModel CONSTANT)
    Q_PROPERTY(SessionModel *sessionModel READ sessionModel CONSTANT)

    Q_INVOKABLE QList<WallpaperInfo> availableWallpaperPlugins()
    {
        return SonicLoginSettings::getInstance().availableWallpaperPlugins();
    }

    Q_INVOKABLE void synchronizeSettings();
    Q_INVOKABLE void resetSynchronizedSettings();
    Q_INVOKABLE bool KDEWalletAvailable();
    Q_INVOKABLE void openKDEWallet();

    Q_INVOKABLE void submitAuth(const QString &username, const QString &password);
    Q_INVOKABLE void cancelAuth();

    Q_PROPERTY(QString currentUser READ currentUser CONSTANT)

    SonicLoginSettings *settings() const;
    QString currentUser() const;
    QUrl wallpaperConfigFile() const;
    WallpaperIntegration *wallpaperIntegration() const;
    QString currentWallpaper() const;
    bool isDefaultsWallpaper() const;
    UserModel *userModel() const;
    SessionModel *sessionModel() const;

public Q_SLOTS:
    void load() override;
    void save() override;
    void defaults() override;
    void updateState();
    void forceUpdateState();

Q_SIGNALS:
    void errorOccurred(const QString &untranslatedMessage);
    void syncAttempted();
    void authRequired();

    void defaultsCalled();
    void isDefaultsWallpaperChanged();
    void currentWallpaperChanged();
    void loadCalled();

private:
    bool isSaveNeeded() const override;
    bool isDefaults() const override;

    QJsonObject collectWallpapers(const QUrl &imageUri);
    KConfigPropertyMap *wallpaperConfiguration() const;

    void sendToDaemon(const QString &op, const QJsonObject &args);

    WallpaperSettings *m_wallpaperSettings;
    QString m_currentWallpaper;
    bool m_forceUpdateState = false;

    QString m_pendingAuthUser;
    QString m_pendingAuthPassword;
    std::function<void()> m_pendingContinuation;
};
