#pragma once

#include "backend.h"

#include <QByteArray>
#include <QPointer>

class QSslSocket;

class NntpBackend final : public ChatBackend {
    Q_OBJECT
public:
    explicit NntpBackend(ConnectionSettings settings, QObject *parent = nullptr);
    ~NntpBackend() override;

    QString protocolName() const override { return QStringLiteral("NNTP/Usenet"); }
    void start() override;
    void stop() override;
    void sendPrivateMessage(const QString &target, const QString &message) override;
    void joinRoom(const QString &room, bool privateRoom = false) override;
    void sendRoomMessage(const QString &room, const QString &message) override;
    void leaveRoom(const QString &room) override;
    void sendRaw(const QString &command, const QString &unusedB = QString(), const QString &unusedC = QString()) override;

private:
    void sendLine(const QString &line);
    void processLines();
    QPointer<QSslSocket> m_socket;
    QByteArray m_buffer;
    QString m_group;
    bool m_greeted = false;
    bool m_authUserSent = false;
    bool m_authenticated = false;
    bool m_posting = false;
    QString m_pendingPost;
};
