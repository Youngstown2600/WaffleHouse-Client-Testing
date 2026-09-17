#include "mediawindow.h"
#include "mediacontroller.h"
#include "appbranding.h"
#include "wafflecast.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDragEnterEvent>
#include <QDesktopServices>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QMimeData>
#include <QPushButton>
#include <QProcess>
#include <QPixmap>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalBlocker>
#include <QSlider>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <functional>

MediaWindow::MediaWindow(QWidget *parent)
    : QMainWindow(parent),
      m_media(new MediaController(this)),
      m_cast(new WaffleCastServer(this)),
      m_network(new QNetworkAccessManager(this)),
      m_artProcess(new QProcess(this)),
      m_castPollTimer(new QTimer(this))
{
    setWindowTitle(QStringLiteral("%1 %2 — Media Center").arg(appDisplayName(), appVersionString()));
    // Keep the media center as a real independent window even though MainWindow
    // owns its lifetime. This avoids desktop/WM differences on Linux and FreeBSD.
    setWindowFlag(Qt::Window, true);
    resize(900, 680);
    setMinimumSize(720, 540);
    setAcceptDrops(true);
    setAttribute(Qt::WA_DeleteOnClose, false);

    buildUi();

    connect(m_media, &MediaController::nowPlayingChanged, this, [this](const QString &title) {
        if (!m_castListening) {
            m_title->setText(title.isEmpty() ? QStringLiteral("Nothing playing") : title);
        } else if (m_castRemoteTitle.isEmpty()) {
            m_title->setText(title.isEmpty() ? QStringLiteral("WaffleCast") : title);
        }
        if (m_cast->active()) {
            m_cast->setTrack(m_media->currentSource(), title, m_position);
        }
    });
    connect(m_media, &MediaController::sourceChanged, this, [this](const QString &source) {
        if (m_castListening && !m_castStreamUrl.isEmpty()
            && QUrl(source) != m_castStreamUrl) {
            m_castListening = false;
            m_castPollTimer->stop();
            m_castStreamUrl.clear();
            m_castMetadataUrl.clear();
            m_castCoverUrl.clear();
            m_castRemoteTitle.clear();
            m_castRemoteHost.clear();
            updateWaffleCastUi();
        }
        if (!m_castListening) {
            m_source->setText(source);
            loadAlbumArtForSource(source);
        }
        if (m_cast->active()) {
            m_cast->setTrack(source, m_media->nowPlaying(), 0.0);
        }
    });
    connect(m_media, &MediaController::positionChanged, this, [this](double value) {
        m_position = value;
        if (!m_draggingSeek && m_duration > 0.0) {
            m_seek->setValue(qBound(0, qRound((m_position / m_duration) * 1000.0), 1000));
        }
        updateTime();
    });
    connect(m_media, &MediaController::durationChanged, this, [this](double value) {
        m_duration = value;
        if (!m_draggingSeek && m_duration <= 0.0) m_seek->setValue(0);
        updateTime();
    });
    connect(m_media, &MediaController::volumeChanged, this, [this](int value) {
        if (m_volume->value() != value) m_volume->setValue(value);
    });
    connect(m_media, &MediaController::playlistEntriesChanged,
            this, &MediaWindow::syncPlaylist);
    connect(m_media, &MediaController::errorMessage, this, [this](const QString &message) {
        statusBar()->showMessage(message, 10000);
    });
    connect(m_media, &MediaController::statusMessage, this, [this](const QString &message) {
        statusBar()->showMessage(message, 7000);
    });
    connect(m_media, &MediaController::pauseChanged, this, [this](bool paused) {
        if (m_cast->active()) m_cast->setPlaybackState(paused, m_media->idle(), m_position);
    });
    connect(m_media, &MediaController::idleChanged, this, [this](bool idle) {
        if (m_cast->active()) m_cast->setPlaybackState(m_media->paused(), idle, m_position);
    });

    connect(m_cast, &WaffleCastServer::listenerCountChanged, this, [this](int) { updateWaffleCastUi(); });
    connect(m_cast, &WaffleCastServer::broadcastStateChanged, this, [this](bool) { updateWaffleCastUi(); });
    connect(m_cast, &WaffleCastServer::statusMessage, this, [this](const QString &message) {
        statusBar()->showMessage(message, 7000);
    });
    connect(m_cast, &WaffleCastServer::errorMessage, this, [this](const QString &message) {
        statusBar()->showMessage(message, 10000);
    });

    connect(m_artProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus) {
        const QByteArray bytes = m_artProcess->readAllStandardOutput();
        if (exitCode == 0 && !bytes.isEmpty()) setAlbumArt(bytes);
        else clearAlbumArt();
    });

    m_castPollTimer->setInterval(2500);
    connect(m_castPollTimer, &QTimer::timeout, this, &MediaWindow::fetchWaffleCastMetadata);

    QString backend = m_media->backendAvailable()
        ? QStringLiteral("mpv backend ready")
        : QStringLiteral("mpv not found — install mpv");
    if (!m_media->backendVersion().isEmpty()) {
        backend += QStringLiteral(" (%1)").arg(m_media->backendVersion());
    }
    m_backend->setText(backend);

    // MediaController restores its persistent library before the window connects
    // signals, so seed the GUI explicitly from the saved state. Nothing starts
    // playing until the user chooses Play/Resume.
    syncPlaylist(m_media->playlistSources(), m_media->playlistTitles(), m_media->playlistIndex());
    {
        const QSignalBlocker shuffleBlocker(m_shuffle);
        m_shuffle->setChecked(m_media->shuffleEnabled());
    }
    {
        const QSignalBlocker repeatBlocker(m_repeat);
        const QString repeat = m_media->repeatMode();
        m_repeat->setCurrentIndex(repeat == QStringLiteral("one") ? 1
                                  : repeat == QStringLiteral("all") ? 2 : 0);
    }
    {
        const QSignalBlocker volumeBlocker(m_volume);
        m_volume->setValue(m_media->volume());
    }
    statusBar()->showMessage(QStringLiteral("Persistent media library: %1")
                                 .arg(m_media->mediaLibraryPath()), 6000);
    clearAlbumArt();
    updateWaffleCastUi();
}

