/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "../src/frontend/greeter/greetereventfilter.h"

#include <QKeyEvent>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QtTest>

#include <memory>

class TestWindow : public QQuickWindow {
public:
    int keyPressCount = 0;

protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        ++keyPressCount;
        QQuickWindow::keyPressEvent(event);
    }
};

class GreeterEventFilterTest : public QObject {
    Q_OBJECT

private:
    static void sendKey(QQuickWindow *window, int key = Qt::Key_A)
    {
        QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &event);
    }

private Q_SLOTS:
    void initTestCase()
    {
        qmlRegisterType<GreeterEventFilter>("org.kde.sonic.login", 0, 1, "GreeterEventFilter");
        // The greeter's qmldir refers to resources embedded in its executable,
        // not this test. Keep this unit-test module limited to the C++ type.
        QVERIFY(qmlProtectModule("org.kde.sonic.login", 0));
    }

    void windowsAreIsolated()
    {
        TestWindow firstWindow;
        TestWindow secondWindow;
        GreeterEventFilter firstFilter;
        GreeterEventFilter secondFilter;
        firstFilter.setWindow(&firstWindow);
        secondFilter.setWindow(&secondWindow);
        QSignalSpy firstKeys(&firstFilter, &GreeterEventFilter::keyPressed);
        QSignalSpy secondKeys(&secondFilter, &GreeterEventFilter::keyPressed);

        sendKey(&firstWindow);
        QCOMPARE(firstKeys.count(), 1);
        QCOMPARE(secondKeys.count(), 0);
        QCOMPARE(firstWindow.keyPressCount, 1);

        sendKey(&secondWindow);
        QCOMPARE(firstKeys.count(), 1);
        QCOMPARE(secondKeys.count(), 1);
        QCOMPARE(secondWindow.keyPressCount, 1);
    }

    void windowCanBeChangedOrCleared()
    {
        TestWindow firstWindow;
        TestWindow secondWindow;
        GreeterEventFilter filter;
        QSignalSpy changed(&filter, &GreeterEventFilter::windowChanged);
        QSignalSpy keys(&filter, &GreeterEventFilter::keyPressed);
        QCOMPARE(filter.window(), nullptr);

        filter.setWindow(&firstWindow);
        filter.setWindow(&firstWindow);
        QCOMPARE(changed.count(), 1);
        sendKey(&firstWindow);
        QCOMPARE(keys.count(), 1);

        filter.setWindow(&secondWindow);
        QCOMPARE(filter.window(), &secondWindow);
        QCOMPARE(changed.count(), 2);
        sendKey(&firstWindow);
        QCOMPARE(keys.count(), 1);
        sendKey(&secondWindow);
        QCOMPARE(keys.count(), 2);

        filter.setWindow(nullptr);
        QCOMPARE(filter.window(), nullptr);
        QCOMPARE(changed.count(), 3);
        sendKey(&secondWindow);
        QCOMPARE(keys.count(), 2);
    }

    void destroyedWindowCanBeReplaced()
    {
        GreeterEventFilter filter;
        {
            TestWindow window;
            filter.setWindow(&window);
        }
        QCOMPARE(filter.window(), nullptr);

        TestWindow replacement;
        filter.setWindow(&replacement);
        QSignalSpy keys(&filter, &GreeterEventFilter::keyPressed);
        sendKey(&replacement);
        QCOMPARE(keys.count(), 1);
    }

    void destroyedFilterIsDetached()
    {
        TestWindow window;
        {
            GreeterEventFilter filter;
            filter.setWindow(&window);
        }
        sendKey(&window);
        QCOMPARE(window.keyPressCount, 1);
    }

    void escapeIsConsumed()
    {
        if (QGuiApplication::platformName() != QStringLiteral("xcb") && QGuiApplication::platformName() != QStringLiteral("wayland")) {
            QSKIP("KScreen DPMS requires X11 or Wayland; run this test under Xvfb to exercise Escape safely.");
        }

        TestWindow window;
        GreeterEventFilter filter;
        filter.setWindow(&window);
        QSignalSpy escape(&filter, &GreeterEventFilter::escapeKeyPressed);
        QSignalSpy keys(&filter, &GreeterEventFilter::keyPressed);

        sendKey(&window, Qt::Key_Escape);
        QCOMPARE(escape.count(), 1);
        QCOMPARE(keys.count(), 0);
        QCOMPARE(window.keyPressCount, 0);
    }

    void qmlCreatesPerWindowFiltersInSharedEngine()
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import org.kde.sonic.login 0.1 as SonicLogin
            Window {
                id: host
                property int keyCount: 0
                Item {
                    id: root
                    SonicLogin.GreeterEventFilter {
                        window: root.Window.window
                        onKeyPressed: host.keyCount++
                    }
                }
            }
        )",
                          QUrl());
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> first(component.create());
        std::unique_ptr<QObject> second(component.create());
        QVERIFY2(first, qPrintable(component.errorString()));
        QVERIFY2(second, qPrintable(component.errorString()));
        auto *firstWindow = qobject_cast<QQuickWindow *>(first.get());
        auto *secondWindow = qobject_cast<QQuickWindow *>(second.get());
        QVERIFY(firstWindow);
        QVERIFY(secondWindow);

        sendKey(firstWindow);
        QCOMPARE(first->property("keyCount").toInt(), 1);
        QCOMPARE(second->property("keyCount").toInt(), 0);
        sendKey(secondWindow);
        QCOMPARE(first->property("keyCount").toInt(), 1);
        QCOMPARE(second->property("keyCount").toInt(), 1);
    }
};

QTEST_MAIN(GreeterEventFilterTest)

#include "GreeterEventFilterTest.moc"
