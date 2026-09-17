#include "wafflecast.h"

#include <QAbstractSocket>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QProcess>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUuid>

namespace {
constexpr qsizetype kMaxRequestBytes = 16 * 1024;
constexpr qint64 kSlowClientLimit = 2 * 1024 * 1024;

QUrl siblingUrl(const QUrl &streamUrl, const QString &leaf)
{
    QUrl out(streamUrl);
    QString path = out.path();
    const int slash = path.lastIndexOf(QLatin1Char('/'));
    if (slash >= 0) path = path.left(slash + 1) + leaf;
    else path = QStringLiteral("/") + leaf;
    out.setPath(path);
    out.setQuery(QString());
    out.setFragment(QString());
    return out;
}
}

QUrl WaffleCastInvite::metadataUrl() const
{
    return siblingUrl(streamUrl, QStringLiteral("meta.json"));
}

QUrl WaffleCastInvite::coverUrl() const
{
    return siblingUrl(streamUrl, QStringLiteral("cover"));
}

QString WaffleCastInvite::encode(const QUrl &streamUrl, const QString &title)
{
    QJsonObject object;
    object.insert(QStringLiteral("v"), 1);
    object.insert(QStringLiteral("u"), streamUrl.toString(QUrl::FullyEncoded));
    if (!title.trimmed().isEmpty()) object.insert(QStringLiteral("t"), title.trimmed().left(64));
    const QByteArray json = QJsonDocument(object).toJson(QJsonDocument::Compact);
    const QByteArray encoded = json.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    return QStringLiteral("[[WAFFLECAST1:%1]]").arg(QString::fromLatin1(encoded));
}

bool WaffleCastInvite::decode(const QString &payload, WaffleCastInvite *invite)
{
    const QString clean = payload.trimmed();
    const QString prefix = QStringLiteral("[[WAFFLECAST1:");
    if (!clean.startsWith(prefix) || !clean.endsWith(QStringLiteral("]]"))) return false;
    const QByteArray encoded = clean.mid(prefix.size(), clean.size() - prefix.size() - 2).toLatin1();
    const QByteArray json = QByteArray::fromBase64(encoded, QByteArray::Base64UrlEncoding);
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) return false;
    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("v")).toInt() != 1) return false;
    const QUrl url(object.value(QStringLiteral("u")).toString());
    if (!url.isValid() || (url.scheme() != QStringLiteral("http") && url.scheme() != QStringLiteral("https"))) return false;
    if (invite) {
        invite->streamUrl = url;
        invite->title = object.value(QStringLiteral("t")).toString().trimmed();
    }
    return true;
}

WaffleCastServer::WaffleCastServer(QObject *parent)
    : QObject(parent),
      m_server(new QTcpServer(this)),
      m_encoder(new QProcess(this))
{
    m_encoder->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_server, &QTcpServer::newConnection, this, &WaffleCastServer::acceptConnections);
    connect(m_encoder, &QProcess::readyReadStandardOutput, this, &WaffleCastServer::encoderReadyRead);
    connect(m_encoder, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus) { encoderFinished(exitCode); });
}

WaffleCastServer::~WaffleCastServer()
{
    stop();
}

bool WaffleCastServer::start(const QString &ffmpegExecutable,
                             const QString &advertisedHost,
                             quint16 port,
                             QString *error)
{
    stop();
    m_ffmpeg = ffmpegExecutable.trimmed();
    m_advertisedHost = advertisedHost.trimmed();
    if (m_ffmpeg.isEmpty()) {
        if (error) *error = QStringLiteral("ffmpeg is required to broadcast WaffleCast audio.");
        return false;
    }
    if (m_advertisedHost.isEmpty()) {
        if (error) *error = QStringLiteral("Enter the hostname or IP address listeners should use.");
        return false;
    }
    m_token = QUuid::createUuid().toString(QUuid::WithoutBraces).remove(QLatin1Char('-'));
    if (!m_server->listen(QHostAddress::Any, port)) {
        if (error) *error = m_server->errorString();
        m_token.clear();
        return false;
    }
    emit broadcastStateChanged(true);
    emit statusMessage(QStringLiteral("WaffleCast is live on port %1.").arg(m_server->serverPort()));
    return true;
}

