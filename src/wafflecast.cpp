#include "wafflecast.h"

#include <QAbstractSocket>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QProcess>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUdpSocket>
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

QUrl WaffleCastInvite::listenPageUrl() const
{
    return siblingUrl(streamUrl, QStringLiteral("listen"));
}

QUrl WaffleCastInvite::m3uUrl() const
{
    return siblingUrl(streamUrl, QStringLiteral("listen.m3u"));
}

QUrl WaffleCastInvite::plsUrl() const
{
    return siblingUrl(streamUrl, QStringLiteral("listen.pls"));
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
      m_discovery(new QUdpSocket(this)),
      m_encoder(new QProcess(this))
{
    m_encoder->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_server, &QTcpServer::newConnection, this, &WaffleCastServer::acceptConnections);
    connect(m_discovery, &QUdpSocket::readyRead, this, &WaffleCastServer::discoveryReadyRead);
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
                             quint16 listenPort,
                             quint16 advertisedPort,
                             QString *error)
{
    stop();
    m_ffmpeg = ffmpegExecutable.trimmed();
    m_advertisedHost = advertisedHost.trimmed();
    m_advertisedPort = advertisedPort;
    if (m_ffmpeg.isEmpty()) {
        if (error) *error = QStringLiteral("ffmpeg is required to broadcast WaffleCast audio.");
        return false;
    }
    if (m_advertisedHost.isEmpty()) {
        if (error) *error = QStringLiteral("Enter the hostname or IP address listeners should use.");
        return false;
    }
    if (m_advertisedPort == 0) m_advertisedPort = listenPort;
    m_token = QUuid::createUuid().toString(QUuid::WithoutBraces).remove(QLatin1Char('-'));
    if (!m_server->listen(QHostAddress::Any, listenPort)) {
        if (error) *error = m_server->errorString();
        m_token.clear();
        return false;
    }
    if (!m_discovery->bind(QHostAddress::AnyIPv4, discoveryPort(),
                           QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        emit statusMessage(QStringLiteral("WaffleCast LAN discovery unavailable on UDP %1: %2")
                           .arg(discoveryPort()).arg(m_discovery->errorString()));
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
    if (m_discovery->state() != QAbstractSocket::UnconnectedState) m_discovery->close();
    m_token.clear();
    m_ffmpeg.clear();
    m_advertisedPort = 0;
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
    url.setPort(m_advertisedPort ? m_advertisedPort : m_server->serverPort());
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

QUrl WaffleCastServer::listenPageUrl() const
{
    return siblingUrl(streamUrl(), QStringLiteral("listen"));
}

QUrl WaffleCastServer::m3uUrl() const
{
    return siblingUrl(streamUrl(), QStringLiteral("listen.m3u"));
}

QUrl WaffleCastServer::plsUrl() const
{
    return siblingUrl(streamUrl(), QStringLiteral("listen.pls"));
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

QUrl WaffleCastServer::normalizeListenUrl(const QUrl &url)
{
    if (!url.isValid() || (url.scheme() != QStringLiteral("http")
        && url.scheme() != QStringLiteral("https"))) return {};

    const QStringList parts = url.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
    const int marker = parts.indexOf(QStringLiteral("wafflecast"));
    if (marker < 0 || marker + 1 >= parts.size()) return {};

    QStringList base = parts.mid(0, marker + 2);
    QUrl out(url);
    out.setPath(QStringLiteral("/") + base.join(QLatin1Char('/')) + QStringLiteral("/stream.mp3"));
    out.setQuery(QString());
    out.setFragment(QString());
    return out;
}

void WaffleCastServer::discoveryReadyRead()
{
    while (m_discovery && m_discovery->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(static_cast<int>(m_discovery->pendingDatagramSize()));
        QHostAddress sender;
        quint16 senderPort = 0;
        if (m_discovery->readDatagram(datagram.data(), datagram.size(), &sender, &senderPort) < 0) continue;
        if (datagram.trimmed() != QByteArrayLiteral("WAFFLECAST_DISCOVER/1")) continue;
        if (!active()) continue;

        QJsonObject object;
        object.insert(QStringLiteral("protocol"), QStringLiteral("WaffleCastDiscovery/1"));
        object.insert(QStringLiteral("title"), m_title);
        object.insert(QStringLiteral("stream_url"), streamUrl().toString(QUrl::FullyEncoded));
        object.insert(QStringLiteral("listen_url"), listenPageUrl().toString(QUrl::FullyEncoded));
        object.insert(QStringLiteral("host"), m_advertisedHost);
        object.insert(QStringLiteral("port"), static_cast<int>(advertisedPort()));
        object.insert(QStringLiteral("local_port"), static_cast<int>(listeningPort()));
        object.insert(QStringLiteral("stream_path"), sessionPath(QStringLiteral("stream.mp3")));
        object.insert(QStringLiteral("listen_path"), sessionPath(QStringLiteral("listen")));
        const QByteArray reply = QJsonDocument(object).toJson(QJsonDocument::Compact);
        m_discovery->writeDatagram(reply, sender, senderPort);
    }
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

    if (path == QStringLiteral("/") || path == QStringLiteral("/.well-known/wafflecast")) {
        QJsonObject object;
        object.insert(QStringLiteral("protocol"), QStringLiteral("WaffleCastDiscovery/1"));
        object.insert(QStringLiteral("title"), m_title);
        object.insert(QStringLiteral("stream_url"), streamUrl().toString(QUrl::FullyEncoded));
        object.insert(QStringLiteral("listen_url"), listenPageUrl().toString(QUrl::FullyEncoded));
        object.insert(QStringLiteral("metadata_url"), metadataUrl().toString(QUrl::FullyEncoded));
        object.insert(QStringLiteral("cover_url"), coverUrl().toString(QUrl::FullyEncoded));
        object.insert(QStringLiteral("host"), m_advertisedHost);
        object.insert(QStringLiteral("port"), static_cast<int>(advertisedPort()));
        object.insert(QStringLiteral("local_port"), static_cast<int>(listeningPort()));
        object.insert(QStringLiteral("stream_path"), sessionPath(QStringLiteral("stream.mp3")));
        object.insert(QStringLiteral("listen_path"), sessionPath(QStringLiteral("listen")));
        object.insert(QStringLiteral("listeners"), m_streamClients.size());
        writeResponse(socket, 200, "OK", "application/json; charset=utf-8",
                      QJsonDocument(object).toJson(QJsonDocument::Compact));
        return;
    }

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

    if (path == sessionPath(QString())
        || path == sessionPath(QStringLiteral("listen"))
        || path == sessionPath(QStringLiteral("listen.html"))) {
        const QString escapedTitle = (m_title.trimmed().isEmpty()
            ? QStringLiteral("WaffleCast Live") : m_title.trimmed()).toHtmlEscaped();
        const QString page = QStringLiteral(R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>%1 — WaffleCast</title>
<style>
body{font-family:system-ui,sans-serif;background:#111;color:#eee;margin:0;display:grid;place-items:center;min-height:100vh}
main{width:min(92vw,560px);background:#1d1d1d;border:1px solid #444;border-radius:14px;padding:22px;box-sizing:border-box}
h1{margin:0 0 4px;font-size:1.4rem}.sub{color:#aaa;margin-bottom:18px}#cover{display:block;width:min(70vw,320px);height:min(70vw,320px);object-fit:contain;background:#090909;margin:0 auto 18px;border-radius:8px}audio{width:100%}.links{display:flex;flex-wrap:wrap;gap:10px;margin-top:16px}.links a{color:#9dd2ff}.status{color:#bbb;margin-top:12px;font-size:.92rem}
</style>
</head>
<body><main>
<h1 id="title">%1</h1><div class="sub">WaffleCast live broadcast</div>
<img id="cover" src="cover" alt="Album art" onerror="this.style.display='none'">
<audio controls autoplay src="stream.mp3"></audio>
<div class="status" id="status">Connecting…</div>
<div class="links"><a href="stream.mp3">Direct MP3</a><a href="listen.m3u">M3U</a><a href="listen.pls">PLS</a></div>
<script>
let cg=-1;
async function poll(){try{const r=await fetch('meta.json',{cache:'no-store'});if(!r.ok)return;const m=await r.json();
if(m.title)document.getElementById('title').textContent=m.title;
document.getElementById('status').textContent=(m.paused?'DJ paused':'Live')+' — '+m.listeners+' listener'+(m.listeners===1?'':'s');
if(m.cover_generation!==cg){cg=m.cover_generation;const i=document.getElementById('cover');i.style.display='block';i.src='cover?g='+cg;}
}catch(e){document.getElementById('status').textContent='Stream metadata unavailable';}}
poll();setInterval(poll,2500);
</script>
</main></body></html>)HTML").arg(escapedTitle);
        writeResponse(socket, 200, "OK", "text/html; charset=utf-8", page.toUtf8());
        return;
    }

    if (path == sessionPath(QStringLiteral("listen.m3u"))) {
        const QByteArray body = QStringLiteral("#EXTM3U\n#EXTINF:-1,%1\n%2\n")
            .arg(m_title.trimmed().isEmpty() ? QStringLiteral("WaffleCast") : m_title.trimmed(),
                 streamUrl().toString(QUrl::FullyEncoded)).toUtf8();
        writeResponse(socket, 200, "OK", "audio/x-mpegurl; charset=utf-8", body);
        return;
    }

    if (path == sessionPath(QStringLiteral("listen.pls"))) {
        const QByteArray body = QStringLiteral(
            "[playlist]\nNumberOfEntries=1\nFile1=%1\nTitle1=%2\nLength1=-1\nVersion=2\n")
            .arg(streamUrl().toString(QUrl::FullyEncoded),
                 m_title.trimmed().isEmpty() ? QStringLiteral("WaffleCast") : m_title.trimmed()).toUtf8();
        writeResponse(socket, 200, "OK", "audio/x-scpls; charset=utf-8", body);
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
