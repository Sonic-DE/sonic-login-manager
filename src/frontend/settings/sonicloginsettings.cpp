/*
 *  SPDX-FileCopyrightText: 2019 Kevin Ottens <kevin.ottens@enioka.com>
 *  SPDX-FileCopyrightText: 2020 David Redondo <kde@david-redondo.de>
 *  SPDX-FileCopyrightText: 2025 Oliver Beard <olib141@outlook.com>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <limits>

#include <QCollator>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QTextStream>

#include <KPackage/Package>
#include <KPackage/PackageLoader>

#include "config.h"

#include "sonicloginsettings.h"

static void addDirectoryToConfig(KSharedConfig::Ptr config, const QDir &dir)
{
    const auto entryInfoList = dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::LocaleAware);

    QStringList sources;

    for (const QFileInfo &fi : entryInfoList) {
        sources << fi.absoluteFilePath();
    }

    config->addConfigSources(sources);
}

SonicLoginSettings &SonicLoginSettings::getInstance()
{
    auto config = KSharedConfig::openConfig(QStringLiteral(SONICLOGIN_CONFIG_FILE), KConfig::NoGlobals);

    QDir sonicLoginSystemConfigDir(QStringLiteral(SONICLOGIN_CONFIG_DIR));
    if (sonicLoginSystemConfigDir.exists()) {
        addDirectoryToConfig(config, sonicLoginSystemConfigDir);
    }

    config->addConfigSources({QStringLiteral(SONICLOGIN_SYSTEM_CONFIG_FILE)});

    QDir sonicLoginConfigDir(QStringLiteral(SONICLOGIN_SYSTEM_CONFIG_DIR));
    if (sonicLoginConfigDir.exists()) {
        addDirectoryToConfig(config, sonicLoginConfigDir);
    }

    static SonicLoginSettings instance(config);
    return instance;
}

SonicLoginSettings::SonicLoginSettings(KSharedConfig::Ptr config, QObject *parent)
    : SonicLoginSettingsBase(config)
{
    setParent(parent);

    getUids();
    getWallpaperPlugins();
}

SonicLoginSettings::~SonicLoginSettings()
{
}

void SonicLoginSettings::getUids()
{
    // Sane defaults, especially for BSD where LOGIN_DEFS_PATH is configured empty
    m_minimumUid = 1000;
    m_maximumUid = 60000;

    if (QStringLiteral(LOGIN_DEFS_PATH).isEmpty()) {
        return;
    }

    QFile loginDefs(QStringLiteral(LOGIN_DEFS_PATH));
    if (!loginDefs.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Failed to determine min/max uid";
        return;
    }

    QTextStream in(&loginDefs);
    const QStringList keys = {QStringLiteral("UID_MIN"), QStringLiteral("UID_MAX")};

    while (!in.atEnd()) {
        QString line = in.readLine().split(QLatin1Char('#')).first().simplified();
        if (!line.isEmpty()) {
            QStringList lineParts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);

            if (lineParts.size() != 2 || !keys.contains(lineParts[0])) {
                continue;
            }

            bool ok;
            unsigned int value = lineParts[1].toUInt(&ok);
            if (ok) {
                if (lineParts[0] == QStringLiteral("UID_MIN")) {
                    m_minimumUid = value;
                } else {
                    m_maximumUid = value;
                }
            }
        }
    }
}

void SonicLoginSettings::getWallpaperPlugins()
{
    const auto wallpaperPackages = KPackage::PackageLoader::self()->listPackages(QStringLiteral("Plasma/Wallpaper"));

    // not all plugins are suitable for the login screen due to file access
    QStringList allowedPlugins = {
        QStringLiteral("org.kde.color"),
        QStringLiteral("org.kde.image"),
        QStringLiteral("org.kde.tiled"),
        QStringLiteral("org.kde.haenau"),
        QStringLiteral("org.kde.potd"),
        QStringLiteral("org.kde.hunyango"),
        // slideshow is explicitly not included as we only sync one file
    };

    for (auto &package : wallpaperPackages) {
        if (!allowedPlugins.contains(package.pluginId())) {
            continue;
        }
        m_availableWallpaperPlugins.append({package.name(), package.pluginId()});
    }

    QCollator collator; // TODO: This isn't even used?
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(m_availableWallpaperPlugins.begin(), m_availableWallpaperPlugins.end(), [](const WallpaperInfo &w1, const WallpaperInfo &w2) {
        return w1.name < w2.name;
    });
}

unsigned int SonicLoginSettings::minimumUid() const
{
    return m_minimumUid;
}

unsigned int SonicLoginSettings::maximumUid() const
{
    return m_maximumUid;
}

QList<WallpaperInfo> SonicLoginSettings::availableWallpaperPlugins() const
{
    return m_availableWallpaperPlugins;
}

#include "moc_sonicloginsettings.cpp"