void WaffleCastServer::stop()
{
    const bool wasActive = active();
    stopEncoder();
    const auto clients = m_streamClients;
    for (QTcpSocket *socket : clients) {
        if (!socket) continue;
        socket->disconnectFromHost();
        socket->deleteLater();
    }
    m_streamClients.clear();
    m_requestBuffers.clear();
    if (m_server->isListening()) m_server->close();
    m_token.clear();
    m_ffmpeg.clear();
    if (wasActive) {
        emit listenerCountChanged(0);
        emit broadcastStateChanged(false);
        emit statusMessage(QStringLiteral("WaffleCast broadcast ended."));
    }
}

bool WaffleCastServer::active() const
{
    return m_server && m_server->isListening() && !m_token.isEmpty();
}

quint16 WaffleCastServer::listeningPort() const
{
    return active() ? m_server->serverPort() : 0;
}

int WaffleCastServer::listenerCount() const
{
    return m_streamClients.size();
}

QString WaffleCastServer::sessionPath(const QString &leaf) const
{
    return QStringLiteral("/wafflecast/%1/%2").arg(m_token, leaf);
}

QUrl WaffleCastServer::streamUrl() const
{
    if (!active()) return {};
    QUrl url;
    url.setScheme(QStringLiteral("http"));
    url.setHost(m_advertisedHost);
    url.setPort(m_server->serverPort());
    url.setPath(sessionPath(QStringLiteral("stream.mp3")));
    return url;
}

QUrl WaffleCastServer::metadataUrl() const
{
    return siblingUrl(streamUrl(), QStringLiteral("meta.json"));
}

QUrl WaffleCastServer::coverUrl() const
{
    return siblingUrl(streamUrl(), QStringLiteral("cover"));
}

QString WaffleCastServer::inviteFrame() const
{
    return active() ? WaffleCastInvite::encode(streamUrl(), m_title) : QString();
}

void WaffleCastServer::setTrack(const QString &source,
                                const QString &title,
                                double positionSeconds)
{
    const QString cleanSource = source.trimmed();
    const QString cleanTitle = title.trimmed();
    const bool sourceChanged = cleanSource != m_source;
    m_source = cleanSource;
    if (!cleanTitle.isEmpty()) m_title = cleanTitle;
    else if (sourceChanged) m_title.clear();
    m_position = qMax(0.0, positionSeconds);
    if (sourceChanged) {
        ++m_trackGeneration;
        clearCoverArt();
        if (active() && !m_paused && !m_idle) restartEncoder();
    }
}

void WaffleCastServer::setPlaybackState(bool paused, bool idle, double positionSeconds)
{
    const bool wasBlocked = m_paused || m_idle;
    m_paused = paused;
    m_idle = idle;
    m_position = qMax(0.0, positionSeconds);
    const bool blocked = m_paused || m_idle;
    if (!active()) return;
    if (blocked) stopEncoder();
    else if (wasBlocked || m_encoder->state() == QProcess::NotRunning) startEncoder();
}

void WaffleCastServer::setCoverArt(const QByteArray &data, const QString &mimeType)
{
    m_coverArt = data;
    m_coverMime = mimeType.trimmed().isEmpty() ? QStringLiteral("image/png") : mimeType.trimmed();
    ++m_coverGeneration;
}

void WaffleCastServer::clearCoverArt()
{
    if (m_coverArt.isEmpty()) return;
    m_coverArt.clear();
    ++m_coverGeneration;
}

