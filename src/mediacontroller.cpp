#include "mediacontroller.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFileDevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QIODevice>
#include <QSocketNotifier>
#include <QThread>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>
#include <QUrl>
#include <QtGlobal>

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <signal.h>

namespace {
QString boolText(bool value)
{
    return value ? QStringLiteral("on") : QStringLiteral("off");
}

QString findExternalExecutable(const QString &name)
{
    const QString onPath = QStandardPaths::findExecutable(name);
    if (!onPath.isEmpty()) return onPath;

#ifdef Q_OS_MACOS
    // Finder-launched .app bundles normally do not inherit the user's shell PATH.
    // Check the conventional package-manager locations explicitly so an mpv/ffmpeg
    // installed by Homebrew, MacPorts, or Fink is still usable.
    const QStringList dirs = {
        QStringLiteral("/opt/homebrew/bin"),
        QStringLiteral("/usr/local/bin"),
        QStringLiteral("/opt/local/bin"),
        QStringLiteral("/sw/bin")
    };
    for (const QString &dir : dirs) {
        const QString candidate = QDir(dir).filePath(name);
        const QFileInfo info(candidate);
        if (info.exists() && info.isFile() && info.isExecutable()) return candidate;
    }

    if (name == QStringLiteral("mpv")) {
        const QString appBinary = QStringLiteral("/Applications/mpv.app/Contents/MacOS/mpv");
        const QFileInfo info(appBinary);
        if (info.exists() && info.isFile() && info.isExecutable()) return appBinary;
    }
#endif
    return {};
}
}

MediaController::MediaController(QObject *parent)
    : QObject(parent)
{
    m_remoteSocket = qEnvironmentVariable("WAFFLEHOUSE_REMOTE_MEDIA_SOCKET").trimmed();
    m_mpv = findExternalExecutable(QStringLiteral("mpv"));
    m_ffmpeg = findExternalExecutable(QStringLiteral("ffmpeg"));
    probeBackendVersion();

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &MediaController::drainBackendOutput);
    connect(m_process, &QProcess::readyReadStandardError,
            this, &MediaController::drainBackendOutput);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus) { processFinished(code); });

    m_remoteProcess = new QProcess(this);
    m_remoteProcess->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_remoteProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus) { remoteProcessFinished(code); });

    m_connectTimer = new QTimer(this);
    m_connectTimer->setInterval(100);
    connect(m_connectTimer, &QTimer::timeout, this, &MediaController::connectIpc);

    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setInterval(1000);
    connect(m_refreshTimer, &QTimer::timeout, this, &MediaController::refreshObservedProperties);

    m_remoteTimer = new QTimer(this);
    m_remoteTimer->setInterval(250);
    connect(m_remoteTimer, &QTimer::timeout, this, &MediaController::refreshRemotePosition);
    m_remoteClock = new QElapsedTimer();

    // The Media Center owns a persistent queue/library independent of mpv.
    // Load it without starting the backend so merely launching WaffleHouse never
    // begins playback. GUI and CLI MediaController instances use the same file.
    loadPersistentLibrary();
}

MediaController::~MediaController()
{
    shutdown();
    delete m_remoteClock;
    m_remoteClock = nullptr;
}

QString MediaController::mediaDataDirectory() const
{
    QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (root.isEmpty()) {
        root = QDir::home().filePath(QStringLiteral(".local/share/WaffleHouse-Client"));
    }
    const QString media = QDir(root).filePath(QStringLiteral("media"));
    QDir().mkpath(QDir(media).filePath(QStringLiteral("playlists")));
    return media;
}

QString MediaController::persistentLibraryFile() const
{
    return QDir(mediaDataDirectory()).filePath(QStringLiteral("library.json"));
}

QString MediaController::persistentQueueFile() const
{
    return QDir(mediaDataDirectory()).filePath(QStringLiteral("queue.m3u8"));
}

QString MediaController::mediaLibraryPath() const
{
    return persistentLibraryFile();
}

QString MediaController::originForSource(const QString &source) const
{
    for (int i = 0; i < m_playlistSources.size(); ++i) {
        if (m_playlistSources.at(i) == source && i < m_playlistOrigins.size())
            return m_playlistOrigins.at(i);
    }
    return {};
}

void MediaController::loadPersistentLibrary()
{
    m_loadingPersistentLibrary = true;
    QFile file(persistentLibraryFile());
    if (!file.open(QIODevice::ReadOnly)) {
        m_loadingPersistentLibrary = false;
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        m_loadingPersistentLibrary = false;
        return;
    }

    const QJsonObject root = document.object();
    const QJsonArray entries = root.value(QStringLiteral("playlist")).toArray();
    for (const QJsonValue &value : entries) {
        if (!value.isObject()) continue;
        const QJsonObject item = value.toObject();
        const QString source = item.value(QStringLiteral("source")).toString().trimmed();
        if (source.isEmpty()) continue;
        QString title = item.value(QStringLiteral("title")).toString().trimmed();
        if (title.isEmpty()) {
            title = QFileInfo(source).fileName();
            if (title.isEmpty()) title = source;
        }
        m_playlistSources << source;
        m_playlistTitles << title;
        m_playlistOrigins << item.value(QStringLiteral("origin")).toString();
    }

    const QJsonArray imports = root.value(QStringLiteral("imported_playlists")).toArray();
    for (const QJsonValue &value : imports) {
        if (!value.isObject()) continue;
        const QJsonObject item = value.toObject();
        const QString source = item.value(QStringLiteral("source")).toString().trimmed();
        if (source.isEmpty()) continue;
        m_importedPlaylistSources << source;
        m_importedPlaylistCaches << item.value(QStringLiteral("cached_copy")).toString();
    }

    m_playlistIndex = root.value(QStringLiteral("current_index")).toInt(-1);
    if (m_playlistSources.isEmpty()) m_playlistIndex = -1;
    else m_playlistIndex = qBound(0, m_playlistIndex < 0 ? 0 : m_playlistIndex,
                                  m_playlistSources.size() - 1);
    m_volume = qBound(0, root.value(QStringLiteral("volume")).toInt(m_volume), 150);
    m_muted = root.value(QStringLiteral("muted")).toBool(m_muted);
    m_shuffle = root.value(QStringLiteral("shuffle")).toBool(m_shuffle);
    const QString repeat = root.value(QStringLiteral("repeat")).toString(m_repeatMode);
    if (repeat == QStringLiteral("off") || repeat == QStringLiteral("one") || repeat == QStringLiteral("all"))
        m_repeatMode = repeat;

    // A freshly launched mpv process has no queue yet. It is hydrated lazily
    // only when the user actually asks to play/resume/enqueue something.
    m_backendPlaylistHydrated = false;
    m_loadingPersistentLibrary = false;
}

