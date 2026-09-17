#include "xmppbackend.h"

#include <QAbstractSocket>
#include <QRegularExpression>
#include <QSslError>
#include <QSslSocket>

#include <utility>

XmppBackend::XmppBackend(ConnectionSettings settings, QObject *parent)
    : ChatBackend(std::move(settings), parent)
{
}

XmppBackend::~XmppBackend()
{
    stop();
}

QString XmppBackend::xmppDomain() const
{
    const QString user = m_settings.username.trimmed();
    const int at = user.indexOf(QLatin1Char('@'));
    if (at >= 0 && at + 1 < user.size()) {
        const QString domain = user.mid(at + 1).section(QLatin1Char('/'), 0, 0).trimmed();
        if (!domain.isEmpty()) return domain;
    }
    return m_settings.server.trimmed();
}

QString XmppBackend::authUser() const
{
    return m_settings.username.trimmed().section(QLatin1Char('@'), 0, 0).section(QLatin1Char('/'), 0, 0);
}

QString XmppBackend::fullJid() const
{
    QString jid = m_settings.username.trimmed();
    if (!jid.contains(QLatin1Char('@')) && !xmppDomain().isEmpty()) jid += QLatin1Char('@') + xmppDomain();
    return jid;
}

QString XmppBackend::xmlEscape(QString text)
{
    text.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    text.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    text.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    text.replace(QLatin1Char('\"'), QStringLiteral("&quot;"));
    text.replace(QLatin1Char('\''), QStringLiteral("&apos;"));
    return text;
}

QString XmppBackend::xmlUnescape(QString text)
{
    text.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
    text.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
    text.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    text.replace(QStringLiteral("&apos;"), QStringLiteral("'"));
    text.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    return text;
}

QString XmppBackend::attr(const QString &tag, const QString &name)
{
    const QRegularExpression re(QStringLiteral("\\b%1\\s*=\\s*(['\"])(.*?)\\1")
                                    .arg(QRegularExpression::escape(name)),
                                QRegularExpression::DotMatchesEverythingOption);
    const auto match = re.match(tag);
    return match.hasMatch() ? xmlUnescape(match.captured(2)) : QString();
}

void XmppBackend::start()
{
    if (m_socket) return;
    m_stopRequested = false;
    m_buffer.clear();
    m_tlsActive = false;
    m_tlsRequested = false;
    m_authenticated = false;
    m_bound = false;
    m_online = false;

    auto *socket = new QSslSocket(this);
    m_socket = socket;

    connect(socket, &QSslSocket::connected, this, [this] {
        if (!m_socket) return;
        if (m_settings.tls && m_settings.port == 5223) return; // encrypted() opens the stream
        openStream();
    });
    connect(socket, &QSslSocket::encrypted, this, [this] {
        m_tlsActive = true;
        m_tlsRequested = false;
        m_buffer.clear();
        openStream();
    });
    connect(socket, &QSslSocket::readyRead, this, [this] {
        if (!m_socket) return;
        m_buffer += m_socket->readAll();
        processIncoming();
    });
    connect(socket, &QSslSocket::sslErrors, this, [this](const QList<QSslError> &errors) {
        QStringList lines;
        for (const auto &e : errors) lines << e.errorString();
        emit backendError(QStringLiteral("XMPP TLS"), lines.join(QStringLiteral("; ")));
    });
    connect(socket, &QSslSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        if (!m_socket) return;
        emit backendError(QStringLiteral("XMPP socket"), m_socket->errorString());
    });
    connect(socket, &QSslSocket::disconnected, this, [this] {
        const bool wasOnline = m_online;
        m_online = false;
        m_socket = nullptr;
        emit disconnected(wasOnline ? QStringLiteral("XMPP connection closed")
                                    : QStringLiteral("XMPP connection failed or closed"));
    });

    QString host = m_settings.server.trimmed();
    if (host.isEmpty()) host = xmppDomain();
    const quint16 port = m_settings.port ? m_settings.port : static_cast<quint16>(5222);
    emit eventReceived(QStringLiteral("status"), QString(),
                       QStringLiteral("[XMPP] Connecting to %1:%2%3")
                           .arg(host).arg(port).arg(m_settings.tls ? QStringLiteral(" with TLS") : QString()));
    if (m_settings.tls && port == 5223) socket->connectToHostEncrypted(host, port);
    else socket->connectToHost(host, port);
}

