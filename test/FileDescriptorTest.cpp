/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "filedescriptor.h"
#include "VirtualTerminal.h"

#include <QtTest>

#include <errno.h>
#include <fcntl.h>
#include <type_traits>
#include <unistd.h>
#include <utility>

using SONICLOGIN::FileDescriptor;
using SONICLOGIN::VirtualTerminal::Terminal;

static_assert(!std::is_copy_constructible_v<FileDescriptor>);
static_assert(!std::is_copy_constructible_v<Terminal>);
static_assert(std::is_move_constructible_v<Terminal>);

class FileDescriptorTest : public QObject {
    Q_OBJECT

    static bool isDescriptorClosed(int fd)
    {
        return ::fcntl(fd, F_GETFD) == -1 && errno == EBADF;
    }

private Q_SLOTS:
    void emptyDescriptor()
    {
        FileDescriptor fd;
        QVERIFY(!fd.isValid());
        QCOMPARE(fd.get(), -1);
        QVERIFY(!fd.duplicate().isValid());
        QCOMPARE(fd.take(), -1);
        fd.reset();
    }

    void scopeClosesDescriptor()
    {
        int fds[2];
        QVERIFY(::pipe(fds) == 0);
        FileDescriptor writer(fds[1]);
        {
            FileDescriptor reader(fds[0]);
            QVERIFY(reader.isValid());
        }
        QVERIFY(isDescriptorClosed(fds[0]));
    }

    void moveConstructionTransfersOwnership()
    {
        int fds[2];
        QVERIFY(::pipe(fds) == 0);
        FileDescriptor writer(fds[1]);
        FileDescriptor source(fds[0]);
        {
            FileDescriptor destination(std::move(source));
            QVERIFY(!source.isValid());
            QCOMPARE(destination.get(), fds[0]);
            source.reset();
            QVERIFY(!isDescriptorClosed(fds[0]));
        }
        QVERIFY(isDescriptorClosed(fds[0]));
    }

    void moveAssignmentClosesOldDescriptor()
    {
        int first[2], second[2];
        QVERIFY(::pipe(first) == 0);
        FileDescriptor firstWriter(first[1]), source(first[0]);
        QVERIFY(::pipe(second) == 0);
        FileDescriptor secondWriter(second[1]), destination(second[0]);
        destination = std::move(source);
        QVERIFY(isDescriptorClosed(second[0]));
        QVERIFY(!source.isValid());
        QCOMPARE(destination.get(), first[0]);
        destination.reset();
        QVERIFY(isDescriptorClosed(first[0]));
    }

    void selfMoveKeepsDescriptor()
    {
        int fds[2];
        QVERIFY(::pipe(fds) == 0);
        FileDescriptor writer(fds[1]), reader(fds[0]);
        FileDescriptor &alias = reader;
        reader = std::move(alias);
        QVERIFY(reader.isValid());
        QCOMPARE(reader.get(), fds[0]);
        QVERIFY(!isDescriptorClosed(fds[0]));
    }

    void duplicateIsIndependentAndCloseOnExec()
    {
        int fds[2];
        QVERIFY(::pipe(fds) == 0);
        FileDescriptor writer(fds[1]), reader(fds[0]);
        FileDescriptor copy = reader.duplicate();
        QVERIFY(copy.isValid());
        QVERIFY(copy.get() != reader.get());
        QVERIFY(::fcntl(copy.get(), F_GETFD) & FD_CLOEXEC);
        reader.reset();
        QVERIFY(isDescriptorClosed(fds[0]));
        QVERIFY(!isDescriptorClosed(copy.get()));
        const char byte = 'x';
        QVERIFY(::write(writer.get(), &byte, 1) == 1);
        QVERIFY(copy.isReadable());
        char result = 0;
        QVERIFY(::read(copy.get(), &result, 1) == 1);
        QCOMPARE(result, byte);
    }

    void takeReleasesOwnership()
    {
        int fds[2];
        QVERIFY(::pipe(fds) == 0);
        FileDescriptor writer(fds[1]);
        {
            FileDescriptor reader(fds[0]);
            QCOMPARE(reader.take(), fds[0]);
            QVERIFY(!reader.isValid());
        }
        QVERIFY(!isDescriptorClosed(fds[0]));
        QVERIFY(::close(fds[0]) == 0);
    }

    void terminalValidityRequiresDescriptor()
    {
        Terminal empty;
        QVERIFY(!empty.isValid());
        QCOMPARE(empty.tty(), -1);
        Terminal unreserved(5, FileDescriptor());
        QVERIFY(!unreserved.isValid());
        QCOMPARE(unreserved.tty(), -1);
    }

    void terminalMoveAndDuplicationHoldReservation()
    {
        int fds[2];
        QVERIFY(::pipe(fds) == 0);
        FileDescriptor writer(fds[1]);
        Terminal source(5, FileDescriptor(fds[0]));
        Terminal moved(std::move(source));
        QVERIFY(!source.isValid());
        QCOMPARE(source.tty(), -1);
        QCOMPARE(moved.tty(), 5);
        Terminal duplicate(moved.tty(), moved.ttyFd().duplicate());
        moved = {};
        QVERIFY(isDescriptorClosed(fds[0]));
        QVERIFY(duplicate.isValid());
        const int duplicateFd = duplicate.ttyFd().get();
        QVERIFY(::fcntl(duplicateFd, F_GETFD) & FD_CLOEXEC);
        duplicate = {};
        QVERIFY(isDescriptorClosed(duplicateFd));
    }
};

QTEST_GUILESS_MAIN(FileDescriptorTest)

#include "FileDescriptorTest.moc"
