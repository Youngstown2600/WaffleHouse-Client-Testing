#pragma once

#include "backend.h"

#include <QPointer>

class QProcess;
class QSocketNotifier;
class QTimer;

class SshBackend final : public ChatBackend {
    Q_OBJECT
public:
    explicit SshBackend(ConnectionSettings settings, QObject *parent = nullptr);
    ~SshBackend() override;

    QString protocolName() const override { return QStringLiteral("SSH"); }
    void start() override;
    void stop() override;
    void sendPrivateMessage(const QString &target, const QString &message) override;
    void joinRoom(const QString &room, bool privateRoom = false) override;
    void sendRoomMessage(const QString &room, const QString &message) override;
    void leaveRoom(const QString &room) override;
    void sendRaw(const QString &line, const QString &unusedB = QString(), const QString &unusedC = QString()) override;
    void sendTerminalInput(const QByteArray &bytes) override;
    void setTerminalSize(int columns, int rows) override;

private:
#ifdef Q_OS_UNIX
    void readPty();
    void reapChild();
    void closePty(bool terminateChild);

    int m_masterFd = -1;
    qint64 m_childPid = -1;
    QSocketNotifier *m_readNotifier = nullptr;
    QTimer *m_reapTimer = nullptr;
#else
    QPointer<QProcess> m_process;
#endif
    int m_columns = 80;
    int m_rows = 24;
};