QString WaffleCastServer::suggestedAdvertisedHost()
{
    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &iface : interfaces) {
        if (!(iface.flags() & QNetworkInterface::IsUp)
            || !(iface.flags() & QNetworkInterface::IsRunning)
            || (iface.flags() & QNetworkInterface::IsLoopBack)) continue;
        for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
            const QHostAddress address = entry.ip();
            if (address.protocol() == QAbstractSocket::IPv4Protocol
                && !address.isLoopback() && !address.isNull()) {
                return address.toString();
            }
        }
    }
    return QStringLiteral("127.0.0.1");
}

void WaffleCastServer::acceptConnections()
{
    while (m_server->hasPendingConnections()) {
        QTcpSocket *socket = m_server->nextPendingConnection();
        if (!socket) continue;
        m_requestBuffers.insert(socket, {});
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { consumeRequest(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] { dropSocket(socket); });
    }
}

void WaffleCastServer::consumeRequest(QTcpSocket *socket)
{
    if (!socket) return;
    QByteArray &buffer = m_requestBuffers[socket];
    buffer += socket->readAll();
    if (buffer.size() > kMaxRequestBytes) {
        writeResponse(socket, 413, "Payload Too Large", "text/plain", "Request too large\n");
        return;
    }
    const int end = buffer.indexOf("\r\n\r\n");
    if (end < 0) return;

    const QByteArray firstLine = buffer.left(buffer.indexOf("\r\n"));
    const QList<QByteArray> parts = firstLine.split(' ');
    if (parts.size() < 2 || parts.at(0) != "GET") {
        writeResponse(socket, 405, "Method Not Allowed", "text/plain", "GET only\n");
        return;
    }
    const QUrl requestUrl = QUrl::fromEncoded(parts.at(1));
    const QString path = requestUrl.path();
    m_requestBuffers.remove(socket);

    if (path == sessionPath(QStringLiteral("stream.mp3"))) {
        QByteArray header;
        header += "HTTP/1.1 200 OK\r\n";
        header += "Content-Type: audio/mpeg\r\n";
        header += "Cache-Control: no-store, no-cache, must-revalidate\r\n";
        header += "Pragma: no-cache\r\n";
        header += "Connection: close\r\n";
        header += "icy-name: WaffleCast\r\n\r\n";
        socket->write(header);
        m_streamClients.insert(socket);
        emit listenerCountChanged(m_streamClients.size());
        if (!m_paused && !m_idle && m_encoder->state() == QProcess::NotRunning) startEncoder();
        return;
    }

    if (path == sessionPath(QStringLiteral("meta.json"))) {
        QJsonObject object;
        object.insert(QStringLiteral("protocol"), QStringLiteral("WaffleCast/1"));
        object.insert(QStringLiteral("title"), m_title);
        object.insert(QStringLiteral("track_generation"), static_cast<double>(m_trackGeneration));
        object.insert(QStringLiteral("cover_generation"), static_cast<double>(m_coverGeneration));
        object.insert(QStringLiteral("paused"), m_paused);
        object.insert(QStringLiteral("idle"), m_idle);
        object.insert(QStringLiteral("listeners"), m_streamClients.size());
        writeResponse(socket, 200, "OK", "application/json; charset=utf-8",
                      QJsonDocument(object).toJson(QJsonDocument::Compact));
        return;
    }

    if (path == sessionPath(QStringLiteral("cover"))) {
        if (m_coverArt.isEmpty()) {
            writeResponse(socket, 404, "Not Found", "text/plain", "No embedded cover art\n");
        } else {
            writeResponse(socket, 200, "OK", m_coverMime.toUtf8(), m_coverArt);
        }
        return;
    }

    writeResponse(socket, 404, "Not Found", "text/plain", "Unknown WaffleCast endpoint\n");
}

