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
            qWarning() << "[PAM] putEnv:" << pam_strerror(m_handle, m_result);
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
        qWarning() << "[PAM] getEnv: Returned NULL";
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
        qWarning() << "[PAM] chAuthTok:" << pam_strerror(m_handle, m_result);
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
        qWarning() << "[PAM] acctMgmt:" << pam_strerror(m_handle, m_result);
        return false;
    }
    return true;
}

bool PamHandle::authenticate(int flags)
{
    qDebug() << "[PAM] Authenticating...";
    m_result = pam_authenticate(m_handle, flags | m_silent);
    if (m_result != PAM_SUCCESS) {
        qCritical() << "[PAM] authenticate FAILED:" << m_result << "-" << pam_strerror(m_handle, m_result);
    } else {
        qDebug() << "[PAM] Authentication successful";
    }
    return m_result == PAM_SUCCESS;
}

bool PamHandle::setCred(int flags)
{
    m_result = pam_setcred(m_handle, flags | m_silent);
    if (m_result != PAM_SUCCESS) {
        qWarning() << "[PAM] setCred:" << pam_strerror(m_handle, m_result);
    }
    return m_result == PAM_SUCCESS;
}

bool PamHandle::openSession()
{
    m_result = pam_open_session(m_handle, m_silent);
    if (m_result != PAM_SUCCESS) {
        qWarning() << "[PAM] openSession:" << pam_strerror(m_handle, m_result);
    }
    m_open = m_result == PAM_SUCCESS;
    return m_open;
}

bool PamHandle::closeSession()
{
    m_result = pam_close_session(m_handle, m_silent);
    if (m_result != PAM_SUCCESS) {
        qWarning() << "[PAM] closeSession:" << pam_strerror(m_handle, m_result);
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
        qWarning() << "[PAM] setItem:" << pam_strerror(m_handle, m_result);
    }
    return m_result == PAM_SUCCESS;
}

const void *PamHandle::getItem(int item_type)
{
    const void *item;
    m_result = pam_get_item(m_handle, item_type, &item);
    if (m_result != PAM_SUCCESS) {
        qWarning() << "[PAM] getItem:" << pam_strerror(m_handle, m_result);
    }
    return item;
}

int PamHandle::converse(int n, const struct pam_message **msg, struct pam_response **resp, void *data)
{
    qDebug() << "[PAM] Preparing to converse...";
    PamBackend *c = static_cast<PamBackend *>(data);
    return c->converse(n, msg, resp);
}

bool PamHandle::start(const QString &service, const QString &user)
{
    const QString pamEtcPath = QStringLiteral("/etc/pam.d/") + service;
    const QString pamLocalEtcPath = QStringLiteral("/usr/local/etc/pam.d/") + service;
    const bool hasEtc = QFileInfo::exists(pamEtcPath);
    const bool hasLocalEtc = QFileInfo::exists(pamLocalEtcPath);


    const QString selectedPamFile = hasEtc ? pamEtcPath : (hasLocalEtc ? pamLocalEtcPath : QString());
    if (!selectedPamFile.isEmpty()) {
        QFile f(selectedPamFile);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream ts(&f);
            QStringList preview;
            for (int i = 0; i < 8 && !ts.atEnd(); ++i) {
                preview << ts.readLine();
            }
        } else {
            qWarning() << "[PAM] start: could not open service file for preview:" << selectedPamFile;
        }
    }

    if (user.isEmpty()) {
        m_result = pam_start(qPrintable(service), NULL, &m_conv, &m_handle);
    } else {
        m_result = pam_start(qPrintable(service), qPrintable(user), &m_conv, &m_handle);
    }
    if (m_result != PAM_SUCCESS) {
        qCritical() << "[PAM] start FAILED for service:" << service << "error:" << m_result << "-" << pam_strerror(m_handle, m_result);
        qCritical() << "[PAM] start FAILED diagnostics: errno=" << errno << "(" << strerror(errno) << ")";
        if (!selectedPamFile.isEmpty()) {
            QFileInfo fi(selectedPamFile);
            qCritical() << "[PAM] start FAILED service file metadata:" << selectedPamFile
                        << "exists=" << fi.exists()
                        << "readable=" << fi.isReadable()
                        << "owner=" << fi.owner()
                        << "group=" << fi.group()
                        << "permissions=" << fi.permissions();
        }
        qCritical() << "[PAM] Check that" << QStringLiteral("/etc/pam.d/%1").arg(service) << "or" << QStringLiteral("/usr/local/etc/pam.d/%1").arg(service)
                    << "exists and contains valid FreeBSD PAM syntax/modules";
        return false;
    } else {
        qDebug() << "[PAM] Starting...";
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
        qWarning() << "[PAM] end:" << pam_strerror(m_handle, m_result);
        return false;
    } else {
        qDebug() << "[PAM] Ended.";
    }
    m_handle = NULL;
    return true;
}

QString PamHandle::errorString()
{
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
