/*
 * Session process wrapper
 * SPDX-FileCopyrightText: 2021 Aleix Pol Gonzalez <aleixpol@kde.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

/**
 * This application sole purpose is to launch an X11 rootless compositor compositor (first
 * argument) and as soon as it's set up to launch a client (second argument)
 */

#include "MessageHandler.h"
#include "SignalHandler.h"
#include "xorguserhelper.h"

#include <QFileInfo>
#include <QCoreApplication>
#include <QDebug>
#include <QProcess>
#include <QTextStream>
#include <signal.h>
#include <unistd.h>

void X11UserHelperMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    PLASMALOGIN::messageHandler(type, context, QStringLiteral("X11UserHelper: "), msg);
}

int main(int argc, char **argv)
{
    qInstallMessageHandler(X11UserHelperMessageHandler);
    QCoreApplication app(argc, argv);
    PLASMALOGIN::SignalHandler s;
    QObject::connect(&s, &PLASMALOGIN::SignalHandler::sigtermReceived, &app, [&argc, &argv] {
        pid_t ppid = getppid();
        QString parentName = PLASMALOGIN::getProcessNameByPid(ppid);
        qWarning() << "X11UserHelper: Received SIGTERM - diagnostic information:"
                   << "parentProcess(PPID)=" << ppid << "=" << parentName
                   << "arguments=" << QCoreApplication::arguments()
                   << "displayServerCmd=" << (argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("<null>"))
                   << "sessionCmd=" << (argc > 2 ? QString::fromLocal8Bit(argv[2]) : QStringLiteral("<null>"))
                   << "uid=" << ::getuid();
        QCoreApplication::instance()->exit(-1);
    });
    s.addCustomSignal(SIGQUIT);
    QObject::connect(&s, &PLASMALOGIN::SignalHandler::customSignalReceived, &app, [&argc, &argv](int signal) {
        if (signal == SIGQUIT) {
            pid_t ppid = getppid();
            QString parentName = PLASMALOGIN::getProcessNameByPid(ppid);
            qWarning() << "X11UserHelper: Received SIGQUIT (signal 3) - diagnostic information:"
                       << "parentProcess(PPID)=" << ppid << "=" << parentName
                       << "arguments=" << QCoreApplication::arguments()
                       << "displayServerCmd=" << (argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("<null>"))
                       << "sessionCmd=" << (argc > 2 ? QString::fromLocal8Bit(argv[2]) : QStringLiteral("<null>"))
                       << "uid=" << ::getuid();
            QCoreApplication::instance()->exit(-1);
        }
    });
    
    if (::getuid() == 0) {
        qCritical() << "HelperStartX11User: ERROR - cannot run as root!";
        return 33;
    }
    if (argc != 3) {
        qCritical() << "HelperStartX11User: ERROR - wrong number of arguments:" << argc << "(expected 3)";
        QTextStream(stderr) << "Wrong number of arguments\n";
        return 33;
    }

    using namespace PLASMALOGIN;

    XOrgUserHelper helper;
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &helper, [&helper] {
        qDebug("quitting helper-start-x11");
        helper.stop();
    });
    QObject::connect(&helper, &XOrgUserHelper::displayChanged, &app, [&helper, &app] {
        auto args = QProcess::splitCommand(app.arguments()[2]);
        if (args.isEmpty()) {
            qCritical() << "HelperStartX11User: ERROR - no session command to run!";
            app.quit();
            return;
        }

        QString program = args.takeFirst();
        QFileInfo fi(program);
        if (!fi.exists()) {
            qCritical() << "HelperStartX11User: ERROR - executable does not exist:" << program;
        } else if (!fi.isExecutable()) {
            qCritical() << "HelperStartX11User: ERROR - file is not executable:" << program;
        }

        // Force flush to ensure logs appear before any crash
        fflush(stdout);
        fflush(stderr);

        QProcess *process = new QProcess(&app);
        process->setProcessChannelMode(QProcess::ForwardedChannels);
        process->setProgram(program);
        process->setArguments(args);
        process->setProcessEnvironment(helper.sessionEnvironment());

        // Log process errors
        QObject::connect(process, &QProcess::errorOccurred, &app, [program](QProcess::ProcessError error) {
            qCritical() << "HelperStartX11User: session process ERROR:" << error << program;
        });

        QObject::connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), &app, &QCoreApplication::quit);
        QObject::connect(process, &QProcess::errorOccurred, &app, [program](QProcess::ProcessError error) {
            qCritical() << "HelperStartX11User: session process" << program << "error occurred:" << error;
        });

        process->start();
    });

    helper.start(app.arguments()[1]);
    return app.exec();
}