void MediaController::savePersistentLibrary()
{
    if (m_loadingPersistentLibrary) return;
    const QString directory = mediaDataDirectory();
    if (directory.isEmpty()) return;

    QJsonArray entries;
    for (int i = 0; i < m_playlistSources.size(); ++i) {
        const QString source = m_playlistSources.at(i).trimmed();
        if (source.isEmpty()) continue;
        QJsonObject item;
        item.insert(QStringLiteral("source"), source);
        const QString title = i < m_playlistTitles.size() ? m_playlistTitles.at(i) : QString();
        item.insert(QStringLiteral("title"), title);
        const QString origin = i < m_playlistOrigins.size() ? m_playlistOrigins.at(i) : QString();
        if (!origin.isEmpty()) item.insert(QStringLiteral("origin"), origin);
        entries.append(item);
    }

    QJsonArray imports;
    for (int i = 0; i < m_importedPlaylistSources.size(); ++i) {
        QJsonObject item;
        item.insert(QStringLiteral("source"), m_importedPlaylistSources.at(i));
        if (i < m_importedPlaylistCaches.size() && !m_importedPlaylistCaches.at(i).isEmpty())
            item.insert(QStringLiteral("cached_copy"), m_importedPlaylistCaches.at(i));
        imports.append(item);
    }

    QJsonObject root;
    root.insert(QStringLiteral("schema"), 1);
    root.insert(QStringLiteral("saved_at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    root.insert(QStringLiteral("playlist"), entries);
    root.insert(QStringLiteral("current_index"), m_playlistIndex);
    root.insert(QStringLiteral("volume"), m_volume);
    root.insert(QStringLiteral("muted"), m_muted);
    root.insert(QStringLiteral("shuffle"), m_shuffle);
    root.insert(QStringLiteral("repeat"), m_repeatMode);
    root.insert(QStringLiteral("imported_playlists"), imports);

    QSaveFile library(persistentLibraryFile());
    if (library.open(QIODevice::WriteOnly)) {
        library.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        (void)library.commit();
    }

    // Keep an ordinary M3U8 mirror as well. Besides being easy for a user to
    // inspect/export, this lets a new mpv process rehydrate the exact saved queue
    // in one loadlist command without depending on the original .pls/.m3u file.
    QSaveFile queue(persistentQueueFile());
    if (queue.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QByteArray out("#EXTM3U\n");
        for (int i = 0; i < m_playlistSources.size(); ++i) {
            QString source = m_playlistSources.at(i);
            source.replace(QLatin1Char('\r'), QLatin1Char(' '));
            source.replace(QLatin1Char('\n'), QLatin1Char(' '));
            QString title = i < m_playlistTitles.size() ? m_playlistTitles.at(i) : QString();
            title.replace(QLatin1Char('\r'), QLatin1Char(' '));
            title.replace(QLatin1Char('\n'), QLatin1Char(' '));
            if (!title.isEmpty()) out += QByteArray("#EXTINF:-1,") + title.toUtf8() + '\n';
            out += source.toUtf8() + '\n';
        }
        queue.write(out);
        (void)queue.commit();
    }
}

QString MediaController::cachePlaylistDefinition(const QString &pathOrUrl)
{
    const QString clean = pathOrUrl.trimmed();
    if (clean.isEmpty()) return {};

    QString cached;
    const QUrl maybeUrl(clean);
    const bool isRemote = maybeUrl.isValid() && !maybeUrl.scheme().isEmpty()
                          && maybeUrl.scheme().compare(QStringLiteral("file"), Qt::CaseInsensitive) != 0;
    if (!isRemote) {
        const QString local = maybeUrl.isLocalFile() ? maybeUrl.toLocalFile() : clean;
        QFile input(local);
        const QString suffix = QFileInfo(local).suffix().toCaseFolded();
        static const QStringList playlistTypes = {
            QStringLiteral("pls"), QStringLiteral("m3u"), QStringLiteral("m3u8"), QStringLiteral("xspf")
        };
        if (playlistTypes.contains(suffix) && input.open(QIODevice::ReadOnly)) {
            const QByteArray bytes = input.readAll();
            const QString digest = QString::fromLatin1(
                QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().left(16));
            QString basename = QFileInfo(local).fileName();
            basename.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]+")), QStringLiteral("_"));
            if (basename.isEmpty()) basename = QStringLiteral("playlist.%1").arg(suffix);
            cached = QDir(mediaDataDirectory()).filePath(
                QStringLiteral("playlists/%1-%2").arg(digest, basename));
            QSaveFile output(cached);
            if (output.open(QIODevice::WriteOnly)) {
                output.write(bytes);
                if (!output.commit()) cached.clear();
            } else {
                cached.clear();
            }
        }
    }

    const int existing = m_importedPlaylistSources.indexOf(clean);
    if (existing >= 0) {
        while (m_importedPlaylistCaches.size() <= existing) m_importedPlaylistCaches << QString();
        if (!cached.isEmpty()) m_importedPlaylistCaches[existing] = cached;
    } else {
        m_importedPlaylistSources << clean;
        m_importedPlaylistCaches << cached;
    }
    savePersistentLibrary();
    return cached;
}

bool MediaController::hydrateBackendPlaylist(int startIndex)
{
    if (remoteAudioMode()) return true;
    if (!ensureBackend()) return false;
    if (m_playlistSources.isEmpty()) {
        m_backendPlaylistHydrated = true;
        return true;
    }

    // WaffleHouse owns the persistent queue. Rebuild mpv directly from those
    // sources rather than trying to resurrect mpv's stopped playlist state or
    // re-importing the queue through loadlist. Direct loadfile commands give the
    // transport a clean, deterministic Stop -> Play path while the application
    // remains authoritative for library order and the selected index.
    const QStringList savedSources = m_playlistSources;
    const QStringList savedTitles = m_playlistTitles;
    const QStringList savedOrigins = m_playlistOrigins;
    const int requestedIndex = startIndex >= 0
        ? qBound(0, startIndex, savedSources.size() - 1)
        : qBound(0, m_playlistIndex < 0 ? 0 : m_playlistIndex, savedSources.size() - 1);

    if (!m_backendPlaylistHydrated) {
        // Ignore mpv's intermediate one-item/two-item playlist snapshots while
        // reconstructing the queue. The application copy remains authoritative.
        m_preserveLibraryWhileStopped = true;

        if (!sendLoadFile(savedSources.first(), QStringLiteral("replace"))) {
            return false;
        }
        for (int i = 1; i < savedSources.size(); ++i) {
            if (!sendLoadFile(savedSources.at(i), QStringLiteral("append"))) {
                return false;
            }
        }

        // Property notifications are asynchronous. Restore the application-owned
        // snapshot immediately so no transient backend event can shrink the GUI
        // queue or the persisted library during this call.
        m_playlistSources = savedSources;
        m_playlistTitles = savedTitles;
        m_playlistOrigins = savedOrigins;
        m_backendPlaylistHydrated = true;
    }

    m_playlistIndex = requestedIndex;
    savePersistentLibrary();

    // loadfile(..., replace) already starts index 0. For a later saved index,
    // select it with the long-standing writable playlist-pos property using a
    // native JSON number. This avoids making Stop -> Play depend on a separate
    // restart command after the queue has just been rebuilt.
    bool ok = true;
    if (requestedIndex > 0) {
        QJsonArray select;
        select.append(QStringLiteral("set_property"));
        select.append(QStringLiteral("playlist-pos"));
        select.append(requestedIndex);
        ok = sendJsonCommand(select, QStringLiteral("select restored playlist index"));
    }
    if (ok) {
        m_idle = false;
        m_paused = false;
        emit idleChanged(false);
        emit pauseChanged(false);
        emit statusMessage(QStringLiteral("Playback restarted from the preserved media library."));
    }
    return ok;
}

bool MediaController::backendAvailable() const
{
    return remoteAudioMode() ? !m_ffmpeg.isEmpty() : !m_mpv.isEmpty();
}

QString MediaController::backendExecutable() const
{
    return remoteAudioMode() ? m_ffmpeg : m_mpv;
}

void MediaController::probeBackendVersion()
{
    if (m_mpv.isEmpty()) return;

    QProcess probe;
    probe.setProcessChannelMode(QProcess::MergedChannels);
    probe.start(m_mpv, {QStringLiteral("--version")});
    if (!probe.waitForStarted(700) || !probe.waitForFinished(1200)) {
        probe.kill();
        probe.waitForFinished(200);
        return;
    }

    const QString output = QString::fromLocal8Bit(probe.readAll()).trimmed();
    const QRegularExpression rx(QStringLiteral("\\bmpv\\s+(\\d+)\\.(\\d+)(?:\\.(\\d+))?"),
                                QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = rx.match(output);
    if (!match.hasMatch()) return;

    const int major = match.captured(1).toInt();
    const int minor = match.captured(2).toInt();
    const QString patch = match.captured(3).isEmpty() ? QStringLiteral("0") : match.captured(3);
    m_backendVersion = QStringLiteral("%1.%2.%3").arg(major).arg(minor).arg(patch);
}

QString MediaController::statusText() const
{
    const QString title = m_title.isEmpty()
        ? (m_source.isEmpty() ? QStringLiteral("Nothing loaded") : m_source)
        : m_title;
    const QString state = m_idle ? QStringLiteral("idle")
                                 : (m_paused ? QStringLiteral("paused") : QStringLiteral("playing"));
    return QStringLiteral("%1 | %2 | %3% | mute %4")
        .arg(title, state)
        .arg(m_volume)
        .arg(boolText(m_muted));
}

QStringList MediaController::statusLines() const
{
    QString backend;
    if (remoteAudioMode()) {
        backend = backendAvailable()
            ? QStringLiteral("SSH companion via %1 (PCM16 48 kHz stereo)").arg(m_ffmpeg)
            : QStringLiteral("SSH companion requested; ffmpeg not found");
    } else {
        backend = backendAvailable() ? m_mpv : QStringLiteral("mpv not found");
        if (!m_backendVersion.isEmpty()) backend += QStringLiteral(" (mpv %1)").arg(m_backendVersion);
    }

    QStringList out;
    out << QStringLiteral("Backend: %1").arg(backend)
        << QStringLiteral("Now playing: %1")
               .arg(m_title.isEmpty() ? QStringLiteral("<nothing>") : m_title)
        << QStringLiteral("Source: %1")
               .arg(m_source.isEmpty() ? QStringLiteral("<none>") : m_source)
        << QStringLiteral("Position: %1 / %2 sec")
               .arg(m_position, 0, 'f', 1)
               .arg(m_duration, 0, 'f', 1)
        << QStringLiteral("State: %1 | volume %2% | mute %3")
               .arg(m_idle ? QStringLiteral("idle")
                           : (m_paused ? QStringLiteral("paused") : QStringLiteral("playing")))
               .arg(m_volume)
               .arg(boolText(m_muted))
        << QStringLiteral("Playlist entries: %1 | current index: %2")
               .arg(m_playlistSources.size()).arg(m_playlistIndex)
        << QStringLiteral("Shuffle: %1 | repeat: %2")
               .arg(boolText(m_shuffle), m_repeatMode);
    return out;
}

QString MediaController::buildIpcPath()
{
    QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (runtime.isEmpty()) runtime = QDir::tempPath();

    // Use an unpredictable per-process directory rather than a shared predictable
    // /tmp/wafflehouse-client path. This matters on FreeBSD/Unix desktops where
    // Qt may not expose RuntimeLocation: another local user must not be able to
    // pre-create or observe the mpv IPC socket directory.
    const QString token = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    // Keep this deliberately short. sockaddr_un.sun_path is only about 104-108
    // bytes on common FreeBSD/Linux targets, and a long XDG runtime path can
    // otherwise make mpv fail to bind the socket even though QProcess started.
    m_ipcDir = QDir(runtime).filePath(
        QStringLiteral("wr-%1-%2")
            .arg(QCoreApplication::applicationPid()).arg(token));

    if (!QDir().mkpath(m_ipcDir)
        || !QFile::setPermissions(m_ipcDir,
                                  QFileDevice::ReadOwner
                                      | QFileDevice::WriteOwner
                                      | QFileDevice::ExeOwner)) {
        QDir(m_ipcDir).removeRecursively();
        m_ipcDir.clear();
        return {};
    }
    return QDir(m_ipcDir).filePath(QStringLiteral("m.sock"));
}

void MediaController::cleanupIpcPath()
{
    if (!m_ipcPath.isEmpty()) QFile::remove(m_ipcPath);
    if (!m_ipcDir.isEmpty()) QDir(m_ipcDir).removeRecursively();
    m_ipcPath.clear();
    m_ipcDir.clear();
}

bool MediaController::ensureBackend()
{
    if (remoteAudioMode()) {
        if (m_ffmpeg.isEmpty()) {
            emit errorMessage(QStringLiteral(
                "SSH remote audio is active, but ffmpeg was not found on the WaffleHouse server."));
            return false;
        }
        return true;
    }
    if (!backendAvailable()) {
        emit errorMessage(QStringLiteral(
            "mpv was not found. Install mpv and retry."));
        return false;
    }

    if (m_process->state() != QProcess::NotRunning && ipcConnected()) {
        return true;
    }

    // A half-started backend is not useful. Tear it down before a clean retry.
    if (m_process->state() != QProcess::NotRunning) stopFailedBackend();

    QString normalFailure;
    if (startBackendProcess(false, &normalFailure)) return true;

    // Some long-term-support distributions and older FreeBSD packages carry an
    // mpv build that understands JSON IPC but not every newer command-line
    // hardening option. Retry with the small, long-established option set before
    // declaring media unavailable. The actual media commands remain identical.
    QString compatibilityFailure;
    if (startBackendProcess(true, &compatibilityFailure)) {
        emit statusMessage(QStringLiteral(
            "Media engine ready in mpv compatibility mode (%1).")
                               .arg(m_backendVersion.isEmpty()
                                        ? QStringLiteral("version unavailable")
                                        : QStringLiteral("mpv %1").arg(m_backendVersion)));
        return true;
    }

    QStringList details;
    if (!normalFailure.isEmpty())
        details << QStringLiteral("normal startup: %1").arg(normalFailure);
    if (!compatibilityFailure.isEmpty())
        details << QStringLiteral("compatibility retry: %1").arg(compatibilityFailure);
    emit errorMessage(QStringLiteral("mpv started, but its local control socket did not become ready.%1")
                          .arg(details.isEmpty()
                                   ? QString()
                                   : QStringLiteral(" %1").arg(details.join(QStringLiteral(" | ")))));
    return false;
}

bool MediaController::startBackendProcess(bool compatibilityMode, QString *failureDetail)
{
    cleanupIpcPath();
    m_ipcPath = buildIpcPath();
    if (m_ipcPath.isEmpty()) {
        if (failureDetail)
            *failureDetail = QStringLiteral("unable to create a private runtime directory");
        return false;
    }

    m_lastBackendError.clear();
    m_lastSocketError.clear();
    m_ipcBuffer.clear();
    m_shuttingDown = false;

    QStringList args = {
        QStringLiteral("--no-config"),
        QStringLiteral("--ytdl=no"),
        QStringLiteral("--idle=yes"),
        QStringLiteral("--no-terminal"),
        QStringLiteral("--force-window=no"),
        QStringLiteral("--input-ipc-server=%1").arg(m_ipcPath),
        QStringLiteral("--volume=%1").arg(m_volume)
    };
    if (!compatibilityMode) {
        // These are quality-of-life/safety options, not requirements for the IPC
        // protocol. If a packaged mpv does not know one, ensureBackend() retries
        // without them instead of disabling all Media Center playback.
        args.insert(3, QStringLiteral("--input-terminal=no"));
        args.insert(4, QStringLiteral("--load-unsafe-playlists=no"));
        args.insert(5, QStringLiteral("--really-quiet"));
        args.insert(6, QStringLiteral("--audio-display=no"));
        args.insert(7, QStringLiteral("--keep-open=no"));
        args.insert(8, QStringLiteral("--save-position-on-quit=no"));
    }

    m_process->start(m_mpv, args);
    if (!m_process->waitForStarted(2500)) {
        if (failureDetail)
            *failureDetail = QStringLiteral("could not start mpv: %1").arg(m_process->errorString());
        cleanupIpcPath();
        return false;
    }
    m_connectTimer->start();

    // Connecting is the authoritative readiness test. Do not gate this on
    // QFileInfo::exists(): Unix-domain socket nodes can race that test and have
    // platform-specific stat behavior. Slow machines get up to five seconds.
    for (int i = 0; i < 50 && !ipcConnected(); ++i) {
        if (m_process->state() == QProcess::NotRunning) {
            drainBackendOutput();
            break;
        }
        connectIpc();
        if (ipcConnected()) break;
        drainBackendOutput();
        QThread::msleep(100);
    }

    if (ipcConnected()) return true;

    drainBackendOutput();
    QStringList details;
    if (!m_lastSocketError.isEmpty())
        details << QStringLiteral("socket: %1").arg(m_lastSocketError);
    if (!m_lastBackendError.isEmpty())
        details << QStringLiteral("mpv: %1").arg(m_lastBackendError);
    if (details.isEmpty())
        details << QStringLiteral("socket path %1 never accepted a connection").arg(m_ipcPath);
    if (failureDetail) *failureDetail = details.join(QStringLiteral("; "));

    stopFailedBackend();
    return false;
}

void MediaController::stopFailedBackend()
{
    m_connectTimer->stop();
    closeIpcSocket();
    m_shuttingDown = true;
    if (m_process->state() != QProcess::NotRunning) {
        m_process->terminate();
        if (!m_process->waitForFinished(750)) {
            m_process->kill();
            m_process->waitForFinished(300);
        }
    }
    m_shuttingDown = false;
    cleanupIpcPath();
}

void MediaController::connectIpc()
{
    if (ipcConnected()) {
        m_connectTimer->stop();
        return;
    }
    if (m_ipcPath.isEmpty()) return;

    QString error;
    if (!connectNativeIpc(&error)) {
        if (!error.isEmpty()) m_lastSocketError = error;
        return;
    }

    m_connectTimer->stop();
    m_lastSocketError.clear();
    m_observing = false;
    observeProperties();
    setVolume(m_volume);
    setMuted(m_muted);
    emit readyChanged(true);
    emit statusMessage(QStringLiteral("Media engine ready."));
}

bool MediaController::connectNativeIpc(QString *error)
{
    if (ipcConnected()) return true;
    if (m_ipcPath.isEmpty()) {
        if (error) *error = QStringLiteral("IPC socket path is empty");
        return false;
    }

    const QByteArray encodedPath = QFile::encodeName(m_ipcPath);
    sockaddr_un address{};
    if (encodedPath.isEmpty() || encodedPath.size() >= static_cast<int>(sizeof(address.sun_path))) {
        if (error) {
            *error = QStringLiteral("IPC socket path is invalid or too long (%1 bytes): %2")
                         .arg(encodedPath.size()).arg(m_ipcPath);
        }
        return false;
    }

    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        if (error) *error = QStringLiteral("socket(AF_UNIX) failed: %1").arg(QString::fromLocal8Bit(std::strerror(errno)));
        return false;
    }
    // Never leak the controller socket into unrelated child processes.
    const int fdFlags = ::fcntl(fd, F_GETFD);
    if (fdFlags >= 0) (void)::fcntl(fd, F_SETFD, fdFlags | FD_CLOEXEC);

#if defined(Q_OS_FREEBSD)
    int one = 1;
    (void)::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif

    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, encodedPath.constData(), static_cast<size_t>(encodedPath.size()));
    address.sun_path[encodedPath.size()] = '\0';
    const socklen_t addressLength = static_cast<socklen_t>(
        offsetof(sockaddr_un, sun_path) + static_cast<size_t>(encodedPath.size()) + 1U);
#if defined(Q_OS_FREEBSD)
    address.sun_len = static_cast<unsigned char>(addressLength);
#endif

    if (::connect(fd, reinterpret_cast<sockaddr *>(&address), addressLength) != 0) {
        const int savedErrno = errno;
        ::close(fd);
        if (error) {
            *error = QStringLiteral("connect(%1) failed: %2")
                         .arg(m_ipcPath, QString::fromLocal8Bit(std::strerror(savedErrno)));
        }
        return false;
    }

    m_ipcFd = fd;
    m_ipcNotifier = new QSocketNotifier(static_cast<qintptr>(m_ipcFd), QSocketNotifier::Read, this);
    connect(m_ipcNotifier, &QSocketNotifier::activated, this,
            [this](QSocketDescriptor, QSocketNotifier::Type) { readIpc(); });
    return true;
}

