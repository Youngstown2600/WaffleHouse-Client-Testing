#pragma once

#include "backend.h"

#include <QByteArray>
#include <QHash>
#include <QPointer>
#include <QSet>

class QSslSocket;

class XmppBackend final : public ChatBackend {
    Q_OBJECT
public:
    explicit XmppBackend(ConnectionSettings settings, QObject *parent = nullptr);
    ~XmppBackend() override;

    QString protocolName() const override { return QStringLiteral("XMPP/Jabber"); }
    void start() override;
    void stop() override;
    void sendPrivateMessage(const QString &target, const QString &message) override;
    void joinRoom(const QString &room, bool privateRoom = false) override;
    void sendRoomMessage(const QString &room, const QString &message) override;
    void leaveRoom(const QString &room) override;
    void sendRaw(const QString &xml, const QString &unusedB = QString(), const QString &unusedC = QString()) override;
    void addBuddy(const QString &name) override;
    void removeBuddy(const QString &name) override;

private:
    void openStream();
    void processIncoming();
    void processFeatures(const QByteArray &features);
    void handleMessageStanzas(const QString &text);
    void handlePresenceStanzas(const QString &text);
    void handleRosterStanzas(const QString &text);
    void writeXml(const QString &xml);
    QString xmppDomain() const;
    QString authUser() const;
    QString fullJid() const;
    static QString xmlEscape(QString text);
    static QString xmlUnescape(QString text);
    static QString attr(const QString &tag, const QString &name);

    QPointer<QSslSocket> m_socket;
    QByteArray m_buffer;
    bool m_tlsActive = false;
    bool m_tlsRequested = false;
    bool m_authenticated = false;
    bool m_bound = false;
    bool m_online = false;
    QHash<QString, QString> m_roomNick;
    QSet<QString> m_buddies;
};