bool MediaWindow::executeCommand(const QString &command,
                                 const QString &arguments,
                                 QString *message)
{
    const QString cmd = command.trimmed().toCaseFolded();
    QString rest = arguments.trimmed();
    auto takeArg = [](QString &value) {
        value = value.trimmed();
        if (value.isEmpty()) return QString();
        if (value.startsWith(QLatin1Char('"')) || value.startsWith(QLatin1Char('\''))) {
            const QChar quote = value.front();
            const int end = value.indexOf(quote, 1);
            if (end > 0) {
                const QString out = value.mid(1, end - 1);
                value = value.mid(end + 1).trimmed();
                return out;
            }
        }
        const int split = value.indexOf(QLatin1Char(' '));
        if (split < 0) { const QString out = value; value.clear(); return out; }
        const QString out = value.left(split);
        value = value.mid(split + 1).trimmed();
        return out;
    };
    auto result = [message](const QString &text) {
        if (message) *message = text;
    };

    if (cmd == QStringLiteral("media") || cmd == QStringLiteral("mstatus")) {
        showAndRaise();
        result(m_media->statusLines().join(QLatin1Char('\n')));
        return true;
    }
    if (cmd == QStringLiteral("mplay") || cmd == QStringLiteral("mstream")) {
        const QString source = takeArg(rest);
        if (source.isEmpty()) {
            result(cmd == QStringLiteral("mstream")
                       ? QStringLiteral("Usage: /mstream URL")
                       : QStringLiteral("Usage: /mplay FILE|URL"));
            return true;
        }
        showAndRaise();
        m_media->play(source);
        return true;
    }
    if (cmd == QStringLiteral("mshoutcast")) {
        const QString query = rest.trimmed();
        if (query.isEmpty()) { result(QStringLiteral("Usage: /mshoutcast SEARCH-TERMS")); return true; }
        const QUrl url = QUrl::fromEncoded(
            QByteArrayLiteral("https://directory.shoutcast.com/Search?query=")
            + QUrl::toPercentEncoding(query));
        QDesktopServices::openUrl(url);
        result(QStringLiteral("Opened SHOUTcast directory search: %1").arg(query));
        return true;
    }
    if (cmd == QStringLiteral("menqueue")) {
        const QString source = takeArg(rest);
        if (source.isEmpty()) { result(QStringLiteral("Usage: /menqueue FILE|URL")); return true; }
        showAndRaise();
        m_media->enqueue(source);
        return true;
    }
    if (cmd == QStringLiteral("mplaylist")) {
        const QString source = takeArg(rest);
        if (source.isEmpty()) { result(QStringLiteral("Usage: /mplaylist PLAYLIST-PATH-OR-URL")); return true; }
        showAndRaise();
        m_media->loadPlaylist(source, true);
        return true;
    }
    if (cmd == QStringLiteral("mpause")) { m_media->pause(); return true; }
    if (cmd == QStringLiteral("mresume") || cmd == QStringLiteral("mplaykey")) { m_media->resume(); return true; }
    if (cmd == QStringLiteral("mtoggle")) {
        if (m_media->idle()) m_media->resume();
        else m_media->togglePause();
        return true;
    }
    if (cmd == QStringLiteral("mstop")) { m_media->stop(); return true; }
    if (cmd == QStringLiteral("mnext")) { m_media->next(); return true; }
    if (cmd == QStringLiteral("mprev")) { m_media->previous(); return true; }
    if (cmd == QStringLiteral("mvolup")) { m_media->setVolume(m_media->volume() + 5); return true; }
    if (cmd == QStringLiteral("mvoldown")) { m_media->setVolume(m_media->volume() - 5); return true; }
    if (cmd == QStringLiteral("mseek")) {
        bool ok = false;
        const double seconds = takeArg(rest).toDouble(&ok);
        if (!ok) result(QStringLiteral("Usage: /mseek SECONDS"));
        else m_media->seekRelative(seconds);
        return true;
    }
    if (cmd == QStringLiteral("mvolume")) {
        bool ok = false;
        const int volume = takeArg(rest).toInt(&ok);
        if (!ok || volume < 0 || volume > 150) result(QStringLiteral("Usage: /mvolume 0..150"));
        else m_media->setVolume(volume);
        return true;
    }
    if (cmd == QStringLiteral("mmute")) {
        const QString value = takeArg(rest).toCaseFolded();
        if (value == QStringLiteral("on")) m_media->setMuted(true);
        else if (value == QStringLiteral("off")) m_media->setMuted(false);
        else if (value == QStringLiteral("toggle") || value.isEmpty()) m_media->toggleMuted();
        else result(QStringLiteral("Usage: /mmute on|off|toggle"));
        return true;
    }
    if (cmd == QStringLiteral("mshuffle")) {
        const QString value = takeArg(rest).toCaseFolded();
        if (value != QStringLiteral("on") && value != QStringLiteral("off")) result(QStringLiteral("Usage: /mshuffle on|off"));
        else m_media->setShuffle(value == QStringLiteral("on"));
        return true;
    }
    if (cmd == QStringLiteral("mrepeat")) {
        const QString value = takeArg(rest).toCaseFolded();
        if (value != QStringLiteral("off") && value != QStringLiteral("one") && value != QStringLiteral("all"))
            result(QStringLiteral("Usage: /mrepeat off|one|all"));
        else m_media->setRepeatMode(value);
        return true;
    }
    if (cmd == QStringLiteral("meq")) {
        const QString bandText = takeArg(rest).toCaseFolded();
        if (bandText == QStringLiteral("flat") || bandText == QStringLiteral("reset")) {
            m_media->resetEqualizer();
            return true;
        }
        bool bandOk = false, gainOk = false;
        const int band = bandText.toInt(&bandOk);
        const double gain = takeArg(rest).toDouble(&gainOk);
        if (!bandOk || !gainOk || band < 0 || band > 9 || gain < -12.0 || gain > 12.0)
            result(QStringLiteral("Usage: /meq BAND(0..9) GAIN(-12..12) or /meq flat"));
        else m_media->setEqualizerBand(band, gain);
        return true;
    }
    return false;
}

