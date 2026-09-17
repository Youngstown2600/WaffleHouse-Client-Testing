#include "sshbackend.h"

#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>

#ifdef Q_OS_UNIX
#include <QSocketNotifier>
#include <QTimer>

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#if defined(Q_OS_FREEBSD)
#include <libutil.h>
#elif defined(Q_OS_MACOS)
#include <util.h>
#else
#include <pty.h>
#endif
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <vector>

namespace {
// Best-effort write used during child-process failure reporting and shutdown.
// The return value from ::write() is always inspected so fortified libc builds
// do not emit -Wunused-result warnings.  EINTR is retried; other failures are
// intentionally ignored because these paths are already error/cleanup paths.
void writeBestEffort(int fd, const char *data, size_t length) noexcept
{
    while (length > 0) {
        const ssize_t written = ::write(fd, data, length);
        if (written > 0) {
            data += written;
            length -= static_cast<size_t>(written);
            continue;
        }
        if (written < 0 && errno == EINTR) continue;
        break;
    }
}
}
#endif

#include <utility>

SshBackend::SshBackend(ConnectionSettings settings, QObject *parent)
    : ChatBackend(std::move(settings), parent) {}

SshBackend::~SshBackend()
{
    stop();
}

void SshBackend::start()
{
#ifdef Q_OS_UNIX
    if (m_masterFd >= 0 || m_childPid > 0) return;
#else
    if (m_process) return;
#endif

    m_stopRequested = false;
    const QString ssh = QStandardPaths::findExecutable(QStringLiteral("ssh"));
    if (ssh.isEmpty()) {
        emit backendError(QStringLiteral("SSH"), QStringLiteral("OpenSSH client 'ssh' was not found in PATH."));
        emit disconnected(QStringLiteral("ssh executable unavailable"));
        return;
    }

    QStringList args;
    // WaffleHouse embeds an interactive terminal. On Unix this process is attached
    // to a real PTY below, so forcing a remote TTY is now safe and intentional.
    args << QStringLiteral("-tt")
         << QStringLiteral("-o") << QStringLiteral("BatchMode=no")
         << QStringLiteral("-o") << QStringLiteral("NumberOfPasswordPrompts=3")
         << QStringLiteral("-o") << QStringLiteral("ServerAliveInterval=30")
         << QStringLiteral("-o") << QStringLiteral("ServerAliveCountMax=3")
         << QStringLiteral("-p") << QString::number(m_settings.port ? m_settings.port : 22);
    if (m_settings.debug) args << QStringLiteral("-v");

    const QString identityFile = m_settings.sshIdentityFile.trimmed();
    if (!identityFile.isEmpty()) {
        args << QStringLiteral("-i") << identityFile;
    }
    for (const QString &optionValue : m_settings.sshOptions) {
        const QString option = optionValue.trimmed();
        if (!option.isEmpty()) args << QStringLiteral("-o") << option;
    }

    QString destination = m_settings.server.trimmed();
    if (!m_settings.username.trimmed().isEmpty())
        destination = m_settings.username.trimmed() + QLatin1Char('@') + destination;
    args << destination;
    if (!m_settings.sshRemoteCommand.trimmed().isEmpty())
        args << m_settings.sshRemoteCommand.trimmed();

#ifdef Q_OS_UNIX
    struct winsize ws {};
    ws.ws_col = static_cast<unsigned short>(qBound(1, m_columns, 65535));
    ws.ws_row = static_cast<unsigned short>(qBound(1, m_rows, 65535));

    int masterFd = -1;
    const pid_t pid = ::forkpty(&masterFd, nullptr, nullptr, &ws);
    if (pid < 0) {
        emit backendError(QStringLiteral("SSH"),
                          QStringLiteral("Could not allocate SSH pseudo-terminal: %1")
                              .arg(QString::fromLocal8Bit(std::strerror(errno))));
        emit disconnected(QStringLiteral("SSH PTY allocation failed"));
        return;
    }

    if (pid == 0) {
        const QByteArray term = (m_settings.telnetTerminalType.trimmed().isEmpty()
                                     ? QStringLiteral("xterm-256color")
                                     : m_settings.telnetTerminalType.trimmed()).toLocal8Bit();
        ::setenv("TERM", term.constData(), 1);

        const QByteArray executable = ssh.toLocal8Bit();
        std::vector<QByteArray> owned;
        owned.reserve(static_cast<size_t>(args.size()) + 1);
        owned.push_back(executable);
        for (const QString &arg : args) owned.push_back(arg.toLocal8Bit());

        std::vector<char *> argv;
        argv.reserve(owned.size() + 1);
        for (QByteArray &arg : owned) argv.push_back(arg.data());
        argv.push_back(nullptr);

        ::execv(executable.constData(), argv.data());
        const char prefix[] = "WaffleHouse SSH: execv failed: ";
        writeBestEffort(STDERR_FILENO, prefix, sizeof(prefix) - 1);
        const char *err = std::strerror(errno);
        writeBestEffort(STDERR_FILENO, err, std::strlen(err));
        writeBestEffort(STDERR_FILENO, "\r\n", 2);
        _exit(127);
    }

    m_masterFd = masterFd;
    m_childPid = static_cast<qint64>(pid);
    const int currentFlags = ::fcntl(m_masterFd, F_GETFL, 0);
    if (currentFlags >= 0) ::fcntl(m_masterFd, F_SETFL, currentFlags | O_NONBLOCK);

    m_readNotifier = new QSocketNotifier(m_masterFd, QSocketNotifier::Read, this);
    connect(m_readNotifier, &QSocketNotifier::activated, this,
            [this](QSocketDescriptor, QSocketNotifier::Type) { readPty(); });

    m_reapTimer = new QTimer(this);
    m_reapTimer->setInterval(200);
    connect(m_reapTimer, &QTimer::timeout, this, &SshBackend::reapChild);
    m_reapTimer->start();

    emit connected(m_settings.username,
                   QStringLiteral("%1:%2").arg(m_settings.server).arg(m_settings.port ? m_settings.port : 22));
    emit eventReceived(QStringLiteral("terminal"), m_settings.server,
                       QStringLiteral("[WaffleHouse-Client SSH] interactive PTY session started. OpenSSH key/agent or password authentication is available.\r\n"));
#else
    // Windows still uses QProcess until the Windows backend grows native ConPTY
    // support. Do not force OpenSSH to perform Unix-style tcgetattr() on a pipe.
    args.removeAll(QStringLiteral("-tt"));

    auto *proc = new QProcess(this);
    m_process = proc;
    proc->setProcessChannelMode(QProcess::MergedChannels);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("TERM"), m_settings.telnetTerminalType.trimmed().isEmpty()
                                       ? QStringLiteral("xterm-256color")
                                       : m_settings.telnetTerminalType.trimmed());
    proc->setProcessEnvironment(env);

    connect(proc, &QProcess::started, this, [this] {
        emit connected(m_settings.username,
                       QStringLiteral("%1:%2").arg(m_settings.server).arg(m_settings.port ? m_settings.port : 22));
        emit eventReceived(QStringLiteral("terminal"), m_settings.server,
                           QStringLiteral("[WaffleHouse-Client SSH] session started. Windows ConPTY support is not yet enabled; interactive terminal behavior may be limited.\r\n"));
    });
    connect(proc, &QProcess::readyReadStandardOutput, this, [this] {
        if (m_process)
            emit eventReceived(QStringLiteral("terminal"), m_settings.server,
                               QString::fromLocal8Bit(m_process->readAllStandardOutput()));
    });
    connect(proc, &QProcess::readyReadStandardError, this, [this] {
        if (m_process)
            emit eventReceived(QStringLiteral("terminal"), m_settings.server,
                               QString::fromLocal8Bit(m_process->readAllStandardError()));
    });
    connect(proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (m_process) emit backendError(QStringLiteral("SSH"), m_process->errorString());
    });
    connect(proc, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus) {
                m_process = nullptr;
                emit disconnected(QStringLiteral("SSH process exited with code %1").arg(code));
            });
    proc->start(ssh, args);