void MediaController::closeIpcSocket()
{
    if (m_ipcNotifier) {
        m_ipcNotifier->setEnabled(false);
        m_ipcNotifier->deleteLater();
        m_ipcNotifier = nullptr;
    }
    if (m_ipcFd >= 0) {
        ::close(m_ipcFd);
        m_ipcFd = -1;
    }
    m_observing = false;
}

bool MediaController::writeIpc(const QByteArray &wire, QString *error)
{
    if (!ipcConnected()) {
        if (error) *error = QStringLiteral("media IPC socket is not connected");
        return false;
    }

    qsizetype offset = 0;
    while (offset < wire.size()) {
#if defined(Q_OS_LINUX)
        const ssize_t written = ::send(m_ipcFd, wire.constData() + offset,
                                       static_cast<size_t>(wire.size() - offset), MSG_NOSIGNAL);
#else
        const ssize_t written = ::send(m_ipcFd, wire.constData() + offset,
                                       static_cast<size_t>(wire.size() - offset), 0);
#endif
        if (written > 0) {
            offset += static_cast<qsizetype>(written);
            continue;
        }
        if (written < 0 && errno == EINTR) continue;
        if (error) {
            *error = QStringLiteral("IPC write failed: %1")
                         .arg(QString::fromLocal8Bit(std::strerror(errno)));
        }
        closeIpcSocket();
        return false;
    }
    return true;
}