void XmppBackend::stop()
{
    m_stopRequested = true;
    if (!m_socket) return;
    if (m_online) writeXml(QStringLiteral("<presence type='unavailable'/></stream:stream>"));
    m_socket->disconnectFromHost();
    if (m_socket) m_socket->deleteLater();
    m_socket = nullptr;
    m_online = false;
}

void XmppBackend::openStream()
{
    writeXml(QStringLiteral("<?xml version='1.0'?><stream:stream to='%1' version='1.0' xmlns='jabber:client' xmlns:stream='http://etherx.jabber.org/streams'>")
                 .arg(xmlEscape(xmppDomain())));
}

void XmppBackend::writeXml(const QString &xml)
{
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState) return;
    m_socket->write(xml.toUtf8());
}

void XmppBackend::processFeatures(const QByteArray &features)
{
    const QString f = QString::fromUtf8(features);
    if (m_settings.debug) emit eventReceived(QStringLiteral("status"), QString(), QStringLiteral("[XMPP] stream features received"));

    if (m_settings.tls && !m_tlsActive && !m_tlsRequested && f.contains(QStringLiteral("<starttls"))) {
        m_tlsRequested = true;
        writeXml(QStringLiteral("<starttls xmlns='urn:ietf:params:xml:ns:xmpp-tls'/>") );
        return;
    }

    if (!m_authenticated) {
        if (!f.contains(QStringLiteral("PLAIN"), Qt::CaseInsensitive)) {
            emit backendError(QStringLiteral("XMPP authentication"),
                              QStringLiteral("Server did not advertise SASL PLAIN. This initial WaffleHouse-Client XMPP backend currently supports SASL PLAIN over TLS."));
            return;
        }
        if (m_settings.tls && !m_tlsActive) {
            emit backendError(QStringLiteral("XMPP authentication"),
                              QStringLiteral("Refusing SASL PLAIN before TLS is active."));
            return;
        }
        QByteArray auth;
        auth.append('\0');
        auth.append(authUser().toUtf8());
        auth.append('\0');
        auth.append(m_settings.password.toUtf8());
        writeXml(QStringLiteral("<auth xmlns='urn:ietf:params:xml:ns:xmpp-sasl' mechanism='PLAIN'>%1</auth>")
                     .arg(QString::fromLatin1(auth.toBase64())));
        return;
    }

    if (!m_bound && f.contains(QStringLiteral("<bind"))) {
        writeXml(QStringLiteral("<iq type='set' id='wafflehouse-bind'><bind xmlns='urn:ietf:params:xml:ns:xmpp-bind'><resource>WaffleHouse-Client</resource></bind></iq>"));
    }
}

