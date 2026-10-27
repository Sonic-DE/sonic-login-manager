/*
 *  SPDX-FileCopyrightText: 2025 Oliver Beard <olib141@outlook.com>
 *
 *  SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QQuickWindow>
#include <QSurfaceFormat>

#include "wallpaperapp.h"

#include <Sonic/Logging/Logging>

#include <unistd.h>

SONIC_LOGGING_COMPONENT("org.sonicde.loginmanager.wallpaper")

int main(int argc, char **argv)
{
    // Logs to ~/.local/state/sonic/soniclogin-wallpaper.log, rotated on every run.
    Sonic::Logging::install({.rotateOnStartup = true});
    qInfo() << "soniclogin-wallpaper: process starting; logging to" << Sonic::Logging::logFilePath() << "as uid" << geteuid();

    QCoreApplication::setApplicationName(QStringLiteral("soniclogin-wallpaper"));

    auto format = QSurfaceFormat::defaultFormat();
    format.setOption(QSurfaceFormat::ResetNotification);
    QSurfaceFormat::setDefaultFormat(format);

    WallpaperApp app(argc, argv);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("SonicLogin wallpaper renderer"));
    parser.addHelpOption();
    const QCommandLineOption testOption(QStringLiteral("test"), QStringLiteral("Show the configured wallpaper in a standalone test window"));
    parser.addOption(testOption);
    parser.process(app);

    app.start(parser.isSet(testOption));

    qInfo() << "soniclogin-wallpaper: entering event loop"
            << "screens=" << app.screens().size();

    return app.exec();
}