bool MediaController::sendJsonCommand(const QJsonArray &command, const QString &description)
{
    if (!ensureBackend()) return false;
    const qint64 requestId = m_nextRequestId++;
    QJsonObject object;
    object.insert(QStringLiteral("command"), command);
    object.insert(QStringLiteral("request_id"), static_cast<double>(requestId));
    const QByteArray wire = QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
    QString ipcError;
    if (!writeIpc(wire, &ipcError)) {
        emit errorMessage(QStringLiteral("Could not send media command to mpv: %1").arg(ipcError));
        return false;
    }
    m_pendingRequests.insert(requestId,
        description.isEmpty() ? (command.isEmpty() ? QStringLiteral("command") : command.at(0).toString()) : description);
    return true;
}

bool MediaController::sendCommand(const QStringList &parts, const QString &description)
{
    QJsonArray command;
    for (const QString &part : parts) command.append(part);
    return sendJsonCommand(command, description);
}

bool MediaController::setProperty(const QString &name, const QJsonValue &value)
{
    QJsonArray command;
    command.append(QStringLiteral("set_property"));
    command.append(name);
    command.append(value);
    return sendJsonCommand(command, QStringLiteral("set %1").arg(name));
}

bool MediaController::sendLoadFile(const QString &source, const QString &flags)
{
    const QString wireSource = source.trimmed();
    if (wireSource.isEmpty()) return false;
    return sendCommand({QStringLiteral("loadfile"), wireSource, flags},
                       QStringLiteral("load media"));
}

bool MediaController::looksLikeVideoFile(const QString &source)
{
    if (source.contains(QStringLiteral("://"))) return false;
    static const QStringList extensions = {
        QStringLiteral("avi"), QStringLiteral("mp4"), QStringLiteral("m4v"),
        QStringLiteral("mkv"), QStringLiteral("mov"), QStringLiteral("webm"),
        QStringLiteral("mpeg"), QStringLiteral("mpg"), QStringLiteral("ts"),
        QStringLiteral("m2ts"), QStringLiteral("wmv"), QStringLiteral("flv")
    };
    return extensions.contains(QFileInfo(source).suffix().toCaseFolded());
}



QString MediaController::remoteAudioFilter() const
{
    static const int frequencies[10] = {60,170,310,600,1000,3000,6000,12000,14000,16000};
    QStringList filters;
    const double linear = m_muted ? 0.0 : static_cast<double>(m_volume) / 100.0;
    filters << QStringLiteral("volume=%1").arg(linear, 0, 'f', 3);
    for (int i = 0; i < m_eq.size(); ++i) {
        if (qAbs(m_eq[i]) < 0.05) continue;
        filters << QStringLiteral("equalizer=f=%1:t=q:w=1:g=%2")
                       .arg(frequencies[i]).arg(m_eq[i], 0, 'f', 1);
    }
    return filters.join(QLatin1Char(','));
}

bool MediaController::startRemotePlayback(const QString &source, double startSeconds)
{
    if (!remoteAudioMode() || !ensureBackend()) return false;
    const QString clean = source.trimmed();
    if (clean.isEmpty()) return false;
    if (m_remoteProcess->state() != QProcess::NotRunning) {
        m_remoteStoppedByUser = true;
        m_remoteProcess->terminate();
        if (!m_remoteProcess->waitForFinished(500)) {
            m_remoteProcess->kill();
            m_remoteProcess->waitForFinished(250);
        }
    }

    QStringList args = {
        QStringLiteral("-nostdin"),
        QStringLiteral("-hide_banner"),
        QStringLiteral("-loglevel"), QStringLiteral("warning"),
        QStringLiteral("-re")
    };
    if (startSeconds > 0.01) {
        args << QStringLiteral("-ss") << QString::number(startSeconds, 'f', 3);
    }
    args << QStringLiteral("-i") << clean
         << QStringLiteral("-vn")
         << QStringLiteral("-af") << remoteAudioFilter()
         << QStringLiteral("-c:a") << QStringLiteral("pcm_s16le")
         << QStringLiteral("-f") << QStringLiteral("s16le")
         << QStringLiteral("-ar") << QStringLiteral("48000")
         << QStringLiteral("-ac") << QStringLiteral("2")
         << QStringLiteral("unix://%1").arg(m_remoteSocket);

    m_remoteStoppedByUser = false;
    m_remoteProcess->start(m_ffmpeg, args);
    if (!m_remoteProcess->waitForStarted(1500)) {
        emit errorMessage(QStringLiteral("Could not start SSH remote media encoder: %1")
                              .arg(m_remoteProcess->errorString()));
        return false;
    }

    m_source = clean;
    m_title = QFileInfo(clean).fileName();
    if (m_title.isEmpty()) m_title = clean;
    m_remoteBasePosition = qMax(0.0, startSeconds);
    m_position = m_remoteBasePosition;
    m_duration = 0.0;
    m_paused = false;
    m_idle = false;
    m_remoteClock->restart();
    m_remoteTimer->start();
    emit idleChanged(false);
    emit pauseChanged(false);
    emit sourceChanged(m_source);
    emit nowPlayingChanged(m_title);
    emit positionChanged(m_position);
    emit statusMessage(QStringLiteral("Streaming to SSH companion: %1").arg(m_title));
    return true;
}

void MediaController::stopRemotePlayback(bool preserveQueue)
{
    Q_UNUSED(preserveQueue);
    if (!remoteAudioMode()) return;
    refreshRemotePosition();
    m_remoteStoppedByUser = true;
    m_remoteTimer->stop();
    if (m_remoteProcess->state() != QProcess::NotRunning) {
        m_remoteProcess->terminate();
        if (!m_remoteProcess->waitForFinished(500)) {
            m_remoteProcess->kill();
            m_remoteProcess->waitForFinished(250);
        }
    }
    m_paused = false;
    m_idle = true;
    emit pauseChanged(false);
    emit idleChanged(true);
}

void MediaController::restartRemoteAt(double seconds)
{
    if (!remoteAudioMode() || m_source.isEmpty()) return;
    startRemotePlayback(m_source, qMax(0.0, seconds));
}