void XmppBackend::processIncoming()
{
    const QString all = QString::fromUtf8(m_buffer);

    if (all.contains(QStringLiteral("<proceed")) && m_tlsRequested && m_socket && !m_tlsActive) {
        m_buffer.clear();
        m_socket->startClientEncryption();
        return;
    }

    if (all.contains(QStringLiteral("<failure")) && !m_authenticated) {
        emit backendError(QStringLiteral("XMPP authentication"), QStringLiteral("Server rejected authentication."));
        if (m_socket) m_socket->disconnectFromHost();
        return;
    }

    if (all.contains(QStringLiteral("<success")) && !m_authenticated) {
        m_authenticated = true;
        m_buffer.clear();
        openStream();
        return;
    }

    const int featureStart = all.indexOf(QStringLiteral("<stream:features"));
    const int featureEnd = all.indexOf(QStringLiteral("</stream:features>"));
    if (featureStart >= 0 && featureEnd > featureStart) {
        const int len = featureEnd + 18 - featureStart;
        const QByteArray features = m_buffer.mid(featureStart, len);
        m_buffer.remove(featureStart, len);
        processFeatures(features);
    }

    const QString current = QString::fromUtf8(m_buffer);
    if (m_authenticated && !m_bound
        && current.contains(QRegularExpression(QStringLiteral("<iq[^>]*(?:id=['\"]wafflehouse-bind['\"][^>]*type=['\"]result['\"]|type=['\"]result['\"][^>]*id=['\"]wafflehouse-bind['\"])[^>]*>")))) {
        m_bound = true;
        writeXml(QStringLiteral("<iq type='get' id='wafflehouse-roster'><query xmlns='jabber:iq:roster'/></iq>"));
        writeXml(QStringLiteral("<presence/>"));
        m_online = true;
        emit connected(fullJid(), QStringLiteral("%1:%2").arg(m_settings.server).arg(m_settings.port));
        emit eventReceived(QStringLiteral("status"), QString(), QStringLiteral("[XMPP] Online as %1").arg(fullJid()));
    }

    handleRosterStanzas(current);
    handleMessageStanzas(current);
    handlePresenceStanzas(current);

    // Keep enough tail for stanzas that arrived fragmented without allowing unbounded growth.
    if (m_buffer.size() > 1024 * 1024) m_buffer = m_buffer.right(128 * 1024);
}


