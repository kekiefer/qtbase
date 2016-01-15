/****************************************************************************
**
** Copyright (C) 2013 David Faure <faure+bluesystems@kde.org>
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the QtCore module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl-3.0.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or (at your option) the GNU General
** Public license version 3 or any later version approved by the KDE Free
** Qt Foundation. The licenses are as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL2 and LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-2.0.html and
** https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "private/qlockfile_p.h"

#include "QtCore/qtemporaryfile.h"
#include "QtCore/qcoreapplication.h"
#include "QtCore/qfileinfo.h"
#include "QtCore/qdebug.h"
#include "QtCore/qdatetime.h"

#include "private/qcore_unix_p.h" // qt_safe_open
#include "private/qabstractfileengine_p.h"
#include "private/qtemporaryfile_p.h"

#if !defined(Q_OS_INTEGRITY)
#include <sys/file.h>  // flock
#endif

#include <sys/types.h> // kill
#include <signal.h>    // kill
#include <unistd.h>    // gethostname

#if defined(Q_OS_OSX)
#   include <libproc.h>
#elif defined(Q_OS_LINUX)
#   include <unistd.h>
#   include <cstdio>
#elif defined(Q_OS_HAIKU)
#   include <kernel/OS.h>
#elif defined(Q_OS_BSD4) && !defined(Q_OS_IOS)
#   include <sys/user.h>
# if defined(__GLIBC__) && defined(__FreeBSD_kernel__)
#   include <sys/cdefs.h>
#   include <sys/param.h>
#   include <sys/sysctl.h>
# else
#   include <libutil.h>
# endif
#endif

QT_BEGIN_NAMESPACE

static QByteArray localHostName() // from QHostInfo::localHostName(), modified to return a QByteArray
{
    QByteArray hostName(512, Qt::Uninitialized);
    if (gethostname(hostName.data(), hostName.size()) == -1)
        return QByteArray();
    hostName.truncate(strlen(hostName.data()));
    return hostName;
}

// ### merge into qt_safe_write?
static qint64 qt_write_loop(int fd, const char *data, qint64 len)
{
    qint64 pos = 0;
    while (pos < len) {
        const qint64 ret = qt_safe_write(fd, data + pos, len - pos);
        if (ret == -1) // e.g. partition full
            return pos;
        pos += ret;
    }
    return pos;
}

int QLockFilePrivate::checkFcntlWorksAfterFlock()
{
#ifndef QT_NO_TEMPORARYFILE
    QTemporaryFile file;
    if (!file.open())
        return 0;
    const int fd = file.d_func()->engine()->handle();
#if defined(LOCK_EX) && defined(LOCK_NB)
    if (flock(fd, LOCK_EX | LOCK_NB) == -1) // other threads, and other processes on a local fs
        return 0;
#endif
    struct flock flockData;
    flockData.l_type = F_WRLCK;
    flockData.l_whence = SEEK_SET;
    flockData.l_start = 0;
    flockData.l_len = 0; // 0 = entire file
    flockData.l_pid = getpid();
    if (fcntl(fd, F_SETLK, &flockData) == -1) // for networked filesystems
        return 0;
    return 1;
#else
    return 0;
#endif
}

static QBasicAtomicInt fcntlOK = Q_BASIC_ATOMIC_INITIALIZER(-1);

/*!
  \internal
  Checks that the OS isn't using POSIX locks to emulate flock().
  OS X is one of those.
*/
static bool fcntlWorksAfterFlock()
{
    int value = fcntlOK.load();
    if (Q_UNLIKELY(value == -1)) {
        value = QLockFilePrivate::checkFcntlWorksAfterFlock();
        fcntlOK.store(value);
    }
    return value == 1;
}

static bool setNativeLocks(int fd)
{
#if defined(LOCK_EX) && defined(LOCK_NB)
    if (flock(fd, LOCK_EX | LOCK_NB) == -1) // other threads, and other processes on a local fs
        return false;
#endif
    struct flock flockData;
    flockData.l_type = F_WRLCK;
    flockData.l_whence = SEEK_SET;
    flockData.l_start = 0;
    flockData.l_len = 0; // 0 = entire file
    flockData.l_pid = getpid();
    if (fcntlWorksAfterFlock() && fcntl(fd, F_SETLK, &flockData) == -1) // for networked filesystems
        return false;
    return true;
}