void MediaController::refreshRemotePosition()
{
    if (!remoteAudioMode() || m_idle || !m_remoteClock->isValid()) return;
    if (!m_paused) {
        m_position = m_remoteBasePosition + static_cast<double>(m_remoteClock->elapsed()) / 1000.0;
        emit positionChanged(m_position);
    }
}

void MediaController::remoteProcessFinished(int exitCode)
{
    if (!remoteAudioMode()) return;
    refreshRemotePosition();
    m_remoteTimer->stop();
    if (m_remoteStoppedByUser) return;

    if (exitCode != 0) {
        const QString detail = QString::fromLocal8Bit(m_remoteProcess->readAll()).trimmed();
        emit errorMessage(QStringLiteral("SSH remote media stream ended unexpectedly%1")
                              .arg(detail.isEmpty() ? QString() : QStringLiteral(": %1").arg(detail.left(500))));
    }

    if (m_playlistSources.isEmpty()) {
        m_idle = true;
        emit idleChanged(true);
        return;
    }

    int nextIndex = m_playlistIndex;
    if (m_repeatMode == QStringLiteral("one")) {
        // keep current index
    } else {
        ++nextIndex;
        if (nextIndex >= m_playlistSources.size()) {
            if (m_repeatMode == QStringLiteral("all")) nextIndex = 0;
            else {
                m_idle = true;
                emit idleChanged(true);
                return;
            }
        }
    }
    m_playlistIndex = nextIndex;
    QTimer::singleShot(0, this, [this, nextIndex] {
        if (nextIndex >= 0 && nextIndex < m_playlistSources.size())
            startRemotePlayback(m_playlistSources.at(nextIndex), 0.0);
    });
}

bool MediaController::playTransient(const QString &source, const QString &displayTitle)
{
    const QString clean = source.trimmed();
    if (clean.isEmpty()) {
        emit errorMessage(QStringLiteral("No media source was supplied."));
        return false;
    }
    if (remoteAudioMode()) {
        emit errorMessage(QStringLiteral("WaffleCast listening is not available through SSH Companion audio mode."));
        return false;
    }
    if (!ensureBackend()) return false;

    // Transient streams (notably WaffleCast) must never replace the user's
    // persistent Media Center library. Ignore mpv playlist snapshots while the
    // transient source owns the backend; the visible saved queue remains intact.
    m_transientMode = true;
    m_preserveLibraryWhileStopped = false;
    m_backendPlaylistHydrated = false;
    if (!sendLoadFile(clean, QStringLiteral("replace"))) {
        m_transientMode = false;
        return false;
    }

    m_source = clean;
    m_title = displayTitle.trimmed();
    if (m_title.isEmpty()) {
        m_title = QFileInfo(clean).fileName();
        if (m_title.isEmpty()) m_title = clean;
    }
    m_position = 0.0;
    m_duration = 0.0;
    m_idle = false;
    m_paused = false;
    emit idleChanged(false);
    emit pauseChanged(false);
    emit sourceChanged(m_source);
    emit nowPlayingChanged(m_title);
    emit statusMessage(QStringLiteral("Listening to WaffleCast: %1").arg(m_title));
    return true;
}

bool MediaController::play(const QString &source)
{
    m_transientMode = false;
    const QString clean = source.trimmed();
    if (clean.isEmpty()) {
        emit errorMessage(QStringLiteral("No media source was supplied."));
        return false;
    }
    const QString title = [&] {
        QString value = QFileInfo(clean).fileName();
        return value.isEmpty() ? clean : value;
    }();
    if (remoteAudioMode()) {
        m_playlistSources = {clean};
        m_playlistTitles = {title};
        m_playlistOrigins = {QString()};
        m_playlistIndex = 0;
        savePersistentLibrary();
        emit playlistChanged();
        emit playlistEntriesChanged(m_playlistSources, m_playlistTitles, m_playlistIndex);
        return startRemotePlayback(clean, 0.0);
    }
    if (!ensureBackend()) return false;
    if (!sendLoadFile(clean, QStringLiteral("replace"))) return false;

    m_preserveLibraryWhileStopped = false;
    m_backendPlaylistHydrated = true;
    m_playlistSources = {clean};
    m_playlistTitles = {title};
    m_playlistOrigins = {QString()};
    m_playlistIndex = 0;
    savePersistentLibrary();
    emit playlistEntriesChanged(m_playlistSources, m_playlistTitles, m_playlistIndex);
    emit playlistChanged();

    m_source = clean;
    m_title = title;
    m_position = 0.0;
    m_duration = 0.0;
    m_idle = false;
    emit idleChanged(false);
    emit sourceChanged(m_source);
    emit nowPlayingChanged(m_title);
    emit statusMessage(looksLikeVideoFile(clean)
        ? QStringLiteral("Playing video in mpv's video window: %1").arg(m_title)
        : QStringLiteral("Playing: %1").arg(m_title));
    return true;
}

bool MediaController::enqueue(const QString &source)
{
    m_transientMode = false;
    const QString clean = source.trimmed();
    if (clean.isEmpty() || !ensureBackend()) return false;
    QString title = QFileInfo(clean).fileName();
    if (title.isEmpty()) title = clean;
    if (remoteAudioMode()) {
        m_playlistSources.append(clean);
        m_playlistTitles.append(title);
        m_playlistOrigins.append(QString());
        if (m_playlistIndex < 0) m_playlistIndex = 0;
        savePersistentLibrary();
        emit playlistChanged();
        emit playlistEntriesChanged(m_playlistSources, m_playlistTitles, m_playlistIndex);
        if (m_idle) return startRemotePlayback(m_playlistSources.at(m_playlistIndex), 0.0);
        return true;
    }

    // If this process has just restored a library from disk, rehydrate the saved
    // queue before adding to it so the first enqueue cannot discard older entries.
    if (!m_backendPlaylistHydrated && !m_playlistSources.isEmpty()) {
        m_playlistSources.append(clean);
        m_playlistTitles.append(title);
        m_playlistOrigins.append(QString());
        if (m_playlistIndex < 0) m_playlistIndex = 0;
        savePersistentLibrary();
        const bool ok = hydrateBackendPlaylist(m_playlistIndex);
        if (ok) {
            emit playlistChanged();
            emit playlistEntriesChanged(m_playlistSources, m_playlistTitles, m_playlistIndex);
        }
        return ok;
    }

    const bool ok = sendLoadFile(clean, QStringLiteral("append-play"));
    if (ok) {
        m_backendPlaylistHydrated = true;
        m_playlistSources.append(clean);
        m_playlistTitles.append(title);
        m_playlistOrigins.append(QString());
        if (m_playlistIndex < 0) m_playlistIndex = 0;
        savePersistentLibrary();
        emit playlistEntriesChanged(m_playlistSources, m_playlistTitles, m_playlistIndex);
        emit playlistChanged();
    }
    return ok;
}

