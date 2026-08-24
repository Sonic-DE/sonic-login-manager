/*
 *  SPDX-FileCopyrightText: 2026 Oliver Beard <olib141@outlook.com>
 *
 *  SPDX-License-Identifier: LGPL-2.0-or-later
 */

pragma Singleton

import QtQuick

import org.kde.sonic.login as SonicLogin

Item {
    id: greeterState

    enum LoginState {
        UserList = 0,
        UserPrompt = 1
    }

    QtObject {
        id: internal

        property var activeWindow: null
        property bool initialized: false
        property var timeoutInhibitors: []
    }

    Binding {
        target: SonicLogin.BlurScreenBridge
        property: "activeWindow"
        value: internal.activeWindow
    }

    readonly property var activeWindow: internal.activeWindow

    // Shared state

    readonly property int beyondUserLimit: SonicLogin.UserModel.rowCount() === 0 || SonicLogin.UserModel.rowCount() > 7

    property int loginState: GreeterState.LoginState.UserList

    onLoginStateChanged: {
        clearPasswords();

        if (internal.initialized) {
            updateSessionForUser(currentUsername());
        }
    }

    property int sessionIndex: 0

    Component.onCompleted: {
        // indexOfData will return -1 if passed an empty string, which these are by default
        let preselectedSessionIndex = SonicLogin.SessionModel.indexOfData(SonicLogin.Settings.preselectedSession, SonicLogin.SessionModel.FileNameRole);
        let user = listedUsername() || SonicLogin.StateConfig.lastLoggedInUser;
        let lastLoggedInSessionIndex = SonicLogin.SessionModel.indexOfData(getLastLoggedInSessionForUser(user), SonicLogin.SessionModel.FileNameRole);

        if (preselectedSessionIndex != -1) {
            sessionIndex = preselectedSessionIndex;
        } else if (lastLoggedInSessionIndex != -1) {
            sessionIndex = lastLoggedInSessionIndex;
        }
        internal.initialized = true;
    }

    property int userListIndex: {
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

    onUserListIndexChanged: {
        if (internal.initialized && loginState === GreeterState.LoginState.UserList && !beyondUserLimit) {
            updateSessionForUser(listedUsername());
        }
    }

    property string userListPassword: ""

    property string userPromptUsername: ""
    property string userPromptPassword: ""

    onUserPromptUsernameChanged: {
        if (internal.initialized && (loginState === GreeterState.LoginState.UserPrompt || beyondUserLimit)) {
            updateSessionForUser(userPromptUsername);
        }
    }

    property bool showPassword: false

    // Shared functionality

    readonly property bool hasTimeoutInhibitors: internal.timeoutInhibitors.length > 0
    readonly property bool greeterHasSessionButtonPopup: hasTimeoutInhibitors

    function setTimeoutInhibited(owner, inhibited) {
        let owners = internal.timeoutInhibitors.filter(candidate => candidate !== owner);
        if (inhibited) {
            owners.push(owner);
        }
        internal.timeoutInhibitors = owners;
    }

    function setSessionButtonPopupVisible(owner, visible) {
        setTimeoutInhibited(owner, visible);
    }

    readonly property bool inhibitGreeterTimeout: {
        if (greeterState.loginState === GreeterState.LoginState.UserList && greeterState.userListPassword.length > 0) {
            // We're on the user list and a password is entered
            return true;
        } else if (greeterState.loginState === GreeterState.LoginState.UserPrompt && greeterState.userPromptPassword.length > 0) {
            // We're on the user prompt and a password is entered
            return true;
        } else if (greeterState.hasTimeoutInhibitors) {
            return true;
        }

        // inputPanel.keyboardActive

        // No reason to block timeout
        return false;
    }
    onInhibitGreeterTimeoutChanged: {
        let greeterShouldTimeOut = greeterState.activeWindow !== null && !greeterState.inhibitGreeterTimeout;
        if (greeterTimeoutTimer.running !== greeterShouldTimeOut) {
            greeterTimeoutTimer.running = greeterShouldTimeOut;
        }
    }

    Timer {
        id: greeterTimeoutTimer
        running: false
        interval: 10000
        onTriggered: {
            if (internal.activeWindow) {
                greeterState.showPassword = false;
                timeoutWindow(internal.activeWindow);
            }
        }
    }

    function clearPasswords(): void {
        userListPassword = "";
        userPromptPassword = "";
    }

    function activateWindow(window): void {
        if (!window) {
            return;
        }

        internal.activeWindow = window;

        window.requestActivate();

        if (!inhibitGreeterTimeout) {
            greeterTimeoutTimer.restart();
        }
    }

    function timeoutWindow(window): void {
        if (internal.activeWindow == window) {
            internal.activeWindow = null;
        }

        greeterTimeoutTimer.stop();
    }

    // Remember last logged in user/session

    property string lastLoggedInUser
    property string lastLoggedInSession

    function handleLoginRequest(username, password) {
        greeterState.lastLoggedInUser = username;

        let sessionType = SonicLogin.SessionModel.data(SonicLogin.SessionModel.index(greeterState.sessionIndex, 0), SonicLogin.SessionModel.TypeRole);
        let sessionFileName = SonicLogin.SessionModel.data(SonicLogin.SessionModel.index(greeterState.sessionIndex, 0), SonicLogin.SessionModel.FileNameRole);
        greeterState.lastLoggedInSession = sessionFileName;

        SonicLogin.Authenticator.login(username, password, sessionType, sessionFileName);
    }

    property var lastLoggedInSessions: {
        let lastLoggedInUser = SonicLogin.StateConfig.lastLoggedInUser;
        let sessionsJson = SonicLogin.StateConfig.lastLoggedInSession;
        // Usernames must be data keys, including names such as "__proto__".
        let result = Object.create(null);
        if (sessionsJson !== "") {
            try {
                let parsed = JSON.parse(sessionsJson);
                if (parsed !== null && typeof parsed === "object" && !Array.isArray(parsed)) {
                    for (let user of Object.keys(parsed)) {
                        if (user !== "" && typeof parsed[user] === "string" && parsed[user] !== "") {
                            result[user] = parsed[user];
                        }
                    }
                }
            } catch (e) {
                if (lastLoggedInUser !== "") {
                    result[lastLoggedInUser] = sessionsJson;
                }
            }
        }

        // Keep users absent from the visible model: they may log in via the prompt.
        return result;
    }

    function getLastLoggedInSessionForUser(username) {
        return greeterState.lastLoggedInSessions?.[username] ?? "";
    }

    function setLastLoggedInSessionForUser(username, sessionPath) {
        greeterState.lastLoggedInSessions[username] = sessionPath;
        SonicLogin.StateConfig.lastLoggedInSession = JSON.stringify(greeterState.lastLoggedInSessions);
    }

    function listedUsername() {
        return SonicLogin.UserModel.data(SonicLogin.UserModel.index(userListIndex, 0), SonicLogin.UserModel.NameRole) || "";
    }

    function currentUsername() {
        return loginState === GreeterState.LoginState.UserPrompt || beyondUserLimit ? userPromptUsername : listedUsername();
    }

    function updateSessionForUser(username) {
        let session = getLastLoggedInSessionForUser(username);
        let lastLoggedInSessionIndex = SonicLogin.SessionModel.indexOfData(session, SonicLogin.SessionModel.FileNameRole);
        if (lastLoggedInSessionIndex != -1) {
            greeterState.sessionIndex = lastLoggedInSessionIndex;
        }
    }

    Connections {
        target: SonicLogin.Authenticator

        function onLoginSucceeded() {
            // Migrate the legacy session under its original user before changing that user.
            setLastLoggedInSessionForUser(greeterState.lastLoggedInUser, greeterState.lastLoggedInSession);
            SonicLogin.StateConfig.lastLoggedInUser = greeterState.lastLoggedInUser;
            SonicLogin.StateConfig.save();
        }

        function onLoginFailed() {
            clearPasswords();
        }
    }
}