void MediaWindow::buildUi()
{
    auto *central = new QWidget(this);
    auto *outer = new QVBoxLayout(central);
    outer->setContentsMargins(14, 14, 14, 14);
    outer->setSpacing(10);

    auto *header = new QGroupBox(QStringLiteral("Now Playing"), central);
    auto *headerLayout = new QHBoxLayout(header);

    auto *artColumn = new QVBoxLayout;
    auto *artCaption = new QLabel(QStringLiteral("Album Art"), header);
    artCaption->setAlignment(Qt::AlignCenter);
    m_albumArt = new QLabel(header);
    m_albumArt->setFixedSize(180, 180);
    m_albumArt->setAlignment(Qt::AlignCenter);
    m_albumArt->setWordWrap(true);
    m_albumArt->setStyleSheet(QStringLiteral("QLabel { border: 1px solid palette(mid); padding: 6px; }"));
    artColumn->addWidget(artCaption);
    artColumn->addWidget(m_albumArt);
    artColumn->addStretch(1);
    headerLayout->addLayout(artColumn);

    auto *nowPlayingLayout = new QVBoxLayout;
    m_title = new QLabel(QStringLiteral("Nothing playing"), header);
    QFont titleFont = m_title->font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 2);
    m_title->setFont(titleFont);
    m_title->setTextInteractionFlags(Qt::TextSelectableByMouse);
    nowPlayingLayout->addWidget(m_title);

    m_source = new QLabel(QStringLiteral(
        "Local files, SHOUTcast/Icecast, HTTP/HLS streams, playlists, and WaffleCast"), header);
    m_source->setWordWrap(true);
    m_source->setTextInteractionFlags(Qt::TextSelectableByMouse);
    nowPlayingLayout->addWidget(m_source);

    auto *seekRow = new QHBoxLayout;
    m_seek = new QSlider(Qt::Horizontal, header);
    m_seek->setRange(0, 1000);
    m_time = new QLabel(QStringLiteral("00:00 / 00:00"), header);
    seekRow->addWidget(m_seek, 1);
    seekRow->addWidget(m_time);
    nowPlayingLayout->addLayout(seekRow);

    auto *controls = new QHBoxLayout;
    auto *prev = new QPushButton(QStringLiteral("Previous"), header);
    auto *play = new QPushButton(QStringLiteral("Play"), header);
    auto *pause = new QPushButton(QStringLiteral("Pause"), header);
    auto *stop = new QPushButton(QStringLiteral("Stop"), header);
    auto *next = new QPushButton(QStringLiteral("Next"), header);
    auto *mute = new QPushButton(QStringLiteral("Mute"), header);
    controls->addWidget(prev);
    controls->addWidget(play);
    controls->addWidget(pause);
    controls->addWidget(stop);
    controls->addWidget(next);
    controls->addWidget(mute);
    controls->addStretch(1);

    controls->addWidget(new QLabel(QStringLiteral("Volume"), header));
    m_volume = new QSlider(Qt::Horizontal, header);
    m_volume->setRange(0, 150);
    m_volume->setValue(80);
    m_volume->setMaximumWidth(130);
    controls->addWidget(m_volume);
    nowPlayingLayout->addLayout(controls);

    auto *modeRow = new QHBoxLayout;
    m_shuffle = new QCheckBox(QStringLiteral("Shuffle"), header);
    m_repeat = new QComboBox(header);
    m_repeat->addItems({QStringLiteral("Repeat Off"), QStringLiteral("Repeat One"), QStringLiteral("Repeat All")});
    m_backend = new QLabel(header);
    modeRow->addWidget(m_shuffle);
    modeRow->addWidget(m_repeat);
    modeRow->addStretch(1);
    modeRow->addWidget(m_backend);
    nowPlayingLayout->addLayout(modeRow);
    nowPlayingLayout->addStretch(1);
    headerLayout->addLayout(nowPlayingLayout, 1);
    outer->addWidget(header);

    auto *castBox = new QGroupBox(QStringLiteral("WaffleCast — Shared Media Session"), central);
    auto *castLayout = new QVBoxLayout(castBox);
    auto *castRow = new QHBoxLayout;
    m_castStatus = new QLabel(QStringLiteral("Broadcast off"), castBox);
    m_castStart = new QPushButton(QStringLiteral("Start Broadcast"), castBox);
    m_castStop = new QPushButton(QStringLiteral("End Broadcast"), castBox);
    m_castCopyInvite = new QPushButton(QStringLiteral("Copy Invite"), castBox);
    castRow->addWidget(m_castStatus, 1);
    castRow->addWidget(m_castStart);
    castRow->addWidget(m_castStop);
    castRow->addWidget(m_castCopyInvite);
    castLayout->addLayout(castRow);
    m_castHint = new QLabel(QStringLiteral(
        "Start a broadcast, then use /wafflecast in an AIM IM or IRC PM/channel to invite WaffleHouse listeners."), castBox);
    m_castHint->setWordWrap(true);
    castLayout->addWidget(m_castHint);
    outer->addWidget(castBox);

    auto *playlistBox = new QGroupBox(QStringLiteral("Playlist / Queue"), central);
    auto *playlistLayout = new QVBoxLayout(playlistBox);
    m_playlist = new QListWidget(playlistBox);
    m_playlist->setSelectionMode(QAbstractItemView::ExtendedSelection);
    playlistLayout->addWidget(m_playlist, 1);

    auto *playlistButtons = new QHBoxLayout;
    auto *add = new QPushButton(QStringLiteral("Add Files"), playlistBox);
    auto *stream = new QPushButton(QStringLiteral("Stream URL"), playlistBox);
    auto *shoutcast = new QPushButton(QStringLiteral("SHOUTcast Search"), playlistBox);
    auto *internetList = new QPushButton(QStringLiteral("Playlist URL"), playlistBox);
    auto *loadList = new QPushButton(QStringLiteral("Load Playlist"), playlistBox);
    auto *saveList = new QPushButton(QStringLiteral("Save Playlist"), playlistBox);
    auto *libraryFolder = new QPushButton(QStringLiteral("Library Folder"), playlistBox);
    auto *remove = new QPushButton(QStringLiteral("Remove"), playlistBox);
    auto *clear = new QPushButton(QStringLiteral("Clear Library"), playlistBox);
    playlistButtons->addWidget(add);
    playlistButtons->addWidget(stream);
    playlistButtons->addWidget(shoutcast);
    playlistButtons->addWidget(internetList);
    playlistButtons->addWidget(loadList);
    playlistButtons->addWidget(saveList);
    playlistButtons->addWidget(libraryFolder);
    playlistButtons->addStretch(1);
    playlistButtons->addWidget(remove);
    playlistButtons->addWidget(clear);
    playlistLayout->addLayout(playlistButtons);
    outer->addWidget(playlistBox, 1);

    auto *eqBox = new QGroupBox(QStringLiteral("10-Band Equalizer"), central);
    auto *eqLayout = new QHBoxLayout(eqBox);
    static const char *labels[10] = {"60", "170", "310", "600", "1K", "3K", "6K", "12K", "14K", "16K"};
    for (int i = 0; i < 10; ++i) {
        auto *column = new QVBoxLayout;
        auto *gain = new QLabel(QStringLiteral("0"), eqBox);
        gain->setAlignment(Qt::AlignCenter);
        auto *slider = new QSlider(Qt::Vertical, eqBox);
        slider->setRange(-12, 12);
        slider->setValue(0);
        slider->setInvertedAppearance(true);
        auto *freq = new QLabel(QString::fromLatin1(labels[i]), eqBox);
        freq->setAlignment(Qt::AlignCenter);
        column->addWidget(gain);
        column->addWidget(slider, 1);
        column->addWidget(freq);
        eqLayout->addLayout(column);
        m_eqSliders.append(slider);
        connect(slider, &QSlider::valueChanged, this, [this, i, gain](int value) {
            gain->setText(QString::number(value));
            m_media->setEqualizerBand(i, value);
        });
    }
    auto *flat = new QPushButton(QStringLiteral("Flat"), eqBox);
    eqLayout->addWidget(flat, 0, Qt::AlignBottom);
    outer->addWidget(eqBox);

    setCentralWidget(central);

    connect(prev, &QPushButton::clicked, m_media, &MediaController::previous);
    connect(play, &QPushButton::clicked, this, [this] {
        // Pause and Stop are deliberately different states. A paused mpv still
        // owns a valid queue/current item, so Play must *only* clear pause.
        // Selecting the playlist index while pause=true leaves mpv paused and was
        // the cause of the Pause -> Play failure.
        if (m_media->paused()) {
            m_media->resume();
            return;
        }

        // After Stop the backend queue is intentionally empty. The visible row
        // is authoritative and playSelected() rebuilds mpv from WaffleHouse's
        // application-owned library.
        if (m_media->idle()) {
            if (m_playlist->currentRow() >= 0) playSelected();
            else m_media->resume();
            return;
        }

        // While already playing, Play keeps the historical behavior of starting
        // the selected row (or doing nothing useful if there is no selection).
        if (m_playlist->currentRow() >= 0) playSelected();
        else m_media->resume();
    });
    connect(pause, &QPushButton::clicked, m_media, &MediaController::pause);
    connect(stop, &QPushButton::clicked, m_media, &MediaController::stop);
    connect(next, &QPushButton::clicked, m_media, &MediaController::next);
    connect(mute, &QPushButton::clicked, m_media, &MediaController::toggleMuted);
    connect(m_volume, &QSlider::valueChanged, m_media, &MediaController::setVolume);
    connect(m_shuffle, &QCheckBox::toggled, m_media, &MediaController::setShuffle);
    connect(m_repeat, &QComboBox::currentIndexChanged, this, [this](int index) {
        m_media->setRepeatMode(index == 1 ? QStringLiteral("one")
                                         : index == 2 ? QStringLiteral("all")
                                                      : QStringLiteral("off"));
    });
    connect(m_seek, &QSlider::sliderPressed, this, [this] { m_draggingSeek = true; });
    connect(m_seek, &QSlider::sliderReleased, this, [this] {
        m_draggingSeek = false;
        if (m_duration > 0.0) m_media->seekAbsolute(m_duration * m_seek->value() / 1000.0);
    });
    connect(add, &QPushButton::clicked, this, &MediaWindow::openMediaFiles);
    connect(stream, &QPushButton::clicked, this, &MediaWindow::openStreamDialog);
    connect(shoutcast, &QPushButton::clicked, this, &MediaWindow::searchShoutcastDirectory);
    connect(internetList, &QPushButton::clicked, this, &MediaWindow::openInternetPlaylistDialog);
    connect(loadList, &QPushButton::clicked, this, &MediaWindow::openPlaylistDialog);
    connect(saveList, &QPushButton::clicked, this, &MediaWindow::savePlaylistDialog);
    connect(libraryFolder, &QPushButton::clicked, this, [this] {
        const QString folder = QFileInfo(m_media->mediaLibraryPath()).absolutePath();
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(folder))) {
            QMessageBox::information(this, QStringLiteral("Media Library"),
                                     QStringLiteral("Media library folder:\n%1").arg(folder));
        }
    });
    connect(remove, &QPushButton::clicked, this, &MediaWindow::removeSelected);
    connect(clear, &QPushButton::clicked, this, &MediaWindow::clearPlaylist);
    connect(m_castStart, &QPushButton::clicked, this, &MediaWindow::startWaffleCast);
    connect(m_castStop, &QPushButton::clicked, this, &MediaWindow::stopWaffleCast);
    connect(m_castCopyInvite, &QPushButton::clicked, this, [this] {
        const QString frame = waffleCastInviteFrame();
        if (frame.isEmpty()) return;
        QApplication::clipboard()->setText(frame);
        statusBar()->showMessage(QStringLiteral("WaffleCast invite copied to clipboard."), 5000);
    });
    connect(flat, &QPushButton::clicked, this, [this] {
        for (QSlider *slider : m_eqSliders) slider->setValue(0);
        m_media->resetEqualizer();
    });
    connect(m_playlist, &QListWidget::itemDoubleClicked, this, [this] { playSelected(); });
}