bool MediaController::loadPlaylist(const QString &pathOrUrl, bool replace)
{
    m_transientMode = false;
    const QString clean = pathOrUrl.trimmed();
    if (clean.isEmpty() || !ensureBackend()) return false;

    // Preserve local playlist definitions inside WaffleHouse AppData. We still
    // load the original path so relative local-media references keep working;
    // the expanded queue is persisted separately and no longer depends on the
    // original .pls/.m3u/.xspf being present on the next launch.
    const QString cached = cachePlaylistDefinition(clean);
    m_pendingPlaylistOrigin = cached.isEmpty() ? clean : cached;

    if (remoteAudioMode()) {
        QStringList entries;
        QStringList titles;
        QFile file(clean);
        if (file.exists() && file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QString baseDir = QFileInfo(clean).absolutePath();
            const QString suffix = QFileInfo(clean).suffix().toCaseFolded();
            if (suffix == QStringLiteral("xspf")) {
                const QString xml = QString::fromUtf8(file.readAll());
                const QRegularExpression rx(QStringLiteral("<location[^>]*>([^<]+)</location>"),
                                            QRegularExpression::CaseInsensitiveOption);
                auto it = rx.globalMatch(xml);
                while (it.hasNext()) {
                    QString entry = it.next().captured(1).trimmed();
                    entry.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
                    if (!entry.isEmpty()) entries << entry;
                }
            } else {
                while (!file.atEnd()) {
                    QString line = QString::fromUtf8(file.readLine()).trimmed();
                    if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
                    if (line.contains(QLatin1Char('=')) && line.startsWith(QStringLiteral("File"), Qt::CaseInsensitive))
                        line = line.mid(line.indexOf(QLatin1Char('=')) + 1).trimmed();
                    if (line.isEmpty()) continue;
                    if (!line.contains(QStringLiteral("://")) && QFileInfo(line).isRelative())
                        line = QDir(baseDir).filePath(line);
                    entries << line;
                }
            }
        } else {
            // A URL or non-playlist source is still a valid one-entry queue.
            entries << clean;
        }
        if (entries.isEmpty()) {
            emit errorMessage(QStringLiteral("Playlist contains no playable entries: %1").arg(clean));
            return false;
        }
        for (const QString &entry : entries) {
            QString title = QFileInfo(entry).fileName();
            titles << (title.isEmpty() ? entry : title);
        }
        if (replace) {
            stopRemotePlayback(true);
            m_playlistSources = entries;
            m_playlistTitles = titles;
            m_playlistOrigins.clear();
            for (int i = 0; i < entries.size(); ++i) m_playlistOrigins << m_pendingPlaylistOrigin;
            m_playlistIndex = 0;
        } else {
            m_playlistSources.append(entries);
            m_playlistTitles.append(titles);
            for (int i = 0; i < entries.size(); ++i) m_playlistOrigins << m_pendingPlaylistOrigin;
            if (m_playlistIndex < 0) m_playlistIndex = 0;
        }
        m_pendingPlaylistOrigin.clear();
        savePersistentLibrary();
        emit playlistChanged();
        emit playlistEntriesChanged(m_playlistSources, m_playlistTitles, m_playlistIndex);
        emit statusMessage(QStringLiteral("Loaded %1 remote playlist entries from %2")
                               .arg(entries.size()).arg(clean));
        if (replace) return startRemotePlayback(m_playlistSources.at(0), 0.0);
        return true;
    }
    const bool ok = sendCommand({
        QStringLiteral("loadlist"),
        clean,
        replace ? QStringLiteral("replace") : QStringLiteral("append")
    }, QStringLiteral("load playlist"));
    if (ok) {
        m_backendPlaylistHydrated = true;
        m_source = clean;
        emit sourceChanged(clean);
        emit playlistChanged();
        savePersistentLibrary(); // also persists the import/cache record now
        emit statusMessage(QStringLiteral("Imported playlist into persistent library: %1").arg(clean));
    } else {
        m_pendingPlaylistOrigin.clear();
    }
    return ok;
}

void MediaController::playPlaylistIndex(int index)
{
    m_transientMode = false;
    if (index < 0 || index >= m_playlistSources.size()) return;
    if (remoteAudioMode()) {
        m_playlistIndex = index;
        savePersistentLibrary();
        startRemotePlayback(m_playlistSources.at(index), 0.0);
        emit playlistEntriesChanged(m_playlistSources, m_playlistTitles, m_playlistIndex);
        return;
    }
    if (!m_backendPlaylistHydrated) {
        if (!hydrateBackendPlaylist(index)) return;
    } else {
        // Choosing a track is an explicit play action. If the transport was
        // paused, clear pause first so playlist-play-index cannot select a track
        // that remains silently paused.
        if (m_paused && setProperty(QStringLiteral("pause"), false)) {
            m_paused = false;
            emit pauseChanged(false);
        }
        m_playlistIndex = index;
        savePersistentLibrary();
        sendCommand({QStringLiteral("playlist-play-index"), QString::number(index)},
                    QStringLiteral("play playlist index"));
    }
    emit playlistEntriesChanged(m_playlistSources, m_playlistTitles, m_playlistIndex);
}

void MediaController::removePlaylistIndex(int index)
{
    if (index < 0 || index >= m_playlistSources.size()) return;
    if (remoteAudioMode()) {
        const bool removingCurrent = index == m_playlistIndex;
        m_playlistSources.removeAt(index);
        if (index < m_playlistTitles.size()) m_playlistTitles.removeAt(index);
        if (index < m_playlistOrigins.size()) m_playlistOrigins.removeAt(index);
        if (m_playlistSources.isEmpty()) {
            stopRemotePlayback(true);
            m_playlistIndex = -1;
        } else if (removingCurrent) {
            m_playlistIndex = qMin(index, m_playlistSources.size() - 1);
            startRemotePlayback(m_playlistSources.at(m_playlistIndex), 0.0);
        } else if (index < m_playlistIndex) {
            --m_playlistIndex;
        }
        savePersistentLibrary();
        emit playlistChanged();
        emit playlistEntriesChanged(m_playlistSources, m_playlistTitles, m_playlistIndex);
        return;
    }

    if (m_backendPlaylistHydrated) {
        sendCommand({QStringLiteral("playlist-remove"), QString::number(index)},
                    QStringLiteral("remove playlist entry"));
    }
    m_playlistSources.removeAt(index);
    if (index < m_playlistTitles.size()) m_playlistTitles.removeAt(index);
    if (index < m_playlistOrigins.size()) m_playlistOrigins.removeAt(index);
    if (m_playlistSources.isEmpty()) m_playlistIndex = -1;
    else if (index < m_playlistIndex) --m_playlistIndex;
    else if (m_playlistIndex >= m_playlistSources.size()) m_playlistIndex = m_playlistSources.size() - 1;
    savePersistentLibrary();
    emit playlistChanged();
    emit playlistEntriesChanged(m_playlistSources, m_playlistTitles, m_playlistIndex);
}

void MediaController::clearPlaylist()
{
    m_preserveLibraryWhileStopped = false;
    if (remoteAudioMode()) {
        stopRemotePlayback(true);
    } else if (m_backendPlaylistHydrated) {
        sendCommand({QStringLiteral("playlist-clear")}, QStringLiteral("clear playlist"));
    }
    m_playlistSources.clear();
    m_playlistTitles.clear();
    m_playlistOrigins.clear();
    m_playlistIndex = -1;
    savePersistentLibrary();
    emit playlistChanged();
    emit playlistEntriesChanged({}, {}, -1);
}

void MediaController::pause()
{
    if (remoteAudioMode()) {
        if (m_remoteProcess->state() == QProcess::NotRunning || m_paused) return;
        refreshRemotePosition();
        if (::kill(static_cast<pid_t>(m_remoteProcess->processId()), SIGSTOP) == 0) {
            m_remoteBasePosition = m_position;
            m_paused = true;
            m_remoteTimer->stop();
            emit pauseChanged(true);
            emit statusMessage(QStringLiteral("Remote playback paused."));
        }
        return;
    }
    // Pause is meaningful only while transport is actively playing. Do not let
    // an idle/stopped backend acquire pause=true, because that state could carry
    // into the next Stop -> Play rebuild.
    if (m_idle || m_paused) return;

    // Update our local state as soon as the IPC write succeeds so a fast
    // Pause -> Play click cannot race mpv's asynchronous property notification.
    if (setProperty(QStringLiteral("pause"), true)) {
        m_paused = true;
        emit pauseChanged(true);
        emit statusMessage(QStringLiteral("Playback paused."));
    }
}

void MediaController::resume()
{
    if (remoteAudioMode()) {
        if (m_idle && !m_playlistSources.isEmpty()) {
            if (m_playlistIndex < 0) m_playlistIndex = 0;
            startRemotePlayback(m_playlistSources.at(m_playlistIndex), 0.0);
            return;
        }
        if (!m_paused || m_remoteProcess->state() == QProcess::NotRunning) return;
        if (::kill(static_cast<pid_t>(m_remoteProcess->processId()), SIGCONT) == 0) {
            m_paused = false;
            m_remoteClock->restart();
            m_remoteTimer->start();
            emit pauseChanged(false);
            emit statusMessage(QStringLiteral("Remote playback resumed."));
        }
        return;
    }
    if (m_idle && !m_playlistSources.isEmpty()) {
        const int index = qBound(0, m_playlistIndex < 0 ? 0 : m_playlistIndex,
                                 m_playlistSources.size() - 1);
        if (!m_backendPlaylistHydrated) hydrateBackendPlaylist(index);
        else sendCommand({QStringLiteral("playlist-play-index"), QString::number(index)},
                         QStringLiteral("resume saved playlist"));
        return;
    }
    // Resume must only clear mpv's pause property. Never select/reload the
    // playlist here unless the player is actually stopped/idle (handled above).
    if (!m_paused) return;
    if (setProperty(QStringLiteral("pause"), false)) {
        m_paused = false;
        m_idle = false;
        emit pauseChanged(false);
        emit idleChanged(false);
        emit statusMessage(QStringLiteral("Playback resumed."));
    }
}

void MediaController::togglePause()
{
    // Route toggles through the same state-aware code as the GUI buttons so
    // local state changes synchronously with the IPC command.
    m_paused ? resume() : pause();
}