#endif
}

#ifdef Q_OS_UNIX
void SshBackend::readPty()
{
    if (m_masterFd < 0) return;

    char buffer[16384];
    for (;;) {
        const ssize_t count = ::read(m_masterFd, buffer, sizeof(buffer));
        if (count > 0) {
            emit eventReceived(QStringLiteral("terminal"), m_settings.server,
                               QString::fromLocal8Bit(buffer, static_cast<int>(count)));
            continue;
        }
        if (count == 0) return;
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EIO) return;

        emit backendError(QStringLiteral("SSH"),
                          QStringLiteral("SSH PTY read failed: %1")
                              .arg(QString::fromLocal8Bit(std::strerror(errno))));
        return;
    }
}

void SshBackend::reapChild()
{
    if (m_childPid <= 0) return;

    int status = 0;
    const pid_t pid = static_cast<pid_t>(m_childPid);
    const pid_t result = ::waitpid(pid, &status, WNOHANG);
    if (result == 0) return;
    if (result < 0) {
        if (errno == EINTR) return;
        closePty(false);
        emit disconnected(QStringLiteral("SSH process ended"));
        return;
    }

    int code = 0;
    if (WIFEXITED(status)) code = WEXITSTATUS(status);
    else if (WIFSIGNALED(status)) code = 128 + WTERMSIG(status);

    closePty(false);
    emit disconnected(QStringLiteral("SSH process exited with code %1").arg(code));
}