bool MediaWindow::waffleCastActive() const
{
    return m_cast && m_cast->active();
}

QString MediaWindow::waffleCastInviteFrame() const
{
    return m_cast ? m_cast->inviteFrame() : QString();
}

QString MediaWindow::waffleCastTitle() const
{
    return m_cast ? m_cast->currentTitle() : QString();
}

void MediaWindow::startWaffleCast()
{
    if (m_castListening) {
        QMessageBox::information(this, QStringLiteral("WaffleCast"),
                                 QStringLiteral("Leave the current WaffleCast before starting your own broadcast."));
        return;
    }
    if (m_media->currentSource().trimmed().isEmpty() || m_media->idle()) {
        QMessageBox::information(this, QStringLiteral("WaffleCast"),
                                 QStringLiteral("Start a song in the Media Center first, then start WaffleCast."));
        return;
    }
    if (m_media->ffmpegExecutable().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("WaffleCast"),
                             QStringLiteral("ffmpeg is required for WaffleCast broadcasting."));
        return;
    }

    bool ok = false;
    const QString host = QInputDialog::getText(
        this, QStringLiteral("Start WaffleCast"),
        QStringLiteral("Hostname or IP listeners should use:\n"
                       "(LAN IP works on the same network; use a reachable public hostname/IP for Internet listeners.)"),
        QLineEdit::Normal, WaffleCastServer::suggestedAdvertisedHost(), &ok).trimmed();
    if (!ok || host.isEmpty()) return;

    const int port = QInputDialog::getInt(
        this, QStringLiteral("WaffleCast Port"),
        QStringLiteral("TCP port for the WaffleCast audio stream:"),
        8173, 1024, 65535, 1, &ok);
    if (!ok) return;

    QString error;
    if (!m_cast->start(m_media->ffmpegExecutable(), host, static_cast<quint16>(port), &error)) {
        QMessageBox::warning(this, QStringLiteral("WaffleCast"), error);
        return;
    }
    m_cast->setTrack(m_media->currentSource(), m_media->nowPlaying(), m_position);
    m_cast->setPlaybackState(m_media->paused(), m_media->idle(), m_position);
    if (!m_albumArtBytes.isEmpty()) m_cast->setCoverArt(m_albumArtBytes, QStringLiteral("image/png"));
    updateWaffleCastUi();
    statusBar()->showMessage(
        QStringLiteral("WaffleCast started. Use /wafflecast in an AIM or IRC conversation to invite listeners."),
        10000);
}