void MediaController::stop()
{
    if (remoteAudioMode()) {
        stopRemotePlayback(true);
        emit statusMessage(QStringLiteral("Remote playback stopped; playlist preserved."));
        return;
    }

    // Save the queue before touching mpv and make the stopped state immediate.
    // The transport queue is deliberately discarded below; WaffleHouse keeps the
    // authoritative library and the next Play rebuilds mpv from that saved state.
    // This removes any dependency on mpv preserving a current entry across Stop.
    savePersistentLibrary();
    m_preserveLibraryWhileStopped = !m_playlistSources.isEmpty();
    // Let mpv clear its transport queue completely. WaffleHouse keeps the real
    // queue in m_playlistSources and rebuilds mpv on the next Play. This avoids
    // carrying any stopped/current-entry state across the transport boundary.
    (void)sendCommand({QStringLiteral("stop")}, QStringLiteral("stop playback"));
    m_backendPlaylistHydrated = false;
    m_idle = true;
    m_paused = false;
    m_position = 0.0;
    m_duration = 0.0;
    emit idleChanged(true);
    emit pauseChanged(false);
    emit positionChanged(0.0);
    emit durationChanged(0.0);
    emit statusMessage(QStringLiteral("Playback stopped; playlist preserved. Press Play to restart the current track."));
}

void MediaController::next()
{
    if (remoteAudioMode()) {
        if (m_playlistSources.isEmpty()) return;
        int index = m_playlistIndex + 1;
        if (index >= m_playlistSources.size()) index = (m_repeatMode == QStringLiteral("all")) ? 0 : m_playlistSources.size()-1;
        playPlaylistIndex(index);
        return;
    }
    if (!m_backendPlaylistHydrated && !m_playlistSources.isEmpty()) {
        int index = m_playlistIndex + 1;
        if (index >= m_playlistSources.size()) index = (m_repeatMode == QStringLiteral("all")) ? 0 : m_playlistSources.size() - 1;
        hydrateBackendPlaylist(index);
        return;
    }
    sendCommand({QStringLiteral("playlist-next"), QStringLiteral("force")});
}

void MediaController::previous()
{
    if (remoteAudioMode()) {
        if (m_playlistSources.isEmpty()) return;
        int index = m_playlistIndex - 1;
        if (index < 0) index = (m_repeatMode == QStringLiteral("all")) ? m_playlistSources.size()-1 : 0;
        playPlaylistIndex(index);
        return;
    }
    if (!m_backendPlaylistHydrated && !m_playlistSources.isEmpty()) {
        int index = m_playlistIndex - 1;
        if (index < 0) index = (m_repeatMode == QStringLiteral("all")) ? m_playlistSources.size() - 1 : 0;
        hydrateBackendPlaylist(index);
        return;
    }
    sendCommand({QStringLiteral("playlist-prev"), QStringLiteral("force")});
}

void MediaController::seekRelative(double seconds)
{
    if (remoteAudioMode()) { refreshRemotePosition(); restartRemoteAt(qMax(0.0, m_position + seconds)); return; }
    sendCommand({QStringLiteral("seek"), QString::number(seconds, 'f', 3), QStringLiteral("relative")});
}

void MediaController::seekAbsolute(double seconds)
{
    if (remoteAudioMode()) { restartRemoteAt(qMax(0.0, seconds)); return; }
    sendCommand({QStringLiteral("seek"), QString::number(qMax(0.0, seconds), 'f', 3), QStringLiteral("absolute")});
}

void MediaController::setVolume(int percent)
{
    m_volume = qBound(0, percent, 150);
    if (remoteAudioMode()) {
        if (!m_idle && !m_source.isEmpty()) { refreshRemotePosition(); restartRemoteAt(m_position); }
    } else {
        setProperty(QStringLiteral("volume"), m_volume);
    }
    savePersistentLibrary();
    emit volumeChanged(m_volume);
}

void MediaController::setMuted(bool muted)
{
    m_muted = muted;
    if (remoteAudioMode()) {
        if (!m_idle && !m_source.isEmpty()) { refreshRemotePosition(); restartRemoteAt(m_position); }
    } else {
        setProperty(QStringLiteral("mute"), muted);
    }
    savePersistentLibrary();
    emit muteChanged(muted);
}

void MediaController::toggleMuted()
{
    setMuted(!m_muted);
}

void MediaController::setShuffle(bool enabled)
{
    if (!ensureBackend()) return;
    if (enabled == m_shuffle) return;
    if (!remoteAudioMode()) {
        sendCommand({enabled ? QStringLiteral("playlist-shuffle")
                             : QStringLiteral("playlist-unshuffle")});
    }
    m_shuffle = enabled;
    savePersistentLibrary();
    emit statusMessage(QStringLiteral("Shuffle %1.").arg(enabled ? QStringLiteral("enabled")
                                                                 : QStringLiteral("disabled")));
}

void MediaController::setRepeatMode(const QString &mode)
{
    const QString normalized = mode.trimmed().toCaseFolded();
    if (normalized != QStringLiteral("off")
        && normalized != QStringLiteral("one")
        && normalized != QStringLiteral("all")) {
        emit errorMessage(QStringLiteral("Repeat mode must be off, one, or all."));
        return;
    }

    m_repeatMode = normalized;
    if (!remoteAudioMode()) {
        setProperty(QStringLiteral("loop-file"),
                    normalized == QStringLiteral("one") ? QJsonValue(QStringLiteral("inf"))
                                                        : QJsonValue(QStringLiteral("no")));
        setProperty(QStringLiteral("loop-playlist"),
                    normalized == QStringLiteral("all") ? QJsonValue(QStringLiteral("inf"))
                                                        : QJsonValue(QStringLiteral("no")));
    }
    savePersistentLibrary();
    emit statusMessage(QStringLiteral("Repeat mode: %1").arg(normalized));
}

void MediaController::setEqualizerBand(int band, double gainDb)
{
    if (band < 0 || band >= m_eq.size()) {
        emit errorMessage(QStringLiteral("EQ band must be 0 through 9."));
        return;
    }
    m_eq[band] = qBound(-12.0, gainDb, 12.0);
    rebuildEqualizer();
}

void MediaController::resetEqualizer()
{
    for (double &value : m_eq) value = 0.0;
    if (remoteAudioMode()) {
        if (!m_idle && !m_source.isEmpty()) { refreshRemotePosition(); restartRemoteAt(m_position); }
    } else {
        sendCommand({QStringLiteral("af"), QStringLiteral("clr"), QString()},
                    QStringLiteral("reset equalizer"));
    }
    emit statusMessage(QStringLiteral("Media equalizer reset to flat."));
}

void MediaController::rebuildEqualizer()
{
    if (remoteAudioMode()) {
        if (!m_idle && !m_source.isEmpty()) { refreshRemotePosition(); restartRemoteAt(m_position); }
        emit statusMessage(QStringLiteral("10-band remote media equalizer updated."));
        return;
    }
    static const int frequencies[10] = {
        60, 170, 310, 600, 1000, 3000, 6000, 12000, 14000, 16000
    };

    QStringList filters;
    for (int i = 0; i < m_eq.size(); ++i) {
        filters << QStringLiteral("equalizer=f=%1:t=q:w=1:g=%2")
                       .arg(frequencies[i])
                       .arg(m_eq[i], 0, 'f', 1);
    }
    sendCommand({
        QStringLiteral("af"),
        QStringLiteral("set"),
        QStringLiteral("lavfi=[%1]").arg(filters.join(QLatin1Char(',')))
    }, QStringLiteral("update equalizer"));
    emit statusMessage(QStringLiteral("10-band media equalizer updated."));
}

void MediaController::observeProperties()
{
    if (m_observing || !ipcConnected()) return;
    const QStringList names = {
        QStringLiteral("time-pos"), QStringLiteral("duration"), QStringLiteral("pause"),
        QStringLiteral("media-title"), QStringLiteral("volume"), QStringLiteral("mute"),
        QStringLiteral("idle-active"), QStringLiteral("path"), QStringLiteral("playlist-pos"),
        QStringLiteral("playlist")
    };
    int id = 100;
    for (const QString &name : names) {
        QJsonArray command;
        command.append(QStringLiteral("observe_property"));
        command.append(id++);
        command.append(name);
        sendJsonCommand(command, QStringLiteral("observe %1").arg(name));
    }
    m_observing = true;
    m_refreshTimer->start();
}

void MediaController::refreshObservedProperties()
{
    if (!ipcConnected()) return;
    if (m_process->state() == QProcess::NotRunning) {
        m_refreshTimer->stop();
        emit readyChanged(false);
    }
}

