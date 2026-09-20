#include "mainwindow.h"
#include "buildfeatures.h"
#include "platforminfo.h"
#include "mediawindow.h"
#include "wafflecast.h"

#include "chatwindow.h"
#include "appbranding.h"
#include "appicon.h"
#include "ircbackend.h"
#include "xmppbackend.h"
#include "sshbackend.h"
#include "nntpbackend.h"
#include "networktoolswindow.h"
#include "oscarbackend.h"
#include "oscarvoice.h"
#include "telnetbackend.h"
#include "bbsdirectory.h"
#include "transferwindow.h"
#include "sipcontroller.h"
#include "sipbackend.h"
#include "softphonewindow.h"
#include "modernstyle.h"
#include "notificationmanager.h"
#include "filetransport.h"
#include "useractivity.h"
#include "core/capabilityregistry.h"
#include "core/contactstore.h"
#include "core/historystore.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QCryptographicHash>
#include <QIcon>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QEvent>
#include <QFrame>
#include <QFont>
#include <QGroupBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QRadioButton>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QShortcut>
#include <QTextBrowser>
#include <QTabWidget>
#include <QTextCursor>
#include <QPushButton>
#include <QSpinBox>
#include <QSettings>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QStyle>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QWidget>
#include <QUuid>
#include <QVariant>

#include <algorithm>

namespace {

QString historyProtocolKey(ConnectionSettings::Protocol protocol)
{
    switch (protocol) {
    case ConnectionSettings::Protocol::Oscar: return QStringLiteral("aim");
    case ConnectionSettings::Protocol::Irc: return QStringLiteral("irc");
    case ConnectionSettings::Protocol::Telnet: return QStringLiteral("telnet");
    case ConnectionSettings::Protocol::Sip: return QStringLiteral("sip");
    case ConnectionSettings::Protocol::Xmpp: return QStringLiteral("xmpp");
    case ConnectionSettings::Protocol::Ssh: return QStringLiteral("ssh");
    case ConnectionSettings::Protocol::Nntp: return QStringLiteral("nntp");
    case ConnectionSettings::Protocol::Unknown: break;
    }
    return QStringLiteral("unknown");
}

void appendGuiHistory(ChatBackend *backend, const QString &kind, const QString &target,
                      const QString &direction, const QString &text)
{
    if (!backend || text.trimmed().isEmpty()) return;
    HistoryStore::append({QDateTime::currentDateTime(),
                          historyProtocolKey(backend->settings().protocol),
                          backend->id(), kind, target, direction, text});
}

QString formatEpochSeconds(qint64 seconds)
{
    if (seconds <= 0) return QStringLiteral("Not supplied");
    return QDateTime::fromSecsSinceEpoch(seconds).toLocalTime()
        .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

QString formatElapsedSeconds(qint64 seconds)
{
    if (seconds < 0) return QStringLiteral("Not supplied");
    const qint64 days = seconds / 86400;
    seconds %= 86400;
    const qint64 hours = seconds / 3600;
    seconds %= 3600;
    const qint64 minutes = seconds / 60;
    QStringList parts;
    if (days) parts << QStringLiteral("%1d").arg(days);
    if (hours || days) parts << QStringLiteral("%1h").arg(hours);
    parts << QStringLiteral("%1m").arg(minutes);
    return parts.join(QLatin1Char(' '));
}

QStringList aimUserFlagNames(quint64 flags)
{
    struct FlagName { quint64 bit; const char *name; };
    static const FlagName known[] = {
        {0x00000001ULL, "Unconfirmed account"},
        {0x00000002ULL, "Server administrator"},
        {0x00000004ULL, "AOL client/account"},
        {0x00000008ULL, "Commercial OSCAR account"},
        {0x00000010ULL, "AIM / OSCAR free account"},
        {0x00000020ULL, "Away / unavailable"},
        {0x00000040ULL, "ICQ user"},
        {0x00000080ULL, "Wireless / mobile user"},
        {0x00000100ULL, "Internal account"},
        {0x00000200ULL, "IM forwarding"},
        {0x00000400ULL, "Bot user"},
        {0x00000800ULL, "Extended/legacy user flag 0x0800"},
        {0x00001000ULL, "One-way wireless device"},
        {0x00002000ULL, "Official/legacy user flag 0x2000"},
        {0x00010000ULL, "Buddy-match direct"},
        {0x00020000ULL, "Buddy-match indirect"},
        {0x00040000ULL, "Trusted/no knock-knock"},
        {0x00080000ULL, "Forward to mobile when offline"},
    };
    QStringList names;
    for (const auto &item : known) if ((flags & item.bit) != 0) names << QString::fromLatin1(item.name);
    return names;
}

QStringList aimStatusDetails(qint64 rawValue)
{
    if (rawValue < 0) return {QStringLiteral("Not supplied by server")};
    const quint32 raw = static_cast<quint32>(rawValue);
    const quint16 state = static_cast<quint16>(raw & 0xffffU);
    const quint16 flags = static_cast<quint16>((raw >> 16) & 0xffffU);

    QString stateText;
    switch (state) {
    case 0x0000: stateText = QStringLiteral("Online / available"); break;
    case 0x0001: stateText = QStringLiteral("Away"); break;
    case 0x0002: stateText = QStringLiteral("Do not disturb"); break;
    case 0x0004: stateText = QStringLiteral("Not available"); break;
    case 0x0010: stateText = QStringLiteral("Busy / occupied"); break;
    case 0x0020: stateText = QStringLiteral("Free for chat"); break;
    case 0x0100: stateText = QStringLiteral("Invisible"); break;
    default: stateText = QStringLiteral("Server-specific state 0x%1").arg(state, 4, 16, QLatin1Char('0')); break;
    }

    QStringList result{stateText};
    QStringList flagNames;
    if (flags & 0x0001) flagNames << QStringLiteral("Web-aware");
    if (flags & 0x0002) flagNames << QStringLiteral("IP visibility flag");
    if (flags & 0x0008) flagNames << QStringLiteral("Birthday");
    if (flags & 0x0020) flagNames << QStringLiteral("Web-front active");
    if (flags & 0x0100) flagNames << QStringLiteral("Direct connection disabled");
    if (flags & 0x1000) flagNames << QStringLiteral("Direct connection requires authorization");
    if (flags & 0x2000) flagNames << QStringLiteral("Direct connection limited to contacts");
    if (!flagNames.isEmpty()) result << QStringLiteral("Flags: %1").arg(flagNames.join(QStringLiteral(", ")));
    result << QStringLiteral("Raw: 0x%1").arg(raw, 8, 16, QLatin1Char('0'));
    return result;
}

void appendCapabilityGroup(QStringList &lines, const QString &heading, const QStringList &entries)
{
    lines << heading;
    if (entries.isEmpty()) lines << QStringLiteral("  (none advertised)");
    else for (const QString &entry : entries) lines << QStringLiteral("  %1").arg(entry);
}

QString aimUserInfoText(const QVariantMap &info, bool profileFocus)
{
    const QString name = info.value(QStringLiteral("screenName")).toString();
    const QString profile = info.value(QStringLiteral("profile")).toString();
    const QString away = info.value(QStringLiteral("awayMessage")).toString();
    const int idleMinutes = info.value(QStringLiteral("idleMinutes"), -1).toInt();
    const qint64 onlineSeconds = info.value(QStringLiteral("onlineSeconds"), -1).toLongLong();
    const bool flagsSupplied = info.value(QStringLiteral("userFlagsSupplied")).toBool();
    const quint64 userFlags = info.value(QStringLiteral("userFlags")).toULongLong();
    const QStringList flagNames = aimUserFlagNames(userFlags);
    const qint64 statusRaw = info.value(QStringLiteral("statusRaw"), -1).toLongLong();

    QStringList lines;
    lines << QStringLiteral("Screen Name: %1").arg(name)
          << QStringLiteral("Presence: %1").arg(info.value(QStringLiteral("presence"), QStringLiteral("Unknown")).toString())
          << QStringLiteral("Warning Level: %1%").arg(info.value(QStringLiteral("warningPercent")).toDouble(), 0, 'f', 1)
          << QStringLiteral("Idle: %1").arg(idleMinutes >= 0 ? QStringLiteral("%1 minute(s)").arg(idleMinutes)
                                                           : QStringLiteral("Not supplied by server"))
          << QStringLiteral("Online For: %1").arg(onlineSeconds >= 0 ? formatElapsedSeconds(onlineSeconds)
                                                                      : QStringLiteral("Not supplied by server"))
          << QStringLiteral("Signed On: %1").arg(formatEpochSeconds(info.value(QStringLiteral("signonTime"), -1).toLongLong()))
          << QStringLiteral("Member Since: %1").arg(formatEpochSeconds(info.value(QStringLiteral("memberSince"), -1).toLongLong()));

    if (!flagsSupplied) {
        lines << QStringLiteral("User Flags: Not supplied by server");
    } else {
        lines << QStringLiteral("User Flags: %1").arg(flagNames.isEmpty() ? QStringLiteral("No recognized flags")
                                                                          : flagNames.join(QStringLiteral("; ")))
              << QStringLiteral("User Flags Raw: 0x%1").arg(userFlags, userFlags > 0xffffULL ? 8 : 4, 16, QLatin1Char('0'));
    }

    const QStringList statusDetails = aimStatusDetails(statusRaw);
    lines << QStringLiteral("OSCAR/ICQ Status: %1").arg(statusDetails.value(0));
    for (int i = 1; i < statusDetails.size(); ++i) lines << QStringLiteral("  %1").arg(statusDetails.at(i));
    lines << QStringLiteral("Last Updated: %1").arg(info.value(QStringLiteral("updatedAt")).toString())
          << QString();

    if (profileFocus) lines << QStringLiteral("=== AIM PROFILE ===");
    else lines << QStringLiteral("AIM PROFILE");
    lines << (profile.trimmed().isEmpty() ? QStringLiteral("(No profile text returned.)") : profile)
          << QString()
          << QStringLiteral("AWAY MESSAGE")
          << (away.trimmed().isEmpty() ? QStringLiteral("(Not away / no away message returned.)") : away)
          << QString()
          << QStringLiteral("OSCAR USER CAPABILITIES")
          << QStringLiteral("Capability UUIDs: %1 unique (%2 raw entries received)")
                 .arg(info.value(QStringLiteral("capabilityCount")).toInt())
                 .arg(info.value(QStringLiteral("rawCapabilityEntries")).toInt())
          << QString();

    appendCapabilityGroup(lines, QStringLiteral("Standard OSCAR"), info.value(QStringLiteral("standardCapabilities")).toStringList());
    lines << QString();
    appendCapabilityGroup(lines, QStringLiteral("Legacy AIM / Rendezvous"), info.value(QStringLiteral("legacyCapabilities")).toStringList());
    lines << QString();
    appendCapabilityGroup(lines, QStringLiteral("WaffleHouse Extensions"), info.value(QStringLiteral("waffleCapabilities")).toStringList());
    const QStringList unknownCaps = info.value(QStringLiteral("unknownCapabilities")).toStringList();
    if (!unknownCaps.isEmpty()) {
        lines << QString();
        appendCapabilityGroup(lines, QStringLiteral("Unknown / Client-specific"), unknownCaps);
    }

    lines << QString()
          << QStringLiteral("WaffleHouse OSCAR Voice: %1").arg(info.value(QStringLiteral("waffleVoice")).toBool() ? QStringLiteral("SUPPORTED") : QStringLiteral("not advertised"))
          << QStringLiteral("Legacy AIM Voice/Talk: %1").arg(info.value(QStringLiteral("legacyVoice")).toBool() ? QStringLiteral("advertised") : QStringLiteral("not advertised"))
          << QStringLiteral("Direct IM: %1").arg(info.value(QStringLiteral("directIm")).toBool() ? QStringLiteral("advertised") : QStringLiteral("not advertised"))
          << QStringLiteral("OSCAR File Transfer: %1").arg(info.value(QStringLiteral("fileTransfer")).toBool() ? QStringLiteral("advertised") : QStringLiteral("not advertised"))
          << QStringLiteral("Buddy Icon: %1").arg(info.value(QStringLiteral("buddyIcon")).toBool() ? QStringLiteral("advertised") : QStringLiteral("not advertised"));
    return lines.join(QLatin1Char('\n'));
}

class ConnectionDialog final : public QDialog {
public:
    explicit ConnectionDialog(const ConnectionSettings &defaults,
                              bool editing = false,
                              QWidget *parent = nullptr)
        : QDialog(parent),
          m_editing(editing),
          m_original(defaults)
    {
        setWindowTitle(m_editing
                           ? QStringLiteral("Edit Connection — %1").arg(appDisplayName())
                           : QStringLiteral("Add Connection — %1").arg(appDisplayName()));
        setModal(true);
        setMinimumWidth(400);

        auto *outer = new QVBoxLayout(this);
        auto *form = new QFormLayout;
        outer->addLayout(form);

        m_protocol = new QComboBox(this);
        m_protocol->addItem(QStringLiteral("Select protocol…"),
                            static_cast<int>(ConnectionSettings::Protocol::Unknown));
        if (BuildFeatures::Oscar) m_protocol->addItem(QStringLiteral("AIM / OSCAR"),
                            static_cast<int>(ConnectionSettings::Protocol::Oscar));
        if (BuildFeatures::Irc) m_protocol->addItem(QStringLiteral("IRC"),
                            static_cast<int>(ConnectionSettings::Protocol::Irc));
        if (BuildFeatures::Telnet) m_protocol->addItem(QStringLiteral("Telnet / MUD / BBS"),
                            static_cast<int>(ConnectionSettings::Protocol::Telnet));
        if (BuildFeatures::Sip) m_protocol->addItem(QStringLiteral("SIP / VoIP"),
                            static_cast<int>(ConnectionSettings::Protocol::Sip));
        if (BuildFeatures::Xmpp) m_protocol->addItem(QStringLiteral("XMPP / Jabber"),
                            static_cast<int>(ConnectionSettings::Protocol::Xmpp));
        if (BuildFeatures::Ssh) m_protocol->addItem(QStringLiteral("SSH"),
                            static_cast<int>(ConnectionSettings::Protocol::Ssh));
        if (BuildFeatures::Nntp) m_protocol->addItem(QStringLiteral("NNTP / Usenet"),
                            static_cast<int>(ConnectionSettings::Protocol::Nntp));

        const int wanted = m_protocol->findData(static_cast<int>(defaults.protocol));
        if (wanted >= 0 && (m_editing || defaults.protocol != ConnectionSettings::Protocol::Unknown)) {
            m_protocol->setCurrentIndex(wanted);
        } else {
            m_protocol->setCurrentIndex(0);
        }
        form->addRow(QStringLiteral("Protocol:"), m_protocol);
        m_protocol->setEnabled(!m_editing);

        m_accountLabelLabel = new QLabel(QStringLiteral("Account label:"), this);
        m_accountLabel = new QLineEdit(defaults.accountLabel, this);
        m_accountLabel->setPlaceholderText(QStringLiteral("optional friendly name"));
        m_accountLabel->setToolTip(QStringLiteral("A local display label only. It does not change the AIM screen name or IRC nickname sent to the server."));
        form->addRow(m_accountLabelLabel, m_accountLabel);

        m_oscarNetworkLabel = new QLabel(QStringLiteral("AIM network:"), this);
        m_oscarNetwork = new QComboBox(this);
        m_oscarNetwork->addItem(QStringLiteral("Auto detect"), QStringLiteral("auto"));
        m_oscarNetwork->addItem(QStringLiteral("NINA Network (NINAPatcher compatibility)"), QStringLiteral("nina"));
        m_oscarNetwork->addItem(QStringLiteral("Custom OSCAR server"), QStringLiteral("custom"));
        QString requestedNetwork = defaults.networkProfile.trimmed().toCaseFolded();
        if (requestedNetwork.isEmpty()) requestedNetwork = QStringLiteral("auto");
        const int networkIndex = m_oscarNetwork->findData(requestedNetwork);
        m_oscarNetwork->setCurrentIndex(networkIndex >= 0 ? networkIndex : 0);
        form->addRow(m_oscarNetworkLabel, m_oscarNetwork);

        m_serverLabel = new QLabel(QStringLiteral("Server:"), this);
        m_server = new QLineEdit(defaults.server, this);
        form->addRow(m_serverLabel, m_server);

        m_portLabel = new QLabel(QStringLiteral("Port:"), this);
        m_port = new QSpinBox(this);
        m_port->setRange(1, 65535);
        m_port->setValue(defaults.port ? defaults.port : 5190);
        form->addRow(m_portLabel, m_port);

        m_userLabel = new QLabel(QStringLiteral("Screen name:"), this);
        m_user = new QLineEdit(defaults.username, this);
        form->addRow(m_userLabel, m_user);

        m_passwordLabel = new QLabel(QStringLiteral("Password:"), this);
        m_password = new QLineEdit(this);
        m_password->setEchoMode(QLineEdit::Password);
        m_password->setText(defaults.password);
        form->addRow(m_passwordLabel, m_password);

        m_savePassword = new QCheckBox(QStringLiteral("Save password on this computer"), this);
        m_savePassword->setChecked(defaults.savePassword);
        m_savePassword->setToolTip(
            QStringLiteral("Stores this password in the local WaffleHouse-Client settings file. "
                           "The saved value is not encrypted at rest."));
        m_savePassword->setEnabled(!m_password->text().isEmpty());
        connect(m_password, &QLineEdit::textChanged, m_savePassword,
                [this](const QString &text) {
                    m_savePassword->setEnabled(!text.isEmpty());
                    if (text.isEmpty()) m_savePassword->setChecked(false);
                });
        form->addRow(QString(), m_savePassword);

        m_secretNote = new QLabel(
            QStringLiteral(
                "Passwords are session-only unless Save password is selected. "
                "Saved passwords are stored in the local application settings and are not "
                "encrypted at rest. Leave the password blank to be prompted when you connect."),
            this);
        m_secretNote->setWordWrap(true);
        form->addRow(QString(), m_secretNote);

        m_realNameLabel = new QLabel(QStringLiteral("Real name:"), this);
        m_realName = new QLineEdit(defaults.realName, this);
        form->addRow(m_realNameLabel, m_realName);

        m_tls = new QCheckBox(QStringLiteral("Use TLS"), this);
        m_tls->setChecked(defaults.tls);
        form->addRow(QString(), m_tls);

        m_redirectHostLabel = new QLabel(QStringLiteral("Redirect host:"), this);
        m_redirectHost = new QLineEdit(defaults.redirectHost, this);
        m_redirectHost->setPlaceholderText(QStringLiteral("optional"));
        form->addRow(m_redirectHostLabel, m_redirectHost);

        m_redirectPortLabel = new QLabel(QStringLiteral("Redirect port:"), this);
        m_redirectPort = new QSpinBox(this);
        m_redirectPort->setRange(0, 65535);
        m_redirectPort->setSpecialValueText(QStringLiteral("server default"));
        m_redirectPort->setValue(defaults.redirectPort);
        form->addRow(m_redirectPortLabel, m_redirectPort);

        m_oscarDebugLabel = new QLabel(QStringLiteral("OSCAR debug / audit:"), this);
        m_oscarDebug = new QComboBox(this);
        m_oscarDebug->addItem(QStringLiteral("Off"), QStringLiteral("off"));
        m_oscarDebug->addItem(QStringLiteral("Login audit — decoded auth/bootstrap, secrets redacted"), QStringLiteral("login"));
        m_oscarDebug->addItem(QStringLiteral("Full wire trace — all FLAP/SNAC + login audit, secrets redacted"), QStringLiteral("full"));
        QString requestedOscarDebug = defaults.oscarDebugMode.trimmed().toCaseFolded();
        if ((requestedOscarDebug.isEmpty() || requestedOscarDebug == QStringLiteral("off")) && defaults.debug)
            requestedOscarDebug = QStringLiteral("full");
        const int oscarDebugIndex = m_oscarDebug->findData(requestedOscarDebug.isEmpty() ? QStringLiteral("off") : requestedOscarDebug);
        m_oscarDebug->setCurrentIndex(oscarDebugIndex >= 0 ? oscarDebugIndex : 0);
        form->addRow(m_oscarDebugLabel, m_oscarDebug);
        m_oscarDebugHelp = new QLabel(
            QStringLiteral("Login Audit shows authentication, redirects, advertised OSCAR families, rate/version negotiation, and decoded failure codes without exposing passwords, password hashes, challenge material, or auth cookies. Full Wire Trace adds every FLAP/SNAC frame with secret-bearing fields redacted."),
            this);
        m_oscarDebugHelp->setWordWrap(true);
        form->addRow(QString(), m_oscarDebugHelp);

        m_telnetTerminalLabel = new QLabel(QStringLiteral("Terminal type:"), this);
        m_telnetTerminal = new QLineEdit(
            defaults.telnetTerminalType.isEmpty()
                ? QStringLiteral("ANSI")
                : defaults.telnetTerminalType,
            this);
        form->addRow(m_telnetTerminalLabel, m_telnetTerminal);

        m_telnetColumnsLabel = new QLabel(QStringLiteral("BBS columns:"), this);
        m_telnetColumns = new QSpinBox(this);
        m_telnetColumns->setRange(20, 240);
        m_telnetColumns->setValue(std::clamp(defaults.telnetColumns, 20, 240));
        form->addRow(m_telnetColumnsLabel, m_telnetColumns);

        m_telnetRowsLabel = new QLabel(QStringLiteral("BBS rows:"), this);
        m_telnetRows = new QSpinBox(this);
        m_telnetRows->setRange(5, 100);
        m_telnetRows->setValue(std::clamp(defaults.telnetRows, 5, 100));
        form->addRow(m_telnetRowsLabel, m_telnetRows);

        m_telnetAutoFit = new QCheckBox(QStringLiteral("Auto-fit terminal font/window to exact BBS geometry"), this);
        m_telnetAutoFit->setChecked(defaults.telnetAutoFit);
        m_telnetAutoFit->setToolTip(QStringLiteral("Keeps the BBS terminal at the configured columns x rows and adjusts the GUI font to fit. CLI mode requests a matching terminal window size when supported."));
        form->addRow(QString(), m_telnetAutoFit);

        m_sshIdentityLabel = new QLabel(QStringLiteral("Identity file:"), this);
        m_sshIdentityRow = new QWidget(this);
        auto *sshIdentityLayout = new QHBoxLayout(m_sshIdentityRow);
        sshIdentityLayout->setContentsMargins(0, 0, 0, 0);
        sshIdentityLayout->setSpacing(6);
        m_sshIdentity = new QLineEdit(defaults.sshIdentityFile, m_sshIdentityRow);
        m_sshIdentity->setPlaceholderText(QStringLiteral("optional; OpenSSH config/agent still apply"));
        auto *sshIdentityBrowse = new QPushButton(QStringLiteral("Browse…"), m_sshIdentityRow);
        sshIdentityLayout->addWidget(m_sshIdentity, 1);
        sshIdentityLayout->addWidget(sshIdentityBrowse);
        connect(sshIdentityBrowse, &QPushButton::clicked, this, [this] {
            const QString start = m_sshIdentity->text().trimmed().isEmpty()
                ? QDir::homePath() + QStringLiteral("/.ssh")
                : QFileInfo(m_sshIdentity->text().trimmed()).absolutePath();
            const QString selected = QFileDialog::getOpenFileName(this, QStringLiteral("Choose SSH Identity File"), start);
            if (!selected.isEmpty()) m_sshIdentity->setText(selected);
        });
        form->addRow(m_sshIdentityLabel, m_sshIdentityRow);

        m_sshOptionsLabel = new QLabel(QStringLiteral("OpenSSH options:"), this);
        m_sshOptions = new QLineEdit(defaults.sshOptions.join(QStringLiteral("; ")), this);
        m_sshOptions->setPlaceholderText(QStringLiteral("ForwardAgent=yes; Compression=yes"));
        m_sshOptions->setToolTip(QStringLiteral("Optional OpenSSH -o entries separated by semicolons. Example: ForwardAgent=yes; StrictHostKeyChecking=accept-new"));
        form->addRow(m_sshOptionsLabel, m_sshOptions);

        m_sshRemoteCommandLabel = new QLabel(QStringLiteral("Remote command:"), this);
        m_sshRemoteCommand = new QLineEdit(defaults.sshRemoteCommand, this);
        m_sshRemoteCommand->setPlaceholderText(QStringLiteral("optional; blank opens an interactive shell"));
        form->addRow(m_sshRemoteCommandLabel, m_sshRemoteCommand);

        m_sshHelp = new QLabel(QStringLiteral("WaffleHouse uses your normal OpenSSH configuration, known_hosts and ssh-agent. The identity/options above are per-connection overrides; passwords and host-key prompts stay inside the embedded PTY terminal."), this);
        m_sshHelp->setWordWrap(true);
        form->addRow(QString(), m_sshHelp);

        m_sipProfileNameLabel = new QLabel(QStringLiteral("Account label:"), this);
        m_sipProfileName = new QLineEdit(defaults.sipProfileName, this);
        m_sipProfileName->setPlaceholderText(QStringLiteral("Office SIP / PBX / trunk"));
        form->addRow(m_sipProfileNameLabel, m_sipProfileName);

        m_sipDomainLabel = new QLabel(QStringLiteral("SIP domain:"), this);
        m_sipDomain = new QLineEdit(defaults.sipDomain.isEmpty() ? defaults.server : defaults.sipDomain, this);
        form->addRow(m_sipDomainLabel, m_sipDomain);

        m_sipRegistrarLabel = new QLabel(QStringLiteral("Registrar:"), this);
        m_sipRegistrar = new QLineEdit(defaults.sipRegistrar, this);
        m_sipRegistrar->setPlaceholderText(QStringLiteral("blank = sip:<domain>"));
        form->addRow(m_sipRegistrarLabel, m_sipRegistrar);

        m_sipAuthLabel = new QLabel(QStringLiteral("Auth username:"), this);
        m_sipAuth = new QLineEdit(defaults.sipAuthUsername, this);
        m_sipAuth->setPlaceholderText(QStringLiteral("blank = SIP username"));
        form->addRow(m_sipAuthLabel, m_sipAuth);

        m_sipDisplayNameLabel = new QLabel(QStringLiteral("Display name:"), this);
        m_sipDisplayName = new QLineEdit(defaults.sipDisplayName, this);
        form->addRow(m_sipDisplayNameLabel, m_sipDisplayName);

        m_sipOutboundProxyLabel = new QLabel(QStringLiteral("Outbound proxy:"), this);
        m_sipOutboundProxy = new QLineEdit(defaults.sipOutboundProxy, this);
        m_sipOutboundProxy->setPlaceholderText(QStringLiteral("optional"));
        form->addRow(m_sipOutboundProxyLabel, m_sipOutboundProxy);

        m_sipCallerIdDomainLabel = new QLabel(QStringLiteral("Caller-ID domain:"), this);
        m_sipCallerIdDomain = new QLineEdit(defaults.sipCallerIdDomain, this);
        m_sipCallerIdDomain->setPlaceholderText(QStringLiteral("optional"));
        form->addRow(m_sipCallerIdDomainLabel, m_sipCallerIdDomain);

        m_sipDialPrefixLabel = new QLabel(QStringLiteral("Dial prefix:"), this);
        m_sipDialPrefix = new QLineEdit(defaults.sipDialPrefix, this);
        m_sipDialPrefix->setPlaceholderText(QStringLiteral("optional"));
        form->addRow(m_sipDialPrefixLabel, m_sipDialPrefix);

        m_sipStunLabel = new QLabel(QStringLiteral("STUN server:"), this);
        m_sipStun = new QLineEdit(defaults.sipStunServer, this);
        m_sipStun->setPlaceholderText(QStringLiteral("optional"));
        form->addRow(m_sipStunLabel, m_sipStun);

        m_sipTransportLabel = new QLabel(QStringLiteral("Transport:"), this);
        m_sipTransport = new QComboBox(this);
        m_sipTransport->addItems({QStringLiteral("udp"), QStringLiteral("tcp"), QStringLiteral("tls")});
        m_sipTransport->setCurrentText(defaults.sipTransport.isEmpty() ? QStringLiteral("udp") : defaults.sipTransport.toCaseFolded());
        form->addRow(m_sipTransportLabel, m_sipTransport);

        m_sipIdentityLabel = new QLabel(QStringLiteral("Caller-ID identity:"), this);
        m_sipIdentity = new QComboBox(this);
        m_sipIdentity->addItems({QStringLiteral("from"), QStringLiteral("pai"), QStringLiteral("rpid"), QStringLiteral("from+pai")});
        m_sipIdentity->setCurrentText(defaults.sipIdentityMode.isEmpty() ? QStringLiteral("from") : defaults.sipIdentityMode.toCaseFolded());
        form->addRow(m_sipIdentityLabel, m_sipIdentity);

        m_sipCompatibilityLabel = new QLabel(QStringLiteral("SIP server type:"), this);
        m_sipCompatibility = new QComboBox(this);
        m_sipCompatibility->addItem(QStringLiteral("Auto / generic SIP (recommended)"), QStringLiteral("auto"));
        m_sipCompatibility->addItem(QStringLiteral("Asterisk PJSIP (chan_pjsip)"), QStringLiteral("standard"));
        m_sipCompatibility->addItem(QStringLiteral("Asterisk legacy SIP (chan_sip)"), QStringLiteral("asterisk-chan_sip"));
        const QString requestedSipCompatibility = defaults.sipCompatibility.isEmpty() ? QStringLiteral("auto") : defaults.sipCompatibility.toCaseFolded();
        const int requestedSipCompatibilityIndex = m_sipCompatibility->findData(requestedSipCompatibility);
        m_sipCompatibility->setCurrentIndex(requestedSipCompatibilityIndex >= 0 ? requestedSipCompatibilityIndex : 0);
        m_sipCompatibility->setToolTip(QStringLiteral("Choose the SIP driver used by the remote Asterisk server. This does not change WaffleHouse's client stack; WaffleHouse always uses PJSIP 2.17 internally."));
        form->addRow(m_sipCompatibilityLabel, m_sipCompatibility);
        auto *sipCompatibilityHelp = new QLabel(QStringLiteral("Asterisk server uses pjsip.conf / chan_pjsip? Choose PJSIP. Uses sip.conf / chan_sip? Choose legacy chan_sip. If you are unsure or the server is not Asterisk, leave Auto selected."), this);
        sipCompatibilityHelp->setWordWrap(true);
        form->addRow(QString(), sipCompatibilityHelp);

        m_sipLocalPortLabel = new QLabel(QStringLiteral("Local SIP port:"), this);
        m_sipLocalPort = new QSpinBox(this);
        m_sipLocalPort->setRange(1, 65535);
        m_sipLocalPort->setValue(defaults.sipLocalPort ? defaults.sipLocalPort : 5060);
        form->addRow(m_sipLocalPortLabel, m_sipLocalPort);

        m_sipExpiresLabel = new QLabel(QStringLiteral("Registration expiry:"), this);
        m_sipExpires = new QSpinBox(this);
        m_sipExpires->setRange(30, 86400);
        m_sipExpires->setSuffix(QStringLiteral(" sec"));
        m_sipExpires->setValue(defaults.sipRegistrationExpires ? static_cast<int>(defaults.sipRegistrationExpires) : 300);
        form->addRow(m_sipExpiresLabel, m_sipExpires);

        m_sipIce = new QCheckBox(QStringLiteral("Enable ICE"), this);
        m_sipIce->setChecked(defaults.sipUseIce);
        form->addRow(QString(), m_sipIce);
        m_sipSrtp = new QCheckBox(QStringLiteral("Enable SRTP"), this);
        m_sipSrtp->setChecked(defaults.sipEnableSrtp);
        form->addRow(QString(), m_sipSrtp);

        m_debug = new QCheckBox(QStringLiteral("Generic protocol debug logging"), this);
        m_debug->setChecked(defaults.debug);
        form->addRow(QString(), m_debug);

        auto *buttons = new QDialogButtonBox(
            QDialogButtonBox::Cancel | QDialogButtonBox::Ok, this);
        buttons->button(QDialogButtonBox::Ok)->setText(
            m_editing ? QStringLiteral("Save") : QStringLiteral("Add & Connect"));
        outer->addWidget(buttons);

        connect(buttons, &QDialogButtonBox::accepted, this, [this] {
            const ConnectionSettings candidate = settings();
            if (candidate.protocol == ConnectionSettings::Protocol::Unknown) {
                QMessageBox::warning(this,
                                     QStringLiteral("Select a protocol"),
                                     QStringLiteral("Choose one of the modules compiled into this WaffleHouse-Client build."));
                return;
            }
            if (candidate.protocol == ConnectionSettings::Protocol::Telnet
                || candidate.protocol == ConnectionSettings::Protocol::Nntp) {
                if (candidate.server.trimmed().isEmpty()) {
                    QMessageBox::warning(this, QStringLiteral("Missing information"), QStringLiteral("A server host is required."));
                    return;
                }
            } else if (candidate.protocol == ConnectionSettings::Protocol::Sip) {
                if (candidate.sipDomain.trimmed().isEmpty() || candidate.username.trimmed().isEmpty()) {
                    QMessageBox::warning(this, QStringLiteral("Missing information"), QStringLiteral("SIP domain and SIP username/extension are required."));
                    return;
                }
                try {
                    trunkmonkey::ProfileStore::validate(sipProfileFromConnectionSettings(candidate));
                } catch (const std::exception &e) {
                    QMessageBox::warning(this, QStringLiteral("Invalid SIP Account"), QString::fromLocal8Bit(e.what()));
                    return;
                }
            } else if (candidate.server.trimmed().isEmpty() || candidate.username.trimmed().isEmpty()) {
                QMessageBox::warning(this, QStringLiteral("Missing information"), QStringLiteral("Server and account/nickname are required."));
                return;
            }
            accept();
        });
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(m_protocol, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this] { updateFields(); });
        connect(m_oscarNetwork, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this] {
                    if (currentProtocol() != ConnectionSettings::Protocol::Oscar) return;
                    const QString profile = m_oscarNetwork->currentData().toString();
                    if (profile == QStringLiteral("nina")) {
                        const QString host = m_server->text().trimmed().toCaseFolded();
                        if (host.isEmpty() || host == QStringLiteral("login.oscar.nina.chat"))
                            m_server->setText(QStringLiteral("login.oscar.nina.chat"));
                        m_port->setValue(5190);
                    }
                });
        connect(m_tls, &QCheckBox::toggled, this, [this](bool enabled) {
            if (currentProtocol() == ConnectionSettings::Protocol::Irc) {
                if (enabled && m_port->value() == 6667) m_port->setValue(6697);
                else if (!enabled && m_port->value() == 6697) m_port->setValue(6667);
            } else if (currentProtocol() == ConnectionSettings::Protocol::Nntp) {
                if (enabled && m_port->value() == 119) m_port->setValue(563);
                else if (!enabled && m_port->value() == 563) m_port->setValue(119);
            }
        });

        updateFields();
    }

    ConnectionSettings settings() const
    {
        ConnectionSettings value;
        value.protocol = currentProtocol();
        value.password = m_password->text();
        value.savePassword = m_savePassword->isChecked() && !value.password.isEmpty();
        value.debug = currentProtocol() == ConnectionSettings::Protocol::Oscar ? false : m_debug->isChecked();
        value.oscarDebugMode = m_oscarDebug->currentData().toString();
        value.networkProfile = m_oscarNetwork->currentData().toString();
        if (value.networkProfile == QStringLiteral("nina")) {
            value.arsHost = QStringLiteral("ars.oscar.nina.chat");
            value.arsPort = 5190;
        } else {
            value.arsHost = m_original.arsHost;
            value.arsPort = m_original.arsPort;
        }
        // AIM profile is edited in the dedicated Profile UI, not in transport
        // settings. Preserve it when the account connection is edited.
        value.oscarProfile = m_original.oscarProfile;

        value.server = m_server->text().trimmed();
        value.port = static_cast<quint16>(m_port->value());
        value.username = m_user->text().trimmed();
        value.accountLabel = m_accountLabel->text().trimmed();
        value.realName = m_realName->text().trimmed();
        value.tls = m_tls->isChecked();
        // Buddy/contact lists belong to the saved connection and are managed
        // by their dedicated Accounts window; editing transport/login fields
        // must not silently erase them.
        value.ircBuddies = m_original.ircBuddies;
        value.sipContacts = m_original.sipContacts;
        value.redirectHost = m_redirectHost->text().trimmed();
        value.redirectPort = static_cast<quint16>(m_redirectPort->value());
        value.telnetTerminalType = m_telnetTerminal->text().trimmed();
        value.telnetColumns = m_telnetColumns->value();
        value.telnetRows = m_telnetRows->value();
        value.telnetAutoFit = m_telnetAutoFit->isChecked();
        value.sshIdentityFile = m_sshIdentity->text().trimmed();
        value.sshOptions.clear();
        for (const QString &rawOption : m_sshOptions->text().split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
            const QString option = rawOption.trimmed();
            if (!option.isEmpty()) value.sshOptions << option;
        }
        value.sshRemoteCommand = m_sshRemoteCommand->text().trimmed();
        value.sipProfileName = m_sipProfileName->text().trimmed();
        value.sipDomain = m_sipDomain->text().trimmed();
        value.sipRegistrar = m_sipRegistrar->text().trimmed();
        value.sipAuthUsername = m_sipAuth->text().trimmed();
        value.sipDisplayName = m_sipDisplayName->text().trimmed();
        value.sipOutboundProxy = m_sipOutboundProxy->text().trimmed();
        value.sipCallerIdDomain = m_sipCallerIdDomain->text().trimmed();
        value.sipDialPrefix = m_sipDialPrefix->text().trimmed();
        value.sipStunServer = m_sipStun->text().trimmed();
        value.sipTransport = m_sipTransport->currentText();
        value.sipIdentityMode = m_sipIdentity->currentText();
        value.sipCompatibility = m_sipCompatibility->currentData().toString();
        value.sipLocalPort = static_cast<quint16>(m_sipLocalPort->value());
        value.sipRegistrationExpires = static_cast<quint32>(m_sipExpires->value());
        value.sipUseIce = m_sipIce->isChecked();
        value.sipEnableSrtp = m_sipSrtp->isChecked();
        if (value.protocol == ConnectionSettings::Protocol::Sip) {
            value.server = value.sipDomain;
            value.port = value.sipLocalPort;
        }
        return value;
    }

private:
    ConnectionSettings::Protocol currentProtocol() const
    {
        return static_cast<ConnectionSettings::Protocol>(m_protocol->currentData().toInt());
    }

    void updateFields()
    {
        const auto protocol = currentProtocol();
        const bool unknown = protocol == ConnectionSettings::Protocol::Unknown;
        const bool irc = protocol == ConnectionSettings::Protocol::Irc;
        const bool oscar = protocol == ConnectionSettings::Protocol::Oscar;
        const bool telnet = protocol == ConnectionSettings::Protocol::Telnet;
        const bool sip = protocol == ConnectionSettings::Protocol::Sip;
        const bool xmpp = protocol == ConnectionSettings::Protocol::Xmpp;
        const bool ssh = protocol == ConnectionSettings::Protocol::Ssh;
        const bool nntp = protocol == ConnectionSettings::Protocol::Nntp;

        m_serverLabel->setVisible(!unknown && !sip);
        m_server->setVisible(!unknown && !sip);
        m_portLabel->setVisible(!unknown && !sip);
        m_port->setVisible(!unknown && !sip);
        m_userLabel->setVisible(!unknown);
        m_user->setVisible(!unknown);
        const bool labeledMessagingAccount = oscar || irc || xmpp || ssh || nntp;
        m_accountLabelLabel->setVisible(labeledMessagingAccount);
        m_accountLabel->setVisible(labeledMessagingAccount);

        m_passwordLabel->setVisible(!telnet && !unknown && !ssh);
        m_password->setVisible(!telnet && !unknown && !ssh);
        m_savePassword->setVisible(!telnet && !unknown && !ssh);
        m_secretNote->setVisible(!telnet && !unknown && !ssh);

        m_realNameLabel->setVisible(irc || nntp);
        m_realName->setVisible(irc || nntp);
        m_tls->setVisible(irc || xmpp || nntp);

        m_oscarNetworkLabel->setVisible(oscar);
        m_oscarNetwork->setVisible(oscar);
        m_redirectHostLabel->setVisible(oscar);
        m_redirectHost->setVisible(oscar);
        m_redirectPortLabel->setVisible(oscar);
        m_redirectPort->setVisible(oscar);
        m_oscarDebugLabel->setVisible(oscar);
        m_oscarDebug->setVisible(oscar);
        m_oscarDebugHelp->setVisible(oscar);
        m_debug->setVisible(!unknown && !oscar);

        const bool terminalProtocol = telnet || ssh;
        m_telnetTerminalLabel->setVisible(terminalProtocol);
        m_telnetTerminal->setVisible(terminalProtocol);
        m_telnetColumnsLabel->setVisible(terminalProtocol);
        m_telnetColumns->setVisible(terminalProtocol);
        m_telnetRowsLabel->setVisible(terminalProtocol);
        m_telnetRows->setVisible(terminalProtocol);
        m_telnetAutoFit->setVisible(terminalProtocol);

        m_sshIdentityLabel->setVisible(ssh);
        m_sshIdentityRow->setVisible(ssh);
        m_sshOptionsLabel->setVisible(ssh);
        m_sshOptions->setVisible(ssh);
        m_sshRemoteCommandLabel->setVisible(ssh);
        m_sshRemoteCommand->setVisible(ssh);
        m_sshHelp->setVisible(ssh);

        const QList<QWidget *> sipWidgets = {
            m_sipProfileNameLabel, m_sipProfileName, m_sipDomainLabel, m_sipDomain,
            m_sipRegistrarLabel, m_sipRegistrar, m_sipAuthLabel, m_sipAuth,
            m_sipDisplayNameLabel, m_sipDisplayName, m_sipOutboundProxyLabel, m_sipOutboundProxy,
            m_sipCallerIdDomainLabel, m_sipCallerIdDomain, m_sipDialPrefixLabel, m_sipDialPrefix,
            m_sipStunLabel, m_sipStun, m_sipTransportLabel, m_sipTransport,
            m_sipIdentityLabel, m_sipIdentity, m_sipCompatibilityLabel, m_sipCompatibility,
            m_sipLocalPortLabel, m_sipLocalPort,
            m_sipExpiresLabel, m_sipExpires, m_sipIce, m_sipSrtp};
        for (QWidget *widget : sipWidgets) widget->setVisible(sip);

        if (irc) {
            m_accountLabel->setPlaceholderText(QStringLiteral("Work IRC / EFnet / Libera"));
            m_userLabel->setText(QStringLiteral("Nickname:"));
            m_passwordLabel->setText(QStringLiteral("Server password:"));
            m_password->setPlaceholderText(QStringLiteral("optional"));
            if (m_port->value() == 5190 || m_port->value() == 23) {
                m_port->setValue(m_tls->isChecked() ? 6697 : 6667);
            }
        } else if (telnet) {
            m_userLabel->setText(QStringLiteral("Session label:"));
            m_user->setPlaceholderText(QStringLiteral("optional"));
            if (m_port->value() == 5190 || m_port->value() == 6667
                || m_port->value() == 6697) {
                m_port->setValue(23);
            }
        } else if (oscar) {
            m_accountLabel->setPlaceholderText(QStringLiteral("NINA / Waffle BBS / Legacy AIM"));
            m_userLabel->setText(QStringLiteral("Screen name:"));
            m_passwordLabel->setText(QStringLiteral("Password:"));
            m_password->setPlaceholderText(QString());
            m_user->setPlaceholderText(QString());
            if (m_port->value() == 6667 || m_port->value() == 6697 || m_port->value() == 23) m_port->setValue(5190);
        } else if (sip) {
            m_userLabel->setText(QStringLiteral("SIP username / extension:"));
            m_passwordLabel->setText(QStringLiteral("SIP password:"));
            m_user->setPlaceholderText(QStringLiteral("1001 / user / auth identity"));
            m_password->setPlaceholderText(QString());
            if (m_sipProfileName->text().trimmed().isEmpty() && !m_user->text().trimmed().isEmpty())
                m_sipProfileName->setPlaceholderText(QStringLiteral("SIP — %1").arg(m_user->text().trimmed()));
        } else if (xmpp) {
            m_accountLabel->setPlaceholderText(QStringLiteral("Jabber / Work XMPP / XMPP chat"));
            m_serverLabel->setText(QStringLiteral("XMPP host / domain:"));
            m_userLabel->setText(QStringLiteral("JID / username:"));
            m_passwordLabel->setText(QStringLiteral("Password:"));
            m_user->setPlaceholderText(QStringLiteral("user@example.org"));
            m_tls->setText(QStringLiteral("Use TLS / STARTTLS"));
            if (m_port->value() == 5190 || m_port->value() == 6667 || m_port->value() == 6697 || m_port->value() == 23 || m_port->value() == 5060) m_port->setValue(5222);
            if (!m_tls->isChecked()) m_tls->setChecked(true);
        } else if (ssh) {
            m_accountLabel->setPlaceholderText(QStringLiteral("Shell / Router / Server"));
            m_serverLabel->setText(QStringLiteral("SSH host:"));
            m_userLabel->setText(QStringLiteral("Username:"));
            m_user->setPlaceholderText(QStringLiteral("login name"));
            if (m_port->value() == 5190 || m_port->value() == 6667 || m_port->value() == 6697 || m_port->value() == 23 || m_port->value() == 5060 || m_port->value() == 5222) m_port->setValue(22);
        } else if (nntp) {
            m_accountLabel->setPlaceholderText(QStringLiteral("Usenet / News server"));
            m_serverLabel->setText(QStringLiteral("NNTP server:"));
            m_userLabel->setText(QStringLiteral("Username:"));
            m_passwordLabel->setText(QStringLiteral("Password:"));
            m_realNameLabel->setText(QStringLiteral("Posting identity / From:"));
            m_tls->setText(QStringLiteral("Use implicit TLS"));
            if (m_port->value() == 5190 || m_port->value() == 6667 || m_port->value() == 6697 || m_port->value() == 23 || m_port->value() == 5060 || m_port->value() == 5222 || m_port->value() == 22) m_port->setValue(m_tls->isChecked() ? 563 : 119);
        }
    }

    bool m_editing = false;
    ConnectionSettings m_original;
    QComboBox *m_protocol = nullptr;
    QLabel *m_accountLabelLabel = nullptr;
    QLineEdit *m_accountLabel = nullptr;
    QLabel *m_serverLabel = nullptr;
    QLineEdit *m_server = nullptr;
    QLabel *m_portLabel = nullptr;
    QSpinBox *m_port = nullptr;
    QLabel *m_userLabel = nullptr;
    QLineEdit *m_user = nullptr;
    QLabel *m_passwordLabel = nullptr;
    QLineEdit *m_password = nullptr;
    QCheckBox *m_savePassword = nullptr;
    QLabel *m_secretNote = nullptr;
    QLabel *m_realNameLabel = nullptr;
    QLineEdit *m_realName = nullptr;
    QCheckBox *m_tls = nullptr;
    QLabel *m_oscarNetworkLabel = nullptr;
    QComboBox *m_oscarNetwork = nullptr;
    QLabel *m_redirectHostLabel = nullptr;
    QLineEdit *m_redirectHost = nullptr;
    QLabel *m_redirectPortLabel = nullptr;
    QSpinBox *m_redirectPort = nullptr;
    QLabel *m_oscarDebugLabel = nullptr;
    QComboBox *m_oscarDebug = nullptr;
    QLabel *m_oscarDebugHelp = nullptr;
    QLabel *m_telnetTerminalLabel = nullptr;
    QLineEdit *m_telnetTerminal = nullptr;
    QLabel *m_telnetColumnsLabel = nullptr; QSpinBox *m_telnetColumns = nullptr;
    QLabel *m_telnetRowsLabel = nullptr; QSpinBox *m_telnetRows = nullptr;
    QCheckBox *m_telnetAutoFit = nullptr;
    QLabel *m_sshIdentityLabel = nullptr; QWidget *m_sshIdentityRow = nullptr; QLineEdit *m_sshIdentity = nullptr;
    QLabel *m_sshOptionsLabel = nullptr; QLineEdit *m_sshOptions = nullptr;
    QLabel *m_sshRemoteCommandLabel = nullptr; QLineEdit *m_sshRemoteCommand = nullptr;
    QLabel *m_sshHelp = nullptr;
    QLabel *m_sipProfileNameLabel = nullptr; QLineEdit *m_sipProfileName = nullptr;
    QLabel *m_sipDomainLabel = nullptr; QLineEdit *m_sipDomain = nullptr;
    QLabel *m_sipRegistrarLabel = nullptr; QLineEdit *m_sipRegistrar = nullptr;
    QLabel *m_sipAuthLabel = nullptr; QLineEdit *m_sipAuth = nullptr;
    QLabel *m_sipDisplayNameLabel = nullptr; QLineEdit *m_sipDisplayName = nullptr;
    QLabel *m_sipOutboundProxyLabel = nullptr; QLineEdit *m_sipOutboundProxy = nullptr;
    QLabel *m_sipCallerIdDomainLabel = nullptr; QLineEdit *m_sipCallerIdDomain = nullptr;
    QLabel *m_sipDialPrefixLabel = nullptr; QLineEdit *m_sipDialPrefix = nullptr;
    QLabel *m_sipStunLabel = nullptr; QLineEdit *m_sipStun = nullptr;
    QLabel *m_sipTransportLabel = nullptr; QComboBox *m_sipTransport = nullptr;
    QLabel *m_sipIdentityLabel = nullptr; QComboBox *m_sipIdentity = nullptr;
    QLabel *m_sipCompatibilityLabel = nullptr; QComboBox *m_sipCompatibility = nullptr;
    QLabel *m_sipLocalPortLabel = nullptr; QSpinBox *m_sipLocalPort = nullptr;
    QLabel *m_sipExpiresLabel = nullptr; QSpinBox *m_sipExpires = nullptr;
    QCheckBox *m_sipIce = nullptr; QCheckBox *m_sipSrtp = nullptr;
    QCheckBox *m_debug = nullptr;
};

QString statusWord(bool connected)
{
    return connected ? QStringLiteral("Online") : QStringLiteral("Offline");
}

void showPlainTextDialog(QWidget *parent, const QString &title, const QString &text)
{
    auto *dialog = new QDialog(parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(title);
    dialog->resize(680, 520);
    auto *layout = new QVBoxLayout(dialog);
    auto *view = new QPlainTextEdit(dialog);
    view->setReadOnly(true);
    view->setPlainText(text);
    layout->addWidget(view, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    layout->addWidget(buttons);
    dialog->show();
}

QString oscarFeatureCenterText(OscarBackend *oscar)
{
    if (!oscar) return QStringLiteral("No AIM/OSCAR backend is selected.");
    struct Family { quint16 id; const char *name; const char *use; };
    static const Family families[] = {
        {Oscar::FAM_OSERVICE, "Generic Service / BOS", "presence, idle, server notices, privacy flags"},
        {Oscar::FAM_LOCATE, "Locate", "profiles, away text, rich user info, directory, email lookup"},
        {Oscar::FAM_BUDDY, "Buddy", "presence watches, temporary buddies, reverse watcher list"},
        {Oscar::FAM_ICBM, "ICBM", "IM, typing, stored messages, rendezvous/voice"},
        {Oscar::FAM_ADVERT, "Advertising", "legacy infrastructure (detected only)"},
        {Oscar::FAM_INVITE, "Invite", "AIM service invitations by email"},
        {Oscar::FAM_ADMIN, "Account Administration", "account info, email/name/password, confirm/delete"},
        {Oscar::FAM_POPUP, "Popup", "legacy server popup infrastructure (detected only)"},
        {Oscar::FAM_PERMIT_DENY, "Permit/Deny", "allow/block/temporary allow privacy lists"},
        {Oscar::FAM_USER_LOOKUP, "User Lookup", "legacy user search service"},
        {Oscar::FAM_STATS, "Statistics", "legacy infrastructure (detected only)"},
        {Oscar::FAM_TRANSLATE, "Translate", "legacy infrastructure (detected only)"},
        {Oscar::FAM_CHATNAV, "Chat Navigation", "discover/create/join AIM chat rooms"},
        {Oscar::FAM_CHAT, "Chat", "AIM room messaging and membership"},
        {Oscar::FAM_ODIR, "Online Directory", "legacy directory service"},
        {Oscar::FAM_BART, "BART / Buddy Art", "buddy icon service (detected; transfer interoperability not yet implemented)"},
        {Oscar::FAM_FEEDBAG, "Feedbag / SSI", "persistent buddy list and authorization workflow"},
        {Oscar::FAM_ICQ, "ICQ extensions", "ICQ-specific features when server/client support them"},
        {Oscar::FAM_BUCP, "BUCP", "authentication service"},
        {Oscar::FAM_ALERT, "Alerts", "server alert infrastructure"},
        {Oscar::FAM_PLUGIN, "Plugins", "legacy plugin infrastructure (detected only)"},
        {Oscar::FAM_UNNAMED_24, "Family 0x0024", "server/client-specific"},
        {Oscar::FAM_MDIR, "MDIR", "modern directory service"},
        {Oscar::FAM_ARS, "ARS", "AOL rendezvous relay service"},
    };
    QStringList lines;
    lines << QStringLiteral("OSCAR Feature Center")
          << QStringLiteral("====================")
          << QStringLiteral("BOS-native features use the connected host's advertised foodgroups. Chat Navigation (0x000D) and Chat (0x000E) are redirect services requested on demand and may work even when BOS does not advertise those families.")
          << QStringLiteral("");
    for (const Family &family : families) {
        const bool redirectService = family.id == Oscar::FAM_CHATNAV || family.id == Oscar::FAM_CHAT;
        const QString availability = redirectService
            ? (oscar->supportsFamily(Oscar::FAM_OSERVICE) ? QStringLiteral("TRY") : QStringLiteral(" --"))
            : (oscar->supportsFamily(family.id) ? QStringLiteral("YES") : QStringLiteral(" --"));
        lines << QStringLiteral("[%1] 0x%2  %3")
                     .arg(availability)
                     .arg(family.id, 4, 16, QLatin1Char('0'))
                     .arg(QString::fromLatin1(family.name));
        lines << QStringLiteral("      %1").arg(QString::fromLatin1(family.use));
    }
    lines << QStringLiteral("")
          << QStringLiteral("Peer-specific rendezvous features are additionally gated by the target user's advertised capability UUIDs.")
          << QStringLiteral("Legacy AIM Talk/File Transfer/Direct IM/Buddy Icon capabilities are reported when detected; WaffleHouse does not claim legacy wire compatibility until those transports are implemented.");
    return lines.join(QLatin1Char('\n'));
}

} // namespace

MainWindow::MainWindow(const ConnectionSettings &defaults, QWidget *parent)
    : QMainWindow(parent),
      m_defaults(defaults)
{
    setWindowTitle(QStringLiteral("%1 %2").arg(appDisplayName(), appVersionString()));
    // Keep the account workspace compact while leaving plenty of room for
    // saved accounts and buddy/contact rows. Protocol launchers live in the
    // account creation workflow rather than occupying permanent main-window space.
    resize(720, 540);
    setMinimumSize(580, 430);

    m_sipController = new SipController(this);
    m_softphoneWindow = new SoftphoneWindow(m_sipController, nullptr);
    connect(m_softphoneWindow, &SoftphoneWindow::profileSaveRequested, this,
            [this](const QString &accountId, const trunkmonkey::SipProfile &profile, bool savePassword) {
                BackendState *state = stateById(accountId);
                if (!state || !state->backend
                    || state->backend->settings().protocol != ConnectionSettings::Protocol::Sip) {
                    return;
                }
                ConnectionSettings updated = state->backend->settings();
                applySipProfileToConnectionSettings(profile, updated);
                updated.savePassword = savePassword && !updated.password.isEmpty();
                state->backend->setConnectionSettings(updated);
                const ConnectionSettings applied = state->backend->settings();
                state->secretRequired = true;
                state->hasSessionSecret = !applied.password.isEmpty();
                updateConnectionItem(state);
                saveConnections();
                refreshBuddyList();
                appendActivity(state->backend, QStringLiteral("SIP account updated from Softphone Profile."));
            });

    m_networkToolsWindow = new NetworkToolsWindow(nullptr);
    buildUi();
    buildMenus();
    buildConnectionsWindow();
    m_transferWindow = new TransferWindow(nullptr);
    connect(m_transferWindow, &TransferWindow::cancelRequested,
            this, &MainWindow::cancelFileTransfer);
    connect(m_transferWindow, &TransferWindow::resumeRequested,
            this, &MainWindow::resumeFileTransfer);
    connect(m_transferWindow, &TransferWindow::clearRequested,
            this, &MainWindow::clearFileTransfer);
    connect(&m_directTransfers, &CpxDirectTransferManager::progress,
            this, &MainWindow::handleDirectProgress);
    connect(&m_directTransfers, &CpxDirectTransferManager::incomingFinished,
            this, &MainWindow::handleDirectIncomingFinished);
    connect(&m_directTransfers, &CpxDirectTransferManager::outgoingFinished,
            this, &MainWindow::handleDirectOutgoingFinished);
    connect(&m_directTransfers, &CpxDirectTransferManager::failed,
            this, &MainWindow::handleDirectFailure);
    buildTrayIcon();
    loadUiSettings();
    loadOptions();

    m_lastUserActivityMs = QDateTime::currentMSecsSinceEpoch();
    qApp->installEventFilter(this);
    m_presenceTimer = new QTimer(this);
    m_presenceTimer->setInterval(1000);
    connect(m_presenceTimer, &QTimer::timeout, this, &MainWindow::updateAutoPresence);
    m_presenceTimer->start();

    m_secureReady = m_secure.initialize(&m_secureError);
    if (m_secureReady) {
        QString roomError;
        if (!m_secureRooms.initialize(&roomError)) {
            m_secureReady = false;
            m_secureError = roomError;
        }
    }
    m_fileTransferTimer = new QTimer(this);
    m_fileTransferTimer->setInterval(100);
    connect(m_fileTransferTimer, &QTimer::timeout, this, &MainWindow::pumpFileTransfers);
    m_fileTransferTimer->start();
    loadConnections();
    m_sipController->initialize();
    connect(m_sipController, &SipController::accountsChanged, this, &MainWindow::refreshSoftphoneControls);
    connect(m_sipController, &SipController::accountStateChanged, this, [this](const QString &) { refreshBuddyList(); refreshSoftphoneControls(); });
    connect(m_sipController, &SipController::callsChanged, this, [this] { refreshBuddyList(); refreshSoftphoneControls(); });
    connect(m_sipController, &SipController::incomingCall, this, [this](const QString &accountId, int, const QString &) {
        m_sipController->setSelectedAccountId(accountId);
        refreshBuddyList(); refreshSoftphoneControls();
    });
    refreshSoftphoneControls();
    applyTheme();

    setWindowOpacity(m_buddyOpacity);
    if (m_connectionsWindow) {
        m_connectionsWindow->setWindowOpacity(m_connectionsOpacity);
    }

    updateActions();
    refreshBuddyList();

    if (m_connectionList && m_connectionList->count() == 0) {
        statusBar()->showMessage(
            QStringLiteral("No saved accounts. Click Add below the account list to create one."));
    } else if (m_connectionList) {
        statusBar()->showMessage(
            QStringLiteral("%1 saved connection(s) restored.")
                .arg(m_connectionList->count()));
    }

    if (!m_secureReady && !m_secureError.isEmpty()) {
        if (m_activity) {
            m_activity->appendPlainText(
                QStringLiteral("[security] Encrypted communications unavailable: %1").arg(m_secureError));
        }
    }
}

MainWindow::~MainWindow()
{
    m_quitting = true;
    if (qApp) qApp->removeEventFilter(this);

    const auto windows = m_windows.values();
    for (ChatWindow *window : windows) {
        if (window) {
            window->close();
        }
    }

    const auto states = m_states.values();
    for (BackendState *state : states) {
        if (state && state->backend) {
            QObject::disconnect(state->backend, nullptr, this, nullptr);
            state->backend->stop();
        }
    }

    for (BackendState *state : states) {
        delete state;
    }
    m_states.clear();

    delete m_transferWindow;
    m_transferWindow = nullptr;

    delete m_softphoneWindow;
    m_softphoneWindow = nullptr;

    delete m_networkToolsWindow;
    m_networkToolsWindow = nullptr;

    delete m_connectionsWindow;
    m_connectionsWindow = nullptr;
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (event) {
        switch (event->type()) {
        case QEvent::KeyPress:
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonDblClick:
        case QEvent::Wheel:
        case QEvent::TouchBegin:
        case QEvent::TabletPress:
            markUserActivity();
            break;
        default:
            break;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::markUserActivity()
{
    m_lastUserActivityMs = QDateTime::currentMSecsSinceEpoch();
    // AIM/OSCAR presence is server-native.  Activity only resets the native
    // OSERVICE idle counter; it must never clear a manually selected Away/AFK
    // state or invent an "auto-away" message on the client side.
    for (BackendState *state : m_states) {
        if (!state || !state->connected || !state->backend
            || state->backend->settings().protocol != ConnectionSettings::Protocol::Oscar) continue;
        if (state->autoPresenceState == QStringLiteral("IDLE-REPORTED")) {
            if (auto *oscar = qobject_cast<OscarBackend *>(state->backend)) oscar->setIdleSeconds(0);
            state->autoPresenceState.clear();
        }
    }
}

void MainWindow::updateAutoPresence()
{
    if (!m_options.autoPresenceEnabled || m_lastUserActivityMs <= 0) return;
    const quint32 inactiveSeconds = static_cast<quint32>(std::min<qint64>(
        UserActivity::idleMilliseconds(m_lastUserActivityMs) / 1000, 0xffffffffLL));

    for (BackendState *state : m_states) {
        if (!state || !state->connected || !state->backend
            || state->backend->settings().protocol != ConnectionSettings::Protocol::Oscar) continue;
        auto *oscar = qobject_cast<OscarBackend *>(state->backend);
        if (!oscar) continue;

        // Do not synthesize Away/AFK locally.  Report the actual workstation idle
        // duration through the native OSCAR 0x0001/0x0011 notification and let
        // the AIM server apply its own presence semantics.  Throttle reports to
        // avoid needless SNAC traffic while still keeping the server current.
        const quint32 previous = state->idleSeconds;
        const bool activeTransition = inactiveSeconds == 0 && previous != 0;
        const bool meaningfulAdvance = inactiveSeconds > previous + 14;
        if (activeTransition || meaningfulAdvance) {
            oscar->setIdleSeconds(inactiveSeconds);
            state->autoPresenceState = inactiveSeconds ? QStringLiteral("IDLE-REPORTED") : QString();
        }
    }
}

void MainWindow::requestClientVersion(BackendState *state, const QString &target)
{
    if (!state || !state->connected || !state->backend || target.trimmed().isEmpty()) return;
    const QString clean = target.trimmed();
    const QString key = state->profileId + QChar(0x1f) + clean.toCaseFolded();
    m_pendingVersionQueries.insert(key);
    const auto protocol = state->backend->settings().protocol;
    QTimer::singleShot(3500, this, [this, key, clean, protocol] {
        if (!m_pendingVersionQueries.remove(key)) return;
        const QString report = protocol == ConnectionSettings::Protocol::Oscar
            ? QStringLiteral("[version] %1: no %2 reply; peer may be an older WaffleHouse/CPX client or another AIM client (exact version unavailable)").arg(clean, appVersionString())
            : QStringLiteral("[version] %1: no CTCP VERSION reply received").arg(clean);
        statusBar()->showMessage(report, 7000);
    });
    if (auto *irc = qobject_cast<IrcBackend *>(state->backend)) {
        irc->requestClientVersion(clean);
        statusBar()->showMessage(QStringLiteral("Version query sent to %1 via IRC CTCP.").arg(clean), 4000);
        return;
    }
    if (auto *oscar = qobject_cast<OscarBackend *>(state->backend)) {
        oscar->requestClientVersion(clean);
        statusBar()->showMessage(QStringLiteral("WaffleHouse version query sent to %1 via AIM.").arg(clean), 4000);
        return;
    }
    m_pendingVersionQueries.remove(key);
    statusBar()->showMessage(QStringLiteral("/version is available for AIM/OSCAR and IRC peers."), 4000);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveConnections();
    saveUiSettings();

    if (!m_quitting && m_trayIcon && m_trayIcon->isVisible()) {
        hide();
#ifdef Q_OS_MACOS
        // Closing the last visible window can make macOS refresh/re-resolve the
        // status item. Re-apply the full-color non-template icon immediately so
        // the old 3.x white-on-white status icon cannot reappear while the app
        // continues running in the menu bar.
        QIcon statusIcon = appIcon();
        if (!statusIcon.isNull()) {
            statusIcon.setIsMask(false);
            m_trayIcon->setIcon(statusIcon);
        }
#endif
        event->ignore();
        if (!m_trayHintShown) {
            m_trayHintShown = true;
            m_trayIcon->showMessage(
                QStringLiteral("%1 is still running").arg(appDisplayName()),
                QStringLiteral("Use the tray icon to reopen WaffleHouse-Client or quit %1.").arg(appDisplayName()),
                QSystemTrayIcon::Information,
                3500);
        }
        return;
    }

    if (!m_quitting) {
        m_quitting = true;
        QApplication::quit();
    }
    QMainWindow::closeEvent(event);
}

void MainWindow::buildUi()
{
    auto *central = new QWidget(this);
    central->setObjectName(QStringLiteral("ModernRoot"));

    auto *outer = new QVBoxLayout(central);
    outer->setContentsMargins(16, 14, 16, 14);
    outer->setSpacing(10);

    auto *topBar = new QFrame(central);
    topBar->setObjectName(QStringLiteral("TopBar"));
    auto *top = new QHBoxLayout(topBar);
    top->setContentsMargins(0, 0, 0, 0);
    top->setSpacing(10);

    auto *title = new QLabel(QStringLiteral("%1 %2").arg(appDisplayName(), appVersionString()), topBar);
    title->setObjectName(QStringLiteral("PageTitle"));
    top->addWidget(title, 1);
    outer->addWidget(topBar);

    auto *accountCard = new QFrame(central);
    accountCard->setObjectName(QStringLiteral("Card"));
    auto *accountLayout = new QVBoxLayout(accountCard);
    accountLayout->setContentsMargins(12, 12, 12, 12);
    accountLayout->setSpacing(9);

    auto *accountTitle = new QLabel(QStringLiteral("Accounts"), accountCard);
    accountTitle->setObjectName(QStringLiteral("CardTitle"));
    accountLayout->addWidget(accountTitle);

    m_buddyTree = new QTreeWidget(accountCard);
    m_buddyTree->setHeaderLabels({QStringLiteral("Account/Buddy List"), QStringLiteral("Protocol / Status")});
    m_buddyTree->setRootIsDecorated(true);
    m_buddyTree->setAlternatingRowColors(false);
    m_buddyTree->setUniformRowHeights(true);
    m_buddyTree->setAnimated(true);
    m_buddyTree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_buddyTree->setIndentation(18);
    accountLayout->addWidget(m_buddyTree, 1);

    auto *accountActions = new QHBoxLayout;
    accountActions->setSpacing(8);
    m_mainAddAccountButton = new QPushButton(QStringLiteral("Add"), accountCard);
    m_mainRemoveAccountButton = new QPushButton(QStringLiteral("Remove"), accountCard);
    m_mainEditAccountButton = new QPushButton(QStringLiteral("Edit"), accountCard);
    m_mainConnectToggleButton = new QPushButton(QStringLiteral("Connect / Disconnect"), accountCard);
    m_mainConnectToggleButton->setProperty("role", "primary");
    accountActions->addWidget(m_mainAddAccountButton);
    accountActions->addWidget(m_mainRemoveAccountButton);
    accountActions->addWidget(m_mainEditAccountButton);
    accountActions->addStretch(1);
    accountActions->addWidget(m_mainConnectToggleButton);
    accountLayout->addLayout(accountActions);

    outer->addWidget(accountCard, 1);
    setCentralWidget(central);
    statusBar()->showMessage(QStringLiteral("%1 %2 ready").arg(appDisplayName(), appVersionString()));

    connect(m_mainAddAccountButton, &QPushButton::clicked, this,
            [this] { openConnectionDialog(m_defaults, nullptr); });
    connect(m_mainRemoveAccountButton, &QPushButton::clicked, this, &MainWindow::deleteSelected);
    connect(m_mainEditAccountButton, &QPushButton::clicked, this, &MainWindow::editSelected);
    connect(m_mainConnectToggleButton, &QPushButton::clicked, this, &MainWindow::toggleSelectedConnection);

    connect(m_buddyTree, &QTreeWidget::currentItemChanged,
            this, [this](QTreeWidgetItem *current) {
                if (BackendState *state = stateFromBuddyItem(current)) {
                    if (m_connectionList && state->connectionItem
                        && m_connectionList->currentItem() != state->connectionItem) {
                        m_connectionList->setCurrentItem(state->connectionItem);
                    }
                }
                updateActions();
            });

    connect(m_buddyTree, &QTreeWidget::customContextMenuRequested,
            this, [this](const QPoint &pos) {
                QTreeWidgetItem *item = m_buddyTree->itemAt(pos);
                if (!item) return;
                BackendState *state = stateFromBuddyItem(item);
                if (!state) return;
                m_buddyTree->setCurrentItem(item);
                const QPoint globalPos = m_buddyTree->viewport()->mapToGlobal(pos);
                if (item->parent()) {
                    const auto protocol = state->backend->settings().protocol;
                    if (protocol != ConnectionSettings::Protocol::Oscar
                        && protocol != ConnectionSettings::Protocol::Irc
                        && protocol != ConnectionSettings::Protocol::Xmpp) return;
                    const QString buddy = item->data(0, Qt::UserRole + 1).toString().trimmed();
                    if (!buddy.isEmpty()) showBuddyContextMenu(state, buddy, globalPos);
                    return;
                }
                showAccountContextMenu(state, globalPos);
            });

    connect(m_buddyTree, &QTreeWidget::itemDoubleClicked,
            this, [this](QTreeWidgetItem *item, int) {
                BackendState *state = stateFromBuddyItem(item);
                if (!state || !state->backend) return;
                selectState(state);
                if (state->backend->settings().protocol == ConnectionSettings::Protocol::Sip) {
                    m_sipController->setSelectedAccountId(state->backend->id());
                    if (item && item->parent()) m_softphoneWindow->showAndRaise();
                    return;
                }
                if (item && item->parent() && state->connected) {
                    const QString buddy = item->data(0, Qt::UserRole + 1).toString();
                    if (!buddy.isEmpty()) ensureConversationWindow(state->backend, QStringLiteral("im"), buddy, true);
                }
            });
}

void MainWindow::buildMenus()
{
    // Keep the menu bar owned by the WaffleHouse-Client window instead of
    // exporting it to a desktop/global menu. This keeps Linux and FreeBSD
    // behavior consistent.
    QMenuBar *bar = menuBar();
    bar->setNativeMenuBar(false);
    bar->setVisible(true);

    // The old top-level Connection menu is intentionally gone. Its account
    // lifecycle actions live under Accounts -> Account Management.
    m_addConnectionAction = new QAction(QStringLiteral("&Add…"), this);
    m_importBbsAction = new QAction(QStringLiteral("Import &BBS List…"), this);
    m_editConnectionAction = new QAction(QStringLiteral("&Edit Selected…"), this);
    m_deleteConnectionAction = new QAction(QStringLiteral("&Delete Selected"), this);
    m_connectAction = new QAction(QStringLiteral("&Connect Selected"), this);
    m_disconnectAction = new QAction(QStringLiteral("&Disconnect Selected"), this);
    m_showConnectionsAction = new QAction(QStringLiteral("Show &Connections/Accounts Window"), this);
#ifdef Q_OS_MACOS
    m_quitAction = new QAction(
        QStringLiteral("&Quit %1").arg(appDisplayName()), this);
#else
    m_quitAction = new QAction(QStringLiteral("E&xit"), this);
#endif
    m_quitAction->setShortcut(QKeySequence::Quit);
    m_quitAction->setMenuRole(QAction::QuitRole);
    m_quitAction->setStatusTip(QStringLiteral("Exit WaffleHouse-Client cleanly"));

    // 5.0r3: expose application shutdown in a conventional, easy-to-find
    // location.  Closing the main window can intentionally minimize to the
    // tray, so File -> Exit/Quit must always perform a real application exit.
    QMenu *fileMenu = bar->addMenu(QStringLiteral("&File"));
    fileMenu->addAction(m_quitAction);

    m_accountsMenu = bar->addMenu(QStringLiteral("&Accounts"));
    connect(m_accountsMenu, &QMenu::aboutToShow, this, &MainWindow::rebuildAccountsMenu);

    QMenu *viewMenu = bar->addMenu(QStringLiteral("&View"));
    QMenu *themeMenu = viewMenu->addMenu(QStringLiteral("&Theme"));
    auto *themeGroup = new QActionGroup(themeMenu);
    themeGroup->setExclusive(true);

    struct ThemeEntry { const char *label; const char *id; };
    static constexpr ThemeEntry themeEntries[] = {
        {"System", "system"}, {"Hacker", "hacker"}, {"Matrix", "matrix"},
        {"Phosphor", "phosphor"}, {"Midnight", "midnight"}, {"Amber", "amber"},
        {"Ice", "ice"}, {"Classic Light", "classic-light"}, {"Solarized", "solarized"},
        {"Solarized Dark", "solarized-dark"}, {"Dracula", "dracula"}, {"Nord", "nord"},
        {"Cyberpunk", "cyberpunk"}, {"Blood Moon", "blood-moon"}, {"Ocean", "ocean"},
        {"Retro Blue", "retro-blue"}, {"Monochrome", "monochrome"},
        {"Blue Box", "blue-box"}, {"Red Box", "red-box"}, {"Beige Box", "beige-box"},
        {"2600", "2600"}, {"WarGames", "wargames"}, {"CRT Green", "crt-green"},
        {"VT220", "vt220"}, {"Cobalt", "cobalt"}, {"Vaporwave", "vaporwave"},
        {"Stealth", "stealth"}, {"Synthwave", "synthwave"}, {"C64", "c64"},
        {"DOS", "dos"}, {"Waffle Iron", "waffle-iron"}, {"Ghostline", "ghostline"},
        {"Hot Dog Stand", "hot-dog-stand"}, {"Neon Miami", "neon-miami"},
    };

    for (const ThemeEntry &entry : themeEntries) {
        QAction *action = themeMenu->addAction(QString::fromLatin1(entry.label));
        action->setCheckable(true);
        action->setData(QString::fromLatin1(entry.id));
        themeGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, action] {
            m_options.theme = action->data().toString();
            saveOptions();
            applyTheme();
            statusBar()->showMessage(
                QStringLiteral("Theme changed to %1.").arg(action->text()), 2500);
        });
    }
    connect(themeMenu, &QMenu::aboutToShow, this, [this, themeMenu] {
        for (QAction *action : themeMenu->actions()) {
            action->setChecked(action->data().toString() == m_options.theme);
        }
    });

    viewMenu->addSeparator();
    m_buddyTransparencyAction =
        viewMenu->addAction(QStringLiteral("Main Window &Transparency…"));
    m_connectionsTransparencyAction =
        viewMenu->addAction(QStringLiteral("Connections/Accounts Window T&ransparency…"));

    QMenu *toolsMenu = bar->addMenu(QStringLiteral("&Tools"));
    m_phoneAction = toolsMenu->addAction(QStringLiteral("Open &Softphone…"));
    m_phoneAction->setVisible(BuildFeatures::Sip);
    m_importBbsAction->setText(QStringLiteral("Import &BBS List…"));
    m_importBbsAction->setVisible(BuildFeatures::Telnet);
    toolsMenu->addAction(m_importBbsAction);
    m_transferWindowAction = toolsMenu->addAction(QStringLiteral("File Transfer &Log / Activity…"));
    QAction *networkToolsAction = toolsMenu->addAction(QStringLiteral("Network &Tools…"));
    networkToolsAction->setVisible(BuildFeatures::Gopher || BuildFeatures::Gemini || BuildFeatures::Mosh);
    connect(networkToolsAction, &QAction::triggered, this, [this] { if (m_networkToolsWindow) m_networkToolsWindow->showAndRaise(); });
    toolsMenu->addSeparator();
    QAction *contactsAction = toolsMenu->addAction(QStringLiteral("Unified &Contacts…"));
    QAction *historyAction = toolsMenu->addAction(QStringLiteral("Search &History…"));
    QAction *capabilitiesAction = toolsMenu->addAction(QStringLiteral("Client &Capabilities…"));
    QAction *oscarAuditAction = toolsMenu->addAction(QStringLiteral("View OSCAR &Audit Log…"));
    oscarAuditAction->setVisible(BuildFeatures::Oscar);
    toolsMenu->addSeparator();
    m_changePasswordAction = toolsMenu->addAction(QStringLiteral("Change AIM &Password…"));
    m_fingerprintAction = toolsMenu->addAction(QStringLiteral("Secure Identity &Fingerprint…"));
    toolsMenu->addSeparator();
    m_rawAction = toolsMenu->addAction(QStringLiteral("Send &Raw Protocol Command…"));
    toolsMenu->addSeparator();
    m_optionsAction = toolsMenu->addAction(QStringLiteral("&Options…"));
    m_optionsAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+,")));
    m_optionsAction->setStatusTip(
        QStringLiteral("Open application options, themes, and security settings"));

    QMenu *helpMenu = bar->addMenu(QStringLiteral("&Help"));
    QAction *helpAction = helpMenu->addAction(QStringLiteral("%1 &Help…").arg(appDisplayName()));
    QAction *runtimeEnvironmentAction = helpMenu->addAction(QStringLiteral("&Runtime Environment…"));
    helpMenu->addSeparator();
    QAction *aboutAction = helpMenu->addAction(QStringLiteral("&About %1").arg(appDisplayName()));

    connect(m_addConnectionAction, &QAction::triggered,
            this, [this] { openConnectionDialog(m_defaults, nullptr); });
    connect(m_importBbsAction, &QAction::triggered, this, [this] { importBbsList(); });
    connect(m_editConnectionAction, &QAction::triggered, this, &MainWindow::editSelected);
    connect(m_deleteConnectionAction, &QAction::triggered, this, &MainWindow::deleteSelected);
    connect(m_connectAction, &QAction::triggered, this, &MainWindow::connectSelected);
    connect(m_disconnectAction, &QAction::triggered, this, &MainWindow::disconnectSelected);
    connect(m_showConnectionsAction, &QAction::triggered, this, &MainWindow::showConnectionsWindow);
    connect(m_phoneAction, &QAction::triggered,
            m_softphoneWindow, &SoftphoneWindow::showAndRaise);
    connect(contactsAction, &QAction::triggered, this, &MainWindow::showUnifiedContacts);
    connect(historyAction, &QAction::triggered, this, &MainWindow::showHistory);
    connect(capabilitiesAction, &QAction::triggered, this, &MainWindow::showClientCapabilities);
    connect(oscarAuditAction, &QAction::triggered, this, [this] { showOscarAuditLog(); });
    connect(m_quitAction, &QAction::triggered, this, &MainWindow::quitApplication);
    connect(m_changePasswordAction, &QAction::triggered, this, &MainWindow::changePassword);
    connect(m_fingerprintAction, &QAction::triggered, this, &MainWindow::showSelectedFingerprint);
    connect(m_transferWindowAction, &QAction::triggered, this, &MainWindow::showTransferWindow);
    connect(m_buddyTransparencyAction, &QAction::triggered, this, &MainWindow::setBuddyTransparency);
    connect(m_connectionsTransparencyAction, &QAction::triggered, this, &MainWindow::setConnectionsTransparency);
    connect(m_rawAction, &QAction::triggered, this, &MainWindow::rawProtocolCommand);
    connect(m_optionsAction, &QAction::triggered, this, &MainWindow::showOptionsDialog);
    connect(helpAction, &QAction::triggered, this, &MainWindow::showHelpDialog);
    connect(runtimeEnvironmentAction, &QAction::triggered, this, [this] {
        const RuntimeEnvironment info = RuntimeEnvironment::detect();
        showPlainTextDialog(
            this,
            QStringLiteral("Runtime Environment — %1").arg(appDisplayName()),
            QStringLiteral("OS: %1\nMode: %2\nSession: %3\nDesktop/WM: %4\nTerminal: %5")
                .arg(info.osName, info.mode, info.sessionType,
                     info.desktop.isEmpty() ? QStringLiteral("not detected") : info.desktop,
                     info.terminal.isEmpty() ? QStringLiteral("not detected") : info.terminal));
    });

    connect(aboutAction, &QAction::triggered, this, [this] {
        const QString aboutHtml = QStringLiteral(
                "<pre>%1</pre>"
                "<b>%2 — Version %3</b><br><br>"
                "A modular C++/Qt communications client built from the WaffleHouse-Client 5.1r4 codebase.<br><br>"
                "Core modules include AIM/OSCAR, IRC, Telnet/BBS, SIP, XMPP/Jabber, SSH and NNTP/Usenet; optional tools include Gopher, Gemini, Mosh and media.<br><br>"
                "%2 retains CPX3-compatible encrypted private messages where supported.")
                .arg(appAsciiLogo().toHtmlEscaped(), appDisplayName(), appVersionString());
        QMessageBox::about(this, QStringLiteral("About %1").arg(appDisplayName()), aboutHtml);
    });
}

QString MainWindow::accountMenuLabel(BackendState *state) const
{
    if (!state || !state->backend) return QStringLiteral("Unknown account");
    const ConnectionSettings &cfg = state->backend->settings();
    QString name;
    if (cfg.protocol == ConnectionSettings::Protocol::Sip) {
        name = cfg.sipProfileName.trimmed();
        if (name.isEmpty() && !cfg.username.trimmed().isEmpty()) {
            const QString domain = cfg.sipDomain.trimmed().isEmpty() ? cfg.server.trimmed() : cfg.sipDomain.trimmed();
            name = domain.isEmpty() ? cfg.username.trimmed()
                                    : QStringLiteral("%1@%2").arg(cfg.username.trimmed(), domain);
        }
    } else {
        if (cfg.protocol == ConnectionSettings::Protocol::Oscar
            || cfg.protocol == ConnectionSettings::Protocol::Irc
            || cfg.protocol == ConnectionSettings::Protocol::Xmpp
            || cfg.protocol == ConnectionSettings::Protocol::Ssh
            || cfg.protocol == ConnectionSettings::Protocol::Nntp) {
            name = cfg.accountLabel.trimmed();
        }
        if (name.isEmpty()) name = state->identity.trimmed();
        if (name.isEmpty()) name = cfg.username.trimmed();
        if (name.isEmpty()) name = cfg.server.trimmed();
    }
    if (name.isEmpty()) name = state->backend->protocolName();
    return QStringLiteral("%1 / %2").arg(name, state->backend->protocolName());
}


void MainWindow::showAccountContextMenu(BackendState *state, const QPoint &globalPos)
{
    if (!state || !state->backend) return;
    selectState(state);

    const QString backendId = state->backend->id();
    const auto protocol = state->backend->settings().protocol;
    QMenu menu(this);

    QAction *toggle = menu.addAction(
        state->connected || state->connecting
            ? QStringLiteral("Disconnect")
            : (protocol == ConnectionSettings::Protocol::Sip
                   ? QStringLiteral("Connect / Register")
                   : QStringLiteral("Connect")));
    connect(toggle, &QAction::triggered, this, [this, backendId] {
        if (BackendState *target = stateById(backendId)) {
            selectState(target);
            if (target->connected || target->connecting) disconnectSelected();
            else connectSelected();
        }
    });

    if (protocol == ConnectionSettings::Protocol::Oscar
        || protocol == ConnectionSettings::Protocol::Irc
        || protocol == ConnectionSettings::Protocol::Xmpp) {
        menu.addSeparator();

        QAction *startIm = menu.addAction(QStringLiteral("Start IM…"));
        startIm->setEnabled(state->connected && (protocol != ConnectionSettings::Protocol::Oscar
            || (qobject_cast<OscarBackend *>(state->backend) && qobject_cast<OscarBackend *>(state->backend)->supportsFamily(Oscar::FAM_ICBM))));
        connect(startIm, &QAction::triggered, this, [this, backendId] {
            if (BackendState *target = stateById(backendId)) {
                selectState(target);
                openMessagingDialog(target, QString(), false);
            }
        });

        QAction *joinChat = menu.addAction(
            protocol == ConnectionSettings::Protocol::Irc
                ? QStringLiteral("Join IRC Channel…")
                : protocol == ConnectionSettings::Protocol::Xmpp
                    ? QStringLiteral("Join XMPP MUC…")
                    : QStringLiteral("Join AIM Chat…"));
        // OSCAR ChatNav (0x000D) and Chat (0x000E) are redirect services.
        // They are commonly absent from the BOS HostOnline family list even when
        // the server can provide them via OSERVICE SERVICE_REQUEST (01/04).  Do
        // not gray Join Chat based on supportsFamily(), which intentionally
        // reflects only families advertised by the current BOS connection.
        // The backend requests ChatNav/Chat on demand and reports a protocol
        // error if that particular OSCAR server declines the service redirect.
        joinChat->setEnabled(state->connected && (protocol != ConnectionSettings::Protocol::Oscar
            || qobject_cast<OscarBackend *>(state->backend) != nullptr));
        connect(joinChat, &QAction::triggered, this, [this, backendId] {
            if (BackendState *target = stateById(backendId)) {
                selectState(target);
                openMessagingDialog(target, QString(), true);
            }
        });

        QAction *buddies = menu.addAction(QStringLiteral("Add / Remove Buddies…"));
        buddies->setEnabled(state->connected && (protocol != ConnectionSettings::Protocol::Oscar
            || (qobject_cast<OscarBackend *>(state->backend)
                && (qobject_cast<OscarBackend *>(state->backend)->supportsFamily(Oscar::FAM_FEEDBAG)
                    || qobject_cast<OscarBackend *>(state->backend)->supportsFamily(Oscar::FAM_BUDDY)))));
        connect(buddies, &QAction::triggered, this, [this, backendId] {
            if (BackendState *target = stateById(backendId)) {
                selectState(target);
                openBuddyManager(target);
            }
        });

        menu.addSeparator();
        if (protocol == ConnectionSettings::Protocol::Oscar) {
            auto *oscar = qobject_cast<OscarBackend *>(state->backend);
            const auto has = [state, oscar](quint16 family) {
                return state && state->connected && oscar && oscar->supportsFamily(family);
            };
            const auto askTarget = [this](const QString &title, const QString &label) -> QString {
                bool ok = false;
                const QString value = QInputDialog::getText(this, title, label, QLineEdit::Normal,
                                                            QString(), &ok).trimmed();
                return ok ? value : QString();
            };

            QMenu *presence = menu.addMenu(QStringLiteral("OSCAR Presence / Profile"));
            presence->setEnabled(state->connected);
            QAction *away = presence->addAction(QStringLiteral("Set Away Message…"));
            away->setEnabled(has(Oscar::FAM_LOCATE));
            connect(away, &QAction::triggered, this, [this, oscar] {
                bool ok = false;
                const QString text = QInputDialog::getMultiLineText(this, QStringLiteral("AIM Away Message"),
                                                                    QStringLiteral("Away message:"), QString(), &ok);
                if (ok && oscar) oscar->setAwayMessage(text);
            });
            QAction *afk = presence->addAction(QStringLiteral("Set AFK Message…"));
            afk->setEnabled(has(Oscar::FAM_LOCATE));
            connect(afk, &QAction::triggered, this, [this, oscar] {
                bool ok = false;
                const QString text = QInputDialog::getMultiLineText(this, QStringLiteral("AIM AFK Message"),
                                                                    QStringLiteral("AFK message:"), QString(), &ok);
                if (ok && oscar) oscar->setAfkMessage(text);
            });
            QAction *available = presence->addAction(QStringLiteral("Return Available / Clear Away"));
            available->setEnabled(has(Oscar::FAM_LOCATE));
            connect(available, &QAction::triggered, this, [oscar] { if (oscar) oscar->setBack(); });
            QAction *idle = presence->addAction(QStringLiteral("Set Idle Seconds…"));
            idle->setEnabled(has(Oscar::FAM_OSERVICE));
            connect(idle, &QAction::triggered, this, [this, oscar] {
                bool ok = false;
                const int seconds = QInputDialog::getInt(this, QStringLiteral("AIM Idle Status"),
                                                         QStringLiteral("Idle seconds (0 clears idle):"), 0, 0, 864000, 1, &ok);
                if (ok && oscar) oscar->setIdleSeconds(static_cast<quint32>(seconds));
            });
            presence->addSeparator();
            QAction *profile = presence->addAction(QStringLiteral("Edit AIM Profile…"));
            profile->setEnabled(has(Oscar::FAM_LOCATE));
            connect(profile, &QAction::triggered, this, [this, backendId] {
                if (BackendState *target = stateById(backendId)) { selectState(target); editAimProfile(target); }
            });
            QAction *viewOwnProfile = presence->addAction(QStringLiteral("View My AIM Profile / User Info…"));
            viewOwnProfile->setEnabled(has(Oscar::FAM_LOCATE));
            connect(viewOwnProfile, &QAction::triggered, this, [this, backendId] {
                if (BackendState *target = stateById(backendId); target && target->backend)
                    showAimUserInfo(target, target->backend->settings().username, true);
            });
            QAction *viewDir = presence->addAction(QStringLiteral("View My AIM Directory Info…"));
            viewDir->setEnabled(has(Oscar::FAM_LOCATE));
            connect(viewDir, &QAction::triggered, this, [oscar, state] {
                if (oscar && state && state->backend) oscar->requestDirectoryInfo(state->backend->settings().username);
            });
            QAction *editDir = presence->addAction(QStringLiteral("Edit My AIM Directory Info…"));
            editDir->setEnabled(has(Oscar::FAM_LOCATE));
            connect(editDir, &QAction::triggered, this, [this, oscar] {
                if (!oscar) return;
                QDialog dialog(this);
                dialog.setWindowTitle(QStringLiteral("Edit AIM Directory Information"));
                auto *layout = new QVBoxLayout(&dialog);
                auto *form = new QFormLayout;
                QHash<QString, QLineEdit *> edits;
                const QList<QPair<QString, QString>> fields = {
                    {QStringLiteral("firstName"), QStringLiteral("First name")},
                    {QStringLiteral("lastName"), QStringLiteral("Last name")},
                    {QStringLiteral("middleName"), QStringLiteral("Middle name")},
                    {QStringLiteral("maidenName"), QStringLiteral("Maiden name")},
                    {QStringLiteral("nickname"), QStringLiteral("Nickname")},
                    {QStringLiteral("street"), QStringLiteral("Street")},
                    {QStringLiteral("city"), QStringLiteral("City")},
                    {QStringLiteral("state"), QStringLiteral("State / region")},
                    {QStringLiteral("zip"), QStringLiteral("ZIP / postal code")},
                    {QStringLiteral("country"), QStringLiteral("Country")},
                };
                for (const auto &field : fields) {
                    auto *edit = new QLineEdit(&dialog); edits.insert(field.first, edit); form->addRow(field.second + QLatin1Char(':'), edit);
                }
                layout->addLayout(form);
                auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
                connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
                connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
                layout->addWidget(buttons);
                if (dialog.exec() == QDialog::Accepted) {
                    QVariantMap values; for (auto it = edits.cbegin(); it != edits.cend(); ++it) values.insert(it.key(), it.value()->text());
                    oscar->setDirectoryInfo(values);
                }
            });

            QMenu *privacy = menu.addMenu(QStringLiteral("OSCAR Privacy / Authorization"));
            privacy->setEnabled(state->connected);
            QAction *watchers = privacy->addAction(QStringLiteral("Who Has Me Listed? / Watcher List…"));
            watchers->setEnabled(has(Oscar::FAM_BUDDY));
            connect(watchers, &QAction::triggered, this, [oscar] { if (oscar) oscar->requestWatcherList(); });
            privacy->addSeparator();
            QAction *permit = privacy->addAction(QStringLiteral("Permit / Allow User…"));
            permit->setEnabled(has(Oscar::FAM_PERMIT_DENY));
            connect(permit, &QAction::triggered, this, [oscar, askTarget] { const QString v=askTarget(QStringLiteral("AIM Permit"), QStringLiteral("Screen name:")); if (!v.isEmpty() && oscar) oscar->addPermit(v); });
            QAction *unpermit = privacy->addAction(QStringLiteral("Remove Permit…"));
            unpermit->setEnabled(has(Oscar::FAM_PERMIT_DENY));
            connect(unpermit, &QAction::triggered, this, [oscar, askTarget] { const QString v=askTarget(QStringLiteral("AIM Remove Permit"), QStringLiteral("Screen name:")); if (!v.isEmpty() && oscar) oscar->removePermit(v); });
            QAction *block = privacy->addAction(QStringLiteral("Block / Deny User…"));
            block->setEnabled(has(Oscar::FAM_PERMIT_DENY));
            connect(block, &QAction::triggered, this, [oscar, askTarget] { const QString v=askTarget(QStringLiteral("AIM Block"), QStringLiteral("Screen name:")); if (!v.isEmpty() && oscar) oscar->addDeny(v); });
            QAction *unblock = privacy->addAction(QStringLiteral("Remove Block…"));
            unblock->setEnabled(has(Oscar::FAM_PERMIT_DENY));
            connect(unblock, &QAction::triggered, this, [oscar, askTarget] { const QString v=askTarget(QStringLiteral("AIM Remove Block"), QStringLiteral("Screen name:")); if (!v.isEmpty() && oscar) oscar->removeDeny(v); });
            QAction *tempPermit = privacy->addAction(QStringLiteral("Temporarily Permit User…"));
            tempPermit->setEnabled(has(Oscar::FAM_PERMIT_DENY));
            connect(tempPermit, &QAction::triggered, this, [oscar, askTarget] { const QString v=askTarget(QStringLiteral("AIM Temporary Permit"), QStringLiteral("Screen name:")); if (!v.isEmpty() && oscar) oscar->addTemporaryPermit(v); });
            privacy->addSeparator();
            QAction *authReq = privacy->addAction(QStringLiteral("Request Buddy Authorization…"));
            authReq->setEnabled(has(Oscar::FAM_FEEDBAG));
            connect(authReq, &QAction::triggered, this, [this, oscar, askTarget] {
                const QString target=askTarget(QStringLiteral("AIM Authorization"), QStringLiteral("Screen name:")); if (target.isEmpty() || !oscar) return;
                bool ok=false; const QString msg=QInputDialog::getText(this, QStringLiteral("Authorization Message"), QStringLiteral("Optional message:"), QLineEdit::Normal, QString(), &ok); if (ok) oscar->requestAuthorization(target,msg);
            });
            QAction *preauth = privacy->addAction(QStringLiteral("Pre-authorize User…"));
            preauth->setEnabled(has(Oscar::FAM_FEEDBAG));
            connect(preauth, &QAction::triggered, this, [oscar, askTarget] { const QString v=askTarget(QStringLiteral("AIM Pre-authorize"), QStringLiteral("Screen name:")); if (!v.isEmpty() && oscar) oscar->preAuthorize(v); });
            QAction *privacyFlags = privacy->addAction(QStringLiteral("Set Raw OSCAR Privacy Flags…"));
            privacyFlags->setEnabled(has(Oscar::FAM_OSERVICE));
            connect(privacyFlags, &QAction::triggered, this, [this, oscar] {
                bool ok=false; const QString text=QInputDialog::getText(this, QStringLiteral("OSCAR Privacy Flags"), QStringLiteral("32-bit flags (hex, e.g. 0x00000001):"), QLineEdit::Normal, QStringLiteral("0x00000000"), &ok);
                if (!ok || !oscar) return;
                QString v = text.trimmed();
                if (v.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) v = v.mid(2);
                bool parsed = false;
                const quint32 flags = v.toUInt(&parsed, 16);
                if (!parsed) QMessageBox::warning(this, QStringLiteral("OSCAR Privacy Flags"), QStringLiteral("Invalid hexadecimal value."));
                else oscar->setPrivacyFlags(flags);
            });

            QMenu *discovery = menu.addMenu(QStringLiteral("OSCAR Discovery / Invitations"));
            discovery->setEnabled(state->connected);
            QAction *emailLookup = discovery->addAction(QStringLiteral("Find AIM User by Email…"));
            emailLookup->setEnabled(has(Oscar::FAM_LOCATE));
            connect(emailLookup, &QAction::triggered, this, [oscar, askTarget] { const QString v=askTarget(QStringLiteral("AIM Email Lookup"), QStringLiteral("Email address:")); if (!v.isEmpty() && oscar) oscar->findByEmail(v); });
            QAction *invite = discovery->addAction(QStringLiteral("Invite Someone to AIM by Email…"));
            invite->setEnabled(has(Oscar::FAM_INVITE));
            connect(invite, &QAction::triggered, this, [this, oscar, askTarget] {
                const QString email=askTarget(QStringLiteral("AIM Invitation"), QStringLiteral("Email address:")); if (email.isEmpty() || !oscar) return;
                bool ok=false; const QString msg=QInputDialog::getMultiLineText(this, QStringLiteral("AIM Invitation"), QStringLiteral("Personal message:"), QString(), &ok); if (ok) oscar->inviteByEmail(email,msg);
            });
            QAction *offline = discovery->addAction(QStringLiteral("Retrieve Stored / Offline Messages"));
            offline->setEnabled(has(Oscar::FAM_ICBM));
            connect(offline, &QAction::triggered, this, [oscar] { if (oscar) oscar->retrieveStoredMessages(); });

            QMenu *admin = menu.addMenu(QStringLiteral("OSCAR Account Administration"));
            admin->setEnabled(state->connected);
            QAction *accountInfo = admin->addAction(QStringLiteral("View Account Information…"));
            accountInfo->setEnabled(has(Oscar::FAM_ADMIN));
            connect(accountInfo, &QAction::triggered, this, [oscar] { if (oscar) oscar->requestAccountInfo(); });
            QAction *email = admin->addAction(QStringLiteral("Change Account Email…"));
            email->setEnabled(has(Oscar::FAM_ADMIN));
            connect(email, &QAction::triggered, this, [this, oscar] { bool ok=false; const QString v=QInputDialog::getText(this, QStringLiteral("Change AIM Email"), QStringLiteral("New email address:"), QLineEdit::Normal, QString(), &ok).trimmed(); if (ok && !v.isEmpty() && oscar) oscar->changeAccountEmail(v); });
            QAction *formatted = admin->addAction(QStringLiteral("Change Formatted Screen Name…"));
            formatted->setEnabled(has(Oscar::FAM_ADMIN));
            connect(formatted, &QAction::triggered, this, [this, oscar] { bool ok=false; const QString v=QInputDialog::getText(this, QStringLiteral("Formatted AIM Screen Name"), QStringLiteral("New formatting:"), QLineEdit::Normal, QString(), &ok).trimmed(); if (ok && !v.isEmpty() && oscar) oscar->changeFormattedScreenName(v); });
            QAction *password = admin->addAction(QStringLiteral("Change OSCAR / AIM Password…"));
            password->setEnabled(has(Oscar::FAM_ADMIN));
            connect(password, &QAction::triggered, this, [this, backendId] { if (BackendState *target = stateById(backendId)) { selectState(target); changePassword(); } });
            QAction *confirm = admin->addAction(QStringLiteral("Request Account Confirmation Email"));
            confirm->setEnabled(has(Oscar::FAM_ADMIN));
            connect(confirm, &QAction::triggered, this, [oscar] { if (oscar) oscar->confirmAccount(); });
            admin->addSeparator();
            QAction *deleteAccount = admin->addAction(QStringLiteral("Delete AIM Account from Server…"));
            deleteAccount->setEnabled(has(Oscar::FAM_ADMIN));
            connect(deleteAccount, &QAction::triggered, this, [this, oscar, state] {
                if (!oscar || !state || !state->backend) return;
                const QString name=state->backend->settings().username;
                if (QMessageBox::warning(this, QStringLiteral("Delete AIM Account"),
                    QStringLiteral("This asks the OSCAR server to DELETE the AIM account '%1'.\n\nThis is not the same as removing the saved connection from WaffleHouse-Client. Continue?").arg(name),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes) oscar->deleteAccount();
            });

            menu.addSeparator();
            QAction *featureCenter = menu.addAction(QStringLiteral("OSCAR Feature Center…"));
            featureCenter->setEnabled(state->connected && oscar);
            connect(featureCenter, &QAction::triggered, this, [this, oscar] { showPlainTextDialog(this, QStringLiteral("OSCAR Feature Center"), oscarFeatureCenterText(oscar)); });

            QAction *auditLog = menu.addAction(QStringLiteral("View OSCAR Audit Log…"));
            connect(auditLog, &QAction::triggered, this, [this, state] { showOscarAuditLog(state); });

            QAction *rawOscar = menu.addAction(QStringLiteral("Advanced Raw OSCAR SNAC…"));
            rawOscar->setEnabled(state->connected);
            connect(rawOscar, &QAction::triggered, this, [this, backendId] { if (BackendState *target=stateById(backendId)) { selectState(target); rawProtocolCommand(); } });

            if (m_oscarVoice && m_oscarVoice->isPrepared() && m_oscarVoiceBackendId == backendId) {
                QAction *hangup = menu.addAction(QStringLiteral("Hang Up OSCAR Voice"));
                connect(hangup, &QAction::triggered, this, [this] { hangupOscarVoice(true); });
            }
        } else {
            QAction *nick = menu.addAction(QStringLiteral("Change IRC Nick / Nickname…"));
            nick->setEnabled(state->connected);
            connect(nick, &QAction::triggered, this, [this, backendId] {
                if (BackendState *target = stateById(backendId)) {
                    selectState(target);
                    changeIrcNick();
                }
            });
        }

        if (protocol == ConnectionSettings::Protocol::Oscar || protocol == ConnectionSettings::Protocol::Irc) {
            QAction *capabilities = menu.addAction(QStringLiteral("Server Capabilities…"));
            capabilities->setEnabled(state->connected);
            connect(capabilities, &QAction::triggered, this, [this, backendId] {
                if (BackendState *target = stateById(backendId)) {
                    selectState(target);
                    showServerCapabilities(target);
                }
            });
        }
    }

    if (protocol == ConnectionSettings::Protocol::Nntp) {
        menu.addSeparator();
        QAction *group = menu.addAction(QStringLiteral("Open / Join Newsgroup…"));
        group->setEnabled(state->connected);
        connect(group, &QAction::triggered, this, [this, backendId] {
            if (BackendState *target = stateById(backendId)) { selectState(target); openMessagingDialog(target, QString(), true); }
        });
        QAction *list = menu.addAction(QStringLiteral("List Newsgroups (raw LIST)"));
        list->setEnabled(state->connected);
        connect(list, &QAction::triggered, this, [this, backendId] {
            if (BackendState *target = stateById(backendId); target && target->backend) target->backend->sendRaw(QStringLiteral("LIST"));
        });
    }

    menu.addSeparator();
    QAction *edit = menu.addAction(QStringLiteral("Edit Connection…"));
    edit->setEnabled(!state->connected && !state->connecting);
    connect(edit, &QAction::triggered, this, [this, backendId] {
        if (BackendState *target = stateById(backendId)) {
            selectState(target);
            openConnectionDialog(target->backend->settings(), target);
        }
    });

    menu.exec(globalPos);
}

void MainWindow::showBuddyContextMenu(BackendState *state,
                                      const QString &buddy,
                                      const QPoint &globalPos)
{
    if (!state || !state->backend || buddy.trimmed().isEmpty()) return;
    const auto protocol = state->backend->settings().protocol;
    if (protocol != ConnectionSettings::Protocol::Oscar
        && protocol != ConnectionSettings::Protocol::Irc) return;

    selectState(state);
    const QString backendId = state->backend->id();
    const QString target = buddy.trimmed();
    QMenu menu(this);

    QAction *sendIm = menu.addAction(QStringLiteral("Send IM"));
    sendIm->setEnabled(state->connected && (protocol != ConnectionSettings::Protocol::Oscar
        || (qobject_cast<OscarBackend *>(state->backend) && qobject_cast<OscarBackend *>(state->backend)->supportsFamily(Oscar::FAM_ICBM))));
    connect(sendIm, &QAction::triggered, this, [this, backendId, target] {
        if (BackendState *current = stateById(backendId)) {
            selectState(current);
            ensureConversationWindow(current->backend, QStringLiteral("im"), target, true);
        }
    });

    QAction *sendFileAction = menu.addAction(QStringLiteral("Send File"));
    sendFileAction->setEnabled(state->connected && (protocol != ConnectionSettings::Protocol::Oscar
        || (qobject_cast<OscarBackend *>(state->backend) && qobject_cast<OscarBackend *>(state->backend)->supportsFamily(Oscar::FAM_ICBM))));
    connect(sendFileAction, &QAction::triggered, this, [this, backendId, target] {
        if (BackendState *current = stateById(backendId)) {
            selectState(current);
            sendFileToTarget(current, target, targetDisplayName(current, QStringLiteral("im"), target));
        }
    });

    menu.addSeparator();
    QAction *linkContact = menu.addAction(QStringLiteral("Add / Link Unified Contact…"));
    connect(linkContact, &QAction::triggered, this, [this, backendId, target, protocol] {
        QString suggested = target;
        bool ok = false;
        const QString name = QInputDialog::getText(this, QStringLiteral("Unified Contact"),
            QStringLiteral("Contact name:"), QLineEdit::Normal, suggested, &ok).trimmed();
        if (!ok || name.isEmpty()) return;
        ContactStore store;
        const QString proto = protocol == ConnectionSettings::Protocol::Oscar ? QStringLiteral("aim") : QStringLiteral("irc");
        QString error;
        if (!store.addEndpoint(name, {proto, target, backendId, QString()}, &error))
            QMessageBox::warning(this, QStringLiteral("Unified Contact"), error);
        else statusBar()->showMessage(QStringLiteral("Linked %1 to unified contact %2.").arg(target, name), 4500);
    });
    ContactStore contactStore; bool linkedOk = false;
    const UnifiedContact linked = contactStore.findByEndpoint(
        protocol == ConnectionSettings::Protocol::Oscar ? QStringLiteral("aim") : QStringLiteral("irc"), target, &linkedOk);
    UnifiedContactEndpoint sipEndpoint; bool hasSipEndpoint = false;
    if (linkedOk) for (const auto &ep : linked.endpoints) {
        if (ContactStore::normalizedProtocol(ep.protocol) == QStringLiteral("sip")) { sipEndpoint = ep; hasSipEndpoint = true; break; }
    }
    if (linkedOk && hasSipEndpoint) {
        QAction *callContact = menu.addAction(QStringLiteral("Call %1 via SIP").arg(linked.displayName));
        connect(callContact, &QAction::triggered, this, [this, sipEndpoint] {
            QString accountId = sipEndpoint.accountId;
            if (accountId.isEmpty()) accountId = m_sipController->selectedAccountId();
            if (accountId.isEmpty() || !m_sipController->hasAccount(accountId)) {
                QMessageBox::warning(this, QStringLiteral("SIP Call"),
                    QStringLiteral("The contact does not have a usable SIP account selected."));
                return;
            }
            QString error;
            if (m_sipController->dial(accountId, sipEndpoint.address, QString(), &error) < 0)
                QMessageBox::warning(this, QStringLiteral("SIP Call Failed"), error);
            else m_softphoneWindow->showAndRaise();
        });
    }

    menu.addSeparator();
    if (protocol == ConnectionSettings::Protocol::Oscar) {
        auto *oscar = qobject_cast<OscarBackend *>(state->backend);
        const auto has = [state, oscar](quint16 family) {
            return state && state->connected && oscar && oscar->supportsFamily(family);
        };
        const auto peerCap = [oscar, target](const char *hex) {
            return oscar && oscar->peerAdvertisesCapability(target, QByteArray::fromHex(hex));
        };

        QAction *profile = menu.addAction(QStringLiteral("View AIM Profile…"));
        profile->setEnabled(has(Oscar::FAM_LOCATE));
        connect(profile, &QAction::triggered, this, [this, backendId, target] {
            if (BackendState *current = stateById(backendId)) showAimUserInfo(current, target, true);
        });

        QAction *userInfo = menu.addAction(QStringLiteral("Get AIM User Info…"));
        userInfo->setEnabled(has(Oscar::FAM_LOCATE));
        connect(userInfo, &QAction::triggered, this, [this, backendId, target] {
            if (BackendState *current = stateById(backendId)) showAimUserInfo(current, target, false);
        });

        QAction *directory = menu.addAction(QStringLiteral("View AIM Directory Info…"));
        directory->setEnabled(has(Oscar::FAM_LOCATE));
        connect(directory, &QAction::triggered, this, [oscar, target] { if (oscar) oscar->requestDirectoryInfo(target); });

        menu.addSeparator();
        QAction *voice = menu.addAction(QStringLiteral("Start OSCAR Voice Chat…"));
        const bool whVoice = peerCap("574846564f4943458001574146464c45");
        voice->setEnabled(has(Oscar::FAM_ICBM) && whVoice && (!m_oscarVoice || !m_oscarVoice->isActive()));
        voice->setToolTip(whVoice ? QStringLiteral("Peer advertises WaffleHouse OSCAR Voice")
                                  : QStringLiteral("Disabled until this buddy advertises the WaffleHouse OSCAR Voice capability; use Get AIM User Info first."));
        connect(voice, &QAction::triggered, this, [this, backendId, target] {
            if (BackendState *current = stateById(backendId)) startOscarVoice(current, target);
        });

        if (m_oscarVoice && m_oscarVoice->isPrepared() && m_oscarVoiceBackendId == backendId) {
            QAction *hangup = menu.addAction(QStringLiteral("Hang Up OSCAR Voice"));
            connect(hangup, &QAction::triggered, this, [this] { hangupOscarVoice(true); });
        }

        QMenu *legacy = menu.addMenu(QStringLiteral("Advertised Peer OSCAR Services"));
        struct PeerFeature { const char *label; const char *hex; const char *note; };
        static const PeerFeature peerFeatures[] = {
            {"Legacy AIM Voice / Talk", "094613414c7f11d18222444553540000", "Detected only; proprietary legacy media framing is not implemented."},
            {"Standard OSCAR File Transfer", "094613434c7f11d18222444553540000", "Detected only; WaffleHouse Secure File Transfer is a separate implementation."},
            {"Standard OSCAR Direct IM", "094613454c7f11d18222444553540000", "Detected only; legacy direct-IM socket transport is not implemented."},
            {"Buddy Icon / Avatar", "094613464c7f11d18222444553540000", "Detected only; BART/icon retrieval is not implemented in this revision."},
            {"File Sharing / Receive File", "094613484c7f11d18222444553540000", "Detected only; legacy file-sharing transport is not implemented."},
            {"UTF-8 Messaging", "0946134e4c7f11d18222444553540000", "Supported for WaffleHouse text messaging."},
            {"Chat", "748f2420628711d18222444553540000", "Server chat features are available through AIM rooms."},
        };
        for (const PeerFeature &feature : peerFeatures) {
            const bool advertised = peerCap(feature.hex);
            QAction *action = legacy->addAction(QStringLiteral("%1: %2")
                                                    .arg(QString::fromLatin1(feature.label),
                                                         advertised ? QStringLiteral("ADVERTISED") : QStringLiteral("not advertised")));
            action->setEnabled(false);
            action->setToolTip(QString::fromLatin1(feature.note));
        }

        menu.addSeparator();
        QMenu *watch = menu.addMenu(QStringLiteral("Buddy Presence / Watch"));
        QAction *tempWatch = watch->addAction(QStringLiteral("Add Temporary Buddy Watch"));
        tempWatch->setEnabled(has(Oscar::FAM_BUDDY));
        connect(tempWatch, &QAction::triggered, this, [oscar, target] { if (oscar) oscar->addTemporaryBuddy(target); });
        QAction *unwatch = watch->addAction(QStringLiteral("Remove Temporary Buddy Watch"));
        unwatch->setEnabled(has(Oscar::FAM_BUDDY));
        connect(unwatch, &QAction::triggered, this, [oscar, target] { if (oscar) oscar->removeTemporaryBuddy(target); });

        QMenu *privacy = menu.addMenu(QStringLiteral("Privacy / Permit-Deny"));
        QAction *permit = privacy->addAction(QStringLiteral("Permit / Allow This User"));
        permit->setEnabled(has(Oscar::FAM_PERMIT_DENY));
        connect(permit, &QAction::triggered, this, [oscar, target] { if (oscar) oscar->addPermit(target); });
        QAction *unpermit = privacy->addAction(QStringLiteral("Remove Permit for This User"));
        unpermit->setEnabled(has(Oscar::FAM_PERMIT_DENY));
        connect(unpermit, &QAction::triggered, this, [oscar, target] { if (oscar) oscar->removePermit(target); });
        QAction *block = privacy->addAction(QStringLiteral("Block / Deny This User"));
        block->setEnabled(has(Oscar::FAM_PERMIT_DENY));
        connect(block, &QAction::triggered, this, [oscar, target] { if (oscar) oscar->addDeny(target); });
        QAction *unblock = privacy->addAction(QStringLiteral("Remove Block for This User"));
        unblock->setEnabled(has(Oscar::FAM_PERMIT_DENY));
        connect(unblock, &QAction::triggered, this, [oscar, target] { if (oscar) oscar->removeDeny(target); });
        QAction *tempPermit = privacy->addAction(QStringLiteral("Temporarily Permit This User"));
        tempPermit->setEnabled(has(Oscar::FAM_PERMIT_DENY));
        connect(tempPermit, &QAction::triggered, this, [oscar, target] { if (oscar) oscar->addTemporaryPermit(target); });
        QAction *tempUnpermit = privacy->addAction(QStringLiteral("Remove Temporary Permit"));
        tempUnpermit->setEnabled(has(Oscar::FAM_PERMIT_DENY));
        connect(tempUnpermit, &QAction::triggered, this, [oscar, target] { if (oscar) oscar->removeTemporaryPermit(target); });

        QMenu *auth = menu.addMenu(QStringLiteral("Buddy Authorization"));
        QAction *requestAuth = auth->addAction(QStringLiteral("Request Authorization…"));
        requestAuth->setEnabled(has(Oscar::FAM_FEEDBAG));
        connect(requestAuth, &QAction::triggered, this, [this, oscar, target] {
            if (!oscar) return;
            bool ok = false;
            const QString msg=QInputDialog::getText(this, QStringLiteral("AIM Buddy Authorization"), QStringLiteral("Optional message:"), QLineEdit::Normal, QString(), &ok);
            if (ok) oscar->requestAuthorization(target,msg);
        });
        QAction *acceptAuth = auth->addAction(QStringLiteral("Accept / Grant Authorization"));
        acceptAuth->setEnabled(has(Oscar::FAM_FEEDBAG));
        connect(acceptAuth, &QAction::triggered, this, [oscar, target] { if (oscar) oscar->respondAuthorization(target,true); });
        QAction *denyAuth = auth->addAction(QStringLiteral("Deny Authorization"));
        denyAuth->setEnabled(has(Oscar::FAM_FEEDBAG));
        connect(denyAuth, &QAction::triggered, this, [oscar, target] { if (oscar) oscar->respondAuthorization(target,false); });
        QAction *preauth = auth->addAction(QStringLiteral("Pre-authorize This User"));
        preauth->setEnabled(has(Oscar::FAM_FEEDBAG));
        connect(preauth, &QAction::triggered, this, [oscar, target] { if (oscar) oscar->preAuthorize(target); });
        QAction *removeMe = auth->addAction(QStringLiteral("Remove Me from Their Buddy List"));
        removeMe->setEnabled(has(Oscar::FAM_FEEDBAG));
        connect(removeMe, &QAction::triggered, this, [this, oscar, target] {
            if (!oscar) return;
            if (QMessageBox::question(this, QStringLiteral("Remove Me"),
                                      QStringLiteral("Ask the OSCAR server to remove you from %1's buddy list?").arg(target)) == QMessageBox::Yes)
                oscar->removeMeFromBuddyList(target);
        });

        menu.addSeparator();
        QAction *version = menu.addAction(QStringLiteral("Check Client Version…"));
        version->setEnabled(has(Oscar::FAM_ICBM));
        connect(version, &QAction::triggered, this, [this, backendId, target] {
            if (BackendState *current = stateById(backendId)) requestClientVersion(current, target);
        });

        QAction *copy = menu.addAction(QStringLiteral("Copy Screen Name"));
        connect(copy, &QAction::triggered, this, [target] { QApplication::clipboard()->setText(target); });

        menu.addSeparator();
        QAction *remove = menu.addAction(QStringLiteral("Remove from AIM Buddy List…"));
        remove->setEnabled(state->connected && oscar
                           && (oscar->supportsFamily(Oscar::FAM_FEEDBAG) || oscar->supportsFamily(Oscar::FAM_BUDDY)));
        connect(remove, &QAction::triggered, this, [this, backendId, target] {
            BackendState *current = stateById(backendId);
            if (!current || !current->backend) return;
            if (QMessageBox::question(this, QStringLiteral("Remove AIM Buddy"),
                                      QStringLiteral("Remove %1 from the AIM buddy list?").arg(target))
                == QMessageBox::Yes) {
                current->backend->removeBuddy(target);
                saveConnections();
            }
        });
    } else {
        QAction *whois = menu.addAction(QStringLiteral("WHOIS / User Info…"));
        whois->setEnabled(state->connected);
        connect(whois, &QAction::triggered, this, [this, backendId, target] {
            if (BackendState *current = stateById(backendId)) showIrcWhois(current, target);
        });
        QAction *version = menu.addAction(QStringLiteral("Check Client Version…"));
        version->setEnabled(state->connected);
        connect(version, &QAction::triggered, this, [this, backendId, target] {
            if (BackendState *current = stateById(backendId)) requestClientVersion(current, target);
        });
        QAction *copy = menu.addAction(QStringLiteral("Copy Nickname"));
        connect(copy, &QAction::triggered, this, [target] { QApplication::clipboard()->setText(target); });
    }

    menu.exec(globalPos);
}

void MainWindow::rebuildAccountsMenu()
{
    if (!m_accountsMenu) return;
    m_accountsMenu->clear();

    // This must remain the first submenu under Accounts.
    QMenu *management = m_accountsMenu->addMenu(QStringLiteral("Account Management"));
    management->addAction(m_addConnectionAction);
    management->addAction(m_editConnectionAction);
    management->addAction(m_deleteConnectionAction);
    management->addSeparator();
    management->addAction(m_connectAction);
    management->addAction(m_disconnectAction);
    management->addSeparator();
    management->addAction(m_showConnectionsAction);
    management->addSeparator();
    management->addAction(m_quitAction);

    m_accountsMenu->addSeparator();

    QList<BackendState *> states = m_states.values();
    std::sort(states.begin(), states.end(), [this](BackendState *a, BackendState *b) {
        return accountMenuLabel(a).compare(accountMenuLabel(b), Qt::CaseInsensitive) < 0;
    });

    if (states.isEmpty()) {
        QAction *empty = m_accountsMenu->addAction(QStringLiteral("No saved accounts"));
        empty->setEnabled(false);
        return;
    }

    for (BackendState *state : states) {
        if (!state || !state->backend) continue;
        const QString backendId = state->backend->id();
        const auto protocol = state->backend->settings().protocol;
        QMenu *account = m_accountsMenu->addMenu(accountMenuLabel(state));

        QAction *select = account->addAction(QStringLiteral("Select Account"));
        connect(select, &QAction::triggered, this, [this, backendId] {
            if (BackendState *target = stateById(backendId)) selectState(target);
        });

        QAction *edit = account->addAction(QStringLiteral("Edit Connection…"));
        edit->setEnabled(!state->connected && !state->connecting);
        connect(edit, &QAction::triggered, this, [this, backendId] {
            if (BackendState *target = stateById(backendId)) {
                selectState(target);
                openConnectionDialog(target->backend->settings(), target);
            }
        });

        QAction *toggle = account->addAction(
            state->connected || state->connecting
                ? QStringLiteral("Disconnect")
                : QStringLiteral("Connect"));
        connect(toggle, &QAction::triggered, this, [this, backendId] {
            if (BackendState *target = stateById(backendId)) {
                selectState(target);
                if (target->connected || target->connecting) disconnectSelected();
                else connectSelected();
            }
        });

        if (protocol == ConnectionSettings::Protocol::Oscar
            || protocol == ConnectionSettings::Protocol::Irc) {
            account->addSeparator();
            QAction *conversation = account->addAction(QStringLiteral("IM / Chatroom…"));
            conversation->setEnabled(state->connected);
            connect(conversation, &QAction::triggered, this, [this, backendId] {
                if (BackendState *target = stateById(backendId)) {
                    selectState(target);
                    openMessagingDialog(target);
                }
            });

            QAction *buddies = account->addAction(QStringLiteral("Add / Remove Buddies…"));
            connect(buddies, &QAction::triggered, this, [this, backendId] {
                if (BackendState *target = stateById(backendId)) {
                    selectState(target);
                    openBuddyManager(target);
                }
            });

            if (protocol == ConnectionSettings::Protocol::Oscar) {
                QAction *presence = account->addAction(QStringLiteral("Set AIM Status / AFK…"));
                presence->setEnabled(state->connected);
                connect(presence, &QAction::triggered, this, [this, backendId] {
                    if (BackendState *target = stateById(backendId)) {
                        selectState(target);
                        setAimPresence(target);
                    }
                });
                QAction *password = account->addAction(QStringLiteral("Change OSCAR / AIM Password…"));
                password->setEnabled(state->connected);
                connect(password, &QAction::triggered, this, [this, backendId] {
                    if (BackendState *target = stateById(backendId)) {
                        selectState(target);
                        changePassword();
                    }
                });
            }

            if (protocol == ConnectionSettings::Protocol::Irc) {
                QAction *nick = account->addAction(QStringLiteral("Change IRC Nick / Nickname…"));
                nick->setEnabled(state->connected);
                connect(nick, &QAction::triggered, this, [this, backendId] {
                    if (BackendState *target = stateById(backendId)) {
                        selectState(target);
                        changeIrcNick();
                    }
                });
            }
        } else if (protocol == ConnectionSettings::Protocol::Sip) {
            account->addSeparator();
            QAction *contacts = account->addAction(QStringLiteral("Add / Remove Buddies / Contacts…"));
            connect(contacts, &QAction::triggered, this, [this, backendId] {
                if (BackendState *target = stateById(backendId)) {
                    selectState(target);
                    openBuddyManager(target);
                }
            });
        }
    }
}

void MainWindow::openMessagingDialog(BackendState *state,
                                     const QString &presetTarget,
                                     bool startRoomTab)
{
    if (!state || !state->backend) return;
    const auto protocol = state->backend->settings().protocol;
    if (protocol != ConnectionSettings::Protocol::Oscar
        && protocol != ConnectionSettings::Protocol::Irc
        && protocol != ConnectionSettings::Protocol::Xmpp
        && protocol != ConnectionSettings::Protocol::Nntp) {
        QMessageBox::information(this, QStringLiteral("IM / Chatroom"),
                                 QStringLiteral("This connection type does not provide message rooms/groups."));
        return;
    }
    if (!state->connected) {
        QMessageBox::information(this, QStringLiteral("IM / Chatroom"),
                                 QStringLiteral("Connect %1 first.").arg(accountMenuLabel(state)));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("%1 — IM / Chatroom").arg(accountMenuLabel(state)));
    dialog.setMinimumWidth(360);
    auto *outer = new QVBoxLayout(&dialog);
    auto *tabs = new QTabWidget(&dialog);

    auto *imTab = new QWidget(tabs);
    auto *imForm = new QFormLayout(imTab);
    auto *target = new QComboBox(imTab);
    target->setEditable(true);
    target->setInsertPolicy(QComboBox::NoInsert);
    QStringList buddies = state->buddies.values();
    buddies.sort(Qt::CaseInsensitive);
    target->addItems(buddies);
    if (!presetTarget.trimmed().isEmpty()) target->setCurrentText(presetTarget.trimmed());
    else target->setCurrentIndex(-1);
    target->setPlaceholderText(protocol == ConnectionSettings::Protocol::Xmpp
                                   ? QStringLiteral("user@example.org")
                                   : QStringLiteral("screen name or nickname"));
    imForm->addRow(QStringLiteral("Buddy / user:"), target);
    auto *imHint = new QLabel(QStringLiteral("Open a private message using this account."), imTab);
    imHint->setWordWrap(true);
    imForm->addRow(QString(), imHint);
    tabs->addTab(imTab, QStringLiteral("Instant Message"));
    if (protocol == ConnectionSettings::Protocol::Nntp) tabs->setTabVisible(0, false);

    auto *roomTab = new QWidget(tabs);
    auto *roomForm = new QFormLayout(roomTab);
    auto *room = new QComboBox(roomTab);
    room->setEditable(true);
    room->setInsertPolicy(QComboBox::NoInsert);
    QStringList rooms = state->discoveredRooms.values();
    rooms.sort(Qt::CaseInsensitive);
    room->addItems(rooms);
    room->setCurrentIndex(-1);
    room->setPlaceholderText(protocol == ConnectionSettings::Protocol::Irc
                                 ? QStringLiteral("#channel")
                                 : protocol == ConnectionSettings::Protocol::Xmpp
                                     ? QStringLiteral("room@conference.example.org")
                                     : protocol == ConnectionSettings::Protocol::Nntp
                                         ? QStringLiteral("comp.sys.retro")
                                         : QStringLiteral("room name"));
    roomForm->addRow(protocol == ConnectionSettings::Protocol::Irc
                         ? QStringLiteral("Channel:")
                         : protocol == ConnectionSettings::Protocol::Nntp
                             ? QStringLiteral("Newsgroup:")
                             : QStringLiteral("Room:"), room);
    QCheckBox *privateRoom = nullptr;
    if (protocol == ConnectionSettings::Protocol::Oscar) {
        privateRoom = new QCheckBox(QStringLiteral("Private AIM exchange (create if needed)"), roomTab);
        roomForm->addRow(QString(), privateRoom);
    }
    auto *roomHint = new QLabel(
        protocol == ConnectionSettings::Protocol::Irc
            ? QStringLiteral("Join an IRC channel and open its conversation window.")
            : protocol == ConnectionSettings::Protocol::Xmpp
                ? QStringLiteral("Join an XMPP multi-user chat room (MUC).")
                : protocol == ConnectionSettings::Protocol::Nntp
                    ? QStringLiteral("Select an NNTP newsgroup. Raw/article commands remain available from the account tools.")
                    : QStringLiteral("Join or create an AIM chat room and open its conversation window."),
        roomTab);
    roomHint->setWordWrap(true);
    roomForm->addRow(QString(), roomHint);
    tabs->addTab(roomTab, QStringLiteral("Chat Room"));

    tabs->setCurrentIndex((startRoomTab || protocol == ConnectionSettings::Protocol::Nntp) ? 1 : 0);
    outer->addWidget(tabs);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok, &dialog);
    if (auto *ok = buttons->button(QDialogButtonBox::Ok)) ok->setText(QStringLiteral("Open"));
    outer->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) return;

    if (tabs->currentIndex() == 0) {
        const QString buddy = target->currentText().trimmed();
        if (!buddy.isEmpty()) ensureConversationWindow(state->backend, QStringLiteral("im"), buddy, true);
        return;
    }

    QString roomName = room->currentText().trimmed();
    if (roomName.isEmpty()) return;
    if (protocol == ConnectionSettings::Protocol::Irc
        && !QStringLiteral("#&+!").contains(roomName.front())) {
        roomName.prepend(QLatin1Char('#'));
    }
    m_closedRoomKeys.remove(conversationKey(state->backend, QStringLiteral("chat"), roomName));
    state->backend->joinRoom(roomName, privateRoom && privateRoom->isChecked());
    ensureConversationWindow(state->backend, QStringLiteral("chat"), roomName, true);
}

void MainWindow::openBuddyManager(BackendState *state)
{
    if (!state || !state->backend) return;
    const auto protocol = state->backend->settings().protocol;
    if (protocol != ConnectionSettings::Protocol::Oscar
        && protocol != ConnectionSettings::Protocol::Irc
        && protocol != ConnectionSettings::Protocol::Sip
        && protocol != ConnectionSettings::Protocol::Xmpp) {
        QMessageBox::information(this, QStringLiteral("Buddies / Contacts"),
                                 QStringLiteral("This connection type does not maintain a buddy/contact list."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("%1 — Buddies / Contacts").arg(accountMenuLabel(state)));
    dialog.resize(410, 350);
    auto *outer = new QVBoxLayout(&dialog);

    auto *description = new QLabel(&dialog);
    if (protocol == ConnectionSettings::Protocol::Sip) {
        description->setText(QStringLiteral(
            "SIP contacts are local WaffleHouse dial targets for this SIP account. "
            "They are not uploaded to the PBX."));
    } else if (!state->connected) {
        description->setText(QStringLiteral(
            "This account is offline. Connect it before changing its buddy list."));
    } else if (protocol == ConnectionSettings::Protocol::Irc) {
        description->setText(QStringLiteral(
            "IRC buddies are local nickname watches for this saved IRC account."));
    } else if (protocol == ConnectionSettings::Protocol::Xmpp) {
        description->setText(QStringLiteral(
            "XMPP contacts use standard roster add/remove and presence subscription stanzas."));
    } else {
        description->setText(QStringLiteral(
            "AIM buddy changes are sent through the selected AIM/OSCAR account."));
    }
    description->setWordWrap(true);
    outer->addWidget(description);

    auto *list = new QListWidget(&dialog);
    outer->addWidget(list, 1);

    auto refill = [list](const QStringList &provided) {
        QStringList names = provided;
        names.removeDuplicates();
        names.sort(Qt::CaseInsensitive);
        const QString selected = list->currentItem() ? list->currentItem()->text() : QString();
        list->clear();
        list->addItems(names);
        if (!selected.isEmpty()) {
            const auto matches = list->findItems(selected, Qt::MatchFixedString);
            if (!matches.isEmpty()) list->setCurrentItem(matches.first());
        }
    };
    refill(state->buddies.values());

    auto *addRow = new QHBoxLayout;
    auto *entry = new QLineEdit(&dialog);
    entry->setPlaceholderText(protocol == ConnectionSettings::Protocol::Sip
                                  ? QStringLiteral("extension, number, user@domain, or SIP URI")
                                  : protocol == ConnectionSettings::Protocol::Irc
                                        ? QStringLiteral("nickname to watch")
                                        : protocol == ConnectionSettings::Protocol::Xmpp
                                            ? QStringLiteral("contact@example.org")
                                            : QStringLiteral("AIM screen name"));
    auto *add = new QPushButton(QStringLiteral("Add"), &dialog);
    auto *remove = new QPushButton(QStringLiteral("Remove Selected"), &dialog);
    addRow->addWidget(entry, 1);
    addRow->addWidget(add);
    outer->addLayout(addRow);
    outer->addWidget(remove);

    const bool mutableNow = protocol == ConnectionSettings::Protocol::Sip || state->connected;
    entry->setEnabled(mutableNow);
    add->setEnabled(mutableNow);
    remove->setEnabled(mutableNow && list->currentItem());
    connect(list, &QListWidget::currentItemChanged, &dialog,
            [remove, mutableNow](QListWidgetItem *current) {
                remove->setEnabled(mutableNow && current);
            });

    connect(state->backend, &ChatBackend::buddyListChanged, &dialog,
            [refill](const QStringList &names) mutable { refill(names); });

    auto addBuddyNow = [this, state, entry] {
        const QString name = entry->text().trimmed();
        if (name.isEmpty()) return;
        state->backend->addBuddy(name);
        saveConnections();
        entry->clear();
    };
    connect(add, &QPushButton::clicked, &dialog, addBuddyNow);
    connect(entry, &QLineEdit::returnPressed, &dialog, addBuddyNow);
    connect(remove, &QPushButton::clicked, &dialog, [this, state, list] {
        QListWidgetItem *item = list->currentItem();
        if (!item) return;
        state->backend->removeBuddy(item->text());
        saveConnections();
    });

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    outer->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    dialog.exec();
}

void MainWindow::showServerCapabilities(BackendState *state)
{
    if (!state || !state->backend) return;
    if (!state->connected) {
        QMessageBox::information(this, QStringLiteral("Server Capabilities"),
                                 QStringLiteral("Connect this account first so the server can advertise its capabilities."));
        return;
    }

    const auto protocol = state->backend->settings().protocol;
    if (protocol != ConnectionSettings::Protocol::Oscar
        && protocol != ConnectionSettings::Protocol::Irc) {
        QMessageBox::information(this, QStringLiteral("Server Capabilities"),
                                 QStringLiteral("Capability inspection is currently available for AIM/OSCAR and IRC accounts."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(protocol == ConnectionSettings::Protocol::Oscar
                              ? QStringLiteral("AIM/OSCAR Server Capabilities")
                              : QStringLiteral("IRC Server Capabilities"));
    dialog.resize(650, 520);
    auto *layout = new QVBoxLayout(&dialog);
    auto *view = new QPlainTextEdit(&dialog);
    view->setReadOnly(true);
    layout->addWidget(view, 1);

    auto render = [state, protocol, view] {
        QString text;
        if (protocol == ConnectionSettings::Protocol::Oscar) {
            text += QStringLiteral("Server: %1\nAccount: %2\nLast Updated: %3\n\nAdvertised OSCAR families:\n")
                        .arg(state->backend->settings().server,
                             state->identity.isEmpty() ? state->backend->settings().username : state->identity,
                             state->serverCapabilitiesUpdated.isEmpty() ? QStringLiteral("Not received yet")
                                                                        : state->serverCapabilitiesUpdated);
            text += state->serverCapabilityDetails.isEmpty()
                ? QStringLiteral("  (No family list has been received yet.)\n")
                : QStringLiteral("  %1\n").arg(state->serverCapabilityDetails.join(QStringLiteral("\n  ")));
            text += QStringLiteral("\nAIM Profile / User Info support: %1")
                        .arg(state->aimProfileSupported ? QStringLiteral("YES") : QStringLiteral("NO"));
            if (state->aimProfileSupported && state->aimProfileMaxLength > 0) {
                text += QStringLiteral("\nMaximum profile size: %1 bytes").arg(state->aimProfileMaxLength);
            }
            text += QStringLiteral("\n\nWaffleHouse-Client maps these families to:\n  %1")
                        .arg(state->serverCapabilities.isEmpty()
                                 ? QStringLiteral("(none identified)")
                                 : state->serverCapabilities.join(QStringLiteral("\n  ")));
        } else {
            text += QStringLiteral("Server: %1\nNick: %2\nLast Updated: %3\n\nIRCv3 CAP LS 302:\n")
                        .arg(state->backend->settings().server,
                             state->identity.isEmpty() ? state->backend->settings().username : state->identity,
                             state->serverCapabilitiesUpdated.isEmpty() ? QStringLiteral("Not received yet")
                                                                        : state->serverCapabilitiesUpdated);
            text += state->serverCapabilities.isEmpty()
                ? QStringLiteral("  (No IRCv3 capabilities advertised.)\n")
                : QStringLiteral("  %1\n").arg(state->serverCapabilities.join(QStringLiteral("\n  ")));
            text += QStringLiteral("\n005 / ISUPPORT tokens:\n");
            text += state->serverCapabilityDetails.isEmpty()
                ? QStringLiteral("  (No ISUPPORT tokens received yet.)")
                : QStringLiteral("  %1").arg(state->serverCapabilityDetails.join(QStringLiteral("\n  ")));
        }
        view->setPlainText(text);
    };
    render();

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    auto *refresh = buttons->addButton(QStringLiteral("Refresh"), QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);
    connect(refresh, &QPushButton::clicked, &dialog, [state] {
        if (auto *oscar = qobject_cast<OscarBackend *>(state->backend)) oscar->refreshServerCapabilities();
        else if (auto *irc = qobject_cast<IrcBackend *>(state->backend)) irc->refreshServerCapabilities();
    });
    if (auto *oscar = qobject_cast<OscarBackend *>(state->backend)) {
        connect(oscar, &OscarBackend::serverCapabilitiesChanged, &dialog,
                [state, render](const QStringList &features, const QStringList &families,
                                bool profileSupported, int maxProfileLength) {
                    state->serverCapabilities = features;
                    state->serverCapabilityDetails = families;
                    state->aimProfileSupported = profileSupported;
                    state->aimProfileMaxLength = maxProfileLength;
                    state->serverCapabilitiesUpdated = QDateTime::currentDateTime().toString(Qt::ISODate);
                    render();
                });
        oscar->refreshServerCapabilities();
    } else if (auto *irc = qobject_cast<IrcBackend *>(state->backend)) {
        connect(irc, &IrcBackend::serverCapabilitiesChanged, &dialog,
                [state, render](const QStringList &caps, const QStringList &isupport) {
                    state->serverCapabilities = caps;
                    state->serverCapabilityDetails = isupport;
                    state->serverCapabilitiesUpdated = QDateTime::currentDateTime().toString(Qt::ISODate);
                    render();
                });
        irc->refreshServerCapabilities();
    }
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    dialog.exec();
}

void MainWindow::showOscarAuditLog(BackendState *state)
{
    BackendState *target = state;
    if (!target || !qobject_cast<OscarBackend *>(target->backend)) {
        if (BackendState *selected = selectedState(); selected && qobject_cast<OscarBackend *>(selected->backend)) {
            target = selected;
        } else {
            QList<BackendState *> oscarStates;
            QStringList labels;
            for (BackendState *candidate : m_states) {
                if (!candidate || !qobject_cast<OscarBackend *>(candidate->backend)) continue;
                oscarStates << candidate;
                labels << accountMenuLabel(candidate);
            }
            if (oscarStates.isEmpty()) {
                QMessageBox::information(this, QStringLiteral("OSCAR Audit Log"),
                    QStringLiteral("No AIM/OSCAR account is configured."));
                return;
            }
            if (oscarStates.size() == 1) {
                target = oscarStates.first();
            } else {
                bool ok = false;
                const QString picked = QInputDialog::getItem(this, QStringLiteral("OSCAR Audit Log"),
                    QStringLiteral("Choose AIM/OSCAR account:"), labels, 0, false, &ok);
                if (!ok) return;
                const int choice = labels.indexOf(picked);
                if (choice < 0) return;
                target = oscarStates.at(choice);
            }
        }
    }

    auto *oscar = target ? qobject_cast<OscarBackend *>(target->backend) : nullptr;
    if (!oscar) return;

    const QString path = oscar->auditLogPath();
    QFile file(path);
    QString body;
    constexpr qint64 maxViewBytes = 512 * 1024;
    if (file.exists() && file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        bool tailOnly = false;
        if (file.size() > maxViewBytes) {
            file.seek(file.size() - maxViewBytes);
            file.readLine(); // discard a partial first line after seeking into the tail
            tailOnly = true;
        }
        body = QString::fromUtf8(file.readAll());
        if (tailOnly)
            body.prepend(QStringLiteral("(Showing the last 512 KiB of the audit log.)\n\n"));
    } else {
        const QString mode = oscar->settings().oscarDebugMode.trimmed().toCaseFolded();
        body = QStringLiteral("No OSCAR audit log has been written yet.\n\n"
                              "Enable Login Audit or Full Wire Trace in the AIM/OSCAR account settings, "
                              "then connect the account.\n\nCurrent OSCAR debug mode: %1")
                   .arg(mode.isEmpty() ? QStringLiteral("off") : mode);
    }

    const QString heading = QStringLiteral("Log file: %1\n\n").arg(QDir::toNativeSeparators(path));
    showPlainTextDialog(this, QStringLiteral("OSCAR Audit Log — %1").arg(target->backend->settings().username),
                        heading + body);
}

void MainWindow::editAimProfile(BackendState *state)
{
    if (!state || !state->backend
        || state->backend->settings().protocol != ConnectionSettings::Protocol::Oscar) {
        return;
    }
    if (!state->connected) {
        QMessageBox::information(this, QStringLiteral("Edit AIM Profile"),
                                 QStringLiteral("Connect the AIM/OSCAR account first."));
        return;
    }
    if (!state->aimProfileSupported) {
        QMessageBox::information(
            this, QStringLiteral("Edit AIM Profile"),
            QStringLiteral("This OSCAR server did not advertise the Locate (0x0002) family, so AIM profiles are not available on this connection."));
        return;
    }

    auto *oscar = qobject_cast<OscarBackend *>(state->backend);
    if (!oscar) return;

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Edit AIM Profile — %1")
                              .arg(state->identity.isEmpty()
                                       ? state->backend->settings().username : state->identity));
    dialog.resize(560, 420);
    auto *layout = new QVBoxLayout(&dialog);
    auto *description = new QLabel(
        state->aimProfileMaxLength > 0
            ? QStringLiteral("Edit the profile shown to other AIM users. Server limit: %1 bytes.")
                  .arg(state->aimProfileMaxLength)
            : QStringLiteral("Edit the profile shown to other AIM users."),
        &dialog);
    description->setWordWrap(true);
    layout->addWidget(description);

    auto *editor = new QPlainTextEdit(&dialog);
    editor->setPlainText(state->aimProfile);
    editor->setPlaceholderText(QStringLiteral("Enter your AIM profile…"));
    layout->addWidget(editor, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        const QString profile = editor->toPlainText();
        const int bytes = profile.toUtf8().size();
        if (state->aimProfileMaxLength > 0 && bytes > state->aimProfileMaxLength) {
            QMessageBox::warning(&dialog, QStringLiteral("AIM Profile Too Long"),
                                 QStringLiteral("The profile is %1 bytes, but this server allows %2 bytes.")
                                     .arg(bytes).arg(state->aimProfileMaxLength));
            return;
        }
        oscar->setProfile(profile);
        state->aimProfile = profile;
        saveConnections();
        dialog.accept();
    });
    dialog.exec();
}

void MainWindow::showAimUserInfo(BackendState *state, const QString &target, bool profileFocus)
{
    if (!state || !state->backend || !state->connected
        || state->backend->settings().protocol != ConnectionSettings::Protocol::Oscar) return;
    auto *oscar = qobject_cast<OscarBackend *>(state->backend);
    if (!oscar) return;
    if (!state->aimProfileSupported) {
        QMessageBox::information(this, QStringLiteral("AIM User Info"),
                                 QStringLiteral("This OSCAR server did not advertise Locate/user-info support."));
        return;
    }

    const QString clean = target.trimmed();
    if (clean.isEmpty()) return;
    auto *dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(profileFocus
                               ? QStringLiteral("AIM Profile — %1").arg(clean)
                               : QStringLiteral("AIM User Information — %1").arg(clean));
    dialog->resize(680, 590);
    auto *layout = new QVBoxLayout(dialog);
    auto *view = new QPlainTextEdit(dialog);
    view->setReadOnly(true);
    view->setPlainText(QStringLiteral("Requesting OSCAR user information for %1…").arg(clean));
    layout->addWidget(view, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    auto *refresh = buttons->addButton(QStringLiteral("Refresh"), QDialogButtonBox::ActionRole);
    auto *copy = buttons->addButton(QStringLiteral("Copy Screen Name"), QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    connect(refresh, &QPushButton::clicked, dialog, [oscar, clean, view] {
        view->setPlainText(QStringLiteral("Refreshing OSCAR user information for %1…").arg(clean));
        oscar->requestUserInfo(clean);
    });
    connect(copy, &QPushButton::clicked, dialog, [clean] { QApplication::clipboard()->setText(clean); });
    connect(oscar, &OscarBackend::userInfoReceived, dialog,
            [this, clean, view, profileFocus, state](const QString &replyTarget, const QVariantMap &info) {
                const QString screenName = info.value(QStringLiteral("screenName")).toString();
                if (replyTarget.compare(clean, Qt::CaseInsensitive) != 0
                    && screenName.compare(clean, Qt::CaseInsensitive) != 0) return;
                view->setPlainText(aimUserInfoText(info, profileFocus));
                view->moveCursor(QTextCursor::Start);

                // If this is already an online buddy, enrich the arrival-state
                // cache with Locate's away message/profile-derived presence.
                // A Get Info lookup for an arbitrary user must never add that user
                // to the buddy list or incorrectly mark an offline user online.
                const QString key = clean.toCaseFolded();
                if (state && state->onlineBuddies.contains(key)) {
                    QVariantMap native = state->oscarBuddyPresence.value(key);
                    for (auto it = info.cbegin(); it != info.cend(); ++it) native.insert(it.key(), it.value());
                    native.insert(QStringLiteral("online"), true);
                    native.insert(QStringLiteral("state"), info.value(QStringLiteral("presence"), QStringLiteral("Online")));
                    state->oscarBuddyPresence.insert(key, native);
                    refreshBuddyList();
                }
            });
    dialog->show();
    oscar->requestUserInfo(clean);
}

void MainWindow::showIrcWhois(BackendState *state, const QString &target)
{
    if (!state || !state->backend || !state->connected
        || state->backend->settings().protocol != ConnectionSettings::Protocol::Irc) return;
    auto *irc = qobject_cast<IrcBackend *>(state->backend);
    if (!irc) return;
    const QString clean = target.trimmed();
    if (clean.isEmpty()) return;

    auto *dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QStringLiteral("IRC WHOIS / User Info — %1").arg(clean));
    dialog->resize(650, 430);
    auto *layout = new QVBoxLayout(dialog);
    auto *view = new QPlainTextEdit(dialog);
    view->setReadOnly(true);
    layout->addWidget(view, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    auto *refresh = buttons->addButton(QStringLiteral("Refresh"), QDialogButtonBox::ActionRole);
    auto *copy = buttons->addButton(QStringLiteral("Copy Nickname"), QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);
    const auto beginRequest = [irc, clean, view] {
        view->setPlainText(QStringLiteral("WHOIS %1\nLast requested: %2\n")
                               .arg(clean, QDateTime::currentDateTime().toString(Qt::ISODate)));
        irc->requestWhois(clean);
    };
    connect(refresh, &QPushButton::clicked, dialog, beginRequest);
    connect(copy, &QPushButton::clicked, dialog, [clean] { QApplication::clipboard()->setText(clean); });
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    connect(irc, &IrcBackend::whoisReply, dialog,
            [clean, view](const QString &nick, const QString &line, bool complete) {
                if (nick.compare(clean, Qt::CaseInsensitive) != 0) return;
                view->appendPlainText(line);
                if (complete) view->appendPlainText(QStringLiteral("--- End of WHOIS ---"));
            });
    dialog->show();
    beginRequest();
}

OscarVoiceSession *MainWindow::ensureOscarVoiceSession()
{
    if (m_oscarVoice) return m_oscarVoice;
    m_oscarVoice = new OscarVoiceSession(this);
    connect(m_oscarVoice, &OscarVoiceSession::statusChanged, this,
            [this](const QString &message) {
                statusBar()->showMessage(message, 6000);
                if (BackendState *state = stateById(m_oscarVoiceBackendId)) appendActivity(state->backend, message);
            });
    connect(m_oscarVoice, &OscarVoiceSession::errorOccurred, this,
            [this](const QString &message) {
                statusBar()->showMessage(message, 8000);
                if (BackendState *state = stateById(m_oscarVoiceBackendId)) appendActivity(state->backend, QStringLiteral("[voice error] %1").arg(message));
            });
    return m_oscarVoice;
}

void MainWindow::startOscarVoice(BackendState *state, const QString &target)
{
    if (!state || !state->connected || state->backend->settings().protocol != ConnectionSettings::Protocol::Oscar) return;
    auto *oscar = qobject_cast<OscarBackend *>(state->backend);
    if (!oscar) return;
    const QString clean = target.trimmed();
    if (clean.isEmpty()) return;

    OscarVoiceSession *voice = ensureOscarVoiceSession();
    if (voice->isPrepared()) {
        QMessageBox::information(this, QStringLiteral("OSCAR Voice"),
                                 QStringLiteral("An OSCAR voice session is already active or ringing. Hang it up before starting another."));
        return;
    }
    QString error;
    if (!voice->prepare(0, &error)) {
        QMessageBox::warning(this, QStringLiteral("OSCAR Voice"), error);
        return;
    }
    m_oscarVoiceBackendId = state->backend->id();
    m_oscarVoicePeer = clean;
    m_oscarVoiceCookie = QStringLiteral("%1").arg(QRandomGenerator::global()->generate64(), 16, 16, QLatin1Char('0'));
    oscar->proposeVoice(clean, m_oscarVoiceCookie, voice->localAddress(), voice->localPort(), voice->sampleRate());
    appendActivity(state->backend,
                   QStringLiteral("[OSCAR voice] Calling %1 over OSCAR rendezvous; unencrypted direct UDP audio from %2:%3 at %4 Hz.")
                       .arg(clean, voice->localAddress()).arg(voice->localPort()).arg(voice->sampleRate()));
    statusBar()->showMessage(QStringLiteral("OSCAR voice: calling %1…").arg(clean), 6000);
}

void MainWindow::hangupOscarVoice(bool notifyPeer)
{
    if (!m_oscarVoice || !m_oscarVoice->isPrepared()) return;
    BackendState *state = stateById(m_oscarVoiceBackendId);
    if (notifyPeer && state && state->backend) {
        if (auto *oscar = qobject_cast<OscarBackend *>(state->backend)) {
            oscar->cancelVoice(m_oscarVoicePeer, m_oscarVoiceCookie, 0x0001);
        }
    }
    const QString peer = m_oscarVoicePeer;
    m_oscarVoice->stop();
    m_oscarVoiceBackendId.clear();
    m_oscarVoicePeer.clear();
    m_oscarVoiceCookie.clear();
    statusBar()->showMessage(QStringLiteral("OSCAR voice call with %1 ended.").arg(peer), 5000);
}

void MainWindow::setMediaWindow(MediaWindow *window)
{
    m_mediaWindow = window;
    if (m_trayShowMediaAction) m_trayShowMediaAction->setEnabled(BuildFeatures::Media && m_mediaWindow != nullptr);
    installMediaKeyShortcuts();
}

void MainWindow::installMediaKeyShortcuts()
{
    if (!m_mediaShortcuts.isEmpty()) return;

    const auto bind = [this](Qt::Key key, const QString &command) {
        auto *shortcut = new QShortcut(
            QKeySequence(QKeyCombination(Qt::NoModifier, key)), this);
        shortcut->setContext(Qt::ApplicationShortcut);
        connect(shortcut, &QShortcut::activated, this, [this, command] {
            runMediaShortcut(command);
        });
        m_mediaShortcuts.append(shortcut);
    };

    bind(Qt::Key_MediaPlay, QStringLiteral("mplaykey"));
    bind(Qt::Key_MediaPause, QStringLiteral("mpause"));
    bind(Qt::Key_MediaTogglePlayPause, QStringLiteral("mtoggle"));
    bind(Qt::Key_MediaStop, QStringLiteral("mstop"));
    bind(Qt::Key_MediaNext, QStringLiteral("mnext"));
    bind(Qt::Key_MediaPrevious, QStringLiteral("mprev"));
    bind(Qt::Key_VolumeUp, QStringLiteral("mvolup"));
    bind(Qt::Key_VolumeDown, QStringLiteral("mvoldown"));
    bind(Qt::Key_VolumeMute, QStringLiteral("mmute"));
}

void MainWindow::runMediaShortcut(const QString &command)
{
    if (!m_mediaWindow) return;
    QString message;
    if (m_mediaWindow->executeCommand(command, QString(), &message)
        && !message.trimmed().isEmpty()) {
        statusBar()->showMessage(message.section(QLatin1Char('\n'), 0, 0), 2500);
    }
}

void MainWindow::showTransferWindow()
{
    if (m_transferWindow) m_transferWindow->showAndRaise();
}

void MainWindow::buildConnectionsWindow()
{
    m_connectionsWindow = new QMainWindow(nullptr);
    m_connectionsWindow->setWindowTitle(QStringLiteral("%1 %2 — Connections/Accounts").arg(appDisplayName(), appVersionString()));
    m_connectionsWindow->resize(620, 430);
    m_connectionsWindow->setMinimumSize(500, 340);
    m_connectionsWindow->setAttribute(Qt::WA_QuitOnClose, false);

    auto *central = new QWidget(m_connectionsWindow);
    auto *outer = new QVBoxLayout(central);
    outer->setContentsMargins(16, 14, 16, 14);
    outer->setSpacing(10);

    auto *header = new QHBoxLayout;
    auto *headings = new QVBoxLayout;
    headings->setSpacing(2);
    auto *title = new QLabel(QStringLiteral("Connections/Accounts"), central);
    title->setObjectName(QStringLiteral("PageTitle"));
    auto *subtitle = new QLabel(QStringLiteral("Saved profiles and live connection activity"), central);
    subtitle->setObjectName(QStringLiteral("PageSubtitle"));
    headings->addWidget(title);
    headings->addWidget(subtitle);
    header->addLayout(headings, 1);
    m_addConnectionButton = new QPushButton(QStringLiteral("+ Add Connection"), central);
    m_addConnectionButton->setProperty("role", "primary");
    header->addWidget(m_addConnectionButton);
    outer->addLayout(header);

    auto *listCard = new QFrame(central);
    listCard->setObjectName(QStringLiteral("ConnectionCard"));
    auto *listLayout = new QVBoxLayout(listCard);
    listLayout->setContentsMargins(10, 10, 10, 10);
    listLayout->setSpacing(10);
    auto *listTitle = new QLabel(QStringLiteral("Saved Accounts"), listCard);
    listTitle->setObjectName(QStringLiteral("CardTitle"));
    listLayout->addWidget(listTitle);
    m_connectionList = new QListWidget(listCard);
    m_connectionList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_connectionList->setMinimumHeight(120);
    listLayout->addWidget(m_connectionList, 1);

    auto *buttonRow = new QHBoxLayout;
    buttonRow->setSpacing(8);
    m_editConnectionButton = new QPushButton(QStringLiteral("Edit"), listCard);
    m_deleteConnectionButton = new QPushButton(QStringLiteral("Delete"), listCard);
    m_deleteConnectionButton->setProperty("role", "danger");
    m_connectButton = new QPushButton(QStringLiteral("Connect"), listCard);
    m_connectButton->setProperty("role", "primary");
    m_disconnectButton = new QPushButton(QStringLiteral("Disconnect"), listCard);
    buttonRow->addWidget(m_editConnectionButton);
    buttonRow->addWidget(m_deleteConnectionButton);
    buttonRow->addStretch(1);
    buttonRow->addWidget(m_connectButton);
    buttonRow->addWidget(m_disconnectButton);
    listLayout->addLayout(buttonRow);
    outer->addWidget(listCard, 1);

    auto *activityCard = new QFrame(central);
    activityCard->setObjectName(QStringLiteral("Card"));
    auto *activityLayout = new QVBoxLayout(activityCard);
    activityLayout->setContentsMargins(14, 14, 14, 14);
    auto *activityLabel = new QLabel(QStringLiteral("Activity"), activityCard);
    activityLabel->setObjectName(QStringLiteral("CardTitle"));
    activityLayout->addWidget(activityLabel);
    m_activity = new QPlainTextEdit(activityCard);
    m_activity->setReadOnly(true);
    m_activity->setMaximumBlockCount(2500);
    m_activity->setMinimumHeight(125);
    activityLayout->addWidget(m_activity, 1);
    outer->addWidget(activityCard, 1);

    m_connectionsWindow->setCentralWidget(central);

    connect(m_addConnectionButton, &QPushButton::clicked,
            this, [this] { openConnectionDialog(m_defaults, nullptr); });
    connect(m_editConnectionButton, &QPushButton::clicked, this, &MainWindow::editSelected);
    connect(m_deleteConnectionButton, &QPushButton::clicked, this, &MainWindow::deleteSelected);
    connect(m_connectButton, &QPushButton::clicked, this, &MainWindow::connectSelected);
    connect(m_disconnectButton, &QPushButton::clicked, this, &MainWindow::disconnectSelected);

    connect(m_connectionList, &QListWidget::currentItemChanged,
            this, [this](QListWidgetItem *current) {
                BackendState *state = current ? stateById(current->data(Qt::UserRole).toString()) : nullptr;
                if (state) selectState(state);
                updateActions();
            });

    connect(m_connectionList, &QListWidget::customContextMenuRequested,
            this, [this](const QPoint &pos) {
                QListWidgetItem *item = m_connectionList->itemAt(pos);
                if (!item) return;
                m_connectionList->setCurrentItem(item);
                BackendState *state = stateById(item->data(Qt::UserRole).toString());
                if (!state) return;
                showAccountContextMenu(state, m_connectionList->viewport()->mapToGlobal(pos));
            });
}

void MainWindow::buildTrayIcon()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        statusBar()->showMessage(
            QStringLiteral("System tray is unavailable; closing Buddy List will quit %1.").arg(appDisplayName()),
            6000);
        return;
    }

    m_trayIcon = new QSystemTrayIcon(this);
    // WaffleHouse-Client 3.1 ships its own multi-resolution icon so the tray
    // appearance is consistent across Linux and FreeBSD desktop themes.
    QIcon icon = appIcon();
    if (icon.isNull()) {
        icon = windowIcon();
    }
    if (icon.isNull()) {
        icon = QIcon::fromTheme(QStringLiteral("internet-chat"));
    }
    if (icon.isNull() && QApplication::style()) {
        icon = QApplication::style()->standardIcon(QStyle::SP_ComputerIcon);
    }
    if (icon.isNull()) {
        delete m_trayIcon;
        m_trayIcon = nullptr;
        statusBar()->showMessage(
            QStringLiteral("No usable tray icon is available; closing Buddy List will quit %1.")
                .arg(appDisplayName()),
            6000);
        return;
    }
#ifdef Q_OS_MACOS
    // Keep the WaffleHouse status-item artwork in full color. Treating it as a
    // template/mask lets macOS recolor it; on a light menu bar that can produce
    // the historical white-on-white icon after the last app window is closed.
    icon.setIsMask(false);
#endif
    m_trayIcon->setIcon(icon);
    m_trayIcon->setToolTip(appDisplayName());

    m_trayMenu = new QMenu(this);
    m_trayShowBuddyAction = m_trayMenu->addAction(QStringLiteral("Show Buddy List"));
    m_trayShowConnectionsAction = m_trayMenu->addAction(QStringLiteral("Show Connections/Accounts"));
    m_trayShowPhoneAction = m_trayMenu->addAction(QStringLiteral("Show Softphone"));
    m_trayShowPhoneAction->setVisible(BuildFeatures::Sip);
    m_trayShowMediaAction = m_trayMenu->addAction(QStringLiteral("Show Media Center"));
    m_trayShowMediaAction->setVisible(BuildFeatures::Media);
    m_trayShowMediaAction->setEnabled(BuildFeatures::Media && m_mediaWindow != nullptr);
    m_trayMenu->addSeparator();
    m_trayConnectAction = m_trayMenu->addAction(QStringLiteral("Connect Selected"));
    m_trayDisconnectAction = m_trayMenu->addAction(QStringLiteral("Disconnect Selected"));
    m_trayMenu->addSeparator();
    QAction *quitAction = m_trayMenu->addAction(QStringLiteral("Quit %1").arg(appDisplayName()));

    connect(m_trayShowBuddyAction, &QAction::triggered,
            this, &MainWindow::showBuddyWindow);
    connect(m_trayShowConnectionsAction, &QAction::triggered,
            this, &MainWindow::showConnectionsWindow);
    connect(m_trayShowPhoneAction, &QAction::triggered,
            m_softphoneWindow, &SoftphoneWindow::showAndRaise);
    connect(m_trayShowMediaAction, &QAction::triggered, this, [this] {
        if (m_mediaWindow) m_mediaWindow->showAndRaise();
    });
    connect(m_trayConnectAction, &QAction::triggered,
            this, &MainWindow::connectSelected);
    connect(m_trayDisconnectAction, &QAction::triggered,
            this, &MainWindow::disconnectSelected);
    connect(quitAction, &QAction::triggered,
            this, &MainWindow::quitApplication);

    connect(m_trayIcon, &QSystemTrayIcon::activated,
            this, [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger
                    || reason == QSystemTrayIcon::DoubleClick) {
                    showBuddyWindow();
                }
            });

    m_trayIcon->setContextMenu(m_trayMenu);
    m_trayIcon->show();
}

void MainWindow::openConnectionDialog(const ConnectionSettings &defaults,
                                      BackendState *editingState)
{
    if (editingState && (editingState->connected || editingState->connecting)) {
        QMessageBox::information(
            this,
            QStringLiteral("Edit Connection"),
            QStringLiteral("Disconnect this connection before editing it."));
        return;
    }

    if (editingState && editingState->backend
        && editingState->backend->settings().protocol == ConnectionSettings::Protocol::Sip) {
        for (const auto &call : m_sipController->calls()) {
            if (!call.disconnected
                && QString::fromStdString(call.accountId) == editingState->backend->id()) {
                QMessageBox::information(
                    this,
                    QStringLiteral("SIP Account In Use"),
                    QStringLiteral("Hang up active calls on this SIP account before editing it."));
                return;
            }
        }
    }

    ConnectionDialog dialog(defaults, editingState != nullptr, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    ConnectionSettings settings = dialog.settings();

    if (editingState && editingState->backend) {
        const ConnectionSettings oldSettings = editingState->backend->settings();
        if (settings.password.isEmpty()) {
            settings.password = oldSettings.password;
            // An unchecked Save password box explicitly disables persistence even when
            // the existing session password remains usable until the application exits.
            if (!settings.savePassword) {
                settings.savePassword = false;
            }
        }

        editingState->backend->setConnectionSettings(settings);
        editingState->identity.clear();
        editingState->endpoint.clear();
        editingState->buddies.clear();
        editingState->onlineBuddies.clear();
        editingState->oscarBuddyPresence.clear();
        if (settings.protocol == ConnectionSettings::Protocol::Irc) {
            for (const QString &buddy : settings.ircBuddies)
                if (!buddy.trimmed().isEmpty()) editingState->buddies.insert(buddy.trimmed());
        } else if (settings.protocol == ConnectionSettings::Protocol::Sip) {
            for (const QString &contact : settings.sipContacts)
                if (!contact.trimmed().isEmpty()) editingState->buddies.insert(contact.trimmed());
        }
        editingState->targetNames.clear();
        editingState->discoveredRooms.clear();

        if (settings.protocol == ConnectionSettings::Protocol::Irc) {
            editingState->secretRequired =
                editingState->secretRequired || !settings.password.isEmpty();
        } else if (settings.protocol == ConnectionSettings::Protocol::Telnet
                   || settings.protocol == ConnectionSettings::Protocol::Ssh) {
            editingState->secretRequired = false;
        } else if (settings.protocol == ConnectionSettings::Protocol::Nntp) {
            editingState->secretRequired = !settings.password.isEmpty();
        } else {
            editingState->secretRequired = true;
        }
        editingState->hasSessionSecret = !settings.password.isEmpty();

        updateConnectionItem(editingState);
        refreshBuddyList();
        updateActions();
        saveConnections();
        appendActivity(editingState->backend,
                       QStringLiteral("Saved connection updated."));
        return;
    }

    ChatBackend *backend = createBackend(settings);
    if (!backend) {
        return;
    }

    const bool secretRequired =
        settings.protocol == ConnectionSettings::Protocol::Oscar
        || settings.protocol == ConnectionSettings::Protocol::Sip
        || settings.protocol == ConnectionSettings::Protocol::Xmpp
        || (settings.protocol == ConnectionSettings::Protocol::Nntp && !settings.password.isEmpty())
        || (settings.protocol == ConnectionSettings::Protocol::Irc
            && !settings.password.isEmpty());

    attachBackend(backend,
                  true,
                  secretRequired,
                  !settings.password.isEmpty(),
                  false);
}


void MainWindow::importBbsList()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Import BBS List"), QString(),
        QStringLiteral("BBS Lists (*.csv *.tsv *.json *.txt);;All Files (*)"));
    if (path.isEmpty()) return;
    importBbsList(path);
}

void MainWindow::importBbsList(const QString &path)
{
    if (path.trimmed().isEmpty()) return;
    QString error;
    const auto entries = BbsDirectory::loadFile(path, &error);
    if (entries.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("BBS Import"),
                             error.isEmpty() ? QStringLiteral("No BBS entries were found in the selected file.") : error);
        return;
    }

    int added = 0, skipped = 0;
    for (const auto &bbs : entries) {
        bool duplicate = false;
        for (BackendState *state : m_states) {
            if (!state || !state->backend) continue;
            const auto &cfg = state->backend->settings();
            if (cfg.protocol == ConnectionSettings::Protocol::Telnet
                && cfg.server.compare(bbs.host, Qt::CaseInsensitive) == 0
                && cfg.port == bbs.port) { duplicate = true; break; }
        }
        if (duplicate) { ++skipped; continue; }
        ConnectionSettings cfg;
        cfg.protocol = ConnectionSettings::Protocol::Telnet;
        cfg.server = bbs.host;
        cfg.port = bbs.port;
        cfg.username = bbs.name;
        cfg.telnetTerminalType = bbs.terminalType.isEmpty() ? QStringLiteral("ANSI") : bbs.terminalType;
        if (ChatBackend *backend = createBackend(cfg)) {
            attachBackend(backend, true, false, false, false);
            ++added;
        }
    }
    QMessageBox::information(this, QStringLiteral("BBS Import"),
        QStringLiteral("Imported %1 BBS entries. %2 duplicates skipped.\n\nImported BBSes are saved offline; use Connect when you want to open one.")
            .arg(added).arg(skipped));
}

void MainWindow::loadUiSettings()
{
    QSettings settings;
    m_buddyOpacity = std::clamp(
        settings.value(QStringLiteral("ui/buddyOpacity"), 1.0).toDouble(),
        0.30,
        1.0);
    m_connectionsOpacity = std::clamp(
        settings.value(QStringLiteral("ui/connectionsOpacity"), 1.0).toDouble(),
        0.30,
        1.0);
}

void MainWindow::saveUiSettings() const
{
    QSettings settings;
    settings.setValue(QStringLiteral("ui/buddyOpacity"), m_buddyOpacity);
    settings.setValue(QStringLiteral("ui/connectionsOpacity"), m_connectionsOpacity);
}

void MainWindow::loadOptions()
{
    QSettings settings;
    m_options.theme = settings.value(QStringLiteral("ui/theme"), QStringLiteral("system"))
                          .toString().toCaseFolded();
    const QSet<QString> validThemes{
        QStringLiteral("system"), QStringLiteral("hacker"), QStringLiteral("matrix"),
        QStringLiteral("phosphor"), QStringLiteral("midnight"), QStringLiteral("amber"),
        QStringLiteral("ice"), QStringLiteral("classic-light"), QStringLiteral("solarized"),
        QStringLiteral("solarized-dark"), QStringLiteral("dracula"), QStringLiteral("nord"),
        QStringLiteral("cyberpunk"), QStringLiteral("blood-moon"), QStringLiteral("ocean"),
        QStringLiteral("retro-blue"), QStringLiteral("monochrome"), QStringLiteral("blue-box"),
        QStringLiteral("red-box"), QStringLiteral("beige-box"), QStringLiteral("2600"),
        QStringLiteral("wargames"), QStringLiteral("crt-green"), QStringLiteral("vt220"),
        QStringLiteral("cobalt"), QStringLiteral("vaporwave"), QStringLiteral("stealth"),
        QStringLiteral("synthwave"), QStringLiteral("c64"), QStringLiteral("dos"),
        QStringLiteral("waffle-iron"), QStringLiteral("ghostline"),
        QStringLiteral("hot-dog-stand"), QStringLiteral("neon-miami")
    };
    if (!validThemes.contains(m_options.theme)) {
        m_options.theme = QStringLiteral("system");
    }
    m_options.showTimestamps = settings.value(QStringLiteral("ui/showTimestamps"), true).toBool();
    m_options.showSidePanes = settings.value(QStringLiteral("ui/showSidePanes"), true).toBool();
    m_options.encryptedDmEnabled = settings.value(QStringLiteral("security/encryptedDmEnabled"), true).toBool();
    m_options.autoReplySecure = settings.value(QStringLiteral("security/autoReplySecure"), true).toBool();
    m_options.showSecureFingerprints = settings.value(QStringLiteral("security/showSecureFingerprints"), true).toBool();
    m_options.autoPresenceEnabled = settings.value(QStringLiteral("presence/autoEnabled"), true).toBool();
}

void MainWindow::saveOptions() const
{
    QSettings settings;
    settings.setValue(QStringLiteral("ui/theme"), m_options.theme);
    settings.setValue(QStringLiteral("ui/showTimestamps"), m_options.showTimestamps);
    settings.setValue(QStringLiteral("ui/showSidePanes"), m_options.showSidePanes);
    settings.setValue(QStringLiteral("security/encryptedDmEnabled"), m_options.encryptedDmEnabled);
    settings.setValue(QStringLiteral("security/autoReplySecure"), m_options.autoReplySecure);
    settings.setValue(QStringLiteral("security/showSecureFingerprints"), m_options.showSecureFingerprints);
    settings.setValue(QStringLiteral("presence/autoEnabled"), m_options.autoPresenceEnabled);
    settings.sync();
}

void MainWindow::applyTheme()
{
    // 3.0 keeps every existing theme key but routes them through one modern
    // design system so the main window, dialogs, chat windows, softphone,
    // transfers, menus, and secondary windows all share the same visual language.
    qApp->setStyleSheet(ModernStyle::styleSheet(m_options.theme));
    applyConversationOptions();
}

void MainWindow::applyConversationOptions()
{
    for (ChatWindow *window : m_windows) {
        if (!window) continue;
        window->setShowTimestamps(m_options.showTimestamps);
        window->setShowSidePane(m_options.showSidePanes);
        updateConversationSecurity(window);
    }
}

void MainWindow::showClientCapabilities()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("%1 %2 — Client Capabilities").arg(appDisplayName(), appVersionString()));
    dialog.resize(720, 500);
    auto *layout = new QVBoxLayout(&dialog);
    auto *view = new QPlainTextEdit(&dialog);
    view->setReadOnly(true);
    view->setLineWrapMode(QPlainTextEdit::NoWrap);
    view->setPlainText(CapabilityRegistry::displayLines().join(QLatin1Char('\n')));
    layout->addWidget(view, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.exec();
}

void MainWindow::showHistory()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("%1 — Search History").arg(appDisplayName()));
    dialog.resize(780, 540);
    auto *layout = new QVBoxLayout(&dialog);
    auto *searchRow = new QHBoxLayout;
    auto *query = new QLineEdit(&dialog);
    query->setPlaceholderText(QStringLiteral("Search AIM, IRC, SIP call history, rooms, contacts, text…"));
    auto *search = new QPushButton(QStringLiteral("Search"), &dialog);
    auto *clear = new QPushButton(QStringLiteral("Clear History"), &dialog);
    searchRow->addWidget(query, 1); searchRow->addWidget(search); searchRow->addWidget(clear);
    layout->addLayout(searchRow);
    auto *view = new QPlainTextEdit(&dialog);
    view->setReadOnly(true); view->setLineWrapMode(QPlainTextEdit::NoWrap);
    layout->addWidget(view, 1);
    auto refresh = [query, view] {
        const auto records = HistoryStore::search(query->text().trimmed(), 500);
        QStringList lines = HistoryStore::displayLines(records);
        if (lines.isEmpty()) lines << QStringLiteral("No matching history entries.");
        view->setPlainText(lines.join(QLatin1Char('\n')));
    };
    connect(search, &QPushButton::clicked, &dialog, refresh);
    connect(query, &QLineEdit::returnPressed, &dialog, refresh);
    connect(clear, &QPushButton::clicked, &dialog, [&dialog, refresh] {
        if (QMessageBox::question(&dialog, QStringLiteral("Clear History"),
                QStringLiteral("Delete the local WaffleHouse message and call history?")) != QMessageBox::Yes) return;
        QString error;
        if (!HistoryStore::clear(&error)) QMessageBox::warning(&dialog, QStringLiteral("Clear History"), error);
        else refresh();
    });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    refresh();
    dialog.exec();
}

void MainWindow::showUnifiedContacts()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("%1 — Unified Contacts").arg(appDisplayName()));
    dialog.resize(760, 520);
    auto *layout = new QVBoxLayout(&dialog);
    auto *view = new QPlainTextEdit(&dialog);
    view->setReadOnly(true); view->setLineWrapMode(QPlainTextEdit::NoWrap);
    layout->addWidget(view, 1);
    auto *row = new QHBoxLayout;
    auto *add = new QPushButton(QStringLiteral("Add Endpoint…"), &dialog);
    auto *notes = new QPushButton(QStringLiteral("Notes…"), &dialog);
    auto *remove = new QPushButton(QStringLiteral("Delete…"), &dialog);
    auto *call = new QPushButton(QStringLiteral("Call…"), &dialog);
    row->addWidget(add); row->addWidget(notes); row->addWidget(remove); row->addWidget(call); row->addStretch(1);
    layout->addLayout(row);
    auto refresh = [view] {
        ContactStore store;
        QStringList lines;
        for (const auto &contact : store.contacts()) {
            lines << QStringLiteral("%1").arg(contact.displayName);
            for (const auto &ep : contact.endpoints) {
                lines << QStringLiteral("    %-6s %1%2")
                    .arg(ep.address, ep.accountId.isEmpty() ? QString() : QStringLiteral("  [account %1]").arg(ep.accountId))
                    .replace(QStringLiteral("%-6s"), ep.protocol.toUpper().leftJustified(6, QLatin1Char(' ')));
            }
            if (!contact.notes.isEmpty()) lines << QStringLiteral("    notes: %1").arg(contact.notes);
            lines << QString();
        }
        if (lines.isEmpty()) lines << QStringLiteral("No unified contacts yet.\n\nAdd an AIM/IRC buddy from its right-click menu, or use Add Endpoint here.");
        view->setPlainText(lines.join(QLatin1Char('\n')));
    };
    connect(add, &QPushButton::clicked, &dialog, [&dialog, refresh] {
        bool ok=false;
        const QString name=QInputDialog::getText(&dialog,QStringLiteral("Unified Contact"),QStringLiteral("Contact name:"),QLineEdit::Normal,{},&ok).trimmed(); if(!ok||name.isEmpty())return;
        const QString protocol=QInputDialog::getItem(&dialog,QStringLiteral("Unified Contact"),QStringLiteral("Protocol:"),{QStringLiteral("aim"),QStringLiteral("irc"),QStringLiteral("sip"),QStringLiteral("telnet")},0,false,&ok).trimmed(); if(!ok||protocol.isEmpty())return;
        const QString address=QInputDialog::getText(&dialog,QStringLiteral("Unified Contact"),QStringLiteral("Address / screen name / SIP URI:"),QLineEdit::Normal,{},&ok).trimmed(); if(!ok||address.isEmpty())return;
        const QString account=QInputDialog::getText(&dialog,QStringLiteral("Unified Contact"),QStringLiteral("Optional WaffleHouse account ID:"),QLineEdit::Normal,{},&ok).trimmed(); if(!ok)return;
        ContactStore store; QString error; if(!store.addEndpoint(name,{protocol,address,account,QString()},&error)) QMessageBox::warning(&dialog,QStringLiteral("Unified Contact"),error); else refresh();
    });
    connect(notes, &QPushButton::clicked, &dialog, [&dialog, refresh] {
        bool ok=false; const QString name=QInputDialog::getText(&dialog,QStringLiteral("Contact Notes"),QStringLiteral("Contact name:"),QLineEdit::Normal,{},&ok).trimmed(); if(!ok||name.isEmpty())return;
        ContactStore store; bool found=false; const auto c=store.findByName(name,&found); if(!found){QMessageBox::warning(&dialog,QStringLiteral("Contact Notes"),QStringLiteral("Contact not found."));return;}
        const QString text=QInputDialog::getMultiLineText(&dialog,QStringLiteral("Contact Notes"),QStringLiteral("Private notes:"),c.notes,&ok); if(!ok)return;
        QString error; if(!store.setNotes(name,text,&error)) QMessageBox::warning(&dialog,QStringLiteral("Contact Notes"),error); else refresh();
    });
    connect(remove, &QPushButton::clicked, &dialog, [&dialog, refresh] {
        bool ok=false; const QString name=QInputDialog::getText(&dialog,QStringLiteral("Delete Contact"),QStringLiteral("Contact name:"),QLineEdit::Normal,{},&ok).trimmed(); if(!ok||name.isEmpty())return;
        if(QMessageBox::question(&dialog,QStringLiteral("Delete Contact"),QStringLiteral("Delete unified contact %1?").arg(name))!=QMessageBox::Yes)return;
        ContactStore store; QString error; if(!store.remove(name,&error)) QMessageBox::warning(&dialog,QStringLiteral("Delete Contact"),error); else refresh();
    });
    connect(call, &QPushButton::clicked, &dialog, [this, &dialog] {
        bool ok=false; const QString name=QInputDialog::getText(&dialog,QStringLiteral("Call Contact"),QStringLiteral("Contact name:"),QLineEdit::Normal,{},&ok).trimmed(); if(!ok||name.isEmpty())return;
        ContactStore store; bool found=false; const auto c=store.findByName(name,&found); if(!found){QMessageBox::warning(&dialog,QStringLiteral("Call Contact"),QStringLiteral("Contact not found."));return;}
        UnifiedContactEndpoint sip; bool have=false; for(const auto &ep:c.endpoints) if(ContactStore::normalizedProtocol(ep.protocol)==QStringLiteral("sip")){sip=ep;have=true;break;}
        if(!have){QMessageBox::warning(&dialog,QStringLiteral("Call Contact"),QStringLiteral("This contact has no SIP endpoint."));return;}
        QString account=sip.accountId; if(account.isEmpty()) account=m_sipController->selectedAccountId();
        if(account.isEmpty()||!m_sipController->hasAccount(account)){QMessageBox::warning(&dialog,QStringLiteral("Call Contact"),QStringLiteral("Select a SIP account or store one on the contact endpoint."));return;}
        QString error; if(m_sipController->dial(account,sip.address,QString(),&error)<0) QMessageBox::warning(&dialog,QStringLiteral("Call Failed"),error); else m_softphoneWindow->showAndRaise();
    });
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Close,&dialog); layout->addWidget(buttons); connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    refresh(); dialog.exec();
}

void MainWindow::showOptionsDialog()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Options — %1").arg(appDisplayName()));
    dialog.setMinimumWidth(360);
    auto *outer = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;

    QComboBox theme(&dialog);
    theme.addItem(QStringLiteral("System"), QStringLiteral("system"));
    theme.addItem(QStringLiteral("Hacker"), QStringLiteral("hacker"));
    theme.addItem(QStringLiteral("Matrix"), QStringLiteral("matrix"));
    theme.addItem(QStringLiteral("Phosphor"), QStringLiteral("phosphor"));
    theme.addItem(QStringLiteral("Midnight"), QStringLiteral("midnight"));
    theme.addItem(QStringLiteral("Amber"), QStringLiteral("amber"));
    theme.addItem(QStringLiteral("Ice"), QStringLiteral("ice"));
    theme.addItem(QStringLiteral("Classic Light"), QStringLiteral("classic-light"));
    theme.addItem(QStringLiteral("Solarized"), QStringLiteral("solarized"));
    theme.addItem(QStringLiteral("Solarized Dark"), QStringLiteral("solarized-dark"));
    theme.addItem(QStringLiteral("Dracula"), QStringLiteral("dracula"));
    theme.addItem(QStringLiteral("Nord"), QStringLiteral("nord"));
    theme.addItem(QStringLiteral("Cyberpunk"), QStringLiteral("cyberpunk"));
    theme.addItem(QStringLiteral("Blood Moon"), QStringLiteral("blood-moon"));
    theme.addItem(QStringLiteral("Ocean"), QStringLiteral("ocean"));
    theme.addItem(QStringLiteral("Retro Blue"), QStringLiteral("retro-blue"));
    theme.addItem(QStringLiteral("Monochrome"), QStringLiteral("monochrome"));
    theme.addItem(QStringLiteral("Blue Box"), QStringLiteral("blue-box"));
    theme.addItem(QStringLiteral("Red Box"), QStringLiteral("red-box"));
    theme.addItem(QStringLiteral("Beige Box"), QStringLiteral("beige-box"));
    theme.addItem(QStringLiteral("2600"), QStringLiteral("2600"));
    theme.addItem(QStringLiteral("WarGames"), QStringLiteral("wargames"));
    theme.addItem(QStringLiteral("CRT Green"), QStringLiteral("crt-green"));
    theme.addItem(QStringLiteral("VT220"), QStringLiteral("vt220"));
    theme.addItem(QStringLiteral("Cobalt"), QStringLiteral("cobalt"));
    theme.addItem(QStringLiteral("Vaporwave"), QStringLiteral("vaporwave"));
    theme.addItem(QStringLiteral("Stealth"), QStringLiteral("stealth"));
    theme.addItem(QStringLiteral("Synthwave"), QStringLiteral("synthwave"));
    theme.addItem(QStringLiteral("C64"), QStringLiteral("c64"));
    theme.addItem(QStringLiteral("DOS"), QStringLiteral("dos"));
    theme.addItem(QStringLiteral("Waffle Iron"), QStringLiteral("waffle-iron"));
    theme.addItem(QStringLiteral("Ghostline"), QStringLiteral("ghostline"));
    theme.addItem(QStringLiteral("Hot Dog Stand"), QStringLiteral("hot-dog-stand"));
    theme.addItem(QStringLiteral("Neon Miami"), QStringLiteral("neon-miami"));
    const int themeIndex = theme.findData(m_options.theme);
    if (themeIndex >= 0) theme.setCurrentIndex(themeIndex);
    form->addRow(QStringLiteral("Theme:"), &theme);

    QCheckBox timestamps(QStringLiteral("Show timestamps in conversations"), &dialog);
    timestamps.setChecked(m_options.showTimestamps);
    form->addRow(QString(), &timestamps);

    QCheckBox sidePanes(QStringLiteral("Show room member side panes"), &dialog);
    sidePanes.setChecked(m_options.showSidePanes);
    form->addRow(QString(), &sidePanes);

    QCheckBox encrypted(QStringLiteral("Enable CPX3 encrypted communications (DMs + rooms)"), &dialog);
    encrypted.setChecked(m_options.encryptedDmEnabled);
    form->addRow(QString(), &encrypted);

    QCheckBox autoReply(QStringLiteral("Automatically reply to secure handshakes"), &dialog);
    autoReply.setChecked(m_options.autoReplySecure);
    form->addRow(QString(), &autoReply);

    QCheckBox fingerprints(QStringLiteral("Show secure fingerprint notices"), &dialog);
    fingerprints.setChecked(m_options.showSecureFingerprints);
    form->addRow(QString(), &fingerprints);

    QCheckBox autoPresence(QStringLiteral("Report native AIM/OSCAR idle time to the server"), &dialog);
    autoPresence.setChecked(m_options.autoPresenceEnabled);
    autoPresence.setToolTip(QStringLiteral("Uses OSCAR Generic Service idle notification (0x0001/0x0011). WaffleHouse does not generate auto-away states; Away/AFK remains server-native/manual."));
    form->addRow(QString(), &autoPresence);

    QLabel identity(&dialog);
    if (BackendState *state = selectedState(); m_secureReady && state) {
        identity.setText(QStringLiteral("Selected profile fingerprint:\n%1")
                             .arg(m_secure.localFingerprint(state->profileId)));
    } else if (!m_secureReady) {
        identity.setText(QStringLiteral("Encrypted communications unavailable: %1").arg(m_secureError));
    } else {
        identity.setText(QStringLiteral("Select a connection to view its secure fingerprint."));
    }
    identity.setTextInteractionFlags(Qt::TextSelectableByMouse);
    identity.setWordWrap(true);
    outer->addLayout(form);
    outer->addWidget(&identity);

    auto *notifyBox = new QGroupBox(QStringLiteral("Notification Sounds"), &dialog);
    auto *notifyLayout = new QGridLayout(notifyBox);
    auto *notifyMaster = new QCheckBox(QStringLiteral("Enable notification sounds"), notifyBox);
    notifyMaster->setChecked(NotificationManager::globalEnabled());
    notifyLayout->addWidget(notifyMaster, 0, 0, 1, 5);
    auto *notifyHint = new QLabel(QStringLiteral("Built-in sounds are included. Choose Custom to use your own audio file (WAV is recommended). GUI and CLI share these settings."), notifyBox);
    notifyHint->setWordWrap(true);
    notifyHint->setObjectName(QStringLiteral("Muted"));
    notifyLayout->addWidget(notifyHint, 1, 0, 1, 5);

    struct NotificationRow {
        NotificationManager::Event event;
        QCheckBox *enabled = nullptr;
        QComboBox *source = nullptr;
        QLineEdit *path = nullptr;
        QPushButton *browse = nullptr;
        QPushButton *test = nullptr;
    };
    QList<NotificationRow> notificationRows;
    int notifyRow = 2;
    for (const auto event : NotificationManager::configurableEvents()) {
        const auto cfg = NotificationManager::setting(event);
        NotificationRow row;
        row.event = event;
        row.enabled = new QCheckBox(NotificationManager::displayName(event), notifyBox);
        row.enabled->setChecked(cfg.enabled);
        row.source = new QComboBox(notifyBox);
        row.source->addItem(QStringLiteral("Built-in"), QStringLiteral("builtin"));
        row.source->addItem(QStringLiteral("Custom file"), QStringLiteral("custom"));
        row.source->addItem(QStringLiteral("None"), QStringLiteral("none"));
        row.path = new QLineEdit(notifyBox);
        row.path->setPlaceholderText(QStringLiteral("/path/to/notification.wav"));
        row.browse = new QPushButton(QStringLiteral("Browse…"), notifyBox);
        row.test = new QPushButton(QStringLiteral("Test"), notifyBox);

        if (NotificationManager::isCustomSpec(cfg.soundSpec)) {
            row.source->setCurrentIndex(row.source->findData(QStringLiteral("custom")));
            row.path->setText(NotificationManager::customPath(cfg.soundSpec));
        } else if (cfg.soundSpec.compare(QStringLiteral("none"), Qt::CaseInsensitive) == 0
                   || cfg.soundSpec.compare(QStringLiteral("off"), Qt::CaseInsensitive) == 0) {
            row.source->setCurrentIndex(row.source->findData(QStringLiteral("none")));
        } else {
            row.source->setCurrentIndex(row.source->findData(QStringLiteral("builtin")));
        }

        auto updateCustom = [source=row.source, path=row.path, browse=row.browse] {
            const bool custom = source->currentData().toString() == QStringLiteral("custom");
            path->setEnabled(custom);
            browse->setEnabled(custom);
        };
        updateCustom();
        connect(row.source, &QComboBox::currentIndexChanged, &dialog, [updateCustom](int) { updateCustom(); });
        connect(row.browse, &QPushButton::clicked, &dialog, [path=row.path, &dialog] {
            const QString selected = QFileDialog::getOpenFileName(
                &dialog, QStringLiteral("Choose Notification Sound"), path->text(),
                QStringLiteral("Audio files (*.wav *.ogg *.oga *.flac *.mp3);;All files (*)"));
            if (!selected.isEmpty()) path->setText(selected);
        });
        connect(row.test, &QPushButton::clicked, &dialog, [event, source=row.source, path=row.path] {
            const QString mode = source->currentData().toString();
            QString spec = QStringLiteral("none");
            if (mode == QStringLiteral("builtin")) spec = NotificationManager::builtinSpec(event);
            else if (mode == QStringLiteral("custom")) spec = NotificationManager::customSpec(path->text());
            if (!NotificationManager::playSpec(spec, false) && mode != QStringLiteral("none")) QApplication::beep();
        });

        notifyLayout->addWidget(row.enabled, notifyRow, 0);
        notifyLayout->addWidget(row.source, notifyRow, 1);
        notifyLayout->addWidget(row.path, notifyRow, 2);
        notifyLayout->addWidget(row.browse, notifyRow, 3);
        notifyLayout->addWidget(row.test, notifyRow, 4);
        notificationRows.push_back(row);
        ++notifyRow;
    }
    notifyLayout->setColumnStretch(2, 1);
    outer->addWidget(notifyBox);

    QDialogButtonBox buttons(QDialogButtonBox::Cancel | QDialogButtonBox::Save, &dialog);
    outer->addWidget(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) return;

    m_options.theme = theme.currentData().toString();
    m_options.showTimestamps = timestamps.isChecked();
    m_options.showSidePanes = sidePanes.isChecked();
    m_options.encryptedDmEnabled = encrypted.isChecked();
    m_options.autoReplySecure = autoReply.isChecked();
    m_options.showSecureFingerprints = fingerprints.isChecked();
    m_options.autoPresenceEnabled = autoPresence.isChecked();
    if (!m_options.autoPresenceEnabled) markUserActivity();
    NotificationManager::setGlobalEnabled(notifyMaster->isChecked());
    for (const NotificationRow &row : notificationRows) {
        NotificationManager::Setting cfg;
        cfg.enabled = row.enabled->isChecked();
        const QString mode = row.source->currentData().toString();
        if (mode == QStringLiteral("custom")) cfg.soundSpec = NotificationManager::customSpec(row.path->text());
        else if (mode == QStringLiteral("none")) cfg.soundSpec = QStringLiteral("none");
        else cfg.soundSpec = NotificationManager::builtinSpec(row.event);
        NotificationManager::setSetting(row.event, cfg);
    }
    saveOptions();
    applyTheme();
}

void MainWindow::showHelpDialog()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("%1 Help").arg(appDisplayName()));
    dialog.resize(580, 460);
    auto *outer = new QVBoxLayout(&dialog);
    auto *help = new QPlainTextEdit(&dialog);
    help->setReadOnly(true);
    help->setPlainText(QStringLiteral(
        "%1\nVersion %3\n\n"
        "%2 HELP\n\n"
        "CONNECTIONS\n"
        "  Accounts > Account Management > Add... creates AIM/OSCAR, IRC, Telnet/BBS, or SIP/VoIP profiles.\n"
        "  New profiles start with no protocol selected. Secrets are not saved unless you opt in.\n"
        "  Accounts begins with Account Management, followed by one submenu per saved account.\n"
        "  AIM/IRC account menus provide IM / Chatroom and Add / Remove Buddies windows.\n"
        "  Tools contains Open Softphone, Import BBS List, File Transfer Log / Activity, AIM password, fingerprint, raw protocol commands, and Options.\n"
        "  Media contains local audio/video, SHOUTcast/Icecast, HTTP/HLS streams, and playlist controls.\n\n"
        "SIP / VOIP SOFTPHONE\n"
        "  SIP accounts are normal saved WaffleHouse-Client accounts and remain visible in the main Accounts tree.\n"
        "  The main-window navigation rail is removed; Tools > Open Softphone and the tray menu open the full phone workspace.\n"
        "  SIP contacts remain available from the account management menus and the Softphone profile/workspace.\n"
        "  Tools > Open Softphone also opens the full phone workspace.\n"
        "  Softphone > Profile edits the same saved SIP account as Accounts > Account Management > Edit Selected.\n"
        "  Softphone left rail: Phone, Active Calls, SIP Log, SIP Ladder, Profile, and Activity.\n"
        "  The Phone page includes Prefix, Destination, Caller ID, a live status strip, and a telephone dial pad.\n\n"
        "TELNET / MUD / BBS\n"
        "  Add a Telnet profile, choose host/port and terminal type, then connect.\n"
        "  A terminal session window opens automatically. Closing it disconnects that Telnet profile.\n"
        "  Telnet is plaintext; credentials and traffic are not encrypted by the Telnet protocol.\n\n"
        "SECURE PRIVATE MESSAGES\n"
        "  Secure DMs interoperate with WaffleHouse-Client, WaffleHouse 1.9.1 family clients, GhostPulse, CrossPoint, and legacy CPX3-compatible clients.\n"
        "  Encryption applies to AIM/OSCAR and IRC private messages; 3.1 also supports CPX secure AIM/IRC rooms.\n"
        "  Telnet traffic, routing metadata, and server-visible endpoints remain outside CPX encryption.\n\n"
        "  1. Open an IM with another compatible WaffleHouse/CPX3-compatible user.\n"
        "  2. Choose Security > Start Secure Session.\n"
        "  3. WaffleHouse automatically saves the first authenticated peer identity.\n"
        "  4. Type normally. Messages are encrypted automatically while the secure session is active.\n"
        "  5. If that peer identity changes later, WaffleHouse blocks the secure session and warns you.\n\n"
        "SECURE AIM / IRC ROOMS\n"
        "  Open an AIM chat room or IRC channel and choose Security > Start Secure Room (or type /secure).\n"
        "  WaffleHouse creates an XChaCha20-Poly1305 shared room key and delivers it only through established CPX encrypted PM sessions.\n"
        "  Public room traffic contains CPXROOM ciphertext. WaffleHouse-Client peers with the key display [secure-room] plaintext locally; ordinary traffic is marked [plaintext].\n"
        "  The key owner rotates the room key when membership changes and redistributes it to current secure peers.\n\n"
        "  The first authenticated peer identity is saved automatically (trust on first use).\n"
        "  If that peer later presents a different key, the client rejects that secure session.\n"
        "  Security > Forget Trusted Fingerprint removes saved trust.\n"
        "  Security > Close Secure Session returns that conversation to plaintext.\n\n"
        "FILE TRANSFER\n"
        "  Choose Send File from an AIM/IRC private-message window, then select Secure or Unsecured.\n"
        "  Secure uses CPX encryption/authentication and requires a verified secure session; the dialog explains setup if one is not active.\n"
        "  Unsecured proceeds over ordinary AIM/IRC PM transport without CPX encryption/authentication.\n"
        "  Both modes retain chunking/resume and SHA-256 verification; incoming transfers require explicit acceptance and a destination path.\n\n"
        "WAFFLECAST\n"
        "  Media Center > Start Broadcast turns the current track/queue into a direct live WaffleCast stream.\n"
        "  Type /wafflecast (or /wcast) in an AIM IM, IRC PM, or IRC channel to send a native Listen invite.\n"
        "  Audio is direct client-to-client; the advertised host/port must be reachable. Embedded cover art and track titles update in listener Media Centers.\n\n"
        "CONTEXTUAL GUI ACTIONS\n"
        "  The main window intentionally has no command bar and Tools has no Command Palette.\n"
        "  Global actions live in File, View, Tools, Media, and Help; account lifecycle actions live in Accounts and the Add / Remove / Edit / Connect-Disconnect row.\n"
        "  Right-click an account for protocol-specific actions such as messaging, rooms, server capabilities, AIM presence/profile/privacy/account administration, and advanced OSCAR controls.\n"
        "  Right-click a buddy/contact for user-specific actions such as IM, file transfer, profile/WHOIS, permit/deny, authorization, client-version checks, and OSCAR voice.\n"
        "  SIP call controls and diagnostics live in the Softphone; media commands live in the Media menu/Media Center; transfer controls live in File Transfer Log / Activity.\n"
        "  Protocol-native slash commands remain available only inside conversation/session windows where they belong (for example IRC channel commands and secure-session aliases).\n\n"
        "AIM / OSCAR USER FEATURES\n"
        "  Right-click an AIM account for capability-gated Presence/Profile, Privacy/Authorization, Discovery/Invitations, Account Administration, Feature Center, and advanced raw OSCAR controls.\n"
        "  Right-click an AIM buddy for profile/user/directory info, temporary watch, permit/deny, authorization, IM/file, client-version, copy/remove, and OSCAR voice actions.\n"
        "  Unsupported server foodgroups remain visible but disabled; peer-specific rendezvous actions are additionally gated by the buddy's advertised capability UUIDs.\n"
        "  Native OSCAR typing notifications are sent from AIM IM windows and displayed in the conversation header.\n"
        "  User Info displays profile, away message, warning level, sign-on/member/idle/online data and advertised capability UUIDs.\n"
        "  WaffleHouse OSCAR Voice uses OSCAR channel-2 rendezvous signaling plus an open, unencrypted WaffleHouse PCM/UDP media payload.\n"
        "  Legacy AIM Talk/Voice, standard Direct IM/File Transfer, Buddy Icon, and other peer capabilities are detected; unsupported legacy transports are not falsely advertised as interoperable.\n\n"
        "SLASH ALIASES INSIDE CONVERSATION WINDOWS\n"
        "  Tab          complete/cycle matching slash commands\n"
        "  Shift+Tab    cycle matching commands backward\n"
        "  /options     open Options\n"
        "  /help        open this Help window\n"
        "  /fingerprint show this connection profile's local secure fingerprint\n"
        "  /secure      start a secure IM, or start/rotate secure-room mode in an AIM/IRC room\n"
        "  /securestatus show IM trust state or the active room-key status\n"
        "  /trust       trust the active peer fingerprint\n"
        "  /untrust     forget the trusted peer fingerprint\n"
        "  /secureoff   close the secure session\n"
        "  /wafflecast  invite this AIM/IRC conversation to your active WaffleCast (/wcast alias)\n\n"
        "IRC SLASH COMMANDS (IRC CONVERSATIONS)\n"
        "  /nick NEWNICK          change nickname\n"
        "  /op NICK... /deop...   grant/remove channel operator\n"
        "  /voice NICK... /devoice grant/remove channel voice\n"
        "  /kick NICK [reason]     kick from the active channel\n"
        "  /ban NICK|MASK /unban   set/remove channel ban\n"
        "  /topic, /mode, /me, /notice, /invite, /who, /whois, /whowas, /ison, /list, /motd\n"
        "  /raw or /quote COMMAND  send an IRC protocol line directly\n"
        "  Unknown /text is sent as normal chat text (and remains CPX-encrypted when the conversation is secured).\n\n"
        "RUNTIME ENVIRONMENT\n"
        "  %4\n"
        "  Graphical-terminal sessions are distinguished from desktop launches and console-only TTYs.\n\n"
        "THEMES\n"
        "  Use Tools > Options or Ctrl+, for full settings.\n"
        "  View > Theme provides the complete WaffleHouse-Client theme collection.\n")
        .arg(appAsciiLogo(), appDisplayName(), appVersionString(), RuntimeEnvironment::detect().summary()));
    outer->addWidget(help, 1);
    QDialogButtonBox close(QDialogButtonBox::Close, &dialog);
    outer->addWidget(&close);
    connect(&close, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(&close, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    dialog.exec();
}

void MainWindow::loadConnections()
{
    if (!m_connectionList) return;

    QSettings settings;
    const int count = settings.beginReadArray(QStringLiteral("connections"));
    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);
        ConnectionSettings value;
        const int protocolValue = settings.value(
            QStringLiteral("protocol"),
            static_cast<int>(ConnectionSettings::Protocol::Unknown)).toInt();
        if (protocolValue == static_cast<int>(ConnectionSettings::Protocol::Oscar))
            value.protocol = ConnectionSettings::Protocol::Oscar;
        else if (protocolValue == static_cast<int>(ConnectionSettings::Protocol::Irc))
            value.protocol = ConnectionSettings::Protocol::Irc;
        else if (protocolValue == static_cast<int>(ConnectionSettings::Protocol::Telnet))
            value.protocol = ConnectionSettings::Protocol::Telnet;
        else if (protocolValue == static_cast<int>(ConnectionSettings::Protocol::Sip))
            value.protocol = ConnectionSettings::Protocol::Sip;
        else if (protocolValue == static_cast<int>(ConnectionSettings::Protocol::Xmpp))
            value.protocol = ConnectionSettings::Protocol::Xmpp;
        else if (protocolValue == static_cast<int>(ConnectionSettings::Protocol::Ssh))
            value.protocol = ConnectionSettings::Protocol::Ssh;
        else if (protocolValue == static_cast<int>(ConnectionSettings::Protocol::Nntp))
            value.protocol = ConnectionSettings::Protocol::Nntp;
        else
            continue;

        if (!BuildFeatures::protocolEnabled(value.protocol)) continue;

        value.server = settings.value(QStringLiteral("server")).toString();
        const int defaultPort = value.protocol == ConnectionSettings::Protocol::Irc ? 6667
            : value.protocol == ConnectionSettings::Protocol::Telnet ? 23
            : value.protocol == ConnectionSettings::Protocol::Sip ? 5060
            : value.protocol == ConnectionSettings::Protocol::Xmpp ? 5222
            : value.protocol == ConnectionSettings::Protocol::Ssh ? 22
            : value.protocol == ConnectionSettings::Protocol::Nntp ? 563 : 5190;
        value.port = static_cast<quint16>(settings.value(QStringLiteral("port"), defaultPort).toUInt());
        value.username = settings.value(QStringLiteral("username")).toString();
        value.accountLabel = settings.value(QStringLiteral("accountLabel")).toString();
        value.networkProfile = settings.value(QStringLiteral("networkProfile"), QStringLiteral("auto")).toString();
        value.arsHost = settings.value(QStringLiteral("arsHost")).toString();
        value.arsPort = static_cast<quint16>(settings.value(QStringLiteral("arsPort"), 5190).toUInt());
        value.redirectHost = settings.value(QStringLiteral("redirectHost")).toString();
        value.redirectPort = static_cast<quint16>(settings.value(QStringLiteral("redirectPort"), 0).toUInt());
        value.oscarDebugMode = settings.value(QStringLiteral("oscarDebugMode"), QStringLiteral("off")).toString();
        value.oscarProfile = settings.value(QStringLiteral("oscarProfile")).toString();
        value.realName = settings.value(QStringLiteral("realName"), appDefaultRealName()).toString();
        value.tls = settings.value(QStringLiteral("tls"), false).toBool();
        value.ircBuddies = settings.value(QStringLiteral("ircBuddies")).toStringList();
        value.sipContacts = settings.value(QStringLiteral("sipContacts")).toStringList();
        value.telnetTerminalType = settings.value(QStringLiteral("telnetTerminalType"), QStringLiteral("ANSI")).toString();
        value.telnetColumns = settings.value(QStringLiteral("telnetColumns"), 80).toInt();
        value.telnetRows = settings.value(QStringLiteral("telnetRows"), 24).toInt();
        value.telnetAutoFit = settings.value(QStringLiteral("telnetAutoFit"), true).toBool();
        value.sshIdentityFile = settings.value(QStringLiteral("sshIdentityFile")).toString();
        value.sshOptions = settings.value(QStringLiteral("sshOptions")).toStringList();
        value.sshRemoteCommand = settings.value(QStringLiteral("sshRemoteCommand")).toString();
        value.sipProfileName = settings.value(QStringLiteral("sipProfileName")).toString();
        value.sipDomain = settings.value(QStringLiteral("sipDomain"), value.protocol == ConnectionSettings::Protocol::Sip ? value.server : QString()).toString();
        value.sipRegistrar = settings.value(QStringLiteral("sipRegistrar")).toString();
        value.sipAuthUsername = settings.value(QStringLiteral("sipAuthUsername")).toString();
        value.sipDisplayName = settings.value(QStringLiteral("sipDisplayName")).toString();
        value.sipOutboundProxy = settings.value(QStringLiteral("sipOutboundProxy")).toString();
        value.sipCallerIdDomain = settings.value(QStringLiteral("sipCallerIdDomain")).toString();
        value.sipDialPrefix = settings.value(QStringLiteral("sipDialPrefix")).toString();
        value.sipStunServer = settings.value(QStringLiteral("sipStunServer")).toString();
        value.sipTransport = settings.value(QStringLiteral("sipTransport"), QStringLiteral("udp")).toString();
        value.sipIdentityMode = settings.value(QStringLiteral("sipIdentityMode"), QStringLiteral("from")).toString();
        value.sipCompatibility = settings.value(QStringLiteral("sipCompatibility"), QStringLiteral("auto")).toString();
        value.sipLocalPort = static_cast<quint16>(settings.value(QStringLiteral("sipLocalPort"), value.protocol == ConnectionSettings::Protocol::Sip ? value.port : 5060).toUInt());
        value.sipRegistrationExpires = settings.value(QStringLiteral("sipRegistrationExpires"), 300).toUInt();
        value.sipUseIce = settings.value(QStringLiteral("sipUseIce"), false).toBool();
        value.sipEnableSrtp = settings.value(QStringLiteral("sipEnableSrtp"), false).toBool();
        value.debug = settings.value(QStringLiteral("debug"), false).toBool();
        value.savePassword = settings.value(QStringLiteral("savePassword"), false).toBool();
        value.password = value.savePassword
            ? settings.value(QStringLiteral("password")).toString()
            : QString();

        const bool defaultSecretRequired = value.protocol == ConnectionSettings::Protocol::Oscar
            || value.protocol == ConnectionSettings::Protocol::Sip
            || value.protocol == ConnectionSettings::Protocol::Xmpp
            || (value.protocol == ConnectionSettings::Protocol::Nntp && !value.username.trimmed().isEmpty());
        const bool secretRequired = settings.value(QStringLiteral("secretRequired"), defaultSecretRequired).toBool();

        QString profileId = settings.value(QStringLiteral("id")).toString().trimmed();
        if (profileId.isEmpty()) {
            const QString seed = QStringLiteral("%1|%2|%3|%4|%5")
                .arg(static_cast<int>(value.protocol))
                .arg(value.server.toCaseFolded())
                .arg(value.port)
                .arg(value.username.toCaseFolded())
                .arg(i);
            profileId = QStringLiteral("migrated-%1").arg(QString::fromLatin1(
                QCryptographicHash::hash(seed.toUtf8(), QCryptographicHash::Sha256)
                    .left(16).toHex()));
        }

        ChatBackend *backend = createBackend(value);
        if (backend) {
            attachBackend(backend, false, secretRequired, !value.password.isEmpty(), false, profileId);
        }
    }
    settings.endArray();

    if (m_connectionList->count() > 0) {
        m_connectionList->setCurrentRow(0);
        saveConnections();
    }
}

void MainWindow::saveConnections() const
{
    if (!m_connectionList) return;
    QSettings settings;
    settings.remove(QStringLiteral("connections"));
    settings.beginWriteArray(QStringLiteral("connections"));

    int outputIndex = 0;
    for (int row = 0; row < m_connectionList->count(); ++row) {
        QListWidgetItem *item = m_connectionList->item(row);
        if (!item) continue;
        BackendState *state = stateById(item->data(Qt::UserRole).toString());
        if (!state || !state->backend) continue;

        const ConnectionSettings &value = state->backend->settings();
        settings.setArrayIndex(outputIndex++);
        settings.setValue(QStringLiteral("id"), state->profileId);
        settings.setValue(QStringLiteral("protocol"), static_cast<int>(value.protocol));
        settings.setValue(QStringLiteral("server"), value.server);
        settings.setValue(QStringLiteral("port"), value.port);
        settings.setValue(QStringLiteral("username"), value.username);
        settings.setValue(QStringLiteral("accountLabel"), value.accountLabel);
        settings.setValue(QStringLiteral("networkProfile"), value.networkProfile);
        settings.setValue(QStringLiteral("arsHost"), value.arsHost);
        settings.setValue(QStringLiteral("arsPort"), value.arsPort);
        settings.setValue(QStringLiteral("redirectHost"), value.redirectHost);
        settings.setValue(QStringLiteral("redirectPort"), value.redirectPort);
        settings.setValue(QStringLiteral("oscarDebugMode"), value.oscarDebugMode);
        settings.setValue(QStringLiteral("oscarProfile"), value.oscarProfile);
        settings.setValue(QStringLiteral("realName"), value.realName);
        settings.setValue(QStringLiteral("tls"), value.tls);
        settings.setValue(QStringLiteral("ircBuddies"), value.ircBuddies);
        settings.setValue(QStringLiteral("sipContacts"), value.sipContacts);
        settings.setValue(QStringLiteral("telnetTerminalType"), value.telnetTerminalType);
        settings.setValue(QStringLiteral("telnetColumns"), value.telnetColumns);
        settings.setValue(QStringLiteral("telnetRows"), value.telnetRows);
        settings.setValue(QStringLiteral("telnetAutoFit"), value.telnetAutoFit);
        settings.setValue(QStringLiteral("sshIdentityFile"), value.sshIdentityFile);
        settings.setValue(QStringLiteral("sshOptions"), value.sshOptions);
        settings.setValue(QStringLiteral("sshRemoteCommand"), value.sshRemoteCommand);
        settings.setValue(QStringLiteral("sipProfileName"), value.sipProfileName);
        settings.setValue(QStringLiteral("sipDomain"), value.sipDomain);
        settings.setValue(QStringLiteral("sipRegistrar"), value.sipRegistrar);
        settings.setValue(QStringLiteral("sipAuthUsername"), value.sipAuthUsername);
        settings.setValue(QStringLiteral("sipDisplayName"), value.sipDisplayName);
        settings.setValue(QStringLiteral("sipOutboundProxy"), value.sipOutboundProxy);
        settings.setValue(QStringLiteral("sipCallerIdDomain"), value.sipCallerIdDomain);
        settings.setValue(QStringLiteral("sipDialPrefix"), value.sipDialPrefix);
        settings.setValue(QStringLiteral("sipStunServer"), value.sipStunServer);
        settings.setValue(QStringLiteral("sipTransport"), value.sipTransport);
        settings.setValue(QStringLiteral("sipIdentityMode"), value.sipIdentityMode);
        settings.setValue(QStringLiteral("sipCompatibility"), value.sipCompatibility);
        settings.setValue(QStringLiteral("sipLocalPort"), value.sipLocalPort);
        settings.setValue(QStringLiteral("sipRegistrationExpires"), value.sipRegistrationExpires);
        settings.setValue(QStringLiteral("sipUseIce"), value.sipUseIce);
        settings.setValue(QStringLiteral("sipEnableSrtp"), value.sipEnableSrtp);
        settings.setValue(QStringLiteral("debug"), value.debug);
        settings.setValue(QStringLiteral("secretRequired"), state->secretRequired);
        settings.setValue(QStringLiteral("savePassword"), value.savePassword);
        if (value.savePassword && !value.password.isEmpty()) {
            settings.setValue(QStringLiteral("password"), value.password);
        }
    }
    settings.endArray();
    settings.sync();
}

bool MainWindow::ensureConnectionSecret(BackendState *state)
{
    if (!state || !state->backend) {
        return false;
    }
    if (!state->secretRequired || state->hasSessionSecret) {
        return true;
    }

    const auto protocol = state->backend->settings().protocol;
    QString title;
    QString label;
    bool allowEmpty = false;

    switch (protocol) {
    case ConnectionSettings::Protocol::Oscar:
        title = QStringLiteral("AIM / OSCAR Password");
        label = QStringLiteral("Password:");
        break;
    case ConnectionSettings::Protocol::Irc:
        title = QStringLiteral("IRC Server Password");
        label = QStringLiteral("Server password (leave blank if none):");
        allowEmpty = true;
        break;
    case ConnectionSettings::Protocol::Telnet:
        return true;
    case ConnectionSettings::Protocol::Sip:
        title = QStringLiteral("SIP Account Password");
        label = QStringLiteral("SIP password:");
        break;
    case ConnectionSettings::Protocol::Xmpp:
        title = QStringLiteral("XMPP / Jabber Password");
        label = QStringLiteral("XMPP password:");
        break;
    case ConnectionSettings::Protocol::Nntp:
        title = QStringLiteral("NNTP / Usenet Password");
        label = QStringLiteral("NNTP password (leave blank for anonymous access):");
        allowEmpty = true;
        break;
    case ConnectionSettings::Protocol::Ssh:
        return true;
    case ConnectionSettings::Protocol::Unknown:
        return false;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(title);
    dialog.setModal(true);

    auto *outer = new QVBoxLayout(&dialog);
    outer->setContentsMargins(16, 16, 16, 16);
    outer->setSpacing(10);

    auto *promptLabel = new QLabel(label, &dialog);
    auto *secretEdit = new QLineEdit(&dialog);
    secretEdit->setEchoMode(QLineEdit::Password);
    secretEdit->setMinimumWidth(280);
    auto *savePassword = new QCheckBox(QStringLiteral("Save password on this computer"), &dialog);
    savePassword->setChecked(false);
    savePassword->setToolTip(
        QStringLiteral("Stores this password in the local WaffleHouse-Client settings file. "
                       "The saved value is not encrypted at rest."));

    auto *note = new QLabel(
        QStringLiteral("Saved passwords are stored in your local application settings and "
                       "are not encrypted at rest."),
        &dialog);
    note->setWordWrap(true);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Cancel | QDialogButtonBox::Ok, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(secretEdit, &QLineEdit::textChanged, savePassword,
                     [savePassword](const QString &text) {
                         savePassword->setEnabled(!text.isEmpty());
                         if (text.isEmpty()) savePassword->setChecked(false);
                     });
    savePassword->setEnabled(false);

    outer->addWidget(promptLabel);
    outer->addWidget(secretEdit);
    outer->addWidget(savePassword);
    outer->addWidget(note);
    outer->addWidget(buttons);
    secretEdit->setFocus();

    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }

    const QString secret = secretEdit->text();
    if (!allowEmpty && secret.isEmpty()) {
        QMessageBox::warning(
            this, title, QStringLiteral("A password is required to connect."));
        return false;
    }

    ConnectionSettings updated = state->backend->settings();
    updated.password = secret;
    updated.savePassword = savePassword->isChecked() && !secret.isEmpty();
    state->backend->setConnectionSettings(updated);
    state->hasSessionSecret = true;
    saveConnections();
    return true;
}

ChatBackend *MainWindow::createBackend(const ConnectionSettings &settings)
{
    if (!BuildFeatures::protocolEnabled(settings.protocol)) {
        return nullptr;
    }
    switch (settings.protocol) {
    case ConnectionSettings::Protocol::Oscar:
        return new OscarBackend(settings, this);
    case ConnectionSettings::Protocol::Irc:
        return new IrcBackend(settings, this);
    case ConnectionSettings::Protocol::Telnet:
        return new TelnetBackend(settings, this);
    case ConnectionSettings::Protocol::Sip:
        return new SipBackend(settings, m_sipController, this);
    case ConnectionSettings::Protocol::Xmpp:
        return new XmppBackend(settings, this);
    case ConnectionSettings::Protocol::Ssh:
        return new SshBackend(settings, this);
    case ConnectionSettings::Protocol::Nntp:
        return new NntpBackend(settings, this);
    case ConnectionSettings::Protocol::Unknown:
        break;
    }
    return nullptr;
}

void MainWindow::wireBackend(ChatBackend *backend)
{
    if (!backend) {
        return;
    }

    connect(backend, &ChatBackend::connected, this,
            [this, backend](const QString &identity, const QString &endpoint) {
                handleConnected(backend, identity, endpoint);
            }, Qt::QueuedConnection);
    connect(backend, &ChatBackend::disconnected, this,
            [this, backend](const QString &reason) {
                handleDisconnected(backend, reason);
            }, Qt::QueuedConnection);
    connect(backend, &ChatBackend::eventReceived, this,
            [this, backend](const QString &kind, const QString &target, const QString &text) {
                handleEvent(backend, kind, target, text);
            }, Qt::QueuedConnection);
    connect(backend, &ChatBackend::membersChanged, this,
            [this, backend](const QString &room, const QString &action,
                            const QStringList &names) {
                handleMembers(backend, room, action, names);
            }, Qt::QueuedConnection);
    connect(backend, &ChatBackend::targetNamed, this,
            [this, backend](const QString &kind, const QString &target,
                            const QString &name) {
                handleTargetNamed(backend, kind, target, name);
            }, Qt::QueuedConnection);
    connect(backend, &ChatBackend::roomDiscovered, this,
            [this, backend](const QString &roomId, const QString &name) {
                handleRoomDiscovered(backend, roomId, name);
            }, Qt::QueuedConnection);
    connect(backend, &ChatBackend::buddyListChanged, this,
            [this, backend](const QStringList &names) {
                handleBuddyList(backend, names);
            }, Qt::QueuedConnection);
    connect(backend, &ChatBackend::buddyPresenceChanged, this,
            [this, backend](const QString &name, bool online) {
                handleBuddyPresence(backend, name, online);
            }, Qt::QueuedConnection);
    connect(backend, &ChatBackend::backendError, this,
            [this, backend](const QString &context, const QString &message) {
                handleBackendError(backend, context, message);
            }, Qt::QueuedConnection);
    if (auto *oscar = qobject_cast<OscarBackend *>(backend)) {
        connect(oscar, &OscarBackend::buddyNativePresenceChanged, this,
                [this, backend](const QString &name, const QVariantMap &presence) {
                    if (BackendState *state = stateFor(backend)) {
                        const QString key = name.trimmed().toCaseFolded();
                        if (key.isEmpty()) return;
                        const bool online = presence.value(QStringLiteral("online"), true).toBool();
                        if (online) {
                            // LOCATE hydration intentionally carries only live
                            // presence fields. Merge it into the last BUDDY
                            // packet so capabilities/sign-on metadata are not
                            // discarded on every refresh.
                            QVariantMap merged = state->oscarBuddyPresence.value(key);
                            for (auto it = presence.cbegin(); it != presence.cend(); ++it)
                                merged.insert(it.key(), it.value());
                            state->oscarBuddyPresence.insert(key, merged);
                        } else {
                            // Offline must replace the record so stale Away text
                            // and idle values cannot survive a departure event.
                            state->oscarBuddyPresence.insert(key, presence);
                        }
                        state->buddies.insert(name.trimmed());
                        if (online) state->onlineBuddies.insert(key);
                        else state->onlineBuddies.remove(key);
                        refreshBuddyList();
                    }
                }, Qt::QueuedConnection);
        connect(oscar, &OscarBackend::userInfoReceived, this,
                [this, backend](const QString &name, const QVariantMap &info) {
                    if (BackendState *state = stateFor(backend)) {
                        const QString clean = name.trimmed();
                        const QString key = clean.toCaseFolded();
                        const bool knownBuddy = std::any_of(state->buddies.cbegin(), state->buddies.cend(),
                            [&clean](const QString &buddy) { return buddy.compare(clean, Qt::CaseInsensitive) == 0; });
                        if (key.isEmpty() || !knownBuddy || !state->onlineBuddies.contains(key)) return;
                        QVariantMap native = state->oscarBuddyPresence.value(key);
                        native.insert(QStringLiteral("online"), true);
                        native.insert(QStringLiteral("state"),
                                      info.value(QStringLiteral("presence"), native.value(QStringLiteral("state"), QStringLiteral("Online"))));
                        native.insert(QStringLiteral("awayMessage"), info.value(QStringLiteral("awayMessage")));
                        native.insert(QStringLiteral("idleMinutes"), info.value(QStringLiteral("idleMinutes"), 0));
                        native.insert(QStringLiteral("idleSeconds"), info.value(QStringLiteral("idleSeconds"), 0));
                        native.insert(QStringLiteral("userFlags"), info.value(QStringLiteral("userFlags"), native.value(QStringLiteral("userFlags"))));
                        native.insert(QStringLiteral("statusSupplied"), info.value(QStringLiteral("statusSupplied"), false));
                        native.insert(QStringLiteral("statusRaw"), info.value(QStringLiteral("statusRaw"), -1));
                        state->oscarBuddyPresence.insert(key, native);
                        state->onlineBuddies.insert(key);
                        refreshBuddyList();
                    }
                }, Qt::QueuedConnection);
        connect(oscar, &OscarBackend::presenceChanged, this,
                [this, backend](const QString &presence, const QString &message, quint32 idleSeconds) {
                    if (BackendState *state = stateFor(backend)) {
                        state->presenceState = presence;
                        state->presenceMessage = message;
                        state->idleSeconds = idleSeconds;
                        updateConnectionItem(state);
                        refreshBuddyList();
                        QString text = presence;
                        if (idleSeconds > 0) text += QStringLiteral(" + IDLE %1s").arg(idleSeconds);
                        if (!message.isEmpty()) text += QStringLiteral(" — %1").arg(message);
                        statusBar()->showMessage(QStringLiteral("AIM presence: %1").arg(text), 4000);
                    }
                }, Qt::QueuedConnection);
        connect(oscar, &OscarBackend::serverCapabilitiesChanged, this,
                [this, backend](const QStringList &features, const QStringList &families,
                                bool profileSupported, int maxProfileLength) {
                    if (BackendState *state = stateFor(backend)) {
                        state->serverCapabilities = features;
                        state->serverCapabilityDetails = families;
                        state->aimProfileSupported = profileSupported;
                        state->aimProfileMaxLength = maxProfileLength;
                        state->serverCapabilitiesUpdated = QDateTime::currentDateTime().toString(Qt::ISODate);
                    }
                }, Qt::QueuedConnection);
        connect(oscar, &OscarBackend::profileChanged, this,
                [this, backend](const QString &profile) {
                    if (BackendState *state = stateFor(backend)) state->aimProfile = profile;
                }, Qt::QueuedConnection);
        connect(oscar, &OscarBackend::directoryInfoReceived, this,
                [this, backend](const QString &target, const QVariantMap &info) {
                    if (!stateFor(backend)) return;
                    QStringList lines;
                    lines << QStringLiteral("Screen Name: %1").arg(target)
                          << QStringLiteral("Last Updated: %1").arg(info.value(QStringLiteral("updatedAt")).toString())
                          << QString();
                    const QList<QPair<QString, QString>> fields = {
                        {QStringLiteral("firstName"), QStringLiteral("First name")},
                        {QStringLiteral("lastName"), QStringLiteral("Last name")},
                        {QStringLiteral("middleName"), QStringLiteral("Middle name")},
                        {QStringLiteral("maidenName"), QStringLiteral("Maiden name")},
                        {QStringLiteral("nickname"), QStringLiteral("Nickname")},
                        {QStringLiteral("street"), QStringLiteral("Street")},
                        {QStringLiteral("city"), QStringLiteral("City")},
                        {QStringLiteral("state"), QStringLiteral("State / region")},
                        {QStringLiteral("zip"), QStringLiteral("ZIP / postal code")},
                        {QStringLiteral("country"), QStringLiteral("Country")},
                    };
                    bool any=false;
                    for (const auto &field : fields) {
                        const QString value=info.value(field.first).toString();
                        if (!value.isEmpty()) { lines << QStringLiteral("%1: %2").arg(field.second, value); any=true; }
                    }
                    if (!any) lines << QStringLiteral("(The server returned no directory fields for this user.)");
                    showPlainTextDialog(this, QStringLiteral("AIM Directory Information — %1").arg(target), lines.join(QLatin1Char('\n')));
                }, Qt::QueuedConnection);
        connect(oscar, &OscarBackend::lookupResultsReceived, this,
                [this, backend](const QString &query, const QStringList &results) {
                    if (!stateFor(backend)) return;
                    QStringList lines{QStringLiteral("Email: %1").arg(query), QString()};
                    lines << (results.isEmpty() ? QStringLiteral("No matching AIM users were returned by the server.")
                                                : results.join(QLatin1Char('\n')));
                    showPlainTextDialog(this, QStringLiteral("AIM User Lookup"), lines.join(QLatin1Char('\n')));
                }, Qt::QueuedConnection);
        connect(oscar, &OscarBackend::accountInfoReceived, this,
                [this, backend](const QVariantMap &info) {
                    if (!stateFor(backend)) return;
                    QStringList lines;
                    lines << QStringLiteral("Screen Name: %1").arg(info.value(QStringLiteral("screenName"), QStringLiteral("Not supplied by server")).toString())
                          << QStringLiteral("Email: %1").arg(info.value(QStringLiteral("email"), QStringLiteral("Not supplied by server")).toString())
                          << QStringLiteral("Registration Status: %1").arg(info.contains(QStringLiteral("registrationStatus")) ? info.value(QStringLiteral("registrationStatus")).toString() : QStringLiteral("Not supplied by server"))
                          << QStringLiteral("Last Updated: %1").arg(info.value(QStringLiteral("updatedAt")).toString());
                    if (info.contains(QStringLiteral("errorCode"))) lines << QStringLiteral("Server Error Code: %1").arg(info.value(QStringLiteral("errorCode")).toString());
                    showPlainTextDialog(this, QStringLiteral("AIM Account Information"), lines.join(QLatin1Char('\n')));
                }, Qt::QueuedConnection);
        connect(oscar, &OscarBackend::watcherListReceived, this,
                [this, backend](const QStringList &users) {
                    if (!stateFor(backend)) return;
                    showPlainTextDialog(this, QStringLiteral("AIM Watcher / Reverse Buddy List"),
                        users.isEmpty() ? QStringLiteral("The server returned no users watching/listing this account.")
                                        : users.join(QLatin1Char('\n')));
                }, Qt::QueuedConnection);
        connect(oscar, &OscarBackend::authorizationRequestReceived, this,
                [this, backend, oscar](const QString &from, const QString &message) {
                    if (!stateFor(backend)) return;
                    const QString question = QStringLiteral("%1 is requesting permission to add/keep you on their AIM buddy list.%2\n\nAllow?")
                        .arg(from, message.isEmpty() ? QString() : QStringLiteral("\n\nMessage: %1").arg(message));
                    const bool accept = QMessageBox::question(this, QStringLiteral("AIM Buddy Authorization"), question,
                                                               QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes;
                    oscar->respondAuthorization(from, accept);
                }, Qt::QueuedConnection);
        connect(oscar, &OscarBackend::authorizationResponseReceived, this,
                [this, backend](const QString &from, bool accepted, const QString &message) {
                    appendActivity(backend, QStringLiteral("[AIM authorization] %1 %2 your request%3")
                        .arg(from, accepted ? QStringLiteral("accepted") : QStringLiteral("denied"),
                             message.isEmpty() ? QString() : QStringLiteral(": %1").arg(message)));
                }, Qt::QueuedConnection);
        connect(oscar, &OscarBackend::buddyAddedYou, this,
                [this, backend](const QString &from) { appendActivity(backend, QStringLiteral("[AIM buddy] %1 added you to their buddy list.").arg(from)); }, Qt::QueuedConnection);
        connect(oscar, &OscarBackend::typingNotificationReceived, this,
                [this, backend](const QString &from, quint16 event) {
                    ChatWindow *window=m_windows.value(conversationKey(backend, QStringLiteral("im"), from), nullptr);
                    if (!window) return;
                    if (event == Oscar::ICBM_EVENT_TYPING) window->setPeerTypingState(QStringLiteral("%1 is typing…").arg(from));
                    else if (event == Oscar::ICBM_EVENT_TYPED) window->setPeerTypingState(QStringLiteral("%1 paused typing").arg(from));
                    else window->setPeerTypingState(QString());
                }, Qt::QueuedConnection);
        connect(oscar, &OscarBackend::oscarNoticeReceived, this,
                [this, backend](const QString &kind, const QString &text) { appendActivity(backend, QStringLiteral("[OSCAR %1] %2").arg(kind, text)); }, Qt::QueuedConnection);
        connect(oscar, &OscarBackend::featureOperationResult, this,
                [this, backend](const QString &operation, bool success, const QString &message) {
                    appendActivity(backend, QStringLiteral("[%1] %2: %3").arg(success ? QStringLiteral("ok") : QStringLiteral("error"), operation, message));
                    statusBar()->showMessage(QStringLiteral("%1: %2").arg(operation, message), 5000);
                }, Qt::QueuedConnection);
        connect(oscar, &OscarBackend::voiceInviteReceived, this,
                [this, backend, oscar](const QString &from, const QString &cookieHex,
                                       const QString &remoteAddress, quint16 remotePort,
                                       int sampleRate, int channels, const QString &invitation) {
                    BackendState *state = stateFor(backend);
                    if (!state || !state->connected) return;
                    if (channels != 1 || sampleRate <= 0 || remoteAddress.isEmpty() || remotePort == 0) {
                        oscar->cancelVoice(from, cookieHex, 0x0003);
                        appendActivity(backend, QStringLiteral("[OSCAR voice] Rejected malformed/incompatible invitation from %1.").arg(from));
                        return;
                    }
                    if (m_oscarVoice && m_oscarVoice->isPrepared()) {
                        oscar->cancelVoice(from, cookieHex, 0x0002);
                        appendActivity(backend, QStringLiteral("[OSCAR voice] Busy; declined invitation from %1.").arg(from));
                        return;
                    }
                    QString question = QStringLiteral("%1 is inviting you to an OSCAR voice chat.\n\n%2\n\nAccept?")
                                           .arg(from, invitation.isEmpty()
                                                          ? QStringLiteral("WaffleHouse/CPX peer voice")
                                                          : invitation);
                    if (QMessageBox::question(this, QStringLiteral("Incoming OSCAR Voice"), question,
                                              QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) != QMessageBox::Yes) {
                        oscar->cancelVoice(from, cookieHex, 0x0001);
                        return;
                    }
                    OscarVoiceSession *voice = ensureOscarVoiceSession();
                    QString error;
                    if (!voice->prepare(sampleRate, &error)) {
                        oscar->cancelVoice(from, cookieHex, 0x0003);
                        QMessageBox::warning(this, QStringLiteral("OSCAR Voice"), error);
                        return;
                    }
                    if (!voice->start(from, remoteAddress, remotePort, &error)) {
                        oscar->cancelVoice(from, cookieHex, 0x0003);
                        voice->stop();
                        QMessageBox::warning(this, QStringLiteral("OSCAR Voice"), error);
                        return;
                    }
                    m_oscarVoiceBackendId = backend->id();
                    m_oscarVoicePeer = from;
                    m_oscarVoiceCookie = cookieHex;
                    oscar->acceptVoice(from, cookieHex, voice->localAddress(), voice->localPort(), voice->sampleRate());
                    appendActivity(backend, QStringLiteral("[OSCAR voice] Accepted voice chat with %1 (%2:%3, %4 Hz).")
                                               .arg(from, remoteAddress).arg(remotePort).arg(sampleRate));
                }, Qt::QueuedConnection);
        connect(oscar, &OscarBackend::voiceInviteAccepted, this,
                [this, backend](const QString &from, const QString &cookieHex,
                                const QString &remoteAddress, quint16 remotePort,
                                int sampleRate, int channels) {
                    if (!m_oscarVoice || !m_oscarVoice->isPrepared()
                        || m_oscarVoiceBackendId != backend->id()
                        || m_oscarVoiceCookie.compare(cookieHex, Qt::CaseInsensitive) != 0) return;
                    if (channels != 1 || sampleRate != m_oscarVoice->sampleRate()) {
                        QMessageBox::warning(this, QStringLiteral("OSCAR Voice"),
                                             QStringLiteral("%1 accepted with an incompatible audio format.").arg(from));
                        hangupOscarVoice(true);
                        return;
                    }
                    QString error;
                    if (!m_oscarVoice->start(from, remoteAddress, remotePort, &error)) {
                        QMessageBox::warning(this, QStringLiteral("OSCAR Voice"), error);
                        hangupOscarVoice(true);
                        return;
                    }
                    m_oscarVoicePeer = from;
                    appendActivity(backend, QStringLiteral("[OSCAR voice] %1 accepted; voice audio is active.").arg(from));
                }, Qt::QueuedConnection);
        connect(oscar, &OscarBackend::voiceInviteCancelled, this,
                [this, backend](const QString &from, const QString &cookieHex, quint16 reason) {
                    if (!m_oscarVoice || m_oscarVoiceBackendId != backend->id()
                        || m_oscarVoiceCookie.compare(cookieHex, Qt::CaseInsensitive) != 0) return;
                    appendActivity(backend, QStringLiteral("[OSCAR voice] %1 ended/declined the voice request (reason 0x%2).")
                                               .arg(from).arg(reason, 4, 16, QLatin1Char('0')));
                    hangupOscarVoice(false);
                }, Qt::QueuedConnection);
        connect(oscar, &OscarBackend::legacyVoiceInviteReceived, this,
                [this, backend](const QString &from, const QString &) {
                    appendActivity(backend, QStringLiteral("[OSCAR voice] %1 sent a legacy AIM Talk invitation. "
                                                           "WaffleHouse identifies it, but does not claim compatibility with the proprietary legacy media framing.")
                                               .arg(from));
                    QMessageBox::information(this, QStringLiteral("Legacy AIM Voice"),
                                             QStringLiteral("%1 sent a legacy AIM Voice/Talk invitation. This build can identify legacy voice capability, but WaffleHouse OSCAR Voice uses its own open media payload.").arg(from));
                }, Qt::QueuedConnection);
    }
    if (auto *irc = qobject_cast<IrcBackend *>(backend)) {
        connect(irc, &IrcBackend::serverCapabilitiesChanged, this,
                [this, backend](const QStringList &caps, const QStringList &isupport) {
                    if (BackendState *state = stateFor(backend)) {
                        state->serverCapabilities = caps;
                        state->serverCapabilityDetails = isupport;
                        state->serverCapabilitiesUpdated = QDateTime::currentDateTime().toString(Qt::ISODate);
                    }
                }, Qt::QueuedConnection);
    }
}

void MainWindow::attachBackend(ChatBackend *backend,
                               bool persist,
                               bool secretRequired,
                               bool hasSessionSecret,
                               bool autoConnect,
                               const QString &profileId)
{
    if (!backend || !m_connectionList) {
        return;
    }

    auto *state = new BackendState;
    state->backend = backend;
    state->profileId = profileId.trimmed().isEmpty()
        ? QUuid::createUuid().toString(QUuid::WithoutBraces)
        : profileId.trimmed();
    state->secretRequired = secretRequired;
    state->hasSessionSecret = hasSessionSecret;
    state->connectionItem = new QListWidgetItem(m_connectionList);
    state->connectionItem->setData(Qt::UserRole, backend->id());
    m_states.insert(backend->id(), state);
    if (backend->settings().protocol == ConnectionSettings::Protocol::Irc) {
        for (const QString &buddy : backend->settings().ircBuddies) {
            if (!buddy.trimmed().isEmpty()) state->buddies.insert(buddy.trimmed());
        }
    }

    // Wire and attach the backend to WaffleHouse state before a SIP account is
    // inserted into the live PJSUA2 endpoint. This prevents controller signals
    // from re-entering GUI refresh code while a new SipBackend is still under
    // construction (the 2.5 Add-SIP crash).
    wireBackend(backend);
    bool backendReady = true;
    if (auto *sip = qobject_cast<SipBackend *>(backend)) {
        for (const QString &contact : backend->settings().sipContacts) {
            if (!contact.trimmed().isEmpty()) state->buddies.insert(contact.trimmed());
        }
        QString sipError;
        backendReady = sip->initializeAccount(&sipError);
        if (!backendReady) {
            appendActivity(backend, QStringLiteral("SIP account initialization failed: %1").arg(sipError));
            QMessageBox::warning(this, QStringLiteral("SIP Account"),
                                 QStringLiteral("The SIP account was saved, but the softphone endpoint could not add it:\n\n%1").arg(sipError));
        }
    }
    updateConnectionItem(state);
    selectState(state);

    appendActivity(
        backend,
        persist
            ? QStringLiteral("Connection added.")
            : QStringLiteral("Saved connection restored."));

    refreshBuddyList();
    updateActions();

    if (persist) {
        saveConnections();
    }

    if (autoConnect && backendReady) {
        connectState(state);
    }
}

MainWindow::BackendState *MainWindow::stateFor(ChatBackend *backend) const
{
    return backend ? m_states.value(backend->id(), nullptr) : nullptr;
}

MainWindow::BackendState *MainWindow::stateById(const QString &id) const
{
    return m_states.value(id, nullptr);
}

MainWindow::BackendState *MainWindow::stateByProfileId(const QString &profileId) const
{
    if (profileId.isEmpty()) return nullptr;
    for (BackendState *state : m_states) {
        if (state && state->profileId == profileId) return state;
    }
    return nullptr;
}

MainWindow::BackendState *MainWindow::stateFromBuddyItem(QTreeWidgetItem *item) const
{
    if (!item) {
        return nullptr;
    }
    return stateById(item->data(0, Qt::UserRole).toString());
}

MainWindow::BackendState *MainWindow::selectedState() const
{
    if (m_buddyTree) {
        if (BackendState *state = stateFromBuddyItem(m_buddyTree->currentItem())) {
            return state;
        }
    }
    QListWidgetItem *item = m_connectionList ? m_connectionList->currentItem() : nullptr;
    return item ? stateById(item->data(Qt::UserRole).toString()) : nullptr;
}

void MainWindow::selectState(BackendState *state)
{
    if (!state || !state->backend) {
        return;
    }

    if (m_connectionList && state->connectionItem
        && m_connectionList->currentItem() != state->connectionItem) {
        m_connectionList->setCurrentItem(state->connectionItem);
    }

    if (m_buddyTree) {
        for (int i = 0; i < m_buddyTree->topLevelItemCount(); ++i) {
            QTreeWidgetItem *root = m_buddyTree->topLevelItem(i);
            if (root && root->data(0, Qt::UserRole).toString() == state->backend->id()) {
                if (!m_buddyTree->currentItem()
                    || stateFromBuddyItem(m_buddyTree->currentItem()) != state) {
                    m_buddyTree->setCurrentItem(root);
                }
                break;
            }
        }
    }
}

void MainWindow::updateConnectionItem(BackendState *state)
{
    if (!state || !state->backend || !state->connectionItem) {
        return;
    }

    const ConnectionSettings &cfg = state->backend->settings();
    const QString identity = state->identity.isEmpty()
        ? cfg.username
        : state->identity;
    QString displayIdentity = identity;
    if ((cfg.protocol == ConnectionSettings::Protocol::Oscar
         || cfg.protocol == ConnectionSettings::Protocol::Irc)
        && !cfg.accountLabel.trimmed().isEmpty()) {
        displayIdentity = cfg.accountLabel.trimmed();
    } else if (cfg.protocol == ConnectionSettings::Protocol::Sip
               && !cfg.sipProfileName.trimmed().isEmpty()) {
        displayIdentity = cfg.sipProfileName.trimmed();
    }
    QString stateWord = state->connecting
        ? QStringLiteral("Connecting")
        : statusWord(state->connected);
    if (state->connected
        && state->backend->settings().protocol == ConnectionSettings::Protocol::Oscar) {
        stateWord = state->presenceState.isEmpty() ? QStringLiteral("ONLINE") : state->presenceState;
        if (state->idleSeconds > 0) stateWord += QStringLiteral(" + Idle");
    }

    QString text = QStringLiteral("%1 — %2").arg(stateWord, state->backend->protocolName());
    if (!displayIdentity.isEmpty()) {
        text += QStringLiteral(" — %1").arg(displayIdentity);
    }
    state->connectionItem->setText(text);
    QString toolTip = state->endpoint;
    if (!displayIdentity.isEmpty() && !identity.isEmpty()
        && displayIdentity.compare(identity, Qt::CaseInsensitive) != 0) {
        toolTip = QStringLiteral("%1\nIdentity: %2").arg(toolTip, identity);
    }
    state->connectionItem->setToolTip(toolTip);
}

void MainWindow::updateActions()
{
    BackendState *state = selectedState();
    const bool exists = state && state->backend;
    const bool online = exists && state->connected;
    const bool connecting = exists && state->connecting;
    const bool editable = exists && !online && !connecting;
    const bool oscar = online
        && state->backend->settings().protocol == ConnectionSettings::Protocol::Oscar;
    const bool isTelnet = exists
        && state->backend->settings().protocol == ConnectionSettings::Protocol::Telnet;
    const bool isSip = exists
        && state->backend->settings().protocol == ConnectionSettings::Protocol::Sip;

    if (m_editConnectionAction) m_editConnectionAction->setEnabled(editable);
    if (m_deleteConnectionAction) m_deleteConnectionAction->setEnabled(exists);
    if (m_connectAction) m_connectAction->setEnabled(exists && !online && !connecting);
    if (m_disconnectAction) m_disconnectAction->setEnabled(exists && (online || connecting));

    if (m_editConnectionButton) m_editConnectionButton->setEnabled(editable);
    if (m_deleteConnectionButton) m_deleteConnectionButton->setEnabled(exists);
    if (m_connectButton) m_connectButton->setEnabled(exists && !online && !connecting);
    if (m_disconnectButton) m_disconnectButton->setEnabled(exists && (online || connecting));

    if (m_mainAddAccountButton) m_mainAddAccountButton->setEnabled(true);
    if (m_mainRemoveAccountButton) m_mainRemoveAccountButton->setEnabled(exists);
    if (m_mainEditAccountButton) m_mainEditAccountButton->setEnabled(editable);
    if (m_mainConnectToggleButton) {
        m_mainConnectToggleButton->setEnabled(exists);
        m_mainConnectToggleButton->setText(
            !exists ? QStringLiteral("Connect / Disconnect")
                    : (online || connecting ? QStringLiteral("Disconnect")
                                            : QStringLiteral("Connect")));
        m_mainConnectToggleButton->setToolTip(
            !exists ? QStringLiteral("Select an account first")
                    : (online || connecting
                           ? QStringLiteral("Disconnect the highlighted account")
                           : QStringLiteral("Connect the highlighted account")));
    }

    if (m_rawAction) m_rawAction->setEnabled(online && !isSip);
    if (m_changePasswordAction) m_changePasswordAction->setEnabled(oscar);
    if (m_fingerprintAction) m_fingerprintAction->setEnabled(exists && m_secureReady && !isTelnet && !isSip);


    if (m_trayConnectAction) {
        m_trayConnectAction->setEnabled(exists && !online && !connecting);
    }
    if (m_trayDisconnectAction) {
        m_trayDisconnectAction->setEnabled(exists && (online || connecting));
    }

    rebuildTrayMenu();
}

void MainWindow::refreshBuddyList()
{
    if (!m_buddyTree) {
        return;
    }

    QString selectedBackendId;
    QString selectedBuddy;
    if (QTreeWidgetItem *current = m_buddyTree->currentItem()) {
        selectedBackendId = current->data(0, Qt::UserRole).toString();
        selectedBuddy = current->data(0, Qt::UserRole + 1).toString();
    }

    m_buddyTree->clear();

    QList<BackendState *> states = m_states.values();
    std::sort(states.begin(), states.end(), [](BackendState *a, BackendState *b) {
        if (!a || !a->backend) return false;
        if (!b || !b->backend) return true;
        const int protocolCompare = a->backend->protocolName().compare(
            b->backend->protocolName(), Qt::CaseInsensitive);
        if (protocolCompare != 0) {
            return protocolCompare < 0;
        }
        const auto visibleAccountName = [](BackendState *state) {
            const ConnectionSettings &cfg = state->backend->settings();
            if ((cfg.protocol == ConnectionSettings::Protocol::Oscar
                 || cfg.protocol == ConnectionSettings::Protocol::Irc)
                && !cfg.accountLabel.trimmed().isEmpty()) return cfg.accountLabel.trimmed();
            const QString identity = state->identity.isEmpty() ? cfg.username : state->identity;
            return identity.isEmpty() ? cfg.server : identity;
        };
        return visibleAccountName(a).compare(visibleAccountName(b), Qt::CaseInsensitive) < 0;
    });

    QTreeWidgetItem *restoreItem = nullptr;

    // Every saved connection remains a top-level account in the compact main tree,
    // including SIP/VoIP.
    for (BackendState *state : states) {
        if (!state || !state->backend) {
            continue;
        }

        const ConnectionSettings &accountCfg = state->backend->settings();
        QString accountName;
        if ((accountCfg.protocol == ConnectionSettings::Protocol::Oscar
             || accountCfg.protocol == ConnectionSettings::Protocol::Irc)
            && !accountCfg.accountLabel.trimmed().isEmpty()) {
            accountName = accountCfg.accountLabel.trimmed();
        } else {
            accountName = state->identity.isEmpty() ? accountCfg.username : state->identity;
        }
        if (accountName.isEmpty()) {
            accountName = accountCfg.protocol == ConnectionSettings::Protocol::Telnet
                ? accountCfg.server
                : state->backend->protocolName();
        }

        auto *root = new QTreeWidgetItem(m_buddyTree);
        root->setText(0, accountName);
        QString accountStatus = state->connecting ? QStringLiteral("Connecting")
                                                        : statusWord(state->connected);
        if (state->connected
            && state->backend->settings().protocol == ConnectionSettings::Protocol::Oscar) {
            accountStatus = state->presenceState.isEmpty() ? QStringLiteral("ONLINE") : state->presenceState;
            if (state->idleSeconds > 0) {
                const quint32 minutes = state->idleSeconds / 60;
                accountStatus += minutes > 0 ? QStringLiteral(" · Idle %1m").arg(minutes)
                                             : QStringLiteral(" · Idle %1s").arg(state->idleSeconds);
            }
        }
        root->setText(1, QStringLiteral("%1 — %2").arg(state->backend->protocolName(), accountStatus));
        if (!state->presenceMessage.isEmpty()) root->setToolTip(1, state->presenceMessage);
        if (!accountCfg.accountLabel.trimmed().isEmpty()
            && (accountCfg.protocol == ConnectionSettings::Protocol::Oscar
                || accountCfg.protocol == ConnectionSettings::Protocol::Irc)) {
            root->setToolTip(0, QStringLiteral("%1 identity: %2")
                .arg(state->backend->protocolName(),
                     state->identity.isEmpty() ? accountCfg.username : state->identity));
        }
        root->setData(0, Qt::UserRole, state->backend->id());
        QFont rootFont = root->font(0);
        rootFont.setBold(true);
        root->setFont(0, rootFont);

        if (state->backend->settings().protocol == ConnectionSettings::Protocol::Oscar
            || state->backend->settings().protocol == ConnectionSettings::Protocol::Irc) {
            QStringList buddies = state->buddies.values();
            std::sort(buddies.begin(), buddies.end(), [state](const QString &a, const QString &b) {
                const bool aOnline = state->onlineBuddies.contains(a.toCaseFolded());
                const bool bOnline = state->onlineBuddies.contains(b.toCaseFolded());
                if (aOnline != bOnline) {
                    return aOnline;
                }
                return a.compare(b, Qt::CaseInsensitive) < 0;
            });

            for (const QString &buddy : buddies) {
                auto *item = new QTreeWidgetItem(root);
                item->setText(0, buddy);
                const QString buddyKey = buddy.toCaseFolded();
                const bool online = state->onlineBuddies.contains(buddyKey);
                QString buddyStatus = online ? QStringLiteral("Online") : QStringLiteral("Offline");
                QString buddyTip;
                if (state->backend->settings().protocol == ConnectionSettings::Protocol::Oscar) {
                    const QVariantMap native = state->oscarBuddyPresence.value(buddyKey);
                    if (!native.isEmpty()) {
                        buddyStatus = native.value(QStringLiteral("state"), buddyStatus).toString();
                        const int idleSeconds = native.value(QStringLiteral("idleSeconds"), 0).toInt();
                        if (online && idleSeconds > 0 && buddyStatus != QStringLiteral("Idle")) {
                            const int mins = idleSeconds / 60;
                            buddyStatus += mins > 0 ? QStringLiteral(" · Idle %1m").arg(mins)
                                                    : QStringLiteral(" · Idle %1s").arg(idleSeconds);
                        } else if (online && idleSeconds > 0 && buddyStatus == QStringLiteral("Idle")) {
                            const int mins = idleSeconds / 60;
                            if (mins > 0) buddyStatus += QStringLiteral(" %1m").arg(mins);
                        }
                        const QString awayMessage = native.value(QStringLiteral("awayMessage")).toString().trimmed();
                        if (!awayMessage.isEmpty()) buddyTip = awayMessage;
                    }
                }
                item->setText(1, QStringLiteral("%1 — %2").arg(state->backend->protocolName(), buddyStatus));
                if (!buddyTip.isEmpty()) item->setToolTip(1, buddyTip);
                item->setData(0, Qt::UserRole, state->backend->id());
                item->setData(0, Qt::UserRole + 1, buddy);
                if (online) {
                    QFont font = item->font(0);
                    font.setBold(true);
                    item->setFont(0, font);
                }

                if (state->backend->id() == selectedBackendId
                    && buddy == selectedBuddy) {
                    restoreItem = item;
                }
            }
        }

        root->setExpanded(true);
        if (!restoreItem && state->backend->id() == selectedBackendId
            && selectedBuddy.isEmpty()) {
            restoreItem = root;
        }
    }

    m_buddyTree->resizeColumnToContents(0);
    if (restoreItem) {
        m_buddyTree->setCurrentItem(restoreItem);
    } else if (m_buddyTree->topLevelItemCount() > 0 && !m_buddyTree->currentItem()) {
        m_buddyTree->setCurrentItem(m_buddyTree->topLevelItem(0));
    }
    refreshSoftphoneControls();
}

void MainWindow::refreshSoftphoneControls()
{
    if (!m_buddySipAccount) return;
    const QString previous = m_buddySipAccount->currentData().toString().isEmpty()
        ? m_sipController->selectedAccountId() : m_buddySipAccount->currentData().toString();
    const QSignalBlocker blocker(m_buddySipAccount);
    m_buddySipAccount->clear();
    QList<BackendState *> states = m_states.values();
    std::sort(states.begin(), states.end(), [](BackendState *a, BackendState *b) {
        if (!a || !a->backend) {
            return false;
        }
        if (!b || !b->backend) {
            return true;
        }
        return a->backend->settings().sipProfileName.compare(b->backend->settings().sipProfileName, Qt::CaseInsensitive) < 0;
    });
    for (BackendState *state : states) {
        if (!state || !state->backend || state->backend->settings().protocol != ConnectionSettings::Protocol::Sip) continue;
        const auto &cfg = state->backend->settings();
        const QString label = cfg.sipProfileName.trimmed().isEmpty()
            ? QStringLiteral("%1@%2").arg(cfg.username, cfg.sipDomain.isEmpty() ? cfg.server : cfg.sipDomain)
            : cfg.sipProfileName;
        const QString reg = m_sipController->registrationText(state->backend->id());
        m_buddySipAccount->addItem(QStringLiteral("%1 — %2").arg(label, reg), state->backend->id());
    }
    int idx = m_buddySipAccount->findData(previous);
    if (idx < 0 && m_buddySipAccount->count() > 0) idx = 0;
    if (idx >= 0) {
        m_buddySipAccount->setCurrentIndex(idx);
        m_sipController->setSelectedAccountId(m_buddySipAccount->currentData().toString());
    }
    const bool haveSip = m_buddySipAccount->count() > 0;
    const QString selectedSipId=haveSip?m_buddySipAccount->currentData().toString():QString();
    if(m_buddyDialPrefix){
        const QSignalBlocker prefixBlocker(m_buddyDialPrefix);
        m_buddyDialPrefix->setEnabled(haveSip);
        m_buddyDialPrefix->setText(haveSip?m_sipController->dialPrefix(selectedSipId):QString());
    }
    BackendState *selectedSip = haveSip ? stateById(selectedSipId) : nullptr;
    const bool sipOnline = selectedSip && selectedSip->connected;
    const bool sipConnecting = selectedSip && selectedSip->connecting;
    if (m_buddyDial) m_buddyDial->setEnabled(haveSip);
    if (m_buddyDialButton) m_buddyDialButton->setEnabled(haveSip);
    if (m_buddySipConnectButton) m_buddySipConnectButton->setEnabled(selectedSip && !sipOnline && !sipConnecting);
    if (m_buddySipDisconnectButton) m_buddySipDisconnectButton->setEnabled(selectedSip && (sipOnline || sipConnecting));
}

void MainWindow::appendActivity(ChatBackend *backend, const QString &text)
{
    if (!m_activity) {
        return;
    }
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    const QString label = backend ? backend->protocolName() : appDisplayName();
    m_activity->appendPlainText(
        QStringLiteral("[%1] [%2] %3").arg(timestamp, label, text));
}

void MainWindow::rebuildTrayMenu()
{
    if (!m_trayIcon) {
        return;
    }

    int onlineCount = 0;
    int totalCount = 0;
    for (BackendState *state : m_states) {
        if (!state || !state->backend) {
            continue;
        }
        ++totalCount;
        if (state->connected) {
            ++onlineCount;
        }
    }

    m_trayIcon->setToolTip(
        QStringLiteral("%1 — %2/%3 connection(s) online")
            .arg(appDisplayName())
            .arg(onlineCount)
            .arg(totalCount));
}

QString MainWindow::conversationKey(ChatBackend *backend,
                                    const QString &kind,
                                    const QString &target) const
{
    return QStringLiteral("%1|%2|%3")
        .arg(backend ? backend->id() : QStringLiteral("none"),
             kind,
             target.toCaseFolded());
}

QString MainWindow::targetDisplayName(BackendState *state,
                                      const QString &kind,
                                      const QString &target) const
{
    if (!state) {
        return target;
    }
    return state->targetNames.value(QStringLiteral("%1|%2").arg(kind, target), target);
}

QString MainWindow::conversationOpacityKey(ChatBackend *backend,
                                           const QString &kind,
                                           const QString &target) const
{
    if (!backend) {
        return QStringLiteral("ui/conversations/default/opacity");
    }
    const ConnectionSettings &s = backend->settings();
    const QString identity = QStringLiteral("%1|%2|%3|%4|%5|%6")
        .arg(static_cast<int>(s.protocol))
        .arg(s.server)
        .arg(s.port)
        .arg(s.username)
        .arg(kind)
        .arg(target.toCaseFolded());
    const QByteArray hash = QCryptographicHash::hash(
        identity.toUtf8(), QCryptographicHash::Sha1).toHex();
    return QStringLiteral("ui/conversations/%1/opacity")
        .arg(QString::fromLatin1(hash));
}

ChatWindow *MainWindow::ensureConversationWindow(ChatBackend *backend,
                                                 const QString &kind,
                                                 const QString &target,
                                                 bool showWindow)
{
    if (!backend || target.isEmpty()) {
        return nullptr;
    }

    BackendState *state = stateFor(backend);
    if (!state || !state->connected) {
        return nullptr;
    }

    const QString key = conversationKey(backend, kind, target);
    if (ChatWindow *existing = m_windows.value(key, nullptr)) {
        if (showWindow) {
            existing->show();
            existing->raise();
            existing->activateWindow();
        }
        return existing;
    }

    auto *window = new ChatWindow(
        backend,
        kind,
        target,
        targetDisplayName(state, kind, target),
        conversationOpacityKey(backend, kind, target));
    window->setBackendOnline(true);
    window->setShowTimestamps(m_options.showTimestamps);
    window->setShowSidePane(m_options.showSidePanes);
    m_windows.insert(key, window);

    connect(window, &ChatWindow::conversationClosing,
            this, &MainWindow::handleConversationClosing);
    connect(window, &ChatWindow::messageSubmitted,
            this, &MainWindow::handleConversationMessage);
    connect(window, &ChatWindow::inputActivity, this, [this](ChatWindow *w, bool hasText) {
        if (!w || w->kind() != QStringLiteral("im")) return;
        BackendState *current = stateById(w->backendId());
        if (!current || !current->connected || !current->backend
            || current->backend->settings().protocol != ConnectionSettings::Protocol::Oscar) return;
        if (auto *oscar = qobject_cast<OscarBackend *>(current->backend);
            oscar && oscar->supportsFamily(Oscar::FAM_ICBM))
            oscar->sendTypingNotification(w->target(), hasText ? Oscar::ICBM_EVENT_TYPING : Oscar::ICBM_EVENT_FINISHED);
    });
    connect(window, &ChatWindow::terminalBytesSubmitted, this,
            [this](ChatWindow *w, const QByteArray &bytes) {
                if (!w) return;
                BackendState *state = stateById(w->backendId());
                if (state && state->backend && state->connected) {
                    const auto protocol = state->backend->settings().protocol;
                    if (protocol == ConnectionSettings::Protocol::Telnet
                        || protocol == ConnectionSettings::Protocol::Ssh) {
                        state->backend->sendTerminalInput(bytes);
                    }
                }
            });
    connect(window, &ChatWindow::secureRequested,
            this, &MainWindow::startSecureSession);
    connect(window, &ChatWindow::secureStatusRequested,
            this, &MainWindow::showSecureStatus);
    connect(window, &ChatWindow::trustRequested,
            this, &MainWindow::trustSecurePeer);
    connect(window, &ChatWindow::untrustRequested,
            this, &MainWindow::untrustSecurePeer);
    connect(window, &ChatWindow::secureOffRequested,
            this, &MainWindow::closeSecureSession);
    connect(window, &ChatWindow::fileSendRequested,
            this, &MainWindow::sendFile);
    connect(window, &QObject::destroyed, this, [this, key] {
        m_windows.remove(key);
    });
    updateConversationSecurity(window);

    if (showWindow) {
        window->show();
    }
    return window;
}

void MainWindow::closeBackendWindows(ChatBackend *backend)
{
    if (!backend) {
        return;
    }
    const QString backendId = backend->id();
    const auto windows = m_windows.values();
    for (ChatWindow *window : windows) {
        if (window && window->backendId() == backendId) {
            window->setBackendOnline(false);
            window->close();
        }
    }
}

void MainWindow::connectState(BackendState *state)
{
    if (!state || !state->backend || state->connected || state->connecting) {
        return;
    }
    if (!ensureConnectionSecret(state)) {
        return;
    }

    state->connecting = true;
    updateConnectionItem(state);
    refreshBuddyList();
    updateActions();
    appendActivity(state->backend, QStringLiteral("Connecting…"));
    state->backend->start();
}

void MainWindow::connectSelected()
{
    connectState(selectedState());
}

void MainWindow::disconnectSelected()
{
    BackendState *state = selectedState();
    if (!state || !state->backend || (!state->connected && !state->connecting)) {
        return;
    }

    appendActivity(state->backend, QStringLiteral("Disconnecting…"));
    state->connected = false;
    state->connecting = false;
    state->onlineBuddies.clear();
    state->oscarBuddyPresence.clear();
    m_secure.closeConnection(state->profileId);
    closeBackendWindows(state->backend);
    updateConnectionItem(state);
    refreshBuddyList();
    updateActions();
    state->backend->stop();
}

void MainWindow::toggleSelectedConnection()
{
    BackendState *state = selectedState();
    if (!state || !state->backend) {
        return;
    }

    if (state->connected || state->connecting) {
        disconnectSelected();
    } else {
        connectSelected();
    }
}

void MainWindow::deleteSelected()
{
    BackendState *state = selectedState();
    if (!state || !state->backend) {
        return;
    }
    if (state->backend->settings().protocol == ConnectionSettings::Protocol::Sip) {
        for (const auto &call : m_sipController->calls()) {
            if (!call.disconnected && QString::fromStdString(call.accountId) == state->backend->id()) {
                QMessageBox::information(this, QStringLiteral("SIP Account In Use"),
                                         QStringLiteral("Hang up active calls on this SIP account before deleting it."));
                return;
            }
        }
    }

    const QString identity = state->identity.isEmpty()
        ? state->backend->settings().username
        : state->identity;
    const QString description = identity.isEmpty()
        ? state->backend->protocolName()
        : QStringLiteral("%1 — %2").arg(state->backend->protocolName(), identity);

    if (QMessageBox::question(
            this,
            QStringLiteral("Delete Connection"),
            QStringLiteral("Delete the saved connection for %1?")
                .arg(description)) != QMessageBox::Yes) {
        return;
    }

    ChatBackend *backend = state->backend;
    const QString backendId = backend->id();
    state->connected = false;
    state->connecting = false;
    m_secure.closeConnection(state->profileId);
    closeBackendWindows(backend);
    QObject::disconnect(backend, nullptr, this, nullptr);
    backend->stop();

    if (m_connectionList && state->connectionItem) {
        const int row = m_connectionList->row(state->connectionItem);
        if (row >= 0) {
            delete m_connectionList->takeItem(row);
        }
    }

    m_states.remove(backendId);
    state->connectionItem = nullptr;
    delete state;
    backend->deleteLater();

    refreshBuddyList();
    updateActions();
    saveConnections();
    statusBar()->showMessage(QStringLiteral("Connection deleted"), 3000);
}

void MainWindow::editSelected()
{
    BackendState *state = selectedState();
    if (!state || !state->backend) {
        return;
    }
    openConnectionDialog(state->backend->settings(), state);
}

void MainWindow::newIm()
{
    BackendState *state = selectedState();
    if (!state || !state->backend) return;

    QString preset;
    if (QTreeWidgetItem *item = m_buddyTree ? m_buddyTree->currentItem() : nullptr) {
        if (item->parent()) preset = item->data(0, Qt::UserRole + 1).toString().trimmed();
    }
    openMessagingDialog(state, preset, false);
}

void MainWindow::joinRoom()
{
    BackendState *state = selectedState();
    if (!state || !state->backend) return;
    openMessagingDialog(state, QString(), true);
}

void MainWindow::addBuddy()
{
    BackendState *state = selectedState();
    if (!state || !state->connected || !state->backend) return;
    const auto protocol = state->backend->settings().protocol;
    if (protocol != ConnectionSettings::Protocol::Oscar
        && protocol != ConnectionSettings::Protocol::Irc) return;

    const bool ircBuddy = protocol == ConnectionSettings::Protocol::Irc;
    bool ok = false;
    const QString buddy = QInputDialog::getText(
        this,
        ircBuddy ? QStringLiteral("Add IRC Buddy / Watch") : QStringLiteral("Add AIM Buddy"),
        ircBuddy ? QStringLiteral("Nickname to watch:") : QStringLiteral("Screen name:"),
        QLineEdit::Normal,
        QString(),
        &ok).trimmed();
    if (ok && !buddy.isEmpty()) {
        state->backend->addBuddy(buddy);
    }
}

void MainWindow::removeBuddy()
{
    QTreeWidgetItem *item = m_buddyTree ? m_buddyTree->currentItem() : nullptr;
    if (!item || !item->parent()) {
        return;
    }

    BackendState *state = stateFromBuddyItem(item);
    const QString buddy = item->data(0, Qt::UserRole + 1).toString();
    if (!state || !state->connected || !state->backend || buddy.isEmpty()) {
        return;
    }

    const bool ircBuddy = state->backend->settings().protocol == ConnectionSettings::Protocol::Irc;
    if (QMessageBox::question(
            this,
            QStringLiteral("Remove Buddy"),
            ircBuddy
                ? QStringLiteral("Remove %1 from this IRC profile's local buddy/watch list?").arg(buddy)
                : QStringLiteral("Remove %1 from the AIM buddy list?").arg(buddy))
        == QMessageBox::Yes) {
        state->backend->removeBuddy(buddy);
    }
}

void MainWindow::setAimPresence(BackendState *state)
{
    if (!state || !state->backend || !state->connected
        || state->backend->settings().protocol != ConnectionSettings::Protocol::Oscar) {
        QMessageBox::information(this, QStringLiteral("AIM Status"),
                                 QStringLiteral("Select and connect an AIM/OSCAR account first."));
        return;
    }
    auto *oscar = qobject_cast<OscarBackend *>(state->backend);
    if (!oscar) return;

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("AIM Status / AFK — %1").arg(accountMenuLabel(state)));
    auto *form = new QFormLayout(&dialog);
    auto *mode = new QComboBox(&dialog);
    mode->addItems({QStringLiteral("Online"), QStringLiteral("Away"),
                    QStringLiteral("AFK"), QStringLiteral("Idle")});
    const QString current = state->presenceState.toCaseFolded();
    if (current == QStringLiteral("away")) mode->setCurrentText(QStringLiteral("Away"));
    else if (current == QStringLiteral("afk")) mode->setCurrentText(QStringLiteral("AFK"));
    else if (state->idleSeconds > 0) mode->setCurrentText(QStringLiteral("Idle"));
    auto *message = new QLineEdit(state->presenceMessage, &dialog);
    message->setPlaceholderText(QStringLiteral("Away/AFK message (optional)"));
    auto *idle = new QSpinBox(&dialog);
    idle->setRange(0, 2147483647);
    idle->setSuffix(QStringLiteral(" seconds"));
    idle->setValue(static_cast<int>(std::min<quint32>(state->idleSeconds, 2147483647U)));
    form->addRow(QStringLiteral("Status:"), mode);
    form->addRow(QStringLiteral("Message:"), message);
    form->addRow(QStringLiteral("Idle time:"), idle);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;

    const QString selected = mode->currentText();
    state->autoPresenceState.clear();
    m_lastUserActivityMs = QDateTime::currentMSecsSinceEpoch();
    if (selected == QStringLiteral("Online")) {
        oscar->setBack();
    } else if (selected == QStringLiteral("Away")) {
        oscar->setAwayMessage(message->text());
        if (idle->value() > 0) oscar->setIdleSeconds(static_cast<quint32>(idle->value()));
    } else if (selected == QStringLiteral("AFK")) {
        oscar->setAfkMessage(message->text());
        if (idle->value() > 0) oscar->setIdleSeconds(static_cast<quint32>(idle->value()));
    } else {
        oscar->setIdleSeconds(static_cast<quint32>(std::max(1, idle->value())));
    }
}

void MainWindow::changePassword()
{
    BackendState *state = selectedState();
    if (!state || !state->connected || !state->backend
        || state->backend->settings().protocol != ConnectionSettings::Protocol::Oscar) {
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Change AIM Password"));
    auto *outer = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    QLineEdit current(&dialog);
    QLineEdit next(&dialog);
    QLineEdit confirm(&dialog);
    current.setEchoMode(QLineEdit::Password);
    next.setEchoMode(QLineEdit::Password);
    confirm.setEchoMode(QLineEdit::Password);
    form->addRow(QStringLiteral("Current password:"), &current);
    form->addRow(QStringLiteral("New password:"), &next);
    form->addRow(QStringLiteral("Confirm new password:"), &confirm);
    outer->addLayout(form);
    QDialogButtonBox buttons(QDialogButtonBox::Cancel | QDialogButtonBox::Ok, &dialog);
    outer->addWidget(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted) {
        if (next.text().isEmpty() || next.text() != confirm.text()) {
            QMessageBox::warning(
                this,
                QStringLiteral("Password change"),
                QStringLiteral("The new passwords are empty or do not match."));
            return;
        }
        state->backend->changePassword(current.text(), next.text());
    }
}

void MainWindow::changeIrcNick()
{
    BackendState *state = selectedState();
    if (!state || !state->connected || !state->backend
        || state->backend->settings().protocol != ConnectionSettings::Protocol::Irc) {
        return;
    }

    bool ok = false;
    const QString nick = QInputDialog::getText(
        this,
        QStringLiteral("Change IRC Nickname"),
        QStringLiteral("New nickname:"),
        QLineEdit::Normal,
        state->identity,
        &ok).trimmed();
    if (ok && !nick.isEmpty()) {
        state->backend->changeNickname(nick);
    }
}

QString MainWindow::selectedBuddyName() const
{
    if (!m_buddyTree) return {};
    QTreeWidgetItem *item = m_buddyTree->currentItem();
    if (!item || !item->parent()) return {};
    return item->data(0, Qt::UserRole + 1).toString().trimmed();
}

void MainWindow::rawProtocolCommand()
{
    BackendState *state = selectedState();
    if (!state || !state->connected || !state->backend) {
        return;
    }

    const auto protocol = state->backend->settings().protocol;
    if (protocol == ConnectionSettings::Protocol::Irc) {
        bool ok = false;
        const QString line = QInputDialog::getText(
            this,
            QStringLiteral("Raw IRC Command"),
            QStringLiteral("IRC line:"),
            QLineEdit::Normal,
            QString(),
            &ok).trimmed();
        if (ok && !line.isEmpty()) {
            state->backend->sendRaw(line);
        }
        return;
    }

    if (protocol == ConnectionSettings::Protocol::Telnet) {
        bool ok = false;
        const QString line = QInputDialog::getText(
            this,
            QStringLiteral("Raw Telnet Line"),
            QStringLiteral("Line to send:"),
            QLineEdit::Normal,
            QString(),
            &ok);
        if (ok) {
            state->backend->sendRaw(line);
        }
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Raw OSCAR SNAC"));
    auto *outer = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    QLineEdit family(QStringLiteral("0x01"), &dialog);
    QLineEdit subtype(QStringLiteral("0x16"), &dialog);
    QLineEdit body(&dialog);
    body.setPlaceholderText(QStringLiteral("hex body, optional"));
    form->addRow(QStringLiteral("Family:"), &family);
    form->addRow(QStringLiteral("Subtype:"), &subtype);
    form->addRow(QStringLiteral("Hex body:"), &body);
    outer->addLayout(form);
    QDialogButtonBox buttons(QDialogButtonBox::Cancel | QDialogButtonBox::Ok, &dialog);
    outer->addWidget(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted) {
        state->backend->sendRaw(family.text(), subtype.text(), body.text());
    }
}

void MainWindow::setBuddyTransparency()
{
    bool ok = false;
    const int current = static_cast<int>(m_buddyOpacity * 100.0 + 0.5);
    const int percent = QInputDialog::getInt(
        this,
        QStringLiteral("Buddy List Transparency"),
        QStringLiteral("Window opacity (%):"),
        current,
        30,
        100,
        5,
        &ok);
    if (!ok) {
        return;
    }
    m_buddyOpacity = static_cast<double>(percent) / 100.0;
    setWindowOpacity(m_buddyOpacity);
    saveUiSettings();
}

void MainWindow::setConnectionsTransparency()
{
    if (!m_connectionsWindow) {
        return;
    }
    bool ok = false;
    const int current = static_cast<int>(m_connectionsOpacity * 100.0 + 0.5);
    const int percent = QInputDialog::getInt(
        this,
        QStringLiteral("Connections/Accounts Window Transparency"),
        QStringLiteral("Window opacity (%):"),
        current,
        30,
        100,
        5,
        &ok);
    if (!ok) {
        return;
    }
    m_connectionsOpacity = static_cast<double>(percent) / 100.0;
    m_connectionsWindow->setWindowOpacity(m_connectionsOpacity);
    saveUiSettings();
}

void MainWindow::showConnectionsWindow()
{
    if (!m_connectionsWindow) {
        return;
    }
    m_connectionsWindow->show();
    m_connectionsWindow->raise();
    m_connectionsWindow->activateWindow();
}

void MainWindow::showBuddyWindow()
{
    show();
    raise();
    activateWindow();
}

void MainWindow::quitApplication()
{
    if (m_quitting) {
        return;
    }
    m_quitting = true;
    saveConnections();
    saveUiSettings();

    const auto windows = m_windows.values();
    for (ChatWindow *window : windows) {
        if (window) {
            window->close();
        }
    }

    if (m_connectionsWindow) {
        m_connectionsWindow->close();
    }

    for (BackendState *state : m_states) {
        if (state && state->backend) {
            state->connected = false;
            state->connecting = false;
            QObject::disconnect(state->backend, nullptr, this, nullptr);
            state->backend->stop();
        }
    }

    if (m_trayIcon) {
        // Remove the macOS NSStatusItem synchronously before QApplication begins
        // teardown. This prevents the status icon from briefly surviving in a
        // recolored/blank state while the last WaffleHouse window disappears.
        m_trayIcon->setContextMenu(nullptr);
        m_trayIcon->hide();
#ifdef Q_OS_MACOS
        delete m_trayIcon;
        m_trayIcon = nullptr;
#endif
    }
    close();
    QApplication::quit();
}

QString MainWindow::imPayload(const QString &text) const
{
    if (text.startsWith(QLatin1Char('<'))) {
        const int end = text.indexOf(QStringLiteral("> "));
        if (end >= 0) return text.mid(end + 2);
    }
    return text;
}

QString MainWindow::imSpeakerPrefix(const QString &text) const
{
    if (text.startsWith(QLatin1Char('<'))) {
        const int end = text.indexOf(QStringLiteral("> "));
        if (end >= 0) return text.left(end + 2);
    }
    return {};
}

QString MainWindow::secureTrustKey(BackendState *state, const QString &target) const
{
    if (!state || state->profileId.isEmpty()) return {};
    const QString source = state->profileId + QChar(0x1f) + target.toCaseFolded();
    const QByteArray digest = QCryptographicHash::hash(source.toUtf8(), QCryptographicHash::Sha256).toHex();
    return QStringLiteral("security/trustedPeers/%1").arg(QString::fromLatin1(digest));
}

QString MainWindow::trustedFingerprint(BackendState *state, const QString &target) const
{
    const QString key = secureTrustKey(state, target);
    if (key.isEmpty()) return {};
    QSettings settings;
    return settings.value(key).toString();
}

void MainWindow::setTrustedFingerprint(BackendState *state,
                                       const QString &target,
                                       const QString &fingerprint)
{
    const QString key = secureTrustKey(state, target);
    if (key.isEmpty() || fingerprint.isEmpty()) return;
    QSettings settings;
    settings.setValue(key, fingerprint);
    settings.sync();
}

void MainWindow::clearTrustedFingerprint(BackendState *state, const QString &target)
{
    const QString key = secureTrustKey(state, target);
    if (key.isEmpty()) return;
    QSettings settings;
    settings.remove(key);
    settings.sync();
}

void MainWindow::startSecureSession(ChatWindow *window)
{
    if (window && window->kind() == QStringLiteral("chat")) {
        startSecureRoom(window);
        return;
    }
    if (!window || window->kind() != QStringLiteral("im")) return;
    BackendState *state = stateById(window->backendId());
    if (!state || !state->connected || !state->backend) return;
    if (!m_options.encryptedDmEnabled || !m_secureReady) {
        QMessageBox::warning(this, QStringLiteral("Encrypted DMs"),
                             m_secureReady
                                 ? QStringLiteral("Encrypted communications are disabled in Tools > Options.")
                                 : QStringLiteral("Encrypted communications are unavailable: %1").arg(m_secureError));
        return;
    }
    if (state->backend->settings().protocol == ConnectionSettings::Protocol::Telnet) return;

    QString notice;
    const QString frame = m_secure.beginHandshake(state->profileId, window->target(), &notice);
    if (frame.isEmpty()) {
        window->appendMessage(QStringLiteral("[error] [secure] Could not start secure handshake."));
        return;
    }
    m_outgoingSecureFrames.insert(state->profileId + QChar(0x1f)
        + window->target().toCaseFolded() + QChar(0x1f) + frame);
    state->backend->sendPrivateMessage(window->target(), frame);
    window->appendMessage(QStringLiteral("[secure] %1").arg(notice));
    updateConversationSecurity(window);
}

void MainWindow::showSecureStatus(ChatWindow *window)
{
    if (window && window->kind() == QStringLiteral("chat")) {
        showSecureRoomStatus(window);
        return;
    }
    if (!window || window->kind() != QStringLiteral("im")) return;
    BackendState *state = stateById(window->backendId());
    if (!state || !m_secureReady) return;

    const QString peer = m_secure.peerFingerprint(state->profileId, window->target());
    const QString local = m_secure.localFingerprint(state->profileId);
    const QString trusted = trustedFingerprint(state, window->target());
    QString trustState = QStringLiteral("not yet pinned");
    if (!peer.isEmpty() && trusted == peer) trustState = QStringLiteral("trusted / verified");
    else if (!trusted.isEmpty() && trusted != peer) trustState = QStringLiteral("TRUST MISMATCH");

    const QString body = peer.isEmpty()
        ? QStringLiteral("No secure session is active with %1.\n\nLocal fingerprint:\n%2")
              .arg(window->displayName(), local)
        : QStringLiteral("Secure session with %1\n\nPeer fingerprint:\n%2\n\nLocal fingerprint:\n%3\n\nTrust: %4\n\nWaffleHouse automatically saves the first authenticated peer identity. A later identity change is blocked and reported as a trust mismatch.")
              .arg(window->displayName(), peer, local, trustState);
    QMessageBox::information(this, QStringLiteral("Secure Session Status"), body);
}

void MainWindow::trustSecurePeer(ChatWindow *window)
{
    if (!window || window->kind() != QStringLiteral("im")) return;
    BackendState *state = stateById(window->backendId());
    if (!state || !m_secureReady) return;
    const QString peer = m_secure.peerFingerprint(state->profileId, window->target());
    if (peer.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Trust Peer"),
                                 QStringLiteral("Start a secure session first so the client can receive the peer fingerprint."));
        return;
    }

    const auto answer = QMessageBox::question(
        this,
        QStringLiteral("Trust Peer Fingerprint"),
        QStringLiteral("Peer: %1\n\nFingerprint:\n%2\n\nHave you compared this fingerprint through a separate trusted channel and confirmed it matches?")
            .arg(window->displayName(), peer),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (answer != QMessageBox::Yes) return;

    setTrustedFingerprint(state, window->target(), peer);
    window->appendMessage(QStringLiteral("[secure] Peer fingerprint trusted: %1").arg(peer));
    updateConversationSecurity(window);
}

void MainWindow::untrustSecurePeer(ChatWindow *window)
{
    if (!window || window->kind() != QStringLiteral("im")) return;
    BackendState *state = stateById(window->backendId());
    if (!state) return;
    clearTrustedFingerprint(state, window->target());
    window->appendMessage(QStringLiteral("[secure] Saved trusted fingerprint cleared."));
    updateConversationSecurity(window);
}

void MainWindow::closeSecureSession(ChatWindow *window)
{
    if (window && window->kind() == QStringLiteral("chat")) {
        closeSecureRoom(window);
        return;
    }
    if (!window || window->kind() != QStringLiteral("im")) return;
    BackendState *state = stateById(window->backendId());
    if (!state) return;
    m_secure.closeSession(state->profileId, window->target());
    window->appendMessage(QStringLiteral("[secure] Secure session closed; messages are plaintext until a new secure session is started."));
    updateConversationSecurity(window);
}

void MainWindow::startSecureRoom(ChatWindow *window)
{
    if (!window || window->kind() != QStringLiteral("chat")) return;
    BackendState *state = stateById(window->backendId());
    if (!state || !state->connected || !state->backend) return;
    const auto protocol = state->backend->settings().protocol;
    if (protocol != ConnectionSettings::Protocol::Oscar
        && protocol != ConnectionSettings::Protocol::Irc) {
        window->appendMessage(QStringLiteral("[error] [secure-room] Secure rooms are available only for AIM/OSCAR and IRC chats."));
        return;
    }
    if (!m_options.encryptedDmEnabled || !m_secureReady) {
        QMessageBox::warning(this, QStringLiteral("Secure Room"),
                             m_secureReady
                                 ? QStringLiteral("Secure communications are disabled in Tools > Options.")
                                 : QStringLiteral("Secure communications are unavailable: %1").arg(m_secureError));
        return;
    }

    QString error;
    if (!m_secureRooms.createOrRotate(state->profileId, window->target(), &error)) {
        window->appendMessage(QStringLiteral("[error] [secure-room] %1").arg(error));
        return;
    }

    window->appendMessage(QStringLiteral(
        "[secure-room] New shared room key %1 created. Key material is distributed only through encrypted CPX private sessions; the public room will carry ciphertext.")
        .arg(m_secureRooms.keyId(state->profileId, window->target())));
    distributeSecureRoomKeyToMembers(state, window);
    updateConversationSecurity(window);
}

void MainWindow::showSecureRoomStatus(ChatWindow *window)
{
    if (!window || window->kind() != QStringLiteral("chat")) return;
    BackendState *state = stateById(window->backendId());
    if (!state || !m_secureReady) return;

    const bool active = m_secureRooms.hasRoom(state->profileId, window->target());
    const QString id = m_secureRooms.keyId(state->profileId, window->target());
    const QString role = m_secureRooms.locallyOwned(state->profileId, window->target())
        ? QStringLiteral("key owner / distributor")
        : QStringLiteral("participant");
    const QString body = active
        ? QStringLiteral(
            "Secure room: %1\n\nKey ID: %2\nRole: %3\n\n"
            "Room messages are encrypted with XChaCha20-Poly1305 before being sent to IRC/AIM. "
            "The room key itself is delivered separately through CPX encrypted private sessions. "
            "Any plaintext message received while secure-room mode is active is marked [plaintext].")
              .arg(window->displayName(), id, role)
        : QStringLiteral("No secure room key is active for %1.\n\nUse /secure or Security > Start Secure Room.")
              .arg(window->displayName());
    QMessageBox::information(this, QStringLiteral("Secure Room Status"), body);
}

void MainWindow::closeSecureRoom(ChatWindow *window)
{
    if (!window || window->kind() != QStringLiteral("chat")) return;
    BackendState *state = stateById(window->backendId());
    if (!state) return;
    m_secureRooms.closeRoom(state->profileId, window->target());
    window->appendMessage(QStringLiteral(
        "[secure-room] Secure room closed locally. New messages from this client will be plaintext until /secure is started again."));
    updateConversationSecurity(window);
}

void MainWindow::distributeSecureRoomKey(BackendState *state,
                                         ChatWindow *window,
                                         const QString &peer)
{
    if (!state || !state->backend || !window || peer.trimmed().isEmpty()) return;
    const QString cleanPeer = peer.trimmed();
    const QString own = state->identity.isEmpty()
        ? state->backend->settings().username
        : state->identity;
    if (!own.isEmpty() && cleanPeer.compare(own, Qt::CaseInsensitive) == 0) return;

    const QString pendingKey = state->profileId + QChar(0x1f) + cleanPeer.toCaseFolded();
    const QString room = window->target();

    if (!m_secure.hasSession(state->profileId, cleanPeer)) {
        m_pendingSecureRoomKeys[pendingKey].insert(room);
        window->appendMessage(QStringLiteral(
            "[secure-room] %1 is not included yet: establish a secure PM with that user first. "
            "Once the CPX session is active, WaffleHouse will send this room key automatically.")
            .arg(cleanPeer));
        return;
    }

    const QStringList caps = m_secure.peerCapabilities(state->profileId, cleanPeer);
    if (!m_secure.peerSupports(state->profileId, cleanPeer, QStringLiteral("secure-room-v1"))) {
        if (caps.isEmpty()) {
            m_pendingSecureRoomKeys[pendingKey].insert(room);
            window->appendMessage(QStringLiteral(
                "[secure-room] Waiting for %1 to advertise secure-room capability.").arg(cleanPeer));
        } else {
            m_pendingSecureRoomKeys[pendingKey].remove(room);
            window->appendMessage(QStringLiteral(
                "[secure-room] %1 does not advertise secure-room-v1 and will not receive this room key.").arg(cleanPeer));
        }
        return;
    }

    QString error;
    const QString offer = m_secureRooms.keyOffer(state->profileId, room, &error);
    if (offer.isEmpty()) {
        window->appendMessage(QStringLiteral("[error] [secure-room] %1").arg(error));
        return;
    }
    const QString encrypted = m_secure.encrypt(state->profileId, cleanPeer, offer, &error);
    if (encrypted.isEmpty()) {
        window->appendMessage(QStringLiteral(
            "[error] [secure-room] Could not encrypt room key for %1: %2").arg(cleanPeer, error));
        return;
    }

    m_outgoingSecureFrames.insert(state->profileId + QChar(0x1f)
        + cleanPeer.toCaseFolded() + QChar(0x1f) + encrypted);
    state->backend->sendPrivateMessage(cleanPeer, encrypted);
    m_pendingSecureRoomKeys[pendingKey].remove(room);
    if (m_pendingSecureRoomKeys[pendingKey].isEmpty()) m_pendingSecureRoomKeys.remove(pendingKey);
    window->appendMessage(QStringLiteral(
        "[secure-room] Room key %1 sent privately to %2 over CPX encryption.")
        .arg(m_secureRooms.keyId(state->profileId, room), cleanPeer));
}

void MainWindow::distributeSecureRoomKeyToMembers(BackendState *state, ChatWindow *window)
{
    if (!state || !window) return;
    QStringList members = window->members();
    members.sort(Qt::CaseInsensitive);
    int peers = 0;
    for (const QString &member : members) {
        const QString own = state->identity.isEmpty()
            ? state->backend->settings().username
            : state->identity;
        if (!own.isEmpty() && member.compare(own, Qt::CaseInsensitive) == 0) continue;
        ++peers;
        distributeSecureRoomKey(state, window, member);
    }
    if (peers == 0) {
        window->appendMessage(QStringLiteral(
            "[secure-room] No other room members are currently known; the key will be distributed when members are discovered."));
    }
}

void MainWindow::flushPendingSecureRoomKeys(BackendState *state, const QString &peer)
{
    if (!state || !state->backend || peer.trimmed().isEmpty()) return;
    const QString key = state->profileId + QChar(0x1f) + peer.trimmed().toCaseFolded();
    const QSet<QString> rooms = m_pendingSecureRoomKeys.value(key);
    if (rooms.isEmpty()) return;
    for (const QString &room : rooms) {
        ChatWindow *window = m_windows.value(
            conversationKey(state->backend, QStringLiteral("chat"), room), nullptr);
        if (window && m_secureRooms.hasRoom(state->profileId, room)) {
            distributeSecureRoomKey(state, window, peer);
        }
    }
}

bool MainWindow::handleSecureRoomKeyOffer(BackendState *state,
                                          const QString &peer,
                                          const QString &plaintext)
{
    if (!state || !SecureRoomManager::looksLikeKeyOffer(plaintext)) return false;
    QString room, id, error;
    if (!m_secureRooms.installKeyOffer(state->profileId, plaintext, &room, &id, &error)) {
        appendActivity(state->backend,
            QStringLiteral("[error] [secure-room] Key offer from %1 rejected: %2").arg(peer, error));
        return true;
    }

    ChatWindow *roomWindow = m_windows.value(
        conversationKey(state->backend, QStringLiteral("chat"), room), nullptr);
    const QString notice = QStringLiteral(
        "[secure-room] Installed shared room key %1 received privately from %2. Public room ciphertext can now be decrypted.")
        .arg(id, peer);
    if (roomWindow) {
        roomWindow->appendMessage(notice);
        updateConversationSecurity(roomWindow);
    } else {
        appendActivity(state->backend, QStringLiteral("%1 (%2)").arg(notice, room));
    }
    return true;
}

void MainWindow::showSelectedFingerprint()
{
    BackendState *state = selectedState();
    if (!state || !m_secureReady) {
        QMessageBox::warning(this, QStringLiteral("Secure Identity"),
                             m_secureReady ? QStringLiteral("Select a connection first.")
                                           : QStringLiteral("Encrypted communications are unavailable: %1").arg(m_secureError));
        return;
    }
    QMessageBox::information(
        this,
        QStringLiteral("Secure Identity Fingerprint"),
        QStringLiteral("This saved connection profile has its own stable secure identity.\n\n%1\n\nFingerprint:\n%2")
            .arg(state->backend ? state->backend->protocolName() : QStringLiteral("Connection"),
                 m_secure.localFingerprint(state->profileId)));
}

void MainWindow::sendFile(ChatWindow *window)
{
    if (!window || window->kind() != QStringLiteral("im")) return;
    BackendState *state = stateById(window->backendId());
    sendFileToTarget(state, window->target(), window->displayName());
}

void MainWindow::sendFileToTarget(BackendState *state,
                          const QString &target,
                          const QString &displayName)
{
    const QString peerTarget = target.trimmed();
    const QString peerName = displayName.trimmed().isEmpty() ? peerTarget : displayName.trimmed();
    if (!state || !state->connected || !state->backend || peerTarget.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Send File"),
                                 QStringLiteral("Connect the AIM or IRC account and select a buddy before sending a file."));
        return;
    }
    if (state->backend->settings().protocol != ConnectionSettings::Protocol::Oscar
        && state->backend->settings().protocol != ConnectionSettings::Protocol::Irc) {
        QMessageBox::information(this, QStringLiteral("Send File"),
                                 QStringLiteral("WaffleHouse file transfer is available in AIM and IRC private messages."));
        return;
    }

    QDialog modeDialog(this);
    modeDialog.setWindowTitle(QStringLiteral("Send File — %1").arg(peerName));
    modeDialog.setMinimumWidth(430);
    auto *outer = new QVBoxLayout(&modeDialog);
    auto *title = new QLabel(QStringLiteral("Choose transfer security"), &modeDialog);
    title->setObjectName(QStringLiteral("CardTitle"));
    outer->addWidget(title);
    auto *secure = new QRadioButton(QStringLiteral("Secure — CPX encrypted and authenticated"), &modeDialog);
    auto *unsecured = new QRadioButton(QStringLiteral("Unsecured — ordinary AIM/IRC private-message transport"), &modeDialog);
    secure->setChecked(true);
    outer->addWidget(secure);
    outer->addWidget(unsecured);
    auto *help = new QLabel(&modeDialog);
    help->setWordWrap(true);
    help->setObjectName(QStringLiteral("Muted"));
    outer->addWidget(help);
    auto updateHelp = [=] {
        help->setText(secure->isChecked()
            ? QStringLiteral("Secure transfer requires an established CPX secure DM with this peer. Open the PM and click Secure; WaffleHouse automatically saves the first authenticated identity. The existing CPX encryption/authentication and encrypted direct path remain unchanged.")
            : QStringLiteral("Unsecured transfer proceeds over ordinary AIM/IRC PM traffic without CPX encryption or authentication. File chunks remain resumable and the completed file is still verified with SHA-256. Transfer control traffic stays out of the visible IM transcript."));
    };
    connect(secure, &QRadioButton::toggled, &modeDialog, updateHelp);
    updateHelp();
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &modeDialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Continue"));
    connect(buttons, &QDialogButtonBox::accepted, &modeDialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &modeDialog, &QDialog::reject);
    outer->addWidget(buttons);
    if (modeDialog.exec() != QDialog::Accepted) return;

    const bool secureTransfer = secure->isChecked();
    if (secureTransfer) {
        if (!m_secureReady || !m_secure.hasSession(state->profileId, peerTarget)) {
            QMessageBox::information(
                this, QStringLiteral("Secure File Transfer — Setup Required"),
                QStringLiteral("To send securely:\n\n"
                               "1. Open the private message with %1.\n"
                               "2. Click Secure to establish the CPX session.\n"
                               "3. WaffleHouse automatically saves the first authenticated peer identity.\n"
                               "4. Choose Send File again and select Secure.\n\n"
                               "Nothing will be sent until the secure session is established.")
                    .arg(peerName));
            return;
        }
        if (!m_secure.peerSupports(state->profileId, peerTarget, QStringLiteral("file-transfer"))) {
            QMessageBox::information(this, QStringLiteral("Secure File Transfer"),
                                     QStringLiteral("This peer has not advertised CPX file-transfer support. The peer may be running an older client."));
            return;
        }
    }

    const QString path = QFileDialog::getOpenFileName(
        this, secureTransfer ? QStringLiteral("Send Secure File") : QStringLiteral("Send Unsecured File"));
    if (path.isEmpty()) return;

    QString transferId;
    QString offer;
    QString error;
    const bool reliableTransfer = secureTransfer
        ? m_secure.peerSupports(state->profileId, peerTarget, QStringLiteral("file-ack"))
        : true; // Unsecured WaffleHouse peers use ACK/resume framing by default.
    const bool directPreferred = secureTransfer && reliableTransfer && m_secure.peerSupports(
        state->profileId, peerTarget, QStringLiteral("file-direct-v1"));
    if (!m_fileTransfers.createOffer(peerTarget, path, transferId, offer, &error,
                                     reliableTransfer)) {
        QMessageBox::warning(this, QStringLiteral("File Transfer"), error);
        return;
    }
    m_fileTransferProfiles.insert(transferId, state->profileId);
    m_fileTransferSecure.insert(transferId, secureTransfer);
    m_fileTransferProgressShown.insert(transferId, -10);

    refreshTransferWindow(transferId, QStringLiteral("Upload"), peerName, QStringLiteral("Offering"));
    logTransfer(QStringLiteral("Offering %1 to %2 [%3] — %4")
                    .arg(QFileInfo(path).fileName(), peerName, transferId,
                         secureTransfer
                             ? (directPreferred ? QStringLiteral("secure CPX; encrypted direct transport preferred")
                                                : QStringLiteral("secure CPX relay"))
                             : QStringLiteral("UNSECURED AIM/IRC relay; SHA-256 verification enabled")));

    if (!sendSecureControlPayload(state, peerTarget, offer)) {
        logTransfer(QStringLiteral("Failed to send file offer for %1 [%2]")
                        .arg(QFileInfo(path).fileName(), transferId));
        m_fileTransfers.cancel(transferId, QStringLiteral("transport failed"));
        return;
    }
}

bool MainWindow::sendSecureControlPayload(BackendState *state,
                                          const QString &target,
                                          const QString &plaintext)
{
    if (!state || !state->backend || !state->connected) return false;

    const QString transferId = WaffleFileTransport::transferId(plaintext);
    const bool secureTransfer = transferId.isEmpty()
        ? true : m_fileTransferSecure.value(transferId, true);

    if (!secureTransfer) {
        const QString frame = WaffleFileTransport::wrapUnsecured(plaintext);
        if (state->backend->settings().protocol == ConnectionSettings::Protocol::Irc
            && frame.toUtf8().size() > 400) {
            logTransfer(QStringLiteral("ERROR: unsecured IRC transfer frame exceeded the safe message size."));
            return false;
        }
        m_outgoingUnsecuredFileFrames.insert(state->profileId + QChar(0x1f)
            + target.toCaseFolded() + QChar(0x1f) + frame);
        state->backend->sendPrivateMessage(target, frame);
        return true;
    }

    if (!m_secureReady || !m_secure.hasSession(state->profileId, target)) return false;
    QString error;
    const QString frame = m_secure.encrypt(state->profileId, target, plaintext, &error);
    if (frame.isEmpty()) {
        logTransfer(QStringLiteral("ERROR: %1").arg(error));
        return false;
    }
    if (state->backend->settings().protocol == ConnectionSettings::Protocol::Irc
        && frame.toUtf8().size() > 400) {
        const QString message = QStringLiteral("Internal IRC transfer frame exceeded the safe message size.");
        logTransfer(QStringLiteral("ERROR: %1").arg(message));
        return false;
    }
    m_outgoingSecureFrames.insert(state->profileId + QChar(0x1f)
        + target.toCaseFolded() + QChar(0x1f) + frame);
    state->backend->sendPrivateMessage(target, frame);
    return true;
}

void MainWindow::logTransfer(const QString &message, bool showWindow)
{
    if (!m_transferWindow || message.trimmed().isEmpty()) return;
    m_transferWindow->appendLog(message);
    if (showWindow) m_transferWindow->showAndRaise();
}

void MainWindow::refreshTransferWindow(const QString &transferId,
                                       const QString &direction,
                                       const QString &peer,
                                       const QString &status)
{
    if (!m_transferWindow || transferId.isEmpty()) return;
    const auto info = m_fileTransfers.transfer(transferId);
    if (info.id.isEmpty()) return;
    m_transferWindow->updateTransfer(
        transferId,
        direction,
        peer,
        info.fileName,
        info.transferred,
        info.total,
        status.isEmpty() ? info.status : status,
        info.resumable);
}

void MainWindow::appendTransferProgress(const CpxFileTransferManager::Event &event,
                                        const QString &direction,
                                        const QString &peer)
{
    if (event.id.isEmpty()) return;
    const auto info = m_fileTransfers.transfer(event.id);
    const qint64 transferred = event.transferred > 0 ? event.transferred : info.transferred;
    const qint64 total = event.total > 0 ? event.total : info.total;
    const int percent = total > 0 ? static_cast<int>((transferred * 100) / total) : event.percent;
    const int bucket = percent >= 100 ? 100 : (qMax(0, percent) / 5) * 5;
    const int previous = m_fileTransferProgressShown.value(event.id, -5);

    refreshTransferWindow(event.id, direction, peer,
                          info.status.isEmpty() ? QStringLiteral("Transferring") : info.status);
    if (bucket <= previous && bucket < 100) return;
    m_fileTransferProgressShown[event.id] = bucket;
    logTransfer(QStringLiteral("%1 %2: %3% (%4 / %5 bytes) [%6]")
                    .arg(direction, info.fileName)
                    .arg(qMax(0, percent))
                    .arg(transferred)
                    .arg(total)
                    .arg(event.id), false);
}

bool MainWindow::handleFileTransferPayload(BackendState *state,
                                           const QString &target,
                                           const QString &plaintext,
                                           ChatWindow *window,
                                           bool secureTransport)
{
    Q_UNUSED(window);
    if (!CpxFileTransferManager::looksLikeMessage(plaintext)) return false;
    if (!state) return true;

    const QString peer = targetDisplayName(state, QStringLiteral("im"), target);
    const auto event = m_fileTransfers.processIncoming(target, plaintext);
    if (!event.id.isEmpty()) {
        m_fileTransferProfiles.insert(event.id, state->profileId);
        m_fileTransferSecure.insert(event.id, secureTransport);
    }
    if (!event.replyPayload.isEmpty()) {
        sendSecureControlPayload(state, target, event.replyPayload);
    }

    using Kind = CpxFileTransferManager::EventKind;
    switch (event.kind) {
    case Kind::OfferReceived: {
        refreshTransferWindow(event.id, QStringLiteral("Download"), peer, QStringLiteral("Waiting for acceptance"));
        logTransfer(QStringLiteral("%1 wants to send %2 (%3 bytes) [%4]")
                        .arg(peer, event.fileName).arg(event.total).arg(event.id));
        const auto answer = QMessageBox::question(
            m_transferWindow ? static_cast<QWidget *>(m_transferWindow) : static_cast<QWidget *>(this),
            secureTransport ? QStringLiteral("Secure File Transfer") : QStringLiteral("Unsecured File Transfer"),
            (secureTransport
                ? QStringLiteral("%1 wants to send:\n\n%2\n%3 bytes\n\nAccept this encrypted CPX transfer?")
                : QStringLiteral("%1 wants to send:\n\n%2\n%3 bytes\n\nThis transfer is NOT encrypted or authenticated by CPX. SHA-256 integrity verification remains enabled. Accept?"))
                .arg(peer, event.fileName).arg(event.total),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (answer != QMessageBox::Yes) {
            const QString reply = m_fileTransfers.declineIncoming(event.id, QStringLiteral("declined by user"));
            sendSecureControlPayload(state, target, reply);
            refreshTransferWindow(event.id, QStringLiteral("Download"), peer, QStringLiteral("Declined"));
            logTransfer(QStringLiteral("Declined incoming transfer %1 [%2]").arg(event.fileName, event.id));
            return true;
        }
        QString downloadDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
        if (downloadDir.isEmpty()) downloadDir = QDir::homePath();
        const QString suggested = QDir(downloadDir).filePath(event.fileName);
        const QString destination = QFileDialog::getSaveFileName(
            m_transferWindow ? static_cast<QWidget *>(m_transferWindow) : static_cast<QWidget *>(this),
            secureTransport ? QStringLiteral("Save Secure File") : QStringLiteral("Save Unsecured File"), suggested);
        if (destination.isEmpty()) {
            const QString reply = m_fileTransfers.declineIncoming(event.id, QStringLiteral("save cancelled"));
            sendSecureControlPayload(state, target, reply);
            refreshTransferWindow(event.id, QStringLiteral("Download"), peer, QStringLiteral("Cancelled"));
            logTransfer(QStringLiteral("Save cancelled for %1 [%2]").arg(event.fileName, event.id));
            return true;
        }
        QString error;
        QString reply = m_fileTransfers.acceptIncoming(event.id, destination, &error);
        if (reply.isEmpty()) {
            logTransfer(QStringLiteral("ERROR accepting %1: %2 [%3]").arg(event.fileName, error, event.id));
            const QString cancel = m_fileTransfers.declineIncoming(event.id, error);
            sendSecureControlPayload(state, target, cancel);
            refreshTransferWindow(event.id, QStringLiteral("Download"), peer, QStringLiteral("Error"));
            return true;
        }

        // Prefer the dedicated encrypted TCP data path whenever both peers
        // advertise it. AIM/IRC remains the authenticated control channel;
        // if the direct socket cannot be established we automatically resume
        // at the receiver's current byte offset through the reliable relay.
        const QString relayReply = reply;
        bool directReady = false;
        if (secureTransport
            && m_secure.peerSupports(state->profileId, target, QStringLiteral("file-direct-v1"))
            && m_secure.peerSupports(state->profileId, target, QStringLiteral("file-ack"))) {
            QString keyError;
            const QByteArray transferKey = m_secure.fileTransferKey(
                state->profileId, target, event.id, &keyError);
            const auto acceptedInfo = m_fileTransfers.transfer(event.id);
            CpxDirectTransferManager::ListenResult listener;
            if (!transferKey.isEmpty()
                && m_directTransfers.prepareIncoming(
                    event.id,
                    acceptedInfo.path + QStringLiteral(".cpxpart"),
                    acceptedInfo.total,
                    acceptedInfo.transferred,
                    transferKey,
                    listener,
                    &error)) {
                const QString directReply = m_fileTransfers.acceptIncoming(
                    event.id, destination, &error, listener.port, listener.hosts);
                directReady = !directReply.isEmpty();
                if (directReady) {
                    reply = directReply;
                } else {
                    reply = relayReply;
                    m_directTransfers.cancel(event.id);
                }
            } else if (!keyError.isEmpty()) {
                error = keyError;
            }
            if (!directReady && !error.isEmpty()) {
                logTransfer(QStringLiteral("Direct transfer unavailable for %1; using secure relay fallback: %2 [%3]")
                                .arg(event.fileName, error, event.id), false);
                error.clear();
            }
        }

        m_fileTransferProgressShown.insert(event.id, -5);
        if (!sendSecureControlPayload(state, target, reply) && directReady) {
            m_directTransfers.cancel(event.id);
            directReady = false;
            reply = m_fileTransfers.fallbackIncomingToRelay(event.id);
            if (!reply.isEmpty()) sendSecureControlPayload(state, target, reply);
            logTransfer(QStringLiteral("Direct endpoint advertisement could not be relayed; using secure relay fallback [%1]")
                            .arg(event.id), false);
        }
        const auto info = m_fileTransfers.transfer(event.id);
        refreshTransferWindow(event.id, QStringLiteral("Download"), peer,
                              directReady ? QStringLiteral("Waiting for direct connection")
                                          : (info.transferred > 0 ? QStringLiteral("Resuming relay")
                                                                : QStringLiteral("Receiving by relay")));
        if (directReady) {
            logTransfer(QStringLiteral("Accepted %1 → %2 [%3] — encrypted direct transport prepared")
                            .arg(event.fileName, destination, event.id));
        } else {
            const QString relayMode = secureTransport ? QStringLiteral("secure relay")
                                                        : QStringLiteral("UNSECURED relay");
            logTransfer(info.transferred > 0
                ? QStringLiteral("Resuming %1 at byte %2 → %3 [%4] by %5")
                      .arg(event.fileName).arg(info.transferred).arg(destination, event.id, relayMode)
                : QStringLiteral("Receiving %1 → %2 [%3] by %4")
                      .arg(event.fileName, destination, event.id, relayMode));
        }
        return true;
    }
    case Kind::Accepted:
        if (event.direct && m_fileTransferSecure.value(event.id, true)) {
            refreshTransferWindow(event.id, QStringLiteral("Upload"), peer,
                                  QStringLiteral("Connecting direct"));
            logTransfer(QStringLiteral("%1 accepted %2; establishing encrypted direct data connection [%3]")
                            .arg(peer, event.fileName, event.id));
            startDirectOutgoing(event, state);
        } else {
            refreshTransferWindow(event.id, QStringLiteral("Upload"), peer, QStringLiteral("Sending by relay"));
            logTransfer(QStringLiteral("%1 accepted %2; %3 upload started [%4]")
                            .arg(peer, event.fileName,
                                 m_fileTransferSecure.value(event.id, true) ? QStringLiteral("secure relay")
                                                                            : QStringLiteral("UNSECURED relay"),
                                 event.id));
            appendTransferProgress(event, QStringLiteral("Upload"), peer);
        }
        return true;
    case Kind::Fallback:
        m_directTransfers.cancel(event.id);
        refreshTransferWindow(event.id, QStringLiteral("Download"), peer,
                              QStringLiteral("Relay fallback"));
        logTransfer(QStringLiteral("Direct transport fallback requested%1; resuming through secure relay at byte %2 [%3]")
                        .arg(event.reason.isEmpty() ? QString() : QStringLiteral(": %1").arg(event.reason))
                        .arg(event.transferred).arg(event.id));
        return true;
    case Kind::Declined:
        refreshTransferWindow(event.id, QStringLiteral("Upload"), peer, QStringLiteral("Declined"));
        logTransfer(QStringLiteral("Transfer declined%1 [%2]")
                        .arg(event.reason.isEmpty() ? QString() : QStringLiteral(": %1").arg(event.reason), event.id));
        return true;
    case Kind::ResumeRequested:
        logTransfer(QStringLiteral("%1 requested resume of %2 [%3]")
                        .arg(peer, event.fileName, event.id));
        resumeIncomingFileTransfer(event.id, state, peer);
        return true;
    case Kind::Progress:
        appendTransferProgress(event, event.outgoing ? QStringLiteral("Upload") : QStringLiteral("Download"), peer);
        return true;
    case Kind::Completed:
        appendTransferProgress(event, event.outgoing ? QStringLiteral("Upload") : QStringLiteral("Download"), peer);
        refreshTransferWindow(event.id,
                              event.outgoing ? QStringLiteral("Upload") : QStringLiteral("Download"),
                              peer, QStringLiteral("Complete"));
        if (event.outgoing) {
            logTransfer(QStringLiteral("Receiver confirmed SHA-256 verification; upload complete: %1 [%2]")
                            .arg(event.fileName, event.id));
        } else {
            logTransfer(QStringLiteral("Download complete and SHA-256 verified: %1 [%2]")
                            .arg(event.path, event.id));
        }
        m_fileTransferProgressShown.remove(event.id);
        QTimer::singleShot(1800, this, [this, id = event.id]() {
            clearFileTransfer(id);
        });
        return true;
    case Kind::Cancelled: {
        m_directTransfers.cancel(event.id);
        const auto info = m_fileTransfers.transfer(event.id);
        refreshTransferWindow(event.id,
                              info.outgoing ? QStringLiteral("Upload") : QStringLiteral("Download"),
                              peer, QStringLiteral("Cancelled"));
        logTransfer(QStringLiteral("Transfer cancelled%1 [%2]")
                        .arg(event.reason.isEmpty() ? QString() : QStringLiteral(": %1").arg(event.reason), event.id));
        m_fileTransferProgressShown.remove(event.id);
        return true;
    }
    case Kind::Error: {
        const auto info = m_fileTransfers.transfer(event.id);
        if (!event.id.isEmpty()) {
            m_directTransfers.cancel(event.id);
            const QString cancel = m_fileTransfers.cancel(event.id, event.reason);
            sendSecureControlPayload(state, target, cancel);
        }
        refreshTransferWindow(event.id,
                              info.outgoing ? QStringLiteral("Upload") : QStringLiteral("Download"),
                              peer, QStringLiteral("Error"));
        logTransfer(QStringLiteral("ERROR: %1%2")
                        .arg(event.reason,
                             event.id.isEmpty() ? QString() : QStringLiteral(" [%1]").arg(event.id)));
        return true;
    }
    case Kind::None:
        return true;
    }
    return true;
}

void MainWindow::cancelFileTransfer(const QString &transferId)
{
    const auto info = m_fileTransfers.transfer(transferId);
    if (info.id.isEmpty() || info.complete) return;

    m_directTransfers.cancel(transferId);
    const QString payload = m_fileTransfers.cancel(
        transferId, QStringLiteral("cancelled by user"));
    BackendState *state = stateByProfileId(m_fileTransferProfiles.value(transferId));
    if (state && state->connected && state->backend && !info.target.isEmpty()) {
        sendSecureControlPayload(state, info.target, payload);
    }
    const QString peer = state
        ? targetDisplayName(state, QStringLiteral("im"), info.target)
        : info.target;
    refreshTransferWindow(transferId,
                          info.outgoing ? QStringLiteral("Upload") : QStringLiteral("Download"),
                          peer, QStringLiteral("Cancelled"));
    logTransfer(QStringLiteral("Cancelled %1 transfer of %2 [%3]; chat connection remains open")
                    .arg(info.outgoing ? QStringLiteral("upload") : QStringLiteral("download"),
                         info.fileName, transferId));
    m_fileTransferProgressShown.remove(transferId);
}

bool MainWindow::resumeIncomingFileTransfer(const QString &transferId,
                                            BackendState *state,
                                            const QString &peer)
{
    const auto info = m_fileTransfers.transfer(transferId);
    if (info.id.isEmpty() || info.outgoing || !state || !state->connected || !state->backend) return false;
    if (!m_fileTransfers.canResume(transferId)) {
        QMessageBox::information(this, QStringLiteral("Resume File Transfer"),
                                 QStringLiteral("This transfer no longer has resumable partial data."));
        refreshTransferWindow(transferId, QStringLiteral("Download"), peer, QStringLiteral("Cancelled"));
        return false;
    }
    const bool secureTransfer = m_fileTransferSecure.value(transferId, true);
    if (secureTransfer && !m_secure.hasSession(state->profileId, info.target)) {
        QMessageBox::information(this, QStringLiteral("Resume File Transfer"),
                                 QStringLiteral("Re-establish the secure CPX session with this peer before resuming the transfer."));
        return false;
    }

    m_directTransfers.cancel(transferId);
    QString error;
    QString payload;
    bool directReady = false;
    if (secureTransfer
        && m_secure.peerSupports(state->profileId, info.target, QStringLiteral("file-direct-v1"))
        && m_secure.peerSupports(state->profileId, info.target, QStringLiteral("file-ack"))) {
        QString keyError;
        const QByteArray transferKey = m_secure.fileTransferKey(
            state->profileId, info.target, transferId, &keyError);
        const QString partPath = info.path + QStringLiteral(".cpxpart");
        qint64 resumeOffset = 0;
        const QFileInfo partInfo(partPath);
        if (partInfo.exists() && partInfo.isFile() && partInfo.size() <= info.total) {
            resumeOffset = partInfo.size();
        }
        CpxDirectTransferManager::ListenResult listener;
        if (!transferKey.isEmpty()
            && m_directTransfers.prepareIncoming(
                transferId, partPath, info.total, resumeOffset,
                transferKey, listener, &error)) {
            payload = m_fileTransfers.resumeIncoming(
                transferId, &error, listener.port, listener.hosts);
            directReady = !payload.isEmpty();
            if (!directReady) m_directTransfers.cancel(transferId);
        } else if (!keyError.isEmpty()) {
            error = keyError;
        }
    }
    if (!directReady) {
        error.clear();
        payload = m_fileTransfers.resumeIncoming(transferId, &error);
    }
    if (payload.isEmpty()) {
        logTransfer(QStringLiteral("ERROR resuming %1: %2 [%3]")
                        .arg(info.fileName, error, transferId));
        return false;
    }
    if (!sendSecureControlPayload(state, info.target, payload)) {
        if (directReady) m_directTransfers.cancel(transferId);
        logTransfer(QStringLiteral("ERROR: could not send resume acceptance for %1 [%2]")
                        .arg(info.fileName, transferId));
        return false;
    }

    const auto resumed = m_fileTransfers.transfer(transferId);
    m_fileTransferProgressShown.insert(transferId, -5);
    refreshTransferWindow(transferId, QStringLiteral("Download"), peer,
                          directReady ? QStringLiteral("Waiting for direct connection")
                                      : (resumed.transferred > 0 ? QStringLiteral("Resuming relay")
                                                                 : QStringLiteral("Receiving by relay")));
    logTransfer(QStringLiteral("Resuming download %1 at byte %2 [%3]%4")
                    .arg(resumed.fileName)
                    .arg(resumed.transferred)
                    .arg(transferId)
                    .arg(directReady ? QStringLiteral(" — encrypted direct transport prepared")
                                     : QStringLiteral(" — secure relay")));
    return true;
}

void MainWindow::resumeFileTransfer(const QString &transferId)
{
    const auto info = m_fileTransfers.transfer(transferId);
    if (info.id.isEmpty() || !m_fileTransfers.canResume(transferId)) {
        QMessageBox::information(this, QStringLiteral("Resume File Transfer"),
                                 QStringLiteral("This transfer is no longer resumable."));
        return;
    }
    BackendState *state = stateByProfileId(m_fileTransferProfiles.value(transferId));
    const bool secureTransfer = m_fileTransferSecure.value(transferId, true);
    if (!state || !state->connected || !state->backend
        || (secureTransfer && !m_secure.hasSession(state->profileId, info.target))) {
        QMessageBox::information(this, QStringLiteral("Resume File Transfer"),
                                 secureTransfer
                                     ? QStringLiteral("Re-establish the connection and secure CPX session with this peer before resuming.")
                                     : QStringLiteral("Re-establish the AIM/IRC connection with this peer before resuming."));
        return;
    }
    const QString peer = targetDisplayName(state, QStringLiteral("im"), info.target);
    if (!info.outgoing) {
        resumeIncomingFileTransfer(transferId, state, peer);
        return;
    }

    QString error;
    const QString payload = m_fileTransfers.requestResume(transferId, &error);
    if (payload.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Resume File Transfer"), error);
        return;
    }
    if (!sendSecureControlPayload(state, info.target, payload)) {
        m_fileTransfers.cancel(transferId, QStringLiteral("resume request could not be sent"));
        refreshTransferWindow(transferId, QStringLiteral("Upload"), peer, QStringLiteral("Cancelled"));
        return;
    }
    refreshTransferWindow(transferId, QStringLiteral("Upload"), peer, QStringLiteral("Resume requested"));
    logTransfer(QStringLiteral("Requested resume of upload %1 [%2]").arg(info.fileName, transferId));
}

void MainWindow::clearFileTransfer(const QString &transferId)
{
    const auto info = m_fileTransfers.transfer(transferId);
    if (info.id.isEmpty()) {
        if (m_transferWindow) m_transferWindow->removeTransfer(transferId);
        return;
    }
    if (!info.complete) return;

    m_directTransfers.cancel(transferId);
    QString error;
    if (!m_fileTransfers.clearTransfer(transferId, &error)) {
        if (!error.isEmpty()) logTransfer(QStringLiteral("ERROR clearing transfer: %1 [%2]").arg(error, transferId));
        return;
    }
    m_fileTransferProfiles.remove(transferId);
    m_fileTransferSecure.remove(transferId);
    m_fileTransferProgressShown.remove(transferId);
    if (m_transferWindow) m_transferWindow->removeTransfer(transferId);
}

void MainWindow::startDirectOutgoing(const CpxFileTransferManager::Event &event,
                                     BackendState *state)
{
    if (!state || event.id.isEmpty()) return;
    const auto info = m_fileTransfers.transfer(event.id);
    QString error;
    const QByteArray key = m_secure.fileTransferKey(
        state->profileId, info.target, event.id, &error);
    if (key.isEmpty()
        || !m_directTransfers.startOutgoing(event.id, info.path, info.total,
                                            info.transferred, event.directHosts,
                                            event.directPort, key, &error)) {
        handleDirectFailure(event.id,
                            error.isEmpty()
                                ? QStringLiteral("direct transfer could not be started")
                                : error,
                            true);
        return;
    }
    const QString peer = targetDisplayName(state, QStringLiteral("im"), info.target);
    refreshTransferWindow(event.id, QStringLiteral("Upload"), peer,
                          QStringLiteral("Direct encrypted transfer"));
    logTransfer(QStringLiteral("Direct encrypted upload connected/preparing for %1 [%2]")
                    .arg(info.fileName, event.id), false);
}

void MainWindow::handleDirectProgress(const QString &transferId,
                                      qint64 transferred,
                                      qint64 total,
                                      bool outgoing)
{
    m_fileTransfers.updateDirectProgress(transferId, transferred, outgoing);
    const auto info = m_fileTransfers.transfer(transferId);
    BackendState *state = stateByProfileId(m_fileTransferProfiles.value(transferId));
    const QString peer = state
        ? targetDisplayName(state, QStringLiteral("im"), info.target)
        : info.target;
    CpxFileTransferManager::Event event;
    event.id = transferId;
    event.fileName = info.fileName;
    event.transferred = transferred;
    event.total = total;
    event.outgoing = outgoing;
    appendTransferProgress(event,
                           outgoing ? QStringLiteral("Upload") : QStringLiteral("Download"),
                           peer);
    refreshTransferWindow(transferId,
                          outgoing ? QStringLiteral("Upload") : QStringLiteral("Download"),
                          peer,
                          outgoing ? QStringLiteral("Sending direct")
                                   : QStringLiteral("Receiving direct"));
}

void MainWindow::handleDirectIncomingFinished(const QString &transferId)
{
    const auto before = m_fileTransfers.transfer(transferId);
    if (before.id.isEmpty()) return;

    BackendState *state = stateByProfileId(m_fileTransferProfiles.value(transferId));
    const QString peer = state
        ? targetDisplayName(state, QStringLiteral("im"), before.target)
        : before.target;

    // Finalize the local file first.  A temporary account lookup/control-channel
    // problem must never leave a fully received, verified file stranded as
    // *.cpxpart.  The peer confirmation is control-plane bookkeeping; the
    // receiver's SHA-256 verification and atomic rename are local data-plane work.
    QString error;
    if (!m_fileTransfers.finalizeIncomingDirect(transferId, &error)) {
        const QString cancel = m_fileTransfers.cancel(transferId, error);
        refreshTransferWindow(transferId, QStringLiteral("Download"), peer,
                              QStringLiteral("Error"));
        logTransfer(QStringLiteral("ERROR verifying direct download %1: %2 [%3]")
                        .arg(before.fileName, error, transferId));
        if (state && state->connected && state->backend) {
            sendSecureControlPayload(state, before.target, cancel);
        }
        return;
    }

    if (state && state->connected && state->backend) {
        sendSecureControlPayload(state, before.target,
                                 m_fileTransfers.completionPayload(transferId));
    } else {
        logTransfer(QStringLiteral("Direct download verified locally, but the peer completion confirmation could not be sent because the owning account is no longer available [%1]")
                        .arg(transferId), false);
    }
    const auto info = m_fileTransfers.transfer(transferId);
    refreshTransferWindow(transferId, QStringLiteral("Download"), peer,
                          QStringLiteral("Complete"));
    logTransfer(QStringLiteral("Direct download complete and SHA-256 verified: %1 [%2]")
                    .arg(info.path, transferId));
    m_fileTransferProgressShown.remove(transferId);
    QTimer::singleShot(1800, this, [this, transferId]() {
        clearFileTransfer(transferId);
    });
}

void MainWindow::handleDirectOutgoingFinished(const QString &transferId)
{
    m_fileTransfers.markOutgoingDirectSent(transferId);
    const auto info = m_fileTransfers.transfer(transferId);
    BackendState *state = stateByProfileId(m_fileTransferProfiles.value(transferId));
    const QString peer = state
        ? targetDisplayName(state, QStringLiteral("im"), info.target)
        : info.target;
    refreshTransferWindow(transferId, QStringLiteral("Upload"), peer,
                          QStringLiteral("Verifying"));
    logTransfer(QStringLiteral("Direct upload transmitted: %1; waiting for receiver SHA-256 confirmation [%2]")
                    .arg(info.fileName, transferId));
}

void MainWindow::handleDirectFailure(const QString &transferId,
                                     const QString &reason,
                                     bool outgoing)
{
    const auto info = m_fileTransfers.transfer(transferId);
    BackendState *state = stateByProfileId(m_fileTransferProfiles.value(transferId));
    if (info.id.isEmpty() || !state || !state->connected || !state->backend) return;

    m_directTransfers.cancel(transferId);
    const QString peer = targetDisplayName(state, QStringLiteral("im"), info.target);
    QString payload;
    if (outgoing) {
        payload = m_fileTransfers.requestOutgoingRelayFallback(transferId, reason);
        refreshTransferWindow(transferId, QStringLiteral("Upload"), peer,
                              QStringLiteral("Switching to relay"));
        logTransfer(QStringLiteral("Direct upload unavailable (%1); requesting secure relay resume [%2]")
                        .arg(reason, transferId));
    } else {
        payload = m_fileTransfers.fallbackIncomingToRelay(transferId);
        refreshTransferWindow(transferId, QStringLiteral("Download"), peer,
                              QStringLiteral("Switching to relay"));
        logTransfer(QStringLiteral("Direct download interrupted (%1); resuming through secure relay at byte %2 [%3]")
                        .arg(reason).arg(info.transferred).arg(transferId));
    }
    if (!payload.isEmpty()) sendSecureControlPayload(state, info.target, payload);
}

void MainWindow::pumpFileTransfers()
{
    const QStringList ids = m_fileTransfers.activeOutgoingIds();
    for (const QString &id : ids) {
        const QString profileId = m_fileTransferProfiles.value(id);
        BackendState *state = nullptr;
        for (BackendState *candidate : m_states) {
            if (candidate && candidate->profileId == profileId) { state = candidate; break; }
        }
        const auto before = m_fileTransfers.transfer(id);
        const bool secureTransfer = m_fileTransferSecure.value(id, true);
        if (!state || !state->connected || !state->backend
            || (secureTransfer && !m_secure.hasSession(profileId, before.target))) continue;

        const QString peer = targetDisplayName(state, QStringLiteral("im"), before.target);
        const bool irc = state->backend->settings().protocol == ConnectionSettings::Protocol::Irc;
        const bool oscar = state->backend->settings().protocol == ConnectionSettings::Protocol::Oscar;
        // OSCAR rate classes are message-count sensitive.  The old 768-byte / 500 ms
        // relay generated too many ICBMs and some servers began dropping ACK/data
        // frames mid-transfer.  A larger payload at roughly one IM per two seconds
        // moves more bytes per SNAC while staying comfortably under the 8 KiB cap.
        const int rawChunk = irc ? (secureTransfer ? 120 : 96)
                                 : (oscar ? 3600 : 768);
        const int minimumSendIntervalMs = irc ? 1000
                                              : (oscar ? 2200 : 500);
        bool finished = false;
        QString error;
        const QString payload = m_fileTransfers.nextOutgoingPayload(
            id, rawChunk, &finished, &error, minimumSendIntervalMs);
        if (payload.isEmpty()) {
            if (!error.isEmpty()) {
                refreshTransferWindow(id, QStringLiteral("Upload"), peer, QStringLiteral("Error"));
                logTransfer(QStringLiteral("ERROR: %1 [%2]").arg(error, id));
            }
            continue;
        }
        if (!sendSecureControlPayload(state, before.target, payload)) continue;

        const auto after = m_fileTransfers.transfer(id);
        CpxFileTransferManager::Event progress;
        progress.id = id; progress.fileName = after.fileName;
        progress.transferred = after.transferred; progress.total = after.total;
        progress.percent = after.total > 0 ? static_cast<int>((after.transferred * 100) / after.total) : 100;
        appendTransferProgress(progress, QStringLiteral("Upload"), peer);
        if (finished) {
            refreshTransferWindow(id, QStringLiteral("Upload"), peer, QStringLiteral("Verifying"));
            logTransfer(QStringLiteral("Finished transmitting %1; waiting for receiver SHA-256 confirmation [%2]")
                            .arg(after.fileName, id));
        }
    }
}

bool MainWindow::sendPrivateText(BackendState *state,
                                 const QString &target,
                                 const QString &text,
                                 ChatWindow *window)
{
    if (!state || !state->backend || !state->connected) return false;

    if (m_options.encryptedDmEnabled && m_secureReady
        && m_secure.hasSession(state->profileId, target)) {
        QString error;
        const QString frame = m_secure.encrypt(state->profileId, target, text, &error);
        if (frame.isEmpty()) {
            if (window) window->appendMessage(QStringLiteral("[error] [secure] %1").arg(error));
            return false;
        }
        if (state->backend->settings().protocol == ConnectionSettings::Protocol::Irc
            && frame.toUtf8().size() > 400) {
            if (window) window->appendMessage(QStringLiteral(
                "[error] [secure] Encrypted IRC message is too long; split it into shorter messages."));
            return false;
        }

        m_outgoingSecureFrames.insert(state->profileId + QChar(0x1f)
            + target.toCaseFolded() + QChar(0x1f) + frame);
        state->backend->sendPrivateMessage(target, frame);
        appendGuiHistory(state->backend, QStringLiteral("im"), target, QStringLiteral("out"), text);
        if (window) {
            const QString me = state->identity.isEmpty()
                ? (state->backend->settings().username.isEmpty()
                       ? QStringLiteral("me")
                       : state->backend->settings().username)
                : state->identity;
            window->appendMessage(QStringLiteral("<%1> [secure] %2").arg(me, text));
        }
        return true;
    }

    state->backend->sendPrivateMessage(target, text);
    appendGuiHistory(state->backend, QStringLiteral("im"), target, QStringLiteral("out"), text);
    return true;
}

void MainWindow::handleConversationMessage(ChatWindow *window, const QString &text)
{
    if (!window) return;
    BackendState *state = stateById(window->backendId());
    if (!state || !state->backend || !state->connected) return;

    const QString trimmedCommand = text.trimmed();
    const QString command = trimmedCommand.toCaseFolded();
    if (window->kind() != QStringLiteral("terminal")) {
        if (command == QStringLiteral("/version") || command.startsWith(QStringLiteral("/version "))) {
            QString target = trimmedCommand.mid(QStringLiteral("/version").size()).trimmed();
            if (target.isEmpty() && window->kind() == QStringLiteral("im")) target = window->target();
            if (target.isEmpty()) {
                window->appendMessage(QStringLiteral("[version] Usage: /version USER (or run /version in a PM)."));
                return;
            }
            requestClientVersion(state, target);
            return;
        }
        if (command == QStringLiteral("/options")) { showOptionsDialog(); return; }
        if (command == QStringLiteral("/help")) { showHelpDialog(); return; }
        if (command == QStringLiteral("/wafflecast") || command == QStringLiteral("/wcast")) {
            if (!m_mediaWindow || !m_mediaWindow->waffleCastActive()) {
                window->appendMessage(QStringLiteral(
                    "[WaffleCast] No broadcast is running. Start one from Media Center first."));
                return;
            }
            if (window->kind() != QStringLiteral("im") && window->kind() != QStringLiteral("chat")) {
                window->appendMessage(QStringLiteral(
                    "[WaffleCast] Invites can be sent from AIM/IRC IMs, PMs, or chat/channel windows."));
                return;
            }
            const QString frame = m_mediaWindow->waffleCastInviteFrame();
            if (frame.isEmpty()) {
                window->appendMessage(QStringLiteral("[WaffleCast] Could not create an invite frame."));
                return;
            }
            if (state->backend->settings().protocol == ConnectionSettings::Protocol::Irc
                && frame.toUtf8().size() > 400) {
                window->appendMessage(QStringLiteral(
                    "[WaffleCast] Invite is too long for IRC. Use a shorter advertised hostname/IP."));
                return;
            }
            if (window->kind() == QStringLiteral("chat")) {
                if (m_secureRooms.hasRoom(state->profileId, window->target())) {
                    QString error;
                    const QString encrypted = m_secureRooms.encrypt(state->profileId, window->target(), frame, &error);
                    if (encrypted.isEmpty()) {
                        window->appendMessage(QStringLiteral("[error] [WaffleCast] %1").arg(error));
                        return;
                    }
                    if (state->backend->settings().protocol == ConnectionSettings::Protocol::Irc
                        && encrypted.toUtf8().size() > 400) {
                        window->appendMessage(QStringLiteral(
                            "[WaffleCast] Secure invite is too long for IRC; try a PM or a shorter advertised hostname/IP."));
                        return;
                    }
                    state->backend->sendRoomMessage(window->target(), encrypted);
                } else {
                    state->backend->sendRoomMessage(window->target(), frame);
                }
            } else {
                if (!sendPrivateText(state, window->target(), frame, nullptr)) return;
            }
            const QString title = m_mediaWindow->waffleCastTitle();
            window->appendMessage(title.isEmpty()
                ? QStringLiteral("[WaffleCast] Invite sent.")
                : QStringLiteral("[WaffleCast] Invite sent: %1").arg(title));
            return;
        }
        if (command == QStringLiteral("/fingerprint")) { selectState(state); showSelectedFingerprint(); return; }
        if (window->kind() == QStringLiteral("im") || window->kind() == QStringLiteral("chat")) {
            if (command == QStringLiteral("/secure")) { startSecureSession(window); return; }
            if (command == QStringLiteral("/securestatus")) { showSecureStatus(window); return; }
            if (command == QStringLiteral("/secureoff")) { closeSecureSession(window); return; }
            if (window->kind() == QStringLiteral("im")) {
                if (command == QStringLiteral("/trust")) { trustSecurePeer(window); return; }
                if (command == QStringLiteral("/untrust")) { untrustSecurePeer(window); return; }
            }
        }
    }

    // IRC conversation input uses the same slash-command parser as the CLI.
    // WaffleHouse-local commands above keep priority. Recognized IRC commands
    // bypass CPX room encryption; unknown /text deliberately falls through and
    // is sent as ordinary (and, when enabled, secure-room-encrypted) chat text.
    if (state->backend->settings().protocol == ConnectionSettings::Protocol::Irc
        && window->kind() != QStringLiteral("terminal")) {
        if (auto *irc = qobject_cast<IrcBackend *>(state->backend)) {
            const QString roomContext = window->kind() == QStringLiteral("chat")
                ? window->target() : QString();
            if (irc->handleSlashCommand(roomContext, text)) return;
        }
    }

    if (window->kind() == QStringLiteral("chat")) {
        if (m_secureRooms.hasRoom(state->profileId, window->target())) {
            QString error;
            const QString frame = m_secureRooms.encrypt(state->profileId, window->target(), text, &error);
            if (frame.isEmpty()) {
                window->appendMessage(QStringLiteral("[error] [secure-room] %1").arg(error));
                return;
            }
            if (state->backend->settings().protocol == ConnectionSettings::Protocol::Irc
                && frame.toUtf8().size() > 400) {
                window->appendMessage(QStringLiteral(
                    "[error] [secure-room] Encrypted IRC room message is too long; split it into shorter messages."));
                return;
            }
            state->backend->sendRoomMessage(window->target(), frame);
        } else {
            state->backend->sendRoomMessage(window->target(), text);
        }
        appendGuiHistory(state->backend, QStringLiteral("chat"), window->target(), QStringLiteral("out"), text);
    } else if (window->kind() == QStringLiteral("terminal")) {
        state->backend->sendPrivateMessage(window->target(), text);
    } else {
        sendPrivateText(state, window->target(), text, window);
    }
}

void MainWindow::updateConversationSecurity(ChatWindow *window)
{
    if (!window || !m_secureReady) {
        if (window) window->setSecurityState(false, false);
        return;
    }
    BackendState *state = stateById(window->backendId());
    if (!state) {
        window->setSecurityState(false, false);
        return;
    }

    if (window->kind() == QStringLiteral("chat")) {
        const bool active = m_secureRooms.hasRoom(state->profileId, window->target());
        window->setSecurityState(active, true,
                                 active ? m_secureRooms.keyId(state->profileId, window->target()) : QString());
        return;
    }

    if (window->kind() != QStringLiteral("im")
        || !m_secure.hasSession(state->profileId, window->target())) {
        window->setSecurityState(false, false);
        return;
    }
    const QString peer = m_secure.peerFingerprint(state->profileId, window->target());
    const QString trusted = trustedFingerprint(state, window->target());
    window->setSecurityState(true, !peer.isEmpty() && trusted == peer,
                             peer, m_secure.localFingerprint(state->profileId));
}

void MainWindow::handleConnected(ChatBackend *backend,
                                 const QString &identity,
                                 const QString &endpoint)
{
    BackendState *state = stateFor(backend);
    if (!state) {
        return;
    }

    state->connecting = false;
    state->connected = true;
    state->identity = identity;
    state->endpoint = endpoint;
    updateConnectionItem(state);
    appendActivity(backend,
                   QStringLiteral("Connected as %1 — %2").arg(identity, endpoint));
    refreshBuddyList();
    selectState(state);
    updateActions();
    statusBar()->showMessage(
        QStringLiteral("%1 connected").arg(backend->protocolName()), 4000);

    if (backend->settings().protocol == ConnectionSettings::Protocol::Telnet
        || backend->settings().protocol == ConnectionSettings::Protocol::Ssh) {
        ChatWindow *terminal = ensureConversationWindow(
            backend, QStringLiteral("terminal"), backend->settings().server, true);
        if (terminal) terminal->setBackendOnline(true);
    }

    // Successful login always brings the Buddy List to the front.
    showBuddyWindow();
}

void MainWindow::handleDisconnected(ChatBackend *backend, const QString &reason)
{
    BackendState *state = stateFor(backend);
    if (!state) {
        return;
    }

    const bool failedWhileConnecting = state->connecting;
    state->connecting = false;
    state->connected = false;
    state->onlineBuddies.clear();
    state->oscarBuddyPresence.clear();
    state->presenceState = QStringLiteral("ONLINE");
    state->presenceMessage.clear();
    state->idleSeconds = 0;
    state->autoPresenceState.clear();

    if (failedWhileConnecting && state->secretRequired) {
        ConnectionSettings cleared = state->backend->settings();
        if (!cleared.savePassword) {
            cleared.password.clear();
            if (auto *sip = qobject_cast<SipBackend *>(state->backend)) {
                // Clearing a failed session secret must not reconfigure PJSUA2
                // from inside a disconnect/error callback.
                sip->clearSessionPassword();
            } else {
                state->backend->setConnectionSettings(cleared);
            }
        }
        state->hasSessionSecret = cleared.savePassword && !cleared.password.isEmpty();
    }

    if (!state->profileId.isEmpty()) {
        m_secure.closeConnection(state->profileId);
        m_secureRooms.closeConnection(state->profileId);
    }
    const QString roomPrefix = backend->id() + QStringLiteral("|chat|");
    for (auto it = m_closedRoomKeys.begin(); it != m_closedRoomKeys.end();) {
        if (it->startsWith(roomPrefix)) it = m_closedRoomKeys.erase(it);
        else ++it;
    }
    if (m_oscarVoice && m_oscarVoice->isPrepared() && m_oscarVoiceBackendId == backend->id()) {
        hangupOscarVoice(false);
    }
    closeBackendWindows(backend);
    updateConnectionItem(state);
    appendActivity(backend, QStringLiteral("Disconnected: %1").arg(reason));
    refreshBuddyList();
    updateActions();
}

bool MainWindow::handleWaffleCastPayload(ChatBackend *backend,
                                              const QString &kind,
                                              const QString &target,
                                              const QString &text)
{
    if (kind != QStringLiteral("im") && kind != QStringLiteral("chat")) return false;
    WaffleCastInvite invite;
    const QString payload = imPayload(text);
    if (!WaffleCastInvite::decode(payload, &invite)) return false;

    QString sender = target;
    const QString prefix = imSpeakerPrefix(text);
    if (prefix.startsWith(QLatin1Char('<'))) {
        const int end = prefix.indexOf(QLatin1Char('>'));
        if (end > 1) sender = prefix.mid(1, end - 1);
    }
    if (sender.trimmed().isEmpty()) sender = QStringLiteral("another WaffleHouse user");

    // AIM and IRC backends locally echo sent messages. Consume our own encoded
    // WaffleCast frame silently; handleConversationMessage() already adds the
    // friendly "Invite sent" line to the conversation.
    if (BackendState *state = stateFor(backend)) {
        QString self = state->identity.trimmed();
        if (self.isEmpty()) self = state->backend->settings().username.trimmed();
        if (!self.isEmpty() && sender.compare(self, Qt::CaseInsensitive) == 0) return true;
    }

    ChatWindow *window = ensureConversationWindow(backend, kind, target, true);
    if (window) {
        const QString title = invite.title.isEmpty() ? QStringLiteral("live audio") : invite.title;
        window->appendMessage(QStringLiteral("[WaffleCast] %1 is broadcasting: %2").arg(sender, title));
    }

    if (!m_mediaWindow) {
        if (window) window->appendMessage(QStringLiteral("[WaffleCast] Media Center is not available in this build."));
        return true;
    }

    const QString title = invite.title.isEmpty() ? QStringLiteral("WaffleCast live audio") : invite.title;
    const QString endpoint = QStringLiteral("%1:%2")
        .arg(invite.streamUrl.host())
        .arg(invite.streamUrl.port(80));
    const auto answer = QMessageBox::question(
        this, QStringLiteral("WaffleCast Invite"),
        QStringLiteral("%1 invited you to listen to:\n\n%2\n\nStream: %3\n\nListen in WaffleHouse Media Center?")
            .arg(sender, title, endpoint),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (answer == QMessageBox::Yes) {
        m_mediaWindow->joinWaffleCast(invite.streamUrl, invite.title, sender);
    }
    return true;
}

void MainWindow::handleEvent(ChatBackend *backend,
                             const QString &kind,
                             const QString &target,
                             const QString &text)
{
    BackendState *state = stateFor(backend);
    if (!state) return;

    if (handleWaffleCastPayload(backend, kind, target, text)) return;

    if (kind == QStringLiteral("version-request")) {
        if (auto *irc = qobject_cast<IrcBackend *>(backend); irc && !target.isEmpty()) {
            const QString ctcp = QString(QChar(0x01))
                + QStringLiteral("VERSION WaffleHouse-Client %1").arg(appVersionString())
                + QChar(0x01);
            irc->sendRaw(QStringLiteral("NOTICE %1 :%2").arg(target, ctcp));
        }
        return;
    }
    if (kind == QStringLiteral("version")) {
        m_pendingVersionQueries.remove(state->profileId + QChar(0x1f) + target.toCaseFolded());
        QString report = text.trimmed();
        if (backend->settings().protocol == ConnectionSettings::Protocol::Irc
            && !report.contains(QStringLiteral("WaffleHouse"), Qt::CaseInsensitive)) {
            report = QStringLiteral("IRC client reports: %1 (not identified as WaffleHouse-Client/WaffleHouse-compatible)").arg(report);
        }
        const QString line = QStringLiteral("[version] %1: %2").arg(target, report);
        ChatWindow *versionWindow = m_windows.value(conversationKey(backend, QStringLiteral("im"), target));
        if (versionWindow) versionWindow->appendMessage(line);
        else appendActivity(backend, line);
        statusBar()->showMessage(line, 7000);
        return;
    }

    // OSCAR typing notifications are transient UI state, not conversation
    // messages.  Keep them attached to the existing IM window instead of
    // letting the generic event path create a separate "typing" window.
    if (kind == QStringLiteral("typing")) {
        ChatWindow *window = m_windows.value(
            conversationKey(backend, QStringLiteral("im"), target), nullptr);
        if (window) {
            const QString stateText = text.trimmed().toCaseFolded();
            if (stateText == QStringLiteral("typing"))
                window->setPeerTypingState(QStringLiteral("%1 is typing…").arg(target));
            else if (stateText == QStringLiteral("paused"))
                window->setPeerTypingState(QStringLiteral("%1 paused typing").arg(target));
            else
                window->setPeerTypingState(QString());
        }
        return;
    }

    if (kind == QStringLiteral("status") || target.isEmpty()) {
        appendActivity(backend, text);
        return;
    }
    if (!state->connected) return;
    if (kind == QStringLiteral("chat")
        && m_closedRoomKeys.contains(conversationKey(backend, kind, target))) {
        return;
    }

    if (kind == QStringLiteral("im")) {
        const QString payload = imPayload(text);
        const QString outgoingToken = state->profileId + QChar(0x1f)
            + target.toCaseFolded() + QChar(0x1f) + payload;
        if (m_outgoingUnsecuredFileFrames.remove(outgoingToken)) return;
        QString filePayload;
        if (WaffleFileTransport::unwrapUnsecured(payload, filePayload)) {
            // File-transfer control/data frames are transport traffic, not IMs.
            // Process them without creating or surfacing a conversation window.
            handleFileTransferPayload(state, target, filePayload, nullptr, false);
            return;
        }
    }

    if (kind == QStringLiteral("im") && m_secureReady) {
        const QString payload = imPayload(text);
        const QString outgoingToken = state->profileId + QChar(0x1f)
            + target.toCaseFolded() + QChar(0x1f) + payload;
        if (m_outgoingSecureFrames.remove(outgoingToken)) {
            return;
        }

        if (SecureChannelManager::looksLikeFrame(payload)) {
            if (!m_options.encryptedDmEnabled) {
                ChatWindow *window = ensureConversationWindow(backend, kind, target, true);
                if (window) {
                    window->appendMessage(QStringLiteral(
                        "[secure] Encrypted DM frame ignored because encrypted DMs are disabled."));
                }
                return;
            }

            const auto result = m_secure.processIncoming(
                state->profileId, target, payload, m_options.autoReplySecure);

            if (!result.replyFrame.isEmpty()) {
                m_outgoingSecureFrames.insert(state->profileId + QChar(0x1f)
                    + target.toCaseFolded() + QChar(0x1f) + result.replyFrame);
                backend->sendPrivateMessage(target, result.replyFrame);
            }

            const QString capsFrame = m_secure.capabilitiesFrame(state->profileId, target);
            if (!capsFrame.isEmpty()) {
                m_outgoingSecureFrames.insert(state->profileId + QChar(0x1f)
                    + target.toCaseFolded() + QChar(0x1f) + capsFrame);
                backend->sendPrivateMessage(target, capsFrame);
            }

            if (result.kind == SecureChannelManager::IncomingKind::Decrypted) {
                if (handleWaffleCastPayload(backend, kind, target, result.plaintext)) return;
                // File traffic rides inside the secure DM envelope, but it must
                // remain invisible to normal conversations.  Handle it before
                // creating a ChatWindow so receiving/sending a file never pops
                // open an IM on either side.
                if (handleFileTransferPayload(state, target, result.plaintext, nullptr, true)) {
                    return;
                }
                if (handleSecureRoomKeyOffer(state, target, result.plaintext)) {
                    if (ChatWindow *existing = m_windows.value(
                            conversationKey(backend, kind, target), nullptr)) {
                        updateConversationSecurity(existing);
                    }
                    return;
                }

                ChatWindow *window = ensureConversationWindow(backend, kind, target, true);
                if (!window) return;
                QString prefix = imSpeakerPrefix(text);
                if (prefix.isEmpty()) {
                    prefix = QStringLiteral("<%1> ").arg(targetDisplayName(state, kind, target));
                }
                window->appendMessage(prefix + QStringLiteral("[secure] ") + result.plaintext);
                appendGuiHistory(backend, kind, target, QStringLiteral("in"), result.plaintext);
                if (const auto event = NotificationManager::classifyIncoming(
                        state->backend->settings(), state->identity, kind, text)) {
                    if (!NotificationManager::play(*event, false)) QApplication::beep();
                    if (m_trayIcon && m_trayIcon->isVisible()) {
                        m_trayIcon->showMessage(NotificationManager::displayName(*event),
                                                targetDisplayName(state, kind, target),
                                                QSystemTrayIcon::Information, 3000);
                    }
                }
                updateConversationSecurity(window);
                return;
            }

            ChatWindow *window = ensureConversationWindow(backend, kind, target, true);
            if (!window) return;

            if (result.kind == SecureChannelManager::IncomingKind::Error) {
                window->appendMessage(QStringLiteral("[error] [secure] %1").arg(result.notice));
                updateConversationSecurity(window);
                return;
            }

            if (result.kind == SecureChannelManager::IncomingKind::Control) {
                QString notice = result.notice;
                if (!result.peerFingerprint.isEmpty()) {
                    const QString trusted = trustedFingerprint(state, target);
                    if (!trusted.isEmpty() && trusted != result.peerFingerprint) {
                        window->appendMessage(
                            QStringLiteral("[error] [secure] TRUST WARNING: fingerprint changed. Trusted %1; received %2")
                                .arg(trusted, result.peerFingerprint));
                        m_secure.closeSession(state->profileId, target);
                        updateConversationSecurity(window);
                        return;
                    }
                    if (trusted == result.peerFingerprint) {
                        notice += QStringLiteral(" [trusted]");
                    } else {
                        // Trust on first use (TOFU): the Secure button is intentionally
                        // one-click for normal users.  The first successfully authenticated
                        // CPX identity is pinned automatically.  A later fingerprint change
                        // is still rejected by the mismatch check above.  This changes only
                        // trust UX; CPX encryption, authentication and key derivation remain
                        // untouched.
                        setTrustedFingerprint(state, target, result.peerFingerprint);
                        notice += QStringLiteral(" [identity saved]");
                    }
                }
                if (!notice.isEmpty()) {
                    if (m_options.showSecureFingerprints || result.peerFingerprint.isEmpty()) {
                        window->appendMessage(QStringLiteral("[secure] %1").arg(notice));
                    } else {
                        window->appendMessage(QStringLiteral("[secure] Secure session established."));
                    }
                }
                updateConversationSecurity(window);
                flushPendingSecureRoomKeys(state, target);
                return;
            }
        }
    }

    if (kind == QStringLiteral("chat") && m_secureReady) {
        const QString payload = imPayload(text);
        if (SecureRoomManager::looksLikeFrame(payload)) {
            ChatWindow *window = ensureConversationWindow(backend, kind, target, true);
            if (!window) return;
            const auto result = m_secureRooms.processIncoming(state->profileId, target, payload);
            if (result.kind == SecureRoomManager::IncomingKind::Decrypted) {
                const QString wafflePrefix = imSpeakerPrefix(text);
                if (handleWaffleCastPayload(backend, kind, target, wafflePrefix + result.plaintext)) return;
                QString prefix = imSpeakerPrefix(text);
                if (prefix.isEmpty()) prefix = QStringLiteral("<room> ");
                window->appendMessage(prefix + QStringLiteral("[secure-room] ") + result.plaintext);
                appendGuiHistory(backend, kind, target, QStringLiteral("in"), result.plaintext);
                updateConversationSecurity(window);
                if (const auto event = NotificationManager::classifyIncoming(
                        state->backend->settings(), state->identity, kind, text)) {
                    if (!NotificationManager::play(*event, false)) QApplication::beep();
                }
                return;
            }
            if (result.kind == SecureRoomManager::IncomingKind::Error) {
                window->appendMessage(QStringLiteral("[error] [secure-room] %1").arg(result.notice));
                return;
            }
        }

        if (m_secureRooms.hasRoom(state->profileId, target)) {
            const QString prefix = imSpeakerPrefix(text);
            if (!prefix.isEmpty()) {
                ChatWindow *window = ensureConversationWindow(backend, kind, target, true);
                if (window) {
                    window->appendMessage(prefix + QStringLiteral("[plaintext] ") + payload);
                    return;
                }
            }
        }
    }

    ChatWindow *window = ensureConversationWindow(backend, kind, target, true);
    if (window) window->appendMessage(text);
    appendGuiHistory(backend, kind, target, QStringLiteral("in"), text);
    if (const auto event = NotificationManager::classifyIncoming(
            state->backend->settings(), state->identity, kind, text)) {
        if (!NotificationManager::play(*event, false)) QApplication::beep();
        if (m_trayIcon && m_trayIcon->isVisible()) {
            m_trayIcon->showMessage(NotificationManager::displayName(*event),
                                    targetDisplayName(state, kind, target),
                                    QSystemTrayIcon::Information, 3000);
        }
    }
}

void MainWindow::handleMembers(ChatBackend *backend,
                               const QString &room,
                               const QString &action,
                               const QStringList &names)
{
    BackendState *state = stateFor(backend);
    if (!state || !state->connected) {
        return;
    }
    if (m_closedRoomKeys.contains(conversationKey(backend, QStringLiteral("chat"), room))) {
        return;
    }

    ChatWindow *window = ensureConversationWindow(
        backend, QStringLiteral("chat"), room, false);
    if (window) {
        window->updateMembers(action, names);
        if ((action == QStringLiteral("add") || action == QStringLiteral("remove"))
            && m_secureRooms.hasRoom(state->profileId, room)
            && m_secureRooms.locallyOwned(state->profileId, room)) {
            QString error;
            if (m_secureRooms.createOrRotate(state->profileId, room, &error)) {
                window->appendMessage(QStringLiteral(
                    "[secure-room] Membership changed; rotated shared key to %1 and redistributing it to current members.")
                    .arg(m_secureRooms.keyId(state->profileId, room)));
                distributeSecureRoomKeyToMembers(state, window);
                updateConversationSecurity(window);
            } else {
                window->appendMessage(QStringLiteral("[error] [secure-room] Key rotation failed: %1").arg(error));
            }
        }
    }
}

void MainWindow::handleTargetNamed(ChatBackend *backend,
                                   const QString &kind,
                                   const QString &target,
                                   const QString &displayName)
{
    BackendState *state = stateFor(backend);
    if (!state) {
        return;
    }
    state->targetNames.insert(QStringLiteral("%1|%2").arg(kind, target), displayName);
    const QString key = conversationKey(backend, kind, target);
    if (ChatWindow *window = m_windows.value(key, nullptr)) {
        window->setDisplayName(displayName);
    }
}

void MainWindow::handleRoomDiscovered(ChatBackend *backend,
                                      const QString &roomId,
                                      const QString &displayName)
{
    BackendState *state = stateFor(backend);
    if (!state) {
        return;
    }
    state->discoveredRooms.insert(roomId, displayName);
    state->targetNames.insert(QStringLiteral("chat|%1").arg(roomId), displayName);
}

void MainWindow::handleBuddyList(ChatBackend *backend, const QStringList &names)
{
    BackendState *state = stateFor(backend);
    if (!state || !state->backend) {
        return;
    }
    const bool localSipContacts =
        state->backend->settings().protocol == ConnectionSettings::Protocol::Sip;
    if (!state->connected && !localSipContacts) {
        return;
    }
    state->buddies.clear();
    for (const QString &name : names) {
        if (!name.trimmed().isEmpty()) {
            state->buddies.insert(name.trimmed());
        }
    }
    if (state->backend
        && (state->backend->settings().protocol == ConnectionSettings::Protocol::Irc
            || state->backend->settings().protocol == ConnectionSettings::Protocol::Sip)) {
        saveConnections();
    }
    refreshBuddyList();
}

void MainWindow::handleBuddyPresence(ChatBackend *backend,
                                     const QString &name,
                                     bool online)
{
    BackendState *state = stateFor(backend);
    if (!state || !state->connected || name.trimmed().isEmpty()) {
        return;
    }

    const QString key = name.toCaseFolded();
    if (online) {
        state->onlineBuddies.insert(key);
        state->buddies.insert(name);
    } else {
        state->onlineBuddies.remove(key);
    }
    refreshBuddyList();
}

void MainWindow::handleBackendError(ChatBackend *backend,
                                    const QString &context,
                                    const QString &message)
{
    appendActivity(backend, QStringLiteral("[error] %1: %2").arg(context, message));
    statusBar()->showMessage(QStringLiteral("%1: %2").arg(context, message), 8000);
}

void MainWindow::handleConversationClosing(ChatWindow *window)
{
    if (!window) return;
    BackendState *state = stateById(window->backendId());
    if (!state || !state->backend) return;

    if (window->kind() == QStringLiteral("im") && !state->profileId.isEmpty()) {
        m_secure.closeSession(state->profileId, window->target());
    }

    if (state->connected && window->kind() == QStringLiteral("chat")) {
        m_secureRooms.closeRoom(state->profileId, window->target());
        m_closedRoomKeys.insert(conversationKey(state->backend, QStringLiteral("chat"), window->target()));
        state->backend->leaveRoom(window->target());
    } else if (state->connected && window->kind() == QStringLiteral("terminal")) {
        state->connected = false;
        state->connecting = false;
        updateConnectionItem(state);
        refreshBuddyList();
        updateActions();
        state->backend->stop();
    }
}