QLockFile::LockError QLockFilePrivate::tryLock_sys()
{
    // Assemble data, to write in a single call to write
    // (otherwise we'd have to check every write call)
    // Use operator% from the fast builder to avoid multiple memory allocations.
    QByteArray fileData = QByteArray::number(QCoreApplication::applicationPid()) % '\n'
                          % QCoreApplication::applicationName().toUtf8() % '\n'
                          % localHostName() % '\n';

    const QByteArray lockFileName = QFile::encodeName(fileName);
    const int fd = qt_safe_open(lockFileName.constData(), O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0) {
        switch (errno) {
        case EEXIST:
            return QLockFile::LockFailedError;
        case EACCES:
        case EROFS:
            return QLockFile::PermissionError;
        default:
            return QLockFile::UnknownError;
        }
    }
    // Ensure nobody else can delete the file while we have it
    if (!setNativeLocks(fd)) {
        const int errnoSaved = errno;
        qWarning() << "setNativeLocks failed:" << qt_error_string(errnoSaved);
    }

    if (qt_write_loop(fd, fileData.constData(), fileData.size()) < fileData.size()) {
        close(fd);
        if (!QFile::remove(fileName))
            qWarning("QLockFile: Could not remove our own lock file %s.", qPrintable(fileName));
        return QLockFile::UnknownError; // partition full
    }

    // We hold the lock, continue.
    fileHandle = fd;

    // Sync to disk if possible. Ignore errors (e.g. not supported).
#if defined(_POSIX_SYNCHRONIZED_IO) && _POSIX_SYNCHRONIZED_IO > 0
    fdatasync(fileHandle);
#else
    fsync(fileHandle);
#endif

    return QLockFile::NoError;
}

bool QLockFilePrivate::removeStaleLock()
{
    const QByteArray lockFileName = QFile::encodeName(fileName);
    const int fd = qt_safe_open(lockFileName.constData(), O_WRONLY, 0644);
    if (fd < 0) // gone already?
        return false;
    bool success = setNativeLocks(fd) && (::unlink(lockFileName) == 0);
    close(fd);
    return success;
}

bool QLockFilePrivate::isApparentlyStale() const
{
    qint64 pid;
    QString hostname, appname;
    if (getLockInfo(&pid, &hostname, &appname)) {
        if (hostname.isEmpty() || hostname == QString::fromLocal8Bit(localHostName())) {
            if (::kill(pid, 0) == -1 && errno == ESRCH)
                return true; // PID doesn't exist anymore
            const QString processName = processNameByPid(pid);
            if (!processName.isEmpty()) {
                QFileInfo fi(appname);
                if (fi.isSymLink())
                    fi.setFile(fi.symLinkTarget());
                if (processName != fi.fileName())
                    return true; // PID got reused by a different application.
            }
        }
    }
    const qint64 age = QFileInfo(fileName).lastModified().msecsTo(QDateTime::currentDateTime());
    return staleLockTime > 0 && age > staleLockTime;
}

QString QLockFilePrivate::processNameByPid(qint64 pid)
{
#if defined(Q_OS_OSX)
    char name[1024];
    proc_name(pid, name, sizeof(name) / sizeof(char));
    return QFile::decodeName(name);
#elif defined(Q_OS_LINUX)
    if (!QFile::exists(QStringLiteral("/proc/version")))
        return QString();
    char exePath[64];
    char buf[PATH_MAX + 1];
    sprintf(exePath, "/proc/%lld/exe", pid);
    size_t len = (size_t)readlink(exePath, buf, sizeof(buf));
    if (len >= sizeof(buf)) {
        // The pid is gone. Return some invalid process name to fail the test.
        return QStringLiteral("/ERROR/");
    }
    buf[len] = 0;
    return QFileInfo(QFile::decodeName(buf)).fileName();
#elif defined(Q_OS_HAIKU)
    thread_info info;
    if (get_thread_info(pid, &info) != B_OK)
        return QString();
    return QFile::decodeName(info.name);
#elif defined(Q_OS_BSD4) && !defined(Q_OS_IOS)
# if defined(__GLIBC__) && defined(__FreeBSD_kernel__)
    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, pid };
    size_t len = 0;
    if (sysctl(mib, 4, NULL, &len, NULL, 0) < 0)
        return QString();
    kinfo_proc *proc = static_cast<kinfo_proc *>(malloc(len));
# else
    kinfo_proc *proc = kinfo_getproc(pid);
# endif
    if (!proc)
        return QString();
# if defined(__GLIBC__) && defined(__FreeBSD_kernel__)
    if (sysctl(mib, 4, proc, &len, NULL, 0) < 0) {
        free(proc);
        return QString();
    }
    if (proc->ki_pid != pid) {
        free(proc);
        return QString();
    }
# endif
    QString name = QFile::decodeName(proc->ki_comm);
    free(proc);
    return name;
#else
    Q_UNUSED(pid);
    return QString();
#endif
}

void QLockFile::unlock()
{
    Q_D(QLockFile);
    if (!d->isLocked)
        return;
    close(d->fileHandle);
    d->fileHandle = -1;
    if (!QFile::remove(d->fileName)) {
        qWarning() << "Could not remove our own lock file" << d->fileName << "maybe permissions changed meanwhile?";
        // This is bad because other users of this lock file will now have to wait for the stale-lock-timeout...
    }
    d->lockError = QLockFile::NoError;
    d->isLocked = false;
}

QT_END_NAMESPACE