void MediaWindow::stopWaffleCast()
{
    if (m_castListening) {
        m_castPollTimer->stop();
        if (m_metadataReply) m_metadataReply->abort();
        if (m_coverReply) m_coverReply->abort();
        m_media->stop();
        m_castListening = false;
        m_castStreamUrl.clear();
        m_castMetadataUrl.clear();
        m_castCoverUrl.clear();
        m_castRemoteTitle.clear();
        m_castRemoteHost.clear();
        m_castTrackGeneration = -1;
        m_castCoverGeneration = -1;
        clearAlbumArt();
        updateWaffleCastUi();
        statusBar()->showMessage(QStringLiteral("Left WaffleCast."), 5000);
        return;
    }
    if (m_cast) m_cast->stop();
    updateWaffleCastUi();
}

void MediaWindow::joinWaffleCast(const QUrl &streamUrl,
                                 const QString &title,
                                 const QString &hostDisplay)
{
    if (!streamUrl.isValid() || streamUrl.isEmpty()) return;
    if (m_cast->active()) m_cast->stop();

    WaffleCastInvite invite;
    invite.streamUrl = streamUrl;
    invite.title = title;
    m_castListening = true;
    m_castStreamUrl = streamUrl;
    m_castMetadataUrl = invite.metadataUrl();
    m_castCoverUrl = invite.coverUrl();
    m_castRemoteTitle = title.trimmed();
    m_castRemoteHost = hostDisplay.trimmed();
    m_castTrackGeneration = -1;
    m_castCoverGeneration = -1;

    showAndRaise();
    m_title->setText(m_castRemoteTitle.isEmpty() ? QStringLiteral("WaffleCast") : m_castRemoteTitle);
    m_source->setText(m_castRemoteHost.isEmpty()
        ? QStringLiteral("WaffleCast live stream")
        : QStringLiteral("WaffleCast from %1").arg(m_castRemoteHost));
    clearAlbumArt(QStringLiteral("Loading WaffleCast album art…"));
    updateWaffleCastUi();

    if (!m_media->playTransient(streamUrl.toString(QUrl::FullyEncoded), m_castRemoteTitle)) {
        m_castListening = false;
        updateWaffleCastUi();
        return;
    }
    m_castPollTimer->start();
    fetchWaffleCastMetadata();
    fetchWaffleCastCover();
}

