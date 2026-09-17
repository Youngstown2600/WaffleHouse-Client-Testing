#include "nntpbackend.h"

#include <QAbstractSocket>
#include <QDateTime>
#include <QSslError>
#include <QSslSocket>

#include <utility>

NntpBackend::NntpBackend(ConnectionSettings settings, QObject *parent)
    : ChatBackend(std::move(settings), parent) {}
NntpBackend::~NntpBackend() { stop(); }

void NntpBackend::start()
{
    if (m_socket) return;
    m_stopRequested = false;
    m_buffer.clear(); m_group.clear(); m_greeted = false; m_authUserSent = false; m_authenticated = false;
    auto *socket = new QSslSocket(this); m_socket = socket;
    connect(socket, &QSslSocket::readyRead, this, [this] { if (m_socket) { m_buffer += m_socket->readAll(); processLines(); } });
    connect(socket, &QSslSocket::encrypted, this, [this] { emit eventReceived(QStringLiteral("status"), QString(), QStringLiteral("[NNTP] TLS established")); });
    connect(socket, &QSslSocket::sslErrors, this, [this](const QList<QSslError> &errors) {
        QStringList s; for (const auto &e : errors) s << e.errorString(); emit backendError(QStringLiteral("NNTP TLS"), s.join(QStringLiteral("; ")));
    });
    connect(socket, &QSslSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) { if (m_socket) emit backendError(QStringLiteral("NNTP"), m_socket->errorString()); });
    connect(socket, &QSslSocket::disconnected, this, [this] { m_socket = nullptr; emit disconnected(QStringLiteral("NNTP connection closed")); });
    const quint16 port = m_settings.port ? m_settings.port : static_cast<quint16>(m_settings.tls ? 563 : 119);
    if (m_settings.tls) socket->connectToHostEncrypted(m_settings.server, port); else socket->connectToHost(m_settings.server, port);
}

void NntpBackend::stop()
{
    m_stopRequested = true;
    if (!m_socket) return;
    sendLine(QStringLiteral("QUIT"));
    m_socket->disconnectFromHost();
    m_socket = nullptr;
}

void NntpBackend::sendLine(const QString &line)
{
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState) return;
    m_socket->write(line.toUtf8() + QByteArray("\r\n"));
    if (m_settings.debug) emit eventReceived(QStringLiteral("status"), QString(), QStringLiteral("[NNTP >>] %1").arg(line));
}

void NntpBackend::processLines()
{
    while (true) {
        const int nl = m_buffer.indexOf('\n'); if (nl < 0) break;
        QByteArray raw = m_buffer.left(nl + 1); m_buffer.remove(0, nl + 1);
        const QString line = QString::fromUtf8(raw).trimmed(); if (line.isEmpty()) continue;
        if (m_settings.debug) emit eventReceived(QStringLiteral("status"), QString(), QStringLiteral("[NNTP <<] %1").arg(line));
        bool ok = false; const int code = line.left(3).toInt(&ok);
        if (!m_greeted && ok && (code == 200 || code == 201)) {
            m_greeted = true;
            if (!m_settings.username.trimmed().isEmpty()) { m_authUserSent = true; sendLine(QStringLiteral("AUTHINFO USER %1").arg(m_settings.username.trimmed())); }
            else { m_authenticated = true; emit connected(QStringLiteral("anonymous"), QStringLiteral("%1:%2").arg(m_settings.server).arg(m_settings.port)); }
            continue;
        }
        if (ok && code == 381 && m_authUserSent) { sendLine(QStringLiteral("AUTHINFO PASS %1").arg(m_settings.password)); continue; }
        if (ok && (code == 281 || (m_authUserSent && code == 250))) { m_authenticated = true; emit connected(m_settings.username, QStringLiteral("%1:%2").arg(m_settings.server).arg(m_settings.port)); continue; }
        if (ok && code == 211) { emit eventReceived(QStringLiteral("status"), m_group, line); continue; }
        if (ok && code == 340 && m_posting) {
            if (m_socket) m_socket->write(m_pendingPost.toUtf8() + QByteArray("\r\n.\r\n"));
            m_posting = false; m_pendingPost.clear(); continue;
        }
        if (ok && code >= 400) emit backendError(QStringLiteral("NNTP"), line);
        else emit eventReceived(QStringLiteral("chat"), m_group.isEmpty() ? QStringLiteral("server") : m_group, line);
    }
}

void NntpBackend::joinRoom(const QString &room, bool)
{
    m_group = room.trimmed(); sendLine(QStringLiteral("GROUP %1").arg(m_group)); emit roomDiscovered(m_group, m_group);
}
void NntpBackend::leaveRoom(const QString &room) { if (m_group == room) m_group.clear(); }
void NntpBackend::sendPrivateMessage(const QString &target, const QString &message)
{
    Q_UNUSED(message);
    sendLine(QStringLiteral("ARTICLE %1").arg(target.trimmed()));
}
void NntpBackend::sendRoomMessage(const QString &room, const QString &message)
{
    const QString group = room.trimmed().isEmpty() ? m_group : room.trimmed();
    if (group.isEmpty()) { emit backendError(QStringLiteral("NNTP post"), QStringLiteral("Select/join a newsgroup first.")); return; }
    QString from = m_settings.realName.trimmed(); if (from.isEmpty()) from = m_settings.username.trimmed(); if (from.isEmpty()) from = QStringLiteral("wafflehouse-client@localhost");
    QString body = message; body.replace(QStringLiteral("\r\n."), QStringLiteral("\r\n.."));
    m_pendingPost = QStringLiteral("From: %1\r\nNewsgroups: %2\r\nSubject: WaffleHouse-Client post\r\nDate: %3\r\nMessage-ID: <%4@wafflehouse-client.local>\r\n\r\n%5")
                        .arg(from, group, QDateTime::currentDateTimeUtc().toString(Qt::RFC2822Date),
                             QString::number(QDateTime::currentMSecsSinceEpoch()), body);
    m_posting = true; sendLine(QStringLiteral("POST"));
}
void NntpBackend::sendRaw(const QString &command, const QString &, const QString &) { sendLine(command); }
