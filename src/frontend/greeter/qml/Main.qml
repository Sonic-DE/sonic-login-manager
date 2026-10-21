/*
 * SPDX-FileCopyrightText: Oliver Beard
 * SPDX-FileCopyrightText: David Edmundson
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import QtQuick.Effects

import org.kde.kirigami as Kirigami
import org.kde.breeze.components as BreezeComponents
import org.kde.plasma.private.keyboardindicator as KeyboardIndicator
import org.kde.plasma.workspace.loginlockscreen as LoginLockScreen

import org.kde.sonic.login as SonicLogin

Item {
    id: root
    readonly property bool keyboardActive: footer.virtualKeyboardVisible
    readonly property real availableLoginHeight: {
        const keyboard = Qt.inputMethod.keyboardRectangle;
        if (!keyboardActive || keyboard.width <= 0 || keyboard.height <= 0
            || keyboard.x >= width || keyboard.x + keyboard.width <= 0
            || keyboard.y >= height || keyboard.y + keyboard.height <= 0) {
            return height;
        }
        return Math.max(0, Math.min(height, keyboard.y));
    }
    anchors.fill: parent

    // If we're using software rendering, draw outlines instead of shadows
    // See https://bugs.kde.org/show_bug.cgi?id=398317
    readonly property bool softwareRendering: GraphicsInfo.api === GraphicsInfo.Software

    Kirigami.Theme.colorSet: Kirigami.Theme.Complementary
    Kirigami.Theme.inherit: false

    property string notificationMessage
    property bool authenticationInProgress: false
    property bool authenticationMessageActive: false

    onAuthenticationMessageActiveChanged: SonicLogin.GreeterState.setTimeoutInhibited(root, authenticationMessageActive)
    Component.onDestruction: {
        if (SonicLogin.GreeterState) {
            SonicLogin.GreeterState.setTimeoutInhibited(root, false);
        }
    }

    LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
    LayoutMirroring.childrenInherit: true

    KeyboardIndicator.KeyState {
        id: capsLockState
        key: Qt.Key_CapsLock
    }

    BreezeComponents.RejectPasswordAnimation {
        id: rejectPasswordAnimation
        target: mainStack
    }

    SonicLogin.GreeterEventFilter {
        window: root.Window.window

        onKeyPressed: {
            // callLater, as otherwise 'enter' key press would arrive after waking
            // and the uiVisible check would pass and a login attempt would be made
            Qt.callLater(() => SonicLogin.GreeterState.activateWindow(loginScreenRoot.Window.window));
        }

        onEscapeKeyPressed: {
            SonicLogin.GreeterState.timeoutWindow(loginScreenRoot.Window.window);
            if (footer) {
                footer.hideVirtualKeyboard();
            }
            SonicLogin.GreeterState.clearPasswords();
        }
    }

    MouseArea {
        id: loginScreenRoot
        anchors.fill: parent

        hoverEnabled: true

        property bool uiVisible: SonicLogin.GreeterState.activeWindow === Window.window

        cursorShape: uiVisible ? Qt.ArrowCursor : Qt.BlankCursor

        onPressed: SonicLogin.GreeterState.activateWindow(Window.window);
        onPositionChanged: SonicLogin.GreeterState.activateWindow(Window.window);

        MultiEffect {
            id: clockShadow
            anchors.fill: clock
            source: clock
            visible: !softwareRendering && clock.visible
            shadowEnabled: true
            shadowHorizontalOffset: 0
            shadowVerticalOffset: 0.8
            shadowColor: "black"
            shadowOpacity: 1
            shadowBlur: 1
            // MultiEffect uses a different blur kernel; spread is not a scale.
            blurMax: 11
            shadowScale: 1
            opacity: loginScreenRoot.uiVisible ? 0 : 1
            Behavior on opacity {
                OpacityAnimator {
                    duration: Kirigami.Units.veryLongDuration * 2
                    easing.type: Easing.InOutQuad
                }
            }
        }

        BreezeComponents.Clock {
            id: clock
            property Item shadow: clockShadow
            visible: !root.keyboardActive && y > 0 && Settings.showClock
            anchors.horizontalCenter: parent.horizontalCenter
            y: (userListComponent.userList.y + mainStack.y)/2 - height/2
            Layout.alignment: Qt.AlignBaseline
        }

        QQC2.StackView {
            id: mainStack
            anchors.left: parent.left
            anchors.right: parent.right

            height: root.keyboardActive ? root.availableLoginHeight : root.height + Kirigami.Units.gridUnit * 3

            hoverEnabled: true

            focus: true

            opacity: loginScreenRoot.uiVisible ? 1 : 0
            Behavior on opacity {
                OpacityAnimator {
                    duration: Kirigami.Units.longDuration
                }
            }

            Connections {
                target: SonicLogin.GreeterState

                function onLoginStateChanged() {
                    switch (SonicLogin.GreeterState.loginState) {
                        case SonicLogin.GreeterState.LoginState.UserList:
                            if (mainStack.depth !== 2) { return; /* already showing user list */ }
                            mainStack.pop();
                            return;
                        case SonicLogin.GreeterState.LoginState.UserPrompt:
                            if (mainStack.depth !== 1) { return; /* already showing user prompt */ }
                            mainStack.push(userPromptComponent);
                            return;
                        default:
                            console.warn("Cannot synchronize login state:", SonicLogin.GreeterState.loginState);
                    }
                }
            }

            initialItem: Login {
                id: userListComponent
                keyboardActive: root.keyboardActive
                userListModel: SonicLogin.UserModel
                loginScreenUiVisible: loginScreenRoot.uiVisible
                userListCurrentIndex: {
                    // indexOfData will return -1 if passed an empty string, which these are by default
                    let preselectedUserIndex = SonicLogin.UserModel.indexOfData(SonicLogin.Settings.preselectedUser, SonicLogin.UserModel.NameRole);
                    let lastLoggedInUserIndex = SonicLogin.UserModel.indexOfData(SonicLogin.StateConfig.lastLoggedInUser, SonicLogin.UserModel.NameRole);

                    if (preselectedUserIndex != -1) {
                        return preselectedUserIndex;
                    } else if (lastLoggedInUserIndex != -1) {
                        return lastLoggedInUserIndex;
                    } else {
                        return 0;
                    }
                }

                showUserList: !SonicLogin.GreeterState.beyondUserLimit

                notificationMessage: {
                    const parts = [];
                    if (capsLockState.locked) {
                        parts.push(i18nd("sonic_login", "Caps Lock is on"));
                    }
                    if (root.notificationMessage) {
                        parts.push(root.notificationMessage);
                    }
                    return parts.join(" • ");
                }

                //actionItemsVisible: !inputPanel.keyboardActive
                actionItems: [
                    BreezeComponents.ActionButton {
                        icon.name: "system-hibernate"
                        text: i18ndc("sonic_login", "Suspend to disk", "Hibernate")
                        visible: SonicLogin.Authenticator.canHibernate
                        onClicked: {
                            SonicLogin.GreeterState.clearPasswords();
                            SonicLogin.Authenticator.hibernate();
                        }
                    },
                    BreezeComponents.ActionButton {
                        icon.name: "system-suspend"
                        text: i18ndc("sonic_login", "Suspend to RAM", "Sleep")
                        visible: SonicLogin.Authenticator.canSuspend
                        onClicked: {
                            SonicLogin.GreeterState.clearPasswords();
                            SonicLogin.Authenticator.suspend();
                        }
                    },
                    BreezeComponents.ActionButton {
                        icon.name: "system-reboot"
                        text: i18nd("sonic_login", "Restart")
                        visible: SonicLogin.Authenticator.canReboot
                        onClicked: SonicLogin.Authenticator.reboot()
                    },
                    BreezeComponents.ActionButton {
                        icon.name: "system-shutdown"
                        text: i18nd("sonic_login", "Shut Down")
                        visible: SonicLogin.Authenticator.canShutdown
                        onClicked: SonicLogin.Authenticator.shutdown()
                    },
                    BreezeComponents.ActionButton {
                        icon.name: "system-user-prompt"
                        text: i18ndc("sonic_login", "For switching to a username and password prompt", "Other…")
                        onClicked: SonicLogin.GreeterState.loginState = SonicLogin.GreeterState.LoginState.UserPrompt
                        visible: !userListComponent.showUsernamePrompt
                    }]

                onLoginRequest: (username, password) => root.handleLoginRequest(username, password)
            }

            readonly property real zoomFactor: 1.5

            popEnter: Transition {
                ScaleAnimator {
                    from: mainStack.zoomFactor
                    to: 1
                    duration: Kirigami.Units.veryLongDuration
                    easing.type: Easing.OutCubic
                }
                OpacityAnimator {
                    from: 0
                    to: 1
                    duration: Kirigami.Units.veryLongDuration
                    easing.type: Easing.OutCubic
                }
            }

            popExit: Transition {
                ScaleAnimator {
                    from: 1
                    to: 1 / mainStack.zoomFactor
                    duration: Kirigami.Units.veryLongDuration
                    easing.type: Easing.OutCubic
                }
                OpacityAnimator {
                    from: 1
                    to: 0
                    duration: Kirigami.Units.veryLongDuration
                    easing.type: Easing.OutCubic
                }
            }

            pushEnter: Transition {
                ScaleAnimator {
                    from: 1 / mainStack.zoomFactor
                    to: 1
                    duration: Kirigami.Units.veryLongDuration
                    easing.type: Easing.OutCubic
                }
                OpacityAnimator {
                    from: 0
                    to: 1
                    duration: Kirigami.Units.veryLongDuration
                    easing.type: Easing.OutCubic
                }
            }

            pushExit: Transition {
                ScaleAnimator {
                    from: 1
                    to: mainStack.zoomFactor
                    duration: Kirigami.Units.veryLongDuration
                    easing.type: Easing.OutCubic
                }
                OpacityAnimator {
                    from: 1
                    to: 0
                    duration: Kirigami.Units.veryLongDuration
                    easing.type: Easing.OutCubic
                }
            }
        }

        Component {
            id: userPromptComponent

            Login {
                keyboardActive: root.keyboardActive
                showUsernamePrompt: true
                loginScreenUiVisible: loginScreenRoot.uiVisible
                fontSize: Kirigami.Theme.defaultFont.pointSize + 2

                notificationMessage: {
                    const parts = [];
                    if (capsLockState.locked) {
                        parts.push(i18nd("sonic_login", "Caps Lock is on"));
                    }
                    if (root.notificationMessage) {
                        parts.push(root.notificationMessage);
                    }
                    return parts.join(" • ");
                }

                // using a model rather than a QObject list to avoid QTBUG-75900
                userListModel: ListModel {
                    ListElement {
                        realName: ""
                        icon: ""
                    }
                    Component.onCompleted: {
                        // as we can't bind inside ListElement
                        setProperty(0, "realName", i18nd("sonic_login", "Type in Username and Password"));
                        setProperty(0, "icon", Qt.resolvedUrl(".face.icon").toString());
                    }
                }

                onLoginRequest: (username, password) => root.handleLoginRequest(username, password)

                //actionItemsVisible: !inputPanel.keyboardActive
                actionItems: [
                    BreezeComponents.ActionButton {
                        icon.name: "system-hibernate"
                        text: i18ndc("sonic_login", "Suspend to disk", "Hibernate")
                        visible: SonicLogin.Authenticator.canHibernate
                        onClicked: {
                            SonicLogin.GreeterState.clearPasswords();
                            SonicLogin.Authenticator.hibernate();
                        }
                    },
                    BreezeComponents.ActionButton {
                        icon.name: "system-suspend"
                        text: i18ndc("sonic_login", "Suspend to RAM", "Sleep")
                        visible: SonicLogin.Authenticator.canSuspend
                        onClicked: {
                            SonicLogin.GreeterState.clearPasswords();
                            SonicLogin.Authenticator.suspend();
                        }
                    },
                    BreezeComponents.ActionButton {
                        icon.name: "system-reboot"
                        text: i18nd("sonic_login", "Restart")
                        visible: SonicLogin.Authenticator.canReboot
                        onClicked: SonicLogin.Authenticator.reboot()
                    },
                    BreezeComponents.ActionButton {
                        icon.name: "system-shutdown"
                        text: i18nd("sonic_login", "Shut Down")
                        visible: SonicLogin.Authenticator.canShutdown
                        onClicked: SonicLogin.Authenticator.shutdown()
                    },
                    BreezeComponents.ActionButton {
                        icon.name: "system-user-list"
                        text: i18nd("sonic_login", "List Users")
                        onClicked: SonicLogin.GreeterState.loginState = SonicLogin.GreeterState.LoginState.UserList
                    }
                ]
            }
        }

        LoginLockScreen.Footer {
            id: footer
            visible: !root.keyboardActive

            onVirtualKeyboardVisibleChanged: SonicLogin.GreeterState.setTimeoutInhibited(footer, virtualKeyboardVisible)
            Component.onDestruction: {
                if (SonicLogin.GreeterState) {
                    SonicLogin.GreeterState.setTimeoutInhibited(footer, false);
                }
            }

            onOskActivated: {
                SonicLogin.GreeterState.activateWindow(loginScreenRoot.Window.window);
                if (mainStack.currentItem) {
                    mainStack.currentItem.focusFirstVisibleFormControl();
                }
            }

            Behavior on opacity {
                OpacityAnimator {
                    duration: Kirigami.Units.longDuration
                }
            }
        }
    }

    function handleLoginRequest(username, password) {
        notificationResetTimer.stop();
        root.authenticationMessageActive = false;
        root.authenticationInProgress = true;
        root.notificationMessage = "";
        // GreeterState handles updating stateconfig with user/session of successful login
        SonicLogin.GreeterState.handleLoginRequest(username, password);
    }

    Connections {
        target: SonicLogin.Authenticator

        // e.g. pam_u2f's "cue" asking the user to touch their key. Not auto-cleared
        // like "Login Failed" is: handleLoginRequest() clears it on the next attempt.
        function onInformationMessage(message) {
            notificationResetTimer.stop();
            root.notificationMessage = message;
            root.authenticationMessageActive = message.length > 0;
            if (root.authenticationMessageActive) {
                // Reveal the persistent cue even if the compact keyboard UI was active.
                footer.hideVirtualKeyboard();
                if (root.authenticationInProgress) {
                    SonicLogin.GreeterState.activateWindow(loginScreenRoot.Window.window);
                }
            }
        }

        function onLoginFailed() {
            root.authenticationInProgress = false;
            root.authenticationMessageActive = false;
            notificationMessage = i18nd("sonic_login", "Login Failed");
            notificationResetTimer.restart();

            footer.enabled = true;
            mainStack.enabled = true;
            userListComponent.userList.opacity = 1;

            rejectPasswordAnimation.start();
        }

        function onSocketDisconnected() {
            root.authenticationInProgress = false;
            root.authenticationMessageActive = false;
            notificationResetTimer.stop();
            notificationMessage = i18nd("sonic_login", "Connection to login service lost");

            footer.enabled = true;
            mainStack.enabled = true;
            userListComponent.userList.opacity = 1;

            rejectPasswordAnimation.start();
        }

        function onLoginSucceeded() {
            root.authenticationInProgress = false;
            root.authenticationMessageActive = false;
            notificationResetTimer.stop();
            root.notificationMessage = "";
            mainStack.opacity = 0;
            footer.opacity = 0;
        }
    }

    Timer {
        id: notificationResetTimer
        interval: 3000
        onTriggered: notificationMessage = ""
    }
}