void MediaController::readIpc()
{
    if (!ipcConnected()) return;

    char buffer[8192];
    while (true) {
        const ssize_t received = ::recv(m_ipcFd, buffer, sizeof(buffer), MSG_DONTWAIT);
        if (received > 0) {
            m_ipcBuffer.append(buffer, static_cast<qsizetype>(received));
            continue;
        }
        if (received == 0) {
            closeIpcSocket();
            emit readyChanged(false);
            break;
        }
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
        m_lastSocketError = QStringLiteral("IPC read failed: %1")
                                .arg(QString::fromLocal8Bit(std::strerror(errno)));
        closeIpcSocket();
        emit readyChanged(false);
        break;
    }

    while (true) {
        const int newline = m_ipcBuffer.indexOf('\n');
        if (newline < 0) break;
        const QByteArray line = m_ipcBuffer.left(newline);
        m_ipcBuffer.remove(0, newline + 1);
        parseIpcLine(line);
    }
}

void MediaController::refreshPlaylistSnapshot(const QJsonValue &data)
{
    if (m_transientMode) return;
    if (!data.isArray()) return;
    const QJsonArray array = data.toArray();

    // `stop` and queue rehydration must never overwrite the application-owned
    // media library. mpv publishes transient playlist snapshots while stop and
    // loadfile commands are being processed (empty, one item, two items, ...).
    // While the stopped/rebuild guard is active, ignore every snapshot that does
    // not yet contain the complete queue. This prevents an intermediate event
    // from shrinking an 11-track library to one track before Play finishes.
    if (m_preserveLibraryWhileStopped && !m_playlistSources.isEmpty()) {
        // While genuinely stopped, no backend playlist notification is allowed
        // to mutate the application library. Once Play starts rehydration we set
        // m_idle false; then only the final full-size snapshot is accepted.
        if (m_idle || array.size() != m_playlistSources.size()) return;
        m_preserveLibraryWhileStopped = false;
    }
    const QStringList oldSources = m_playlistSources;
    const QStringList oldOrigins = m_playlistOrigins;
    QStringList sources;
    QStringList titles;
    QStringList origins;
    int current = -1;
    for (int i = 0; i < array.size(); ++i) {
        if (!array.at(i).isObject()) continue;
        const QJsonObject entry = array.at(i).toObject();
        QString source = entry.value(QStringLiteral("filename")).toString();
        QString title = entry.value(QStringLiteral("title")).toString();
        if (title.isEmpty()) {
            title = QFileInfo(source).fileName();
            if (title.isEmpty()) title = source;
        }
        sources << source;
        titles << title;
        QString origin = m_pendingPlaylistOrigin;
        if (origin.isEmpty()) {
            const int oldIndex = oldSources.indexOf(source);
            if (oldIndex >= 0 && oldIndex < oldOrigins.size()) origin = oldOrigins.at(oldIndex);
        }
        origins << origin;
        if (entry.value(QStringLiteral("current")).toBool(false)) current = i;
    }
    m_playlistSources = sources;
    m_playlistTitles = titles;
    m_playlistOrigins = origins;
    m_playlistIndex = current;
    if (!m_playlistSources.isEmpty() && m_playlistIndex < 0) m_playlistIndex = 0;
    m_pendingPlaylistOrigin.clear();
    m_backendPlaylistHydrated = true;
    savePersistentLibrary();
    emit playlistEntriesChanged(sources, titles, m_playlistIndex);
    emit playlistChanged();
}

void MediaController::parseIpcLine(const QByteArray &line)
{
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) return;

    const QJsonObject object = doc.object();
    if (object.contains(QStringLiteral("request_id"))) {
        const qint64 requestId = static_cast<qint64>(object.value(QStringLiteral("request_id")).toDouble());
        const QString description = m_pendingRequests.take(requestId);
        const QString result = object.value(QStringLiteral("error")).toString();
        if (!description.isEmpty() && !result.isEmpty() && result != QStringLiteral("success")) {
            emit errorMessage(QStringLiteral("mpv command '%1' failed: %2").arg(description, result));
        }
    }

    const QString event = object.value(QStringLiteral("event")).toString();
    if (event == QStringLiteral("end-file")
        && object.value(QStringLiteral("reason")).toString() == QStringLiteral("error")) {
        QString detail = object.value(QStringLiteral("file_error")).toString().trimmed();
        if (detail.isEmpty()) detail = object.value(QStringLiteral("error")).toString().trimmed();
        if (detail.isEmpty()) detail = QStringLiteral("unknown media/network error");
        emit errorMessage(QStringLiteral("mpv could not play the media stream: %1").arg(detail));
        return;
    }
    if (event != QStringLiteral("property-change")) return;

    const QString name = object.value(QStringLiteral("name")).toString();
    const QJsonValue data = object.value(QStringLiteral("data"));

    if (name == QStringLiteral("time-pos") && data.isDouble()) {
        m_position = data.toDouble();
        emit positionChanged(m_position);
    } else if (name == QStringLiteral("duration") && data.isDouble()) {
        m_duration = data.toDouble();
        emit durationChanged(m_duration);
    } else if (name == QStringLiteral("pause") && data.isBool()) {
        m_paused = data.toBool();
        emit pauseChanged(m_paused);
    } else if (name == QStringLiteral("media-title") && data.isString()) {
        m_title = data.toString();
        emit nowPlayingChanged(m_title);
    } else if (name == QStringLiteral("volume") && data.isDouble()) {
        m_volume = qBound(0, qRound(data.toDouble()), 150);
        emit volumeChanged(m_volume);
    } else if (name == QStringLiteral("mute") && data.isBool()) {
        m_muted = data.toBool();
        emit muteChanged(m_muted);
    } else if (name == QStringLiteral("idle-active") && data.isBool()) {
        m_idle = data.toBool();
        // Do not drop m_preserveLibraryWhileStopped merely because playback
        // became active. During a direct queue rebuild mpv can report active
        // before it has emitted the final full playlist snapshot. The snapshot
        // handler releases the guard only after the whole queue is present.
        emit idleChanged(m_idle);
        if (m_idle) {
            m_position = 0.0;
            m_duration = 0.0;
            emit positionChanged(0.0);
            emit durationChanged(0.0);
        }
    } else if (name == QStringLiteral("path") && data.isString()) {
        m_source = data.toString();
        emit sourceChanged(m_source);
    } else if (name == QStringLiteral("playlist-pos") && data.isDouble() && !m_transientMode) {
        const int nextIndex = qRound(data.toDouble());
        if (nextIndex != m_playlistIndex) {
            m_playlistIndex = nextIndex;
            savePersistentLibrary();
            // Keep the GUI's current row synchronized with mpv as tracks advance.
            // This makes the visible row a reliable restart target after Stop.
            emit playlistEntriesChanged(m_playlistSources, m_playlistTitles, m_playlistIndex);
        }
    } else if (name == QStringLiteral("playlist")) {
        refreshPlaylistSnapshot(data);
    }
}

void MediaController::drainBackendOutput()
{
    const QByteArray out = m_process->readAllStandardOutput();
    const QByteArray err = m_process->readAllStandardError();
    Q_UNUSED(out);
    if (!err.trimmed().isEmpty()) {
        m_lastBackendError = QString::fromLocal8Bit(err).trimmed().right(2000);
    }
}

void MediaController::processFinished(int exitCode)
{
    m_connectTimer->stop();
    m_refreshTimer->stop();
    m_observing = false;
    m_backendPlaylistHydrated = false;
    closeIpcSocket();
    cleanupIpcPath();
    emit readyChanged(false);
    if (!m_shuttingDown && exitCode != 0) {
        emit errorMessage(m_lastBackendError.isEmpty()
            ? QStringLiteral("mpv exited unexpectedly with status %1.").arg(exitCode)
            : QStringLiteral("mpv exited unexpectedly with status %1: %2")
                  .arg(exitCode).arg(m_lastBackendError));
    }
}

void MediaController::shutdown()
{
    if (m_shuttingDown) return;
    savePersistentLibrary();
    m_shuttingDown = true;
    m_connectTimer->stop();
    m_refreshTimer->stop();
    if (m_remoteTimer) m_remoteTimer->stop();
    m_remoteStoppedByUser = true;
    if (m_remoteProcess && m_remoteProcess->state() != QProcess::NotRunning) {
        m_remoteProcess->terminate();
        if (!m_remoteProcess->waitForFinished(750)) {
            m_remoteProcess->kill();
            m_remoteProcess->waitForFinished(300);
        }
    }
    if (ipcConnected()) {
        // Ask mpv to exit cleanly, then close our native Unix-domain socket.
        QJsonArray command;
        command.append(QStringLiteral("quit"));
        QJsonObject object;
        object.insert(QStringLiteral("command"), command);
        const QByteArray wire = QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
        QString ignored;
        (void)writeIpc(wire, &ignored);
        closeIpcSocket();
    }
    if (m_process->state() != QProcess::NotRunning) {
        m_process->terminate();
        if (!m_process->waitForFinished(1000)) {
            m_process->kill();
            m_process->waitForFinished(500);
        }
    }
    cleanupIpcPath();
}