void WaffleCastServer::writeResponse(QTcpSocket *socket,
                                     int status,
                                     const QByteArray &reason,
                                     const QByteArray &contentType,
                                     const QByteArray &body,
                                     bool close)
{
    if (!socket) return;
    QByteArray response = "HTTP/1.1 " + QByteArray::number(status) + ' ' + reason + "\r\n";
    response += "Content-Type: " + contentType + "\r\n";
    response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    response += "Cache-Control: no-store\r\n";
    response += close ? "Connection: close\r\n\r\n" : "Connection: keep-alive\r\n\r\n";
    response += body;
    socket->write(response);
    if (close) socket->disconnectFromHost();
}

void WaffleCastServer::startEncoder()
{
    if (!active() || m_streamClients.isEmpty() || m_source.isEmpty() || m_paused || m_idle) return;
    if (m_encoder->state() != QProcess::NotRunning) return;

    QStringList args{
        QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
        QStringLiteral("-re")
    };
    if (m_position > 0.25) {
        args << QStringLiteral("-ss") << QString::number(m_position, 'f', 3);
    }
    args << QStringLiteral("-i") << m_source
         << QStringLiteral("-map") << QStringLiteral("0:a:0")
         << QStringLiteral("-vn")
         << QStringLiteral("-c:a") << QStringLiteral("libmp3lame")
         << QStringLiteral("-b:a") << QStringLiteral("128k")
         << QStringLiteral("-map_metadata") << QStringLiteral("-1")
         << QStringLiteral("-id3v2_version") << QStringLiteral("0")
         << QStringLiteral("-write_id3v1") << QStringLiteral("0")
         << QStringLiteral("-write_xing") << QStringLiteral("0")
         << QStringLiteral("-f") << QStringLiteral("mp3")
         << QStringLiteral("pipe:1");

    m_stoppingEncoder = false;
    m_encoder->start(m_ffmpeg, args);
    if (!m_encoder->waitForStarted(1500)) {
        emit errorMessage(QStringLiteral("WaffleCast encoder could not start: %1").arg(m_encoder->errorString()));
    }
}

void WaffleCastServer::stopEncoder()
{
    if (!m_encoder || m_encoder->state() == QProcess::NotRunning) return;
    m_stoppingEncoder = true;
    m_encoder->terminate();
    if (!m_encoder->waitForFinished(800)) {
        m_encoder->kill();
        m_encoder->waitForFinished(500);
    }
}

void WaffleCastServer::restartEncoder()
{
    stopEncoder();
    if (!m_paused && !m_idle) startEncoder();
}

void WaffleCastServer::encoderReadyRead()
{
    const QByteArray data = m_encoder->readAllStandardOutput();
    if (data.isEmpty()) return;
    const auto clients = m_streamClients;
    for (QTcpSocket *socket : clients) {
        if (!socket || socket->state() != QAbstractSocket::ConnectedState) {
            dropSocket(socket);
            continue;
        }
        if (socket->bytesToWrite() > kSlowClientLimit) {
            socket->disconnectFromHost();
            dropSocket(socket);
            continue;
        }
        socket->write(data);
    }
}

void WaffleCastServer::encoderFinished(int exitCode)
{
    const bool intentional = m_stoppingEncoder;
    m_stoppingEncoder = false;
    const QByteArray errorText = m_encoder->readAllStandardError().trimmed();
    if (!intentional && exitCode != 0 && active() && !m_source.isEmpty()) {
        const QString detail = errorText.isEmpty()
            ? QStringLiteral("ffmpeg exited with code %1").arg(exitCode)
            : QString::fromUtf8(errorText.left(1200));
        emit errorMessage(QStringLiteral("WaffleCast encoder stopped: %1").arg(detail));
    }
}

void WaffleCastServer::dropSocket(QTcpSocket *socket)
{
    if (!socket) return;
    const bool wasListener = m_streamClients.remove(socket);
    m_requestBuffers.remove(socket);
    socket->deleteLater();
    if (wasListener) {
        emit listenerCountChanged(m_streamClients.size());
        if (m_streamClients.isEmpty()) stopEncoder();
    }
}
