/*
 * PAM API Qt wrapper
 * SPDX-FileCopyrightText: 2013 Martin Bříza <mbriza@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */
#include "PamHandle.h"
#include "PamBackend.h"

#include <QtCore/QDebug>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QTextStream>

#include <cerrno>
#include <cstring>

namespace PLASMALOGIN
{
bool PamHandle::putEnv(const QProcessEnvironment &env)
{
    const auto envs = env.toStringList();
    for (const QString &s : envs) {
        m_result = pam_putenv(m_handle, qPrintable(s));
        if (m_result != PAM_SUCCESS) {
            qWarning() << "PamHandle: putEnv:" << pam_strerror(m_handle, m_result);
            return false;
        }
    }
    return true;
}

QProcessEnvironment PamHandle::getEnv()
{
    QProcessEnvironment env;
    // get pam environment
    char **envlist = pam_getenvlist(m_handle);
    if (envlist == NULL) {
        qWarning() << "PamHandle: getEnv: Returned NULL";
        return env;
    }

    // copy it to the env map
    for (int i = 0; envlist[i] != nullptr; ++i) {
        QString s = QString::fromLocal8Bit(envlist[i]);

        // find equal sign
        int index = s.indexOf(QLatin1Char('='));

        // add to the hash
        if (index != -1) {
            env.insert(s.left(index), s.mid(index + 1));
        }

        free(envlist[i]);
    }
    free(envlist);
    return env;
}

bool PamHandle::chAuthTok(int flags)
{
    m_result = pam_chauthtok(m_handle, flags | m_silent);
    if (m_result != PAM_SUCCESS) {
        qWarning() << "PamHandle: chAuthTok:" << pam_strerror(m_handle, m_result);
    }
    return m_result == PAM_SUCCESS;
}

bool PamHandle::acctMgmt(int flags)
{
    m_result = pam_acct_mgmt(m_handle, flags | m_silent);
    if (m_result == PAM_NEW_AUTHTOK_REQD) {
        // TODO see if this should really return the value or just true regardless of the outcome
        return chAuthTok(PAM_CHANGE_EXPIRED_AUTHTOK);
    } else if (m_result != PAM_SUCCESS) {
        qWarning() << "PamHandle: acctMgmt:" << pam_strerror(m_handle, m_result);
        return false;
    }
    return true;
}

bool PamHandle::authenticate(int flags)
{
    qDebug() << "PamHandle: Authenticating...";
    m_result = pam_authenticate(m_handle, flags | m_silent);
    if (m_result != PAM_SUCCESS) {
        qCritical() << "PamHandle: authenticate FAILED:" << m_result << "-" << pam_strerror(m_handle, m_result);
    } else {
        qDebug() << "PamHandle: Authentication successful";
    }
    return m_result == PAM_SUCCESS;
}

bool PamHandle::setCred(int flags)
{
    m_result = pam_setcred(m_handle, flags | m_silent);
    if (m_result != PAM_SUCCESS) {
        qWarning() << "PamHandle: setCred:" << pam_strerror(m_handle, m_result);
    }
    return m_result == PAM_SUCCESS;
}

bool PamHandle::openSession()
{
    m_result = pam_open_session(m_handle, m_silent);
    if (m_result != PAM_SUCCESS) {
        qWarning() << "PamHandle: openSession:" << pam_strerror(m_handle, m_result);
    }
    m_open = m_result == PAM_SUCCESS;
    return m_open;
}

bool PamHandle::closeSession()
{
    m_result = pam_close_session(m_handle, m_silent);
    if (m_result != PAM_SUCCESS) {
        qWarning() << "PamHandle: closeSession:" << pam_strerror(m_handle, m_result);
    }
    return m_result == PAM_SUCCESS;
}

bool PamHandle::isOpen() const
{
    return m_open;
}

bool PamHandle::setItem(int item_type, const void *item)
{
    m_result = pam_set_item(m_handle, item_type, item);
    if (m_result != PAM_SUCCESS) {
        qWarning() << "PamHandle: setItem:" << pam_strerror(m_handle, m_result);
    }
    return m_result == PAM_SUCCESS;
}

const void *PamHandle::getItem(int item_type)
{
    const void *item;
    m_result = pam_get_item(m_handle, item_type, &item);
    if (m_result != PAM_SUCCESS) {
        qWarning() << "PamHandle: getItem:" << pam_strerror(m_handle, m_result);
    }
    return item;
}

int PamHandle::converse(int n, const struct pam_message **msg, struct pam_response **resp, void *data)
{
    qDebug() << "PamHandle: Preparing to converse...";
    PamBackend *c = static_cast<PamBackend *>(data);
    return c->converse(n, msg, resp);
}

bool PamHandle::start(const QString &service, const QString &user)
{
    const QStringList pamSearchPaths = {QStringLiteral("/etc/pam.d/"), QStringLiteral("/usr/local/etc/pam.d/"), QStringLiteral("/usr/lib/pam.d/")};

    QString selectedPamFile;
    for (const QString &path : pamSearchPaths) {
        const QString pamPath = path + service;
        if (QFileInfo::exists(pamPath)) {
            selectedPamFile = pamPath;
            break;
        }
    }

    if (user.isEmpty()) {
        m_result = pam_start(qPrintable(service), NULL, &m_conv, &m_handle);
    } else {
        m_result = pam_start(qPrintable(service), qPrintable(user), &m_conv, &m_handle);
    }
    if (m_result != PAM_SUCCESS) {
        if (!selectedPamFile.isEmpty()) {
            QFileInfo fi(selectedPamFile);
            qCritical() << "PamHandle: start FAILED for service:" << service << "error:" << m_result << "-" << pam_strerror(m_handle, m_result);
            qCritical() << "PamHandle: start FAILED diagnostics: errno:" << errno << QStringLiteral("(%1)").arg(strerror(errno));
        } else {
            m_specificError = QStringLiteral("PAM configuration file not found.");
            qCritical() << "PamHandle: " << m_specificError;
        }
        return false;
    } else {
        qDebug() << "PamHandle: Starting...";
    }
    return true;
}

bool PamHandle::end(int flags)
{
    if (!m_handle) {
        return false;
    }
    m_result = pam_end(m_handle, m_silent | flags);
    if (m_result != PAM_SUCCESS) {
        qWarning() << "PamHandle: end:" << pam_strerror(m_handle, m_result);
        return false;
    } else {
        qDebug() << "PamHandle: Ended.";
    }
    m_handle = NULL;
    return true;
}

QString PamHandle::errorString()
{
    if (!m_specificError.isEmpty()) {
        return m_specificError;
    }
    return QString::fromLocal8Bit(pam_strerror(m_handle, m_result));
}

PamHandle::PamHandle(PamBackend *parent)
    : m_conv{&PamHandle::converse, parent}
{ // create context
}

PamHandle::~PamHandle()
{
    // stop service
    end();
}
}