void MediaWindow::loadAlbumArtForSource(const QString &source)
{
    if (!m_artProcess || m_castListening) return;
    if (m_artProcess->state() != QProcess::NotRunning) {
        m_artProcess->kill();
        m_artProcess->waitForFinished(300);
    }
    m_artProcess->readAllStandardOutput();
    m_artProcess->readAllStandardError();
    m_artSource = source.trimmed();
    clearAlbumArt();
    if (m_artSource.isEmpty() || m_media->ffmpegExecutable().isEmpty()) return;

    const QUrl url(m_artSource);
    if (url.isValid() && !url.scheme().isEmpty() && !url.isLocalFile()) return;
    const QString localPath = url.isLocalFile() ? url.toLocalFile() : m_artSource;
    static const QStringList supported = {
        QStringLiteral("mp3"), QStringLiteral("m4a"), QStringLiteral("aac"),
        QStringLiteral("flac"), QStringLiteral("ogg"), QStringLiteral("opus"),
        QStringLiteral("wma")
    };
    if (!supported.contains(QFileInfo(localPath).suffix().toCaseFolded())) return;

    // Embedded MP3/MP4/FLAC artwork is exposed by ffmpeg as an attached-picture
    // video stream. Convert only that first image to PNG and keep the audio file
    // itself private; WaffleCast serves only these extracted image bytes.
    QStringList args{
        QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
        QStringLiteral("-i"), localPath,
        QStringLiteral("-map"), QStringLiteral("0:v:0"),
        QStringLiteral("-frames:v"), QStringLiteral("1"),
        QStringLiteral("-f"), QStringLiteral("image2pipe"),
        QStringLiteral("-vcodec"), QStringLiteral("png"),
        QStringLiteral("pipe:1")
    };
    m_artProcess->start(m_media->ffmpegExecutable(), args);
}

