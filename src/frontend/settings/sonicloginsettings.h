/*
 *  SPDX-FileCopyrightText: 2019 Kevin Ottens <kevin.ottens@enioka.com>
 *  SPDX-FileCopyrightText: 2025 Oliver Beard <olib141@outlook.com>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "sonicloginsettingsbase.h"

struct WallpaperInfo {
    Q_PROPERTY(QString name MEMBER name CONSTANT)
    Q_PROPERTY(QString id MEMBER id CONSTANT)
    QString name;
    QString id;
    Q_GADGET
};

class SonicLoginSettings : public SonicLoginSettingsBase
{
    Q_OBJECT

public:
    static SonicLoginSettings &getInstance();

    ~SonicLoginSettings() override;

    unsigned int minimumUid() const;
    unsigned int maximumUid() const;

    QList<WallpaperInfo> availableWallpaperPlugins() const;

    SonicLoginSettings(SonicLoginSettings const &) = delete;
    void operator=(SonicLoginSettings const &) = delete;

protected:
    SonicLoginSettings(KSharedConfig::Ptr config, QObject *parent = nullptr);

private:
    void getUids();
    void getWallpaperPlugins();

    unsigned int m_minimumUid;
    unsigned int m_maximumUid;
    QList<WallpaperInfo> m_availableWallpaperPlugins;
};
