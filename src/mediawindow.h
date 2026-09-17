#pragma once

#include <QMainWindow>
#include <QList>
#include <QStringList>
#include <QUrl>

class MediaController;
class WaffleCastServer;
class QCheckBox;
class QCloseEvent;
class QComboBox;
class QDragEnterEvent;
class QDropEvent;
class QLabel;
class QListWidget;
class QNetworkAccessManager;
class QNetworkReply;
class QProcess;
class QPushButton;
class QSlider;
class QTimer;

class MediaWindow final : public QMainWindow
{
    Q_OBJECT
public:
    explicit MediaWindow(QWidget *parent = nullptr);
    bool executeCommand(const QString &command, const QString &arguments, QString *message = nullptr);

    bool waffleCastActive() const;
    QString waffleCastInviteFrame() const;
    QString waffleCastTitle() const;
    void joinWaffleCast(const QUrl &streamUrl,
                        const QString &title,
                        const QString &hostDisplay = QString());

public slots:
    void showAndRaise();
    void openMediaFiles();
    void openStreamDialog();
    void openInternetPlaylistDialog();
    void searchShoutcastDirectory();
    void openPlaylistDialog();
    void savePlaylistDialog();
    void startWaffleCast();
    void stopWaffleCast();
    void connectWaffleCastDialog();

protected:
    void closeEvent(QCloseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void buildUi();
    void addSources(const QStringList &sources, bool playFirst);
    void playSelected();
    void removeSelected();
    void clearPlaylist();
    void syncPlaylist(const QStringList &sources,
                      const QStringList &titles,
                      int currentIndex);
    void updateTime();
    QString timeText(double seconds) const;

    void loadAlbumArtForSource(const QString &source);
    void setAlbumArt(const QByteArray &data);
    void clearAlbumArt(const QString &message = QStringLiteral("No embedded album art"));
    void fetchWaffleCastMetadata();
    void fetchWaffleCastCover();
    void updateWaffleCastUi();

    MediaController *m_media = nullptr;
    WaffleCastServer *m_cast = nullptr;
    QNetworkAccessManager *m_network = nullptr;
    QProcess *m_artProcess = nullptr;
    QTimer *m_castPollTimer = nullptr;
    QNetworkReply *m_metadataReply = nullptr;
    QNetworkReply *m_coverReply = nullptr;

    QLabel *m_title = nullptr;
    QLabel *m_source = nullptr;
    QLabel *m_time = nullptr;
    QLabel *m_backend = nullptr;
    QLabel *m_albumArt = nullptr;
    QLabel *m_castStatus = nullptr;
    QLabel *m_castHint = nullptr;
    QPushButton *m_castStart = nullptr;
    QPushButton *m_castStop = nullptr;
    QPushButton *m_castCopyInvite = nullptr;
    QPushButton *m_castCopyPublic = nullptr;
    QPushButton *m_castConnect = nullptr;
    QListWidget *m_playlist = nullptr;
    QSlider *m_seek = nullptr;
    QSlider *m_volume = nullptr;
    QCheckBox *m_shuffle = nullptr;
    QComboBox *m_repeat = nullptr;
    QList<QSlider *> m_eqSliders;

    QByteArray m_albumArtBytes;
    QString m_artSource;
    QUrl m_castStreamUrl;
    QUrl m_castMetadataUrl;
    QUrl m_castCoverUrl;
    QString m_castRemoteTitle;
    QString m_castRemoteHost;
    qint64 m_castTrackGeneration = -1;
    qint64 m_castCoverGeneration = -1;
    bool m_castListening = false;

    double m_position = 0.0;
    double m_duration = 0.0;
    bool m_draggingSeek = false;
    bool m_syncingPlaylist = false;
};