void MediaWindow::setAlbumArt(const QByteArray &data)
{
    QPixmap pixmap;
    if (!pixmap.loadFromData(data)) {
        clearAlbumArt();
        return;
    }
    m_albumArtBytes = data;
    m_albumArt->setText(QString());
    m_albumArt->setPixmap(pixmap.scaled(m_albumArt->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    if (m_cast->active()) m_cast->setCoverArt(m_albumArtBytes, QStringLiteral("image/png"));
}

void MediaWindow::clearAlbumArt(const QString &message)
{
    m_albumArtBytes.clear();
    if (!m_albumArt) return;
    m_albumArt->setPixmap(QPixmap());
    m_albumArt->setText(message);
    if (m_cast && m_cast->active()) m_cast->clearCoverArt();
}

void MediaWindow::fetchWaffleCastMetadata()
{
    if (!m_castListening || m_castMetadataUrl.isEmpty() || m_metadataReply) return;
    QNetworkRequest request(m_castMetadataUrl);
    request.setRawHeader("Cache-Control", "no-cache");
    m_metadataReply = m_network->get(request);
    connect(m_metadataReply, &QNetworkReply::finished, this, [this] {
        QNetworkReply *reply = m_metadataReply;
        m_metadataReply = nullptr;
        if (!reply) return;
        const QByteArray body = reply->readAll();
        const bool ok = reply->error() == QNetworkReply::NoError;
        reply->deleteLater();
        if (!ok || !m_castListening) return;

        QJsonParseError error{};
        const QJsonDocument document = QJsonDocument::fromJson(body, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) return;
        const QJsonObject object = document.object();
        const QString title = object.value(QStringLiteral("title")).toString().trimmed();
        const qint64 trackGeneration = object.value(QStringLiteral("track_generation")).toInteger(-1);
        const qint64 coverGeneration = object.value(QStringLiteral("cover_generation")).toInteger(-1);
        if (!title.isEmpty()) {
            m_castRemoteTitle = title;
            m_title->setText(title);
        }
        if (trackGeneration != m_castTrackGeneration) {
            m_castTrackGeneration = trackGeneration;
        }
        if (coverGeneration != m_castCoverGeneration) {
            m_castCoverGeneration = coverGeneration;
            fetchWaffleCastCover();
        }
        const bool paused = object.value(QStringLiteral("paused")).toBool(false);
        const int listeners = object.value(QStringLiteral("listeners")).toInt(0);
        m_castStatus->setText(QStringLiteral("Listening to WaffleCast%1 — %2 listener%3%4")
            .arg(m_castRemoteHost.isEmpty() ? QString() : QStringLiteral(" from %1").arg(m_castRemoteHost))
            .arg(listeners)
            .arg(listeners == 1 ? QString() : QStringLiteral("s"))
            .arg(paused ? QStringLiteral(" — DJ paused") : QString()));
    });
}

void MediaWindow::fetchWaffleCastCover()
{
    if (!m_castListening || m_castCoverUrl.isEmpty() || m_coverReply) return;
    QUrl url = m_castCoverUrl;
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("g"), QString::number(m_castCoverGeneration));
    url.setQuery(query);
    QNetworkRequest request(url);
    request.setRawHeader("Cache-Control", "no-cache");
    m_coverReply = m_network->get(request);
    connect(m_coverReply, &QNetworkReply::finished, this, [this] {
        QNetworkReply *reply = m_coverReply;
        m_coverReply = nullptr;
        if (!reply) return;
        const QByteArray bytes = reply->readAll();
        const bool ok = reply->error() == QNetworkReply::NoError;
        reply->deleteLater();
        if (!m_castListening) return;
        if (ok && !bytes.isEmpty()) setAlbumArt(bytes);
        else clearAlbumArt(QStringLiteral("No embedded album art"));
    });
}

void MediaWindow::updateWaffleCastUi()
{
    if (!m_castStatus || !m_castStart || !m_castStop || !m_castCopyInvite) return;
    if (m_cast->active()) {
        const int listeners = m_cast->listenerCount();
        m_castStatus->setText(QStringLiteral("Broadcasting — %1 listener%2 — %3")
            .arg(listeners)
            .arg(listeners == 1 ? QString() : QStringLiteral("s"))
            .arg(m_cast->streamUrl().toString(QUrl::RemovePassword)));
        m_castStart->setEnabled(false);
        m_castStop->setEnabled(true);
        m_castStop->setText(QStringLiteral("End Broadcast"));
        m_castCopyInvite->setEnabled(true);
        m_castHint->setText(QStringLiteral(
            "Use /wafflecast in an AIM IM, IRC PM, or IRC channel. WaffleHouse clients recognize the invite and offer Listen."));
        return;
    }
    if (m_castListening) {
        m_castStatus->setText(m_castRemoteHost.isEmpty()
            ? QStringLiteral("Listening to WaffleCast")
            : QStringLiteral("Listening to WaffleCast from %1").arg(m_castRemoteHost));
        m_castStart->setEnabled(false);
        m_castStop->setEnabled(true);
        m_castStop->setText(QStringLiteral("Leave WaffleCast"));
        m_castCopyInvite->setEnabled(false);
        m_castHint->setText(QStringLiteral("The DJ controls the live stream; track title and embedded artwork update automatically."));
        return;
    }
    m_castStatus->setText(QStringLiteral("Broadcast off"));
    m_castStart->setEnabled(true);
    m_castStop->setEnabled(false);
    m_castStop->setText(QStringLiteral("End Broadcast"));
    m_castCopyInvite->setEnabled(false);
    m_castHint->setText(QStringLiteral(
        "Start a broadcast, then use /wafflecast in an AIM IM or IRC PM/channel to invite WaffleHouse listeners."));
}

void MediaWindow::showAndRaise()
{
    show();
    raise();
    activateWindow();
}

void MediaWindow::openMediaFiles()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this,
        QStringLiteral("Open media"),
        QStandardPaths::writableLocation(QStandardPaths::MusicLocation),
        QStringLiteral(
            "Media (*.mp3 *.flac *.ogg *.opus *.wav *.aac *.m4a *.wma "
            "*.avi *.mp4 *.m4v *.mkv *.mov *.webm *.mpeg *.mpg *.ts *.m2ts *.wmv *.flv);;"
            "Audio (*.mp3 *.flac *.ogg *.opus *.wav *.aac *.m4a *.wma);;"
            "Video (*.avi *.mp4 *.m4v *.mkv *.mov *.webm *.mpeg *.mpg *.ts *.m2ts *.wmv *.flv);;"
            "All files (*)"));
    addSources(files, true);
}