void SshBackend::closePty(bool terminateChild)
{
    if (m_readNotifier) {
        m_readNotifier->setEnabled(false);
        m_readNotifier->deleteLater();
        m_readNotifier = nullptr;
    }
    if (m_reapTimer) {
        m_reapTimer->stop();
        m_reapTimer->deleteLater();
        m_reapTimer = nullptr;
    }

    const pid_t pid = static_cast<pid_t>(m_childPid);
    if (terminateChild && pid > 0) {
        ::kill(pid, SIGHUP);
        int status = 0;
        for (int i = 0; i < 10; ++i) {
            const pid_t result = ::waitpid(pid, &status, WNOHANG);
            if (result == pid || (result < 0 && errno == ECHILD)) break;
            ::usleep(20000);
        }
        if (::waitpid(pid, &status, WNOHANG) == 0) {
            ::kill(pid, SIGTERM);
            ::waitpid(pid, &status, 0);
        }
    }

    if (m_masterFd >= 0) {
        ::close(m_masterFd);
        m_masterFd = -1;
    }
    m_childPid = -1;
}
#endif

void SshBackend::stop()
{
    m_stopRequested = true;
#ifdef Q_OS_UNIX
    if (m_masterFd < 0 && m_childPid <= 0) return;
    // Let a cooperative remote shell see "exit" first, then make sure the local
    // ssh process is reaped so WaffleHouse never leaves a zombie behind.
    if (m_masterFd >= 0) {
        const char exitCommand[] = "exit\r";
        writeBestEffort(m_masterFd, exitCommand, sizeof(exitCommand) - 1);
    }
    closePty(true);
#else
    if (!m_process) return;
    m_process->write("exit\r\n");
    m_process->closeWriteChannel();
    if (!m_process->waitForFinished(1000)) {
        m_process->terminate();
        if (!m_process->waitForFinished(1000)) m_process->kill();
    }
    m_process = nullptr;
#endif
}

void SshBackend::sendTerminalInput(const QByteArray &bytes)
{
#ifdef Q_OS_UNIX
    if (m_masterFd < 0 || bytes.isEmpty()) return;
    qsizetype offset = 0;
    while (offset < bytes.size()) {
        const ssize_t written = ::write(m_masterFd, bytes.constData() + offset,
                                        static_cast<size_t>(bytes.size() - offset));
        if (written > 0) {
            offset += static_cast<qsizetype>(written);
            continue;
        }
        if (written < 0 && errno == EINTR) continue;
        if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
        if (written < 0) {
            emit backendError(QStringLiteral("SSH"),
                              QStringLiteral("SSH PTY write failed: %1")
                                  .arg(QString::fromLocal8Bit(std::strerror(errno))));
        }
        break;
    }
#else
    if (m_process) m_process->write(bytes);
#endif
}

void SshBackend::sendRaw(const QString &line, const QString &, const QString &)
{
    sendTerminalInput(line.toUtf8() + QByteArray("\r"));
}

void SshBackend::sendPrivateMessage(const QString &, const QString &message)
{
    sendRaw(message);
}

void SshBackend::sendRoomMessage(const QString &, const QString &message)
{
    sendRaw(message);
}

void SshBackend::joinRoom(const QString &, bool)
{
    emit backendError(QStringLiteral("SSH"),
                      QStringLiteral("SSH sessions use a terminal rather than chat rooms."));
}

void SshBackend::leaveRoom(const QString &)
{
    stop();
}

void SshBackend::setTerminalSize(int columns, int rows)
{
    m_columns = qMax(1, columns);
    m_rows = qMax(1, rows);
#ifdef Q_OS_UNIX
    if (m_masterFd >= 0) {
        struct winsize ws {};
        ws.ws_col = static_cast<unsigned short>(qBound(1, m_columns, 65535));
        ws.ws_row = static_cast<unsigned short>(qBound(1, m_rows, 65535));
        if (::ioctl(m_masterFd, TIOCSWINSZ, &ws) != 0 && errno != ENOTTY) {
            emit backendError(QStringLiteral("SSH"),
                              QStringLiteral("Could not resize SSH terminal: %1")
                                  .arg(QString::fromLocal8Bit(std::strerror(errno))));
        }
    }
#endif
}