void XmppBackend::handleRosterStanzas(const QString &text)
{
    static const QRegularExpression queryRe(
        QStringLiteral("<query\\b[^>]*xmlns=[\'\"]jabber:iq:roster[\'\"][^>]*>(.*?)</query>"),
        QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
    auto queries = queryRe.globalMatch(text);
    bool changed = false;
    while (queries.hasNext()) {
        const QString inner = queries.next().captured(1);
        static const QRegularExpression itemRe(
            QStringLiteral("<item\\b([^>]*)/?>"), QRegularExpression::CaseInsensitiveOption);
        auto items = itemRe.globalMatch(inner);
        while (items.hasNext()) {
            const QString tag = QStringLiteral("<item ") + items.next().captured(1) + QLatin1Char('>');
            const QString jid = attr(tag, QStringLiteral("jid")).trimmed();
            if (jid.isEmpty()) continue;
            if (attr(tag, QStringLiteral("subscription")) == QStringLiteral("remove")) changed = m_buddies.remove(jid) > 0 || changed;
            else if (!m_buddies.contains(jid)) { m_buddies.insert(jid); changed = true; }
        }
    }
    if (changed || text.contains(QStringLiteral("id='wafflehouse-roster'")) || text.contains(QStringLiteral("id=\"wafflehouse-roster\""))) {
        QStringList names = m_buddies.values();
        names.sort(Qt::CaseInsensitive);
        emit buddyListChanged(names);
    }
}

void XmppBackend::handleMessageStanzas(const QString &text)
{
    static const QRegularExpression re(
        QStringLiteral("<message\\b([^>]*)>(.*?)</message>"),
        QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
    auto it = re.globalMatch(text);
    int consumed = 0;
    while (it.hasNext()) {
        const auto m = it.next();
        consumed = qMax(consumed, m.capturedEnd());
        const QString attrs = QStringLiteral("<message ") + m.captured(1) + QLatin1Char('>');
        const QString from = attr(attrs, QStringLiteral("from"));
        const QString type = attr(attrs, QStringLiteral("type"));
        const QString inner = m.captured(2);
        const QRegularExpression bodyRe(QStringLiteral("<body[^>]*>(.*?)</body>"),
                                        QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
        const auto bodyMatch = bodyRe.match(inner);
        if (!bodyMatch.hasMatch()) continue;
        const QString body = xmlUnescape(bodyMatch.captured(1));
        if (type.compare(QStringLiteral("groupchat"), Qt::CaseInsensitive) == 0) {
            const QString room = from.section(QLatin1Char('/'), 0, 0);
            const QString nick = from.section(QLatin1Char('/'), 1);
            emit eventReceived(QStringLiteral("chat"), room,
                               nick.isEmpty() ? body : QStringLiteral("%1: %2").arg(nick, body));
        } else {
            emit eventReceived(QStringLiteral("im"), from.section(QLatin1Char('/'), 0, 0), body);
        }
    }
    if (consumed > 0 && consumed <= m_buffer.size()) m_buffer.remove(0, consumed);
}

void XmppBackend::handlePresenceStanzas(const QString &text)
{
    static const QRegularExpression re(QStringLiteral("<presence\\b([^>]*)/?>"), QRegularExpression::CaseInsensitiveOption);
    auto it = re.globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        const QString tag = QStringLiteral("<presence ") + m.captured(1) + QLatin1Char('>');
        const QString from = attr(tag, QStringLiteral("from")).section(QLatin1Char('/'), 0, 0);
        if (from.isEmpty()) continue;
        const bool online = attr(tag, QStringLiteral("type")) != QStringLiteral("unavailable");
        emit buddyPresenceChanged(from, online);
    }
}

void XmppBackend::sendPrivateMessage(const QString &target, const QString &message)
{
    if (!m_online) { emit backendError(QStringLiteral("XMPP"), QStringLiteral("Not connected.")); return; }
    writeXml(QStringLiteral("<message to='%1' type='chat'><body>%2</body></message>")
                 .arg(xmlEscape(target.trimmed()), xmlEscape(message)));
}

void XmppBackend::joinRoom(const QString &room, bool)
{
    if (!m_online) { emit backendError(QStringLiteral("XMPP MUC"), QStringLiteral("Not connected.")); return; }
    QString nick = authUser();
    if (nick.isEmpty()) nick = QStringLiteral("WaffleHouseUser");
    m_roomNick.insert(room, nick);
    writeXml(QStringLiteral("<presence to='%1/%2'><x xmlns='http://jabber.org/protocol/muc'/></presence>")
                 .arg(xmlEscape(room.trimmed()), xmlEscape(nick)));
    emit roomDiscovered(room, room);
}

void XmppBackend::sendRoomMessage(const QString &room, const QString &message)
{
    if (!m_online) { emit backendError(QStringLiteral("XMPP MUC"), QStringLiteral("Not connected.")); return; }
    writeXml(QStringLiteral("<message to='%1' type='groupchat'><body>%2</body></message>")
                 .arg(xmlEscape(room.trimmed()), xmlEscape(message)));
}

void XmppBackend::leaveRoom(const QString &room)
{
    const QString nick = m_roomNick.value(room, authUser());
    writeXml(QStringLiteral("<presence to='%1/%2' type='unavailable'/>")
                 .arg(xmlEscape(room.trimmed()), xmlEscape(nick)));
    m_roomNick.remove(room);
}

void XmppBackend::sendRaw(const QString &xml, const QString &, const QString &)
{
    writeXml(xml);
}

void XmppBackend::addBuddy(const QString &name)
{
    const QString jid = name.trimmed();
    if (jid.isEmpty()) return;
    writeXml(QStringLiteral("<iq type='set' id='wafflehouse-roster-add'><query xmlns='jabber:iq:roster'><item jid='%1'/></query></iq><presence to='%1' type='subscribe'/>")
                 .arg(xmlEscape(jid)));
    m_buddies.insert(jid);
    QStringList names = m_buddies.values(); names.sort(Qt::CaseInsensitive); emit buddyListChanged(names);
}

void XmppBackend::removeBuddy(const QString &name)
{
    const QString jid = name.trimmed();
    if (jid.isEmpty()) return;
    writeXml(QStringLiteral("<iq type='set' id='wafflehouse-roster-del'><query xmlns='jabber:iq:roster'><item jid='%1' subscription='remove'/></query></iq>")
                 .arg(xmlEscape(jid)));
    m_buddies.remove(jid);
    QStringList names = m_buddies.values(); names.sort(Qt::CaseInsensitive); emit buddyListChanged(names);
}
