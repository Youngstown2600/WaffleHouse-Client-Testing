#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QUrl>

class QProcess;
class QTcpServer;
class QTcpSocket;
class QUdpSocket;

struct WaffleCastInvite
{
    QUrl streamUrl;
    QString title;

    bool isValid() const { return streamUrl.isValid() && !streamUrl.isEmpty(); }
    QUrl metadataUrl() const;
    QUrl coverUrl() const;
    QUrl listenPageUrl() const;
    QUrl m3uUrl() const;
    QUrl plsUrl() const;

    static QString encode(const QUrl &streamUrl, const QString &title);
    static bool decode(const QString &payload, WaffleCastInvite *invite);
};

class WaffleCastServer final : public QObject
{
    Q_OBJECT
public:
    explicit WaffleCastServer(QObject *parent = nullptr);
    ~WaffleCastServer() override;

    bool start(const QString &ffmpegExecutable,
               const QString &advertisedHost,
               quint16 listenPort,
               quint16 advertisedPort,
               QString *error = nullptr);
    void stop();

    bool active() const;
    quint16 listeningPort() const;
    quint16 advertisedPort() const { return m_advertisedPort; }
    int listenerCount() const;
    QString advertisedHost() const { return m_advertisedHost; }
    QString currentTitle() const { return m_title; }
    QUrl streamUrl() const;
    QUrl metadataUrl() const;
    QUrl coverUrl() const;
    QUrl listenPageUrl() const;
    QUrl m3uUrl() const;
    QUrl plsUrl() const;
    QString inviteFrame() const;

    void setTrack(const QString &source,
                  const QString &title,
                  double positionSeconds);
    void setPlaybackState(bool paused, bool idle, double positionSeconds);
    void setCoverArt(const QByteArray &data,
                     const QString &mimeType = QStringLiteral("image/png"));
    void clearCoverArt();

    static QString suggestedAdvertisedHost();
    static QUrl normalizeListenUrl(const QUrl &url);
    static quint16 discoveryPort() { return 8172; }

signals:
    void listenerCountChanged(int count);
    void broadcastStateChanged(bool active);
    void statusMessage(const QString &message);
    void errorMessage(const QString &message);

private slots:
    void acceptConnections();
    void encoderReadyRead();
    void encoderFinished(int exitCode);
    void discoveryReadyRead();

private:
    void consumeRequest(QTcpSocket *socket);
    void writeResponse(QTcpSocket *socket,
                       int status,
                       const QByteArray &reason,
                       const QByteArray &contentType,
                       const QByteArray &body,
                       bool close = true);
    void startEncoder();
    void stopEncoder();
    void restartEncoder();
    void dropSocket(QTcpSocket *socket);
    QString sessionPath(const QString &leaf) const;

    QTcpServer *m_server = nullptr;
    QUdpSocket *m_discovery = nullptr;
    QProcess *m_encoder = nullptr;
    QSet<QTcpSocket *> m_streamClients;
    QHash<QTcpSocket *, QByteArray> m_requestBuffers;

    QString m_ffmpeg;
    QString m_advertisedHost;
    quint16 m_advertisedPort = 0;
    QString m_token;
    QString m_source;
    QString m_title;
    QByteArray m_coverArt;
    QString m_coverMime = QStringLiteral("image/png");
    double m_position = 0.0;
    bool m_paused = false;
    bool m_idle = true;
    bool m_stoppingEncoder = false;
    quint64 m_trackGeneration = 0;
    quint64 m_coverGeneration = 0;
};
