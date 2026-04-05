/*
    SPDX-FileCopyrightText: 2010 Ivan Cukic <ivan.cukic(at)kde.org>
    SPDX-FileCopyrightText: 2025 Oliver Beard <olib141@outlook.com
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <QTimer>

#include <KPackage/Package>
#include <KPackage/PackageLoader>
#include <KWindowSystem>

#include "wallpaperwindow.h"

WallpaperWindow::WallpaperWindow(QScreen *screen)
    : PlasmaQuick::QuickViewSharedEngine()
    , m_screen(screen)
{
    setColor(Qt::black);
    setScreen(m_screen);

    setGeometry(m_screen->geometry());
    connect(m_screen, &QScreen::geometryChanged, this, [this]() {
        setGeometry(m_screen->geometry());
    });

    setResizeMode(PlasmaQuick::QuickViewSharedEngine::SizeRootObjectToView);

    setFlags(Qt::BypassWindowManagerHint);
}

bool WallpaperWindow::blur() const
{
    return m_blur;
}

void WallpaperWindow::setBlur(bool enable)
{
    if (m_blur == enable) {
        return;
    }

    m_blur = enable;
    Q_EMIT blurChanged();
}
