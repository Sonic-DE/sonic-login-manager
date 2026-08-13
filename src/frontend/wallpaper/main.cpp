/*
 *  SPDX-FileCopyrightText: 2025 Oliver Beard <olib141@outlook.com>
 *
 *  SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QQuickWindow>
#include <QSurfaceFormat>

#include "MessageHandler.h"
#include "wallpaperapp.h"

void WallpaperMessageHandler(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    SONICLOGIN::messageHandler(type, QStringLiteral("SONICLOGIN WALLPAPER"), msg);
}

int main(int argc, char **argv)
{
    // Install message handler to log to soniclogin.log
    qInstallMessageHandler(WallpaperMessageHandler);
    qInfo() << "soniclogin-wallpaper: process starting";

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