void MediaWindow::openStreamDialog()
{
    bool ok = false;
    const QString url = QInputDialog::getText(
        this, QStringLiteral("Open internet stream"),
        QStringLiteral("SHOUTcast / Icecast / HTTP(S) / HLS media URL:"),
        QLineEdit::Normal, {}, &ok).trimmed();
    if (!ok || url.isEmpty()) return;
    m_media->play(url);
}

void MediaWindow::searchShoutcastDirectory()
{
    bool ok = false;
    const QString query = QInputDialog::getText(
        this, QStringLiteral("Search SHOUTcast Directory"),
        QStringLiteral("Station, artist, or genre:"),
        QLineEdit::Normal, {}, &ok).trimmed();
    if (!ok || query.isEmpty()) return;

    const QByteArray encoded = QByteArrayLiteral("https://directory.shoutcast.com/Search?query=")
        + QUrl::toPercentEncoding(query);
    const QUrl url = QUrl::fromEncoded(encoded);
    if (!QDesktopServices::openUrl(url)) {
        QMessageBox::information(this, QStringLiteral("SHOUTcast Directory"),
                                 QStringLiteral("Open this URL in a browser:\n%1").arg(url.toString()));
    } else {
        statusBar()->showMessage(QStringLiteral("Opened SHOUTcast directory search for: %1").arg(query), 5000);
    }
}

void MediaWindow::openInternetPlaylistDialog()
{
    bool ok = false;
    const QString url = QInputDialog::getText(
        this, QStringLiteral("Open internet playlist"),
        QStringLiteral("M3U / PLS / XSPF playlist URL (use Stream URL for HLS .m3u8 manifests):"),
        QLineEdit::Normal, {}, &ok).trimmed();
    if (!ok || url.isEmpty()) return;
    m_media->loadPlaylist(url, true);
}

void MediaWindow::openPlaylistDialog()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Import playlist"), {},
        QStringLiteral("Playlists (*.m3u *.m3u8 *.pls *.xspf);;All files (*)"));
    if (path.isEmpty()) return;
    m_media->loadPlaylist(path, true);
}

void MediaWindow::savePlaylistDialog()
{
    QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Save playlist"), {},
        QStringLiteral("M3U8 Playlist (*.m3u8)"));
    if (path.isEmpty()) return;
    if (!path.endsWith(QStringLiteral(".m3u8"), Qt::CaseInsensitive)) path += QStringLiteral(".m3u8");

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QStringLiteral("Playlist"), file.errorString());
        return;
    }

    QTextStream out(&file);
    out << "#EXTM3U\n";
    for (int i = 0; i < m_playlist->count(); ++i) {
        out << m_playlist->item(i)->data(Qt::UserRole).toString() << '\n';
    }
    statusBar()->showMessage(QStringLiteral("Saved playlist: %1").arg(path), 5000);
}

void MediaWindow::addSources(const QStringList &sources, bool playFirst)
{
    QStringList added;
    for (const QString &source : sources) {
        if (!source.trimmed().isEmpty()) added << source.trimmed();
    }
    if (added.isEmpty()) return;

    if (playFirst) {
        m_media->play(added.first());
        for (int i = 1; i < added.size(); ++i) {
            m_media->enqueue(added.at(i));
        }
    } else {
        for (const QString &source : added) {
            m_media->enqueue(source);
        }
    }
}

void MediaWindow::syncPlaylist(const QStringList &sources,
                               const QStringList &titles,
                               int currentIndex)
{
    if (m_syncingPlaylist) return;
    m_syncingPlaylist = true;
    const QSignalBlocker blocker(m_playlist);
    m_playlist->clear();
    for (int i = 0; i < sources.size(); ++i) {
        const QString title = i < titles.size() && !titles.at(i).isEmpty()
            ? titles.at(i) : sources.at(i);
        auto *item = new QListWidgetItem(title, m_playlist);
        item->setData(Qt::UserRole, sources.at(i));
        item->setToolTip(sources.at(i));
    }
    if (currentIndex >= 0 && currentIndex < m_playlist->count()) {
        m_playlist->setCurrentRow(currentIndex);
    }
    m_syncingPlaylist = false;
}

void MediaWindow::playSelected()
{
    const int row = m_playlist->currentRow();
    if (row < 0) return;
    m_media->playPlaylistIndex(row);
}

void MediaWindow::removeSelected()
{
    QList<int> rows;
    for (QListWidgetItem *item : m_playlist->selectedItems()) {
        rows << m_playlist->row(item);
    }
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    for (int row : rows) m_media->removePlaylistIndex(row);
}

void MediaWindow::clearPlaylist()
{
    m_media->clearPlaylist();
}

QString MediaWindow::timeText(double seconds) const
{
    const int total = qMax(0, qRound(seconds));
    const int hours = total / 3600;
    const int minutes = (total % 3600) / 60;
    const int secs = total % 60;
    if (hours > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(secs, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2")
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(secs, 2, 10, QLatin1Char('0'));
}

void MediaWindow::updateTime()
{
    m_time->setText(QStringLiteral("%1 / %2").arg(timeText(m_position), timeText(m_duration)));
}

void MediaWindow::closeEvent(QCloseEvent *event)
{
    hide();
    event->ignore();
}

void MediaWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void MediaWindow::dropEvent(QDropEvent *event)
{
    QStringList sources;
    for (const QUrl &url : event->mimeData()->urls()) {
        sources << (url.isLocalFile() ? url.toLocalFile() : url.toString());
    }
    addSources(sources, true);
    event->acceptProposedAction();
}
