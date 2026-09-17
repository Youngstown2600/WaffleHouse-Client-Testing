#include "softphonewindow.h"

#include "appbranding.h"
#include "sipcontroller.h"
#include "trunkmonkey/Profile.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QList>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextCursor>
#include <QTabWidget>
#include <QTabBar>
#include <QVBoxLayout>
#include <QAbstractItemView>

#include <exception>

using trunkmonkey::CallDirection;
using trunkmonkey::SipProfile;

namespace {
QString q(const std::string &v) { return QString::fromStdString(v); }
std::string s(const QString &v) { return v.toStdString(); }
QString yesNo(bool v) { return v ? QStringLiteral("yes") : QStringLiteral("no"); }
double rxLossPercent(const trunkmonkey::CallSnapshot &c)
{
    const double total = static_cast<double>(c.rtpRxPackets + c.rtpRxLoss);
    return total > 0.0 ? (100.0 * static_cast<double>(c.rtpRxLoss) / total) : 0.0;
}
QString qualityLabel(const trunkmonkey::CallSnapshot &c)
{
    if (c.estimatedMos <= 0.0) return QStringLiteral("UNKNOWN");
    if (c.estimatedMos >= 4.0) return QStringLiteral("EXCELLENT");
    if (c.estimatedMos >= 3.6) return QStringLiteral("GOOD");
    if (c.estimatedMos >= 3.1) return QStringLiteral("FAIR");
    return QStringLiteral("POOR");
}
}

SoftphoneWindow::SoftphoneWindow(SipController *controller, QWidget *parent)
    : QWidget(parent), m_controller(controller)
{
    setWindowTitle(QStringLiteral("%1 %2 — Softphone").arg(appDisplayName(), appVersionString()));
    resize(1260, 800);
    setMinimumSize(1120, 680);
    setAttribute(Qt::WA_QuitOnClose, false);

    setObjectName(QStringLiteral("ModernRoot"));
    auto *outer = new QHBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // 5.0r1: the old 164 px rail clipped SOFTPHONE and several navigation
    // labels on common Linux font/DPI combinations. Give the rail enough room
    // to behave like navigation instead of a row of abbreviated buttons.
    auto *sidebar = new QFrame(this);
    sidebar->setObjectName(QStringLiteral("Sidebar"));
    sidebar->setFixedWidth(220);
    auto *side = new QVBoxLayout(sidebar);
    side->setContentsMargins(16, 18, 16, 14);
    side->setSpacing(8);
    auto *brand = new QLabel(QStringLiteral("SOFTPHONE"), sidebar);
    brand->setObjectName(QStringLiteral("BrandTitle"));
    brand->setMinimumWidth(180);
    auto *edition = new QLabel(QStringLiteral("WAFFLEHOUSE-CLIENT %1").arg(appVersionString().toUpper()), sidebar);
    edition->setObjectName(QStringLiteral("BrandVersion"));
    edition->setWordWrap(true);
    side->addWidget(brand);
    side->addWidget(edition);
    side->addSpacing(12);

    auto makeNav = [sidebar](const QString &text, bool checked = false) {
        auto *button = new QPushButton(text, sidebar);
        button->setProperty("nav", true);
        button->setCheckable(true);
        button->setAutoExclusive(true);
        button->setChecked(checked);
        button->setCursor(Qt::PointingHandCursor);
        button->setMinimumHeight(36);
        return button;
    };

    auto *navPhone = makeNav(QStringLiteral("  Phone"), true);
    auto *navAccounts = makeNav(QStringLiteral("  SIP Accounts"));
    auto *navLog = makeNav(QStringLiteral("  SIP Log"));
    auto *navLadder = makeNav(QStringLiteral("  SIP Ladder"));
    auto *navProfile = makeNav(QStringLiteral("  Account Profile"));
    auto *navActivity = makeNav(QStringLiteral("  Activity"));
    const QList<QPushButton *> navButtons{navPhone, navAccounts, navLog, navLadder, navProfile, navActivity};
    for (auto *button : navButtons) side->addWidget(button);
    side->addStretch(1);
    outer->addWidget(sidebar);

    m_tabs = new QTabWidget(this);
    m_tabs->tabBar()->hide();
    m_tabs->setDocumentMode(true);
    outer->addWidget(m_tabs, 1);

    // ---------------------------------------------------------------------
    // PHONE — dialing and live-call control live together in 5.0r1.
    // ---------------------------------------------------------------------
    auto *phone = new QWidget(m_tabs);
    auto *phoneLayout = new QVBoxLayout(phone);
    phoneLayout->setContentsMargins(16, 16, 16, 16);
    phoneLayout->setSpacing(12);

    auto *phoneTitle = new QLabel(QStringLiteral("Phone"), phone);
    phoneTitle->setObjectName(QStringLiteral("PageTitle"));
    auto *phoneSubtitle = new QLabel(QStringLiteral("Dial, answer, hold, mute, transfer, send DTMF, and inspect live SIP quality from one workspace."), phone);
    phoneSubtitle->setWordWrap(true);
    phoneLayout->addWidget(phoneTitle);
    phoneLayout->addWidget(phoneSubtitle);

    auto *phoneSplit = new QSplitter(Qt::Horizontal, phone);
    phoneSplit->setChildrenCollapsible(false);

    auto *dialBox = new QGroupBox(QStringLiteral("Dialer"), phoneSplit);
    dialBox->setMinimumWidth(405);
    auto *dialGrid = new QGridLayout(dialBox);
    dialGrid->setHorizontalSpacing(10);
    dialGrid->setVerticalSpacing(10);

    m_phoneStatus = new QLabel(QStringLiteral("READY — No active call"), dialBox);
    m_phoneStatus->setObjectName(QStringLiteral("StatusPill"));
    m_phoneStatus->setAlignment(Qt::AlignCenter);
    // Keep the status banner informative without allowing it to consume the dialer.
    m_phoneStatus->setFixedHeight(58);
    dialGrid->addWidget(m_phoneStatus, 0, 0, 1, 4);

    m_phoneAccount = new QComboBox(dialBox);
    m_phoneAccount->setMinimumHeight(34);
    m_phoneAccount->setToolTip(QStringLiteral("SIP account used for new outgoing calls."));
    dialGrid->addWidget(new QLabel(QStringLiteral("SIP Account:"), dialBox), 1, 0);
    dialGrid->addWidget(m_phoneAccount, 1, 1, 1, 3);

    m_destination = new QLineEdit(dialBox);
    m_destination->setPlaceholderText(QStringLiteral("extension, number, user@domain, or sip: URI"));
    m_destination->setAlignment(Qt::AlignCenter);
    m_destination->setMinimumHeight(42);
    QFont dialFont = m_destination->font();
    dialFont.setPointSize(15);
    dialFont.setBold(true);
    m_destination->setFont(dialFont);

    m_runtimeDialPrefix = new QLineEdit(dialBox);
    m_runtimeDialPrefix->setPlaceholderText(QStringLiteral("e.g. 9 or 4071"));
    m_runtimeDialPrefix->setMinimumHeight(34);
    m_runtimeDialPrefix->setMaximumWidth(220);
    m_runtimeDialPrefix->setToolTip(QStringLiteral("Session routing prefix for the selected SIP account. Explicit sip:/sips: URIs and user@domain destinations are never modified."));
    m_callerId = new QLineEdit(dialBox);
    m_callerId->setMinimumHeight(34);
    m_callerId->setPlaceholderText(QStringLiteral("optional caller ID override"));

    dialGrid->addWidget(new QLabel(QStringLiteral("Caller ID:"), dialBox), 2, 0);
    dialGrid->addWidget(m_callerId, 2, 1, 1, 3);

    // Prefix and destination used to share one narrow row. On Linux/macOS this
    // could collapse Destination to only a few characters. Give the actual dial
    // target its own full-width row and keep the optional prefix compact.
    dialGrid->addWidget(new QLabel(QStringLiteral("Prefix:"), dialBox), 3, 0);
    dialGrid->addWidget(m_runtimeDialPrefix, 3, 1, 1, 3, Qt::AlignLeft);
    dialGrid->addWidget(new QLabel(QStringLiteral("Destination:"), dialBox), 4, 0);
    dialGrid->addWidget(m_destination, 4, 1, 1, 3);
    dialGrid->setColumnStretch(1, 1);
    dialGrid->setColumnStretch(2, 1);
    dialGrid->setColumnStretch(3, 2);

    auto *keypadHost = new QWidget(dialBox);
    auto *keypadOuter = new QHBoxLayout(keypadHost);
    keypadOuter->setContentsMargins(0, 8, 0, 2);
    keypadOuter->addStretch(1);
    auto *keypad = new QGridLayout;
    keypad->setHorizontalSpacing(12);
    keypad->setVerticalSpacing(10);
    const QStringList digits{QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3"),
                             QStringLiteral("4"), QStringLiteral("5"), QStringLiteral("6"),
                             QStringLiteral("7"), QStringLiteral("8"), QStringLiteral("9"),
                             QStringLiteral("*"), QStringLiteral("0"), QStringLiteral("#")};
    const QStringList dialLabels{QStringLiteral("1"), QStringLiteral("2\nABC"), QStringLiteral("3\nDEF"),
                                 QStringLiteral("4\nGHI"), QStringLiteral("5\nJKL"), QStringLiteral("6\nMNO"),
                                 QStringLiteral("7\nPQRS"), QStringLiteral("8\nTUV"), QStringLiteral("9\nWXYZ"),
                                 QStringLiteral("*"), QStringLiteral("0\n+"), QStringLiteral("#")};
    // Keep every dial key geometrically identical.  Some Qt platform styles
    // derive different size hints for one-line keys (1, *, #) versus two-line
    // keys (2/ABC, 0/+), which made the keypad look warped on Linux/macOS.
    constexpr int dialKeyWidth = 76;
    constexpr int dialKeyHeight = 58;
    for (int row = 0; row < 4; ++row) keypad->setRowMinimumHeight(row, dialKeyHeight);
    for (int col = 0; col < 3; ++col) {
        keypad->setColumnMinimumWidth(col, dialKeyWidth);
        keypad->setColumnStretch(col, 1);
    }
    for (int i = 0; i < digits.size(); ++i) {
        auto *key = new QPushButton(dialLabels.at(i), keypadHost);
        key->setProperty("dialKey", true);
        key->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        key->setFixedSize(76, 58);
        key->setCursor(Qt::PointingHandCursor);
        QFont keyFont = key->font();
        keyFont.setPointSize(12);
        keyFont.setBold(true);
        key->setFont(keyFont);
        keypad->addWidget(key, i / 3, i % 3, Qt::AlignCenter);
        connect(key, &QPushButton::clicked, this, [this, digit = digits.at(i)] {
            const int liveId = m_controller->foregroundOrOnlyLiveCall();
            if (liveId >= 0) {
                QString error;
                if (!m_controller->sendDtmf(liveId, digit, &error)) {
                    showError(QStringLiteral("DTMF Failed"), error);
                }
            } else {
                m_destination->insert(digit);
            }
        });
    }
    keypadOuter->addLayout(keypad);
    keypadOuter->addStretch(1);
    dialGrid->addWidget(keypadHost, 5, 0, 1, 4);

    auto *utilityRow = new QWidget(dialBox);
    auto *utilityLayout = new QHBoxLayout(utilityRow);
    utilityLayout->setContentsMargins(0, 0, 0, 0);
    utilityLayout->addStretch(1);
    auto *backspace = new QPushButton(QStringLiteral("⌫"), utilityRow);
    auto *clear = new QPushButton(QStringLiteral("Clear"), utilityRow);
    backspace->setProperty("phoneUtility", true);
    clear->setProperty("phoneUtility", true);
    backspace->setFixedSize(112, 36);
    clear->setFixedSize(112, 36);
    utilityLayout->addWidget(backspace);
    utilityLayout->addSpacing(10);
    utilityLayout->addWidget(clear);
    utilityLayout->addStretch(1);
    dialGrid->addWidget(utilityRow, 6, 0, 1, 4);

    auto *actionRow = new QWidget(dialBox);
    auto *actionLayout = new QHBoxLayout(actionRow);
    actionLayout->setContentsMargins(0, 3, 0, 0);
    actionLayout->addStretch(1);
    auto *dialButton = new QPushButton(QStringLiteral("CALL"), actionRow);
    dialButton->setProperty("phoneAction", "call");
    dialButton->setDefault(true);
    dialButton->setFixedSize(132, 42);
    auto *hangupMain = new QPushButton(QStringLiteral("HANG UP"), actionRow);
    hangupMain->setProperty("phoneAction", "hangup");
    hangupMain->setFixedSize(132, 42);
    actionLayout->addWidget(dialButton);
    actionLayout->addSpacing(12);
    actionLayout->addWidget(hangupMain);
    actionLayout->addStretch(1);
    dialGrid->addWidget(actionRow, 7, 0, 1, 4);

    connect(backspace, &QPushButton::clicked, m_destination, &QLineEdit::backspace);
    connect(clear, &QPushButton::clicked, m_destination, &QLineEdit::clear);
    connect(hangupMain, &QPushButton::clicked, this, [this] {
        const int liveId = m_controller->foregroundOrOnlyLiveCall();
        if (liveId < 0) return;
        QString error;
        if (!m_controller->hangup(liveId, &error)) {
            showError(QStringLiteral("Hangup Failed"), error);
        }
    });

    // Active call pane: intentionally compact enough for Linux desktops and
    // complete enough that Hold/Mute/Transfer are never hidden off the right.
    auto *activeBox = new QGroupBox(QStringLiteral("Active Calls & Controls"), phoneSplit);
    activeBox->setMinimumWidth(440);
    auto *activeLayout = new QVBoxLayout(activeBox);
    m_calls = new QTableWidget(0, 14, activeBox);
    m_calls->setHorizontalHeaderLabels({QStringLiteral("ID"), QStringLiteral("Account"), QStringLiteral("Dir"), QStringLiteral("Remote"),
                                        QStringLiteral("State"), QStringLiteral("Codec"), QStringLiteral("Media"),
                                        QStringLiteral("Muted"), QStringLiteral("Foreground"), QStringLiteral("MOS"),
                                        QStringLiteral("Loss %"), QStringLiteral("Jitter ms"), QStringLiteral("RTT ms"),
                                        QStringLiteral("Quality")});
    m_calls->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_calls->setSelectionMode(QAbstractItemView::SingleSelection);
    m_calls->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_calls->horizontalHeader()->setStretchLastSection(true);
    m_calls->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    activeLayout->addWidget(m_calls, 1);

    auto *callButtons = new QGridLayout;
    callButtons->setHorizontalSpacing(6);
    callButtons->setVerticalSpacing(6);
    auto *answer = new QPushButton(QStringLiteral("Answer"), activeBox);
    auto *reject = new QPushButton(QStringLiteral("Reject"), activeBox);
    auto *hangup = new QPushButton(QStringLiteral("Hang Up"), activeBox);
    auto *hold = new QPushButton(QStringLiteral("Hold"), activeBox);
    auto *resume = new QPushButton(QStringLiteral("Resume"), activeBox);
    auto *mute = new QPushButton(QStringLiteral("Mute / Unmute"), activeBox);
    // Compact labels keep the full two-row call-control bank readable without
    // making the active-call pane unnecessarily tall. Full action names remain
    // available as tooltips.
    auto *blindTransfer = new QPushButton(QStringLiteral("Blind Xfer…"), activeBox);
    blindTransfer->setToolTip(QStringLiteral("Blind Transfer…"));
    auto *attendedTransfer = new QPushButton(QStringLiteral("Attended Xfer…"), activeBox);
    attendedTransfer->setToolTip(QStringLiteral("Attended Transfer…"));
    auto *diagnostics = new QPushButton(QStringLiteral("Diagnostics"), activeBox);
    m_dtmf = new QLineEdit(activeBox);
    m_dtmf->setPlaceholderText(QStringLiteral("DTMF digits"));
    auto *dtmfButton = new QPushButton(QStringLiteral("Send DTMF"), activeBox);

    const QList<QPushButton *> liveCallButtons{answer, reject, hangup, hold, resume, mute,
                                               blindTransfer, attendedTransfer, dtmfButton, diagnostics};
    for (auto *button : liveCallButtons) {
        button->setMinimumHeight(30);
        button->setMaximumHeight(34);
    }

    // Eight primary controls fit in two rows: the common call actions first,
    // then resume/mute/transfers.  DTMF and diagnostics stay below as utility
    // rows. This matches the more compact layout requested for 5.1r3.
    callButtons->addWidget(answer, 0, 0);
    callButtons->addWidget(reject, 0, 1);
    callButtons->addWidget(hangup, 0, 2);
    callButtons->addWidget(hold, 0, 3);
    callButtons->addWidget(resume, 1, 0);
    callButtons->addWidget(mute, 1, 1);
    callButtons->addWidget(blindTransfer, 1, 2);
    callButtons->addWidget(attendedTransfer, 1, 3);
    callButtons->addWidget(m_dtmf, 2, 0, 1, 3);
    callButtons->addWidget(dtmfButton, 2, 3);
    callButtons->addWidget(diagnostics, 3, 0, 1, 4);
    for (int col = 0; col < 4; ++col) callButtons->setColumnStretch(col, 1);
    activeLayout->addLayout(callButtons);

    phoneSplit->addWidget(dialBox);
    phoneSplit->addWidget(activeBox);
    phoneSplit->setStretchFactor(0, 0);
    phoneSplit->setStretchFactor(1, 1);
    phoneSplit->setSizes({455, 585});
    phoneLayout->addWidget(phoneSplit, 1);
    m_tabs->addTab(phone, QStringLiteral("Phone"));

    // ---------------------------------------------------------------------
    // SIP ACCOUNTS — registration and audio status no longer consume dialer
    // space. This is now a first-class left-side destination.
    // ---------------------------------------------------------------------
    auto *accountsTab = new QWidget(m_tabs);
    auto *accountsLayout = new QVBoxLayout(accountsTab);
    accountsLayout->setContentsMargins(18, 18, 18, 18);
    auto *accountsTitle = new QLabel(QStringLiteral("SIP Accounts"), accountsTab);
    accountsTitle->setObjectName(QStringLiteral("PageTitle"));
    auto *accountsSubtitle = new QLabel(QStringLiteral("Select an account, control registration, and review the active audio path. Saved credentials and advanced account fields remain under Account Profile."), accountsTab);
    accountsSubtitle->setWordWrap(true);
    accountsLayout->addWidget(accountsTitle);
    accountsLayout->addWidget(accountsSubtitle);

    auto *stateBox = new QGroupBox(QStringLiteral("SIP Account"), accountsTab);
    auto *stateGrid = new QGridLayout(stateBox);
    m_account = new QComboBox(stateBox);
    m_registration = new QLabel(stateBox);
    m_registration->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_audio = new QLabel(stateBox);
    m_audio->setWordWrap(true);
    m_audio->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_startStop = new QPushButton(stateBox);
    m_autoAudio = new QCheckBox(QStringLiteral("Automatically follow headset / system audio changes"), stateBox);
    stateGrid->addWidget(new QLabel(QStringLiteral("Account:"), stateBox), 0, 0);
    stateGrid->addWidget(m_account, 0, 1);
    stateGrid->addWidget(m_startStop, 0, 2);
    stateGrid->addWidget(new QLabel(QStringLiteral("Registration:"), stateBox), 1, 0);
    stateGrid->addWidget(m_registration, 1, 1, 1, 2);
    stateGrid->addWidget(new QLabel(QStringLiteral("Audio:"), stateBox), 2, 0);
    stateGrid->addWidget(m_audio, 2, 1, 1, 2);
    stateGrid->addWidget(m_autoAudio, 3, 0, 1, 3);
    accountsLayout->addWidget(stateBox);
    accountsLayout->addStretch(1);
    m_tabs->addTab(accountsTab, QStringLiteral("SIP Accounts"));

    // SIP Log
    auto *logTab = new QWidget(m_tabs);
    auto *logLayout = new QVBoxLayout(logTab);
    auto *logTop = new QHBoxLayout;
    m_logCall = new QComboBox(logTab);
    auto *logRefresh = new QPushButton(QStringLiteral("Refresh"), logTab);
    logTop->addWidget(new QLabel(QStringLiteral("Call:"), logTab));
    logTop->addWidget(m_logCall, 1);
    logTop->addWidget(logRefresh);
    logLayout->addLayout(logTop);
    m_sipLog = new QPlainTextEdit(logTab);
    m_sipLog->setReadOnly(true);
    m_sipLog->setLineWrapMode(QPlainTextEdit::NoWrap);
    logLayout->addWidget(m_sipLog, 1);
    m_tabs->addTab(logTab, QStringLiteral("SIP Log"));

    // SIP Ladder
    auto *ladderTab = new QWidget(m_tabs);
    auto *ladderLayout = new QVBoxLayout(ladderTab);
    auto *ladderTop = new QHBoxLayout;
    m_ladderCall = new QComboBox(ladderTab);
    auto *ladderRefresh = new QPushButton(QStringLiteral("Refresh"), ladderTab);
    ladderTop->addWidget(new QLabel(QStringLiteral("Call:"), ladderTab));
    ladderTop->addWidget(m_ladderCall, 1);
    ladderTop->addWidget(ladderRefresh);
    ladderLayout->addLayout(ladderTop);
    m_ladder = new QPlainTextEdit(ladderTab);
    m_ladder->setReadOnly(true);
    m_ladder->setLineWrapMode(QPlainTextEdit::NoWrap);
    ladderLayout->addWidget(m_ladder, 1);
    m_tabs->addTab(ladderTab, QStringLiteral("SIP Ladder"));

    // Account Profile — edits the same saved SIP account that /add created.
    auto *profileTab = new QWidget(m_tabs);
    auto *profileLayout = new QVBoxLayout(profileTab);
    auto *form = new QFormLayout;
    m_profileAccount = new QComboBox(profileTab);
    m_profileName = new QLineEdit(profileTab);
    m_domain = new QLineEdit(profileTab);
    m_registrar = new QLineEdit(profileTab);
    m_registrar->setPlaceholderText(QStringLiteral("blank = sip:<SIP domain>"));
    m_username = new QLineEdit(profileTab);
    m_authUsername = new QLineEdit(profileTab);
    m_authUsername->setPlaceholderText(QStringLiteral("blank = username"));
    m_password = new QLineEdit(profileTab);
    m_password->setEchoMode(QLineEdit::Password);
    m_displayName = new QLineEdit(profileTab);
    m_outboundProxy = new QLineEdit(profileTab);
    m_callerIdDomain = new QLineEdit(profileTab);
    m_dialPrefix = new QLineEdit(profileTab);
    m_stunServer = new QLineEdit(profileTab);
    m_transport = new QComboBox(profileTab);
    m_transport->addItems({QStringLiteral("udp"), QStringLiteral("tcp"), QStringLiteral("tls")});
    m_identityMode = new QComboBox(profileTab);
    m_identityMode->addItems({QStringLiteral("from"), QStringLiteral("pai"), QStringLiteral("rpid"), QStringLiteral("from+pai")});
    m_compatibility = new QComboBox(profileTab);
    m_compatibility->addItem(QStringLiteral("Auto / generic SIP (recommended)"), QStringLiteral("auto"));
    m_compatibility->addItem(QStringLiteral("Asterisk PJSIP (chan_pjsip)"), QStringLiteral("standard"));
    m_compatibility->addItem(QStringLiteral("Asterisk legacy SIP (chan_sip)"), QStringLiteral("asterisk-chan_sip"));
    m_compatibility->setToolTip(QStringLiteral("Choose the SIP driver used by the remote Asterisk server. WaffleHouse-Client uses the inherited PJSIP 2.17 engine internally."));
    m_localPort = new QSpinBox(profileTab);
    m_localPort->setRange(1, 65535);
    m_regExpires = new QSpinBox(profileTab);
    m_regExpires->setRange(30, 86400);
    m_useIce = new QCheckBox(QStringLiteral("Enable ICE"), profileTab);
    m_enableSrtp = new QCheckBox(QStringLiteral("Enable SRTP"), profileTab);
    m_savePassword = new QCheckBox(QStringLiteral("Save SIP password with this WaffleHouse-Client account"), profileTab);
    form->addRow(QStringLiteral("Account:"), m_profileAccount);
    form->addRow(QStringLiteral("Account label:"), m_profileName);
    form->addRow(QStringLiteral("SIP domain:"), m_domain);
    form->addRow(QStringLiteral("Registrar:"), m_registrar);
    form->addRow(QStringLiteral("Username:"), m_username);
    form->addRow(QStringLiteral("Auth username:"), m_authUsername);
    form->addRow(QStringLiteral("Password:"), m_password);
    form->addRow(QStringLiteral("Display name:"), m_displayName);
    form->addRow(QStringLiteral("Outbound proxy:"), m_outboundProxy);
    form->addRow(QStringLiteral("Caller-ID domain:"), m_callerIdDomain);
    form->addRow(QStringLiteral("Startup dial prefix:"), m_dialPrefix);
    form->addRow(QStringLiteral("STUN server:"), m_stunServer);
    form->addRow(QStringLiteral("Transport:"), m_transport);
    form->addRow(QStringLiteral("Identity mode:"), m_identityMode);
    form->addRow(QStringLiteral("SIP server type:"), m_compatibility);
    auto *compatibilityHelp = new QLabel(QStringLiteral("Asterisk pjsip.conf / chan_pjsip = PJSIP. Asterisk sip.conf / chan_sip = legacy chan_sip. Use Auto for generic SIP servers or when unsure."), profileTab);
    compatibilityHelp->setWordWrap(true);
    form->addRow(QString(), compatibilityHelp);
    form->addRow(QStringLiteral("Local SIP port:"), m_localPort);
    form->addRow(QStringLiteral("Registration expires:"), m_regExpires);
    form->addRow(QString(), m_useIce);
    form->addRow(QString(), m_enableSrtp);
    form->addRow(QString(), m_savePassword);
    profileLayout->addLayout(form);
    auto *profileButtons = new QHBoxLayout;
    auto *save = new QPushButton(QStringLiteral("Save Account"), profileTab);
    auto *saveRestart = new QPushButton(QStringLiteral("Save + Re-register"), profileTab);
    profileButtons->addStretch(1);
    profileButtons->addWidget(save);
    profileButtons->addWidget(saveRestart);
    profileLayout->addLayout(profileButtons);
    m_tabs->addTab(profileTab, QStringLiteral("Account Profile"));

    // Activity
    auto *activityTab = new QWidget(m_tabs);
    auto *activityLayout = new QVBoxLayout(activityTab);
    m_activity = new QPlainTextEdit(activityTab);
    m_activity->setReadOnly(true);
    activityLayout->addWidget(m_activity);
    m_tabs->addTab(activityTab, QStringLiteral("Activity"));

    for (int i = 0; i < navButtons.size(); ++i) {
        connect(navButtons.at(i), &QPushButton::clicked, this, [this, i] {
            m_tabs->setCurrentIndex(i);
        });
    }
    connect(m_tabs, &QTabWidget::currentChanged, this, [navButtons](int index) {
        if (index >= 0 && index < navButtons.size()) navButtons.at(index)->setChecked(true);
    });

    connect(dialButton, &QPushButton::clicked, this, &SoftphoneWindow::dial);
    connect(m_destination, &QLineEdit::returnPressed, this, &SoftphoneWindow::dial);
    connect(m_phoneAccount, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SoftphoneWindow::accountSelectionChanged);
    connect(m_account, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SoftphoneWindow::accountSelectionChanged);
    connect(m_profileAccount, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SoftphoneWindow::accountSelectionChanged);
    connect(m_startStop, &QPushButton::clicked, this, [this] {
        const QString id = selectedAccountId();
        if (id.isEmpty()) return;
        QString error;
        if (m_controller->accountRegistrationEnabled(id)) {
            if (!m_controller->disconnectAccount(id, &error)) {
                showError(QStringLiteral("SIP Disconnect Failed"), error);
            }
        } else if (!m_controller->connectAccount(id, &error)) {
            showError(QStringLiteral("SIP Registration Failed"), error);
        }
    });
    connect(m_autoAudio, &QCheckBox::toggled, m_controller, &SipController::setAudioAutoSwitch);

    connect(answer, &QPushButton::clicked, this, &SoftphoneWindow::answerSelected);
    connect(reject, &QPushButton::clicked, this, &SoftphoneWindow::rejectSelected);
    connect(hangup, &QPushButton::clicked, this, &SoftphoneWindow::hangupSelected);
    connect(hold, &QPushButton::clicked, this, &SoftphoneWindow::holdSelected);
    connect(resume, &QPushButton::clicked, this, &SoftphoneWindow::resumeSelected);
    connect(mute, &QPushButton::clicked, this, &SoftphoneWindow::muteSelected);
    connect(dtmfButton, &QPushButton::clicked, this, &SoftphoneWindow::sendDtmf);

    connect(diagnostics, &QPushButton::clicked, this, [this] {
        const int id = selectedCallId();
        if (id < 0) return;
        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("SIP Call Diagnostics — #%1").arg(id));
        dialog.resize(700, 520);
        auto *layout = new QVBoxLayout(&dialog);
        auto *view = new QPlainTextEdit(&dialog);
        view->setReadOnly(true);
        view->setLineWrapMode(QPlainTextEdit::NoWrap);
        view->setPlainText(m_controller->callDiagnosticsText(id));
        layout->addWidget(view, 1);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        dialog.exec();
    });

    connect(blindTransfer, &QPushButton::clicked, this, [this] {
        const int id = selectedCallId();
        if (id < 0) return;
        bool ok = false;
        const QString destination = QInputDialog::getText(this, QStringLiteral("Blind Transfer"),
                                                          QStringLiteral("Transfer call to:"), QLineEdit::Normal,
                                                          {}, &ok).trimmed();
        if (!ok || destination.isEmpty()) return;
        QString error;
        if (!m_controller->blindTransfer(id, destination, &error)) {
            showError(QStringLiteral("Transfer Failed"), error);
        }
    });

    connect(attendedTransfer, &QPushButton::clicked, this, [this] {
        const int id = selectedCallId();
        if (id < 0) return;
        QStringList choices;
        QHash<QString, int> ids;
        for (const auto &c : m_controller->calls()) {
            if (c.disconnected || c.id == id) continue;
            const QString label = QStringLiteral("#%1 — %2 — %3").arg(c.id).arg(q(c.remoteUri), q(c.state));
            choices << label;
            ids.insert(label, c.id);
        }
        if (choices.isEmpty()) {
            showError(QStringLiteral("Attended Transfer"),
                      QStringLiteral("Start or answer a consultation call first; there is no second live call to replace this call with."));
            return;
        }
        bool ok = false;
        const QString choice = QInputDialog::getItem(this, QStringLiteral("Attended Transfer"),
                                                     QStringLiteral("Consultation call:"), choices, 0, false, &ok);
        if (!ok || choice.isEmpty()) return;
        QString error;
        if (!m_controller->attendedTransfer(id, ids.value(choice, -1), &error)) {
            showError(QStringLiteral("Attended Transfer Failed"), error);
        }
    });

    connect(logRefresh, &QPushButton::clicked, this, &SoftphoneWindow::refreshSipLog);
    connect(m_logCall, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SoftphoneWindow::refreshSipLog);
    connect(ladderRefresh, &QPushButton::clicked, this, &SoftphoneWindow::refreshLadder);
    connect(m_ladderCall, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SoftphoneWindow::refreshLadder);
    connect(save, &QPushButton::clicked, this, [this] { saveProfile(false); });
    connect(saveRestart, &QPushButton::clicked, this, [this] { saveProfile(true); });

    connect(m_controller, &SipController::stateChanged, this, &SoftphoneWindow::refreshState);
    connect(m_controller, &SipController::accountsChanged, this, &SoftphoneWindow::refreshAccounts);
    connect(m_controller, &SipController::accountStateChanged, this, [this](const QString &) {
        refreshAccounts();
        refreshState();
    });
    connect(m_controller, &SipController::callsChanged, this, &SoftphoneWindow::refreshCalls);
    connect(m_controller, &SipController::sipLogChanged, this, &SoftphoneWindow::refreshSipLog);
    connect(m_controller, &SipController::activityChanged, this, &SoftphoneWindow::refreshActivity);
    connect(m_controller, &SipController::incomingCall, this, &SoftphoneWindow::incomingCall);
    connect(m_runtimeDialPrefix, &QLineEdit::editingFinished, this, [this] {
        const QString id = selectedAccountId();
        if (id.isEmpty()) return;
        QString error;
        if (!m_controller->setDialPrefix(id, m_runtimeDialPrefix->text().trimmed(), &error)) {
            showError(QStringLiteral("Dial Prefix"), error);
            const QSignalBlocker b(m_runtimeDialPrefix);
            m_runtimeDialPrefix->setText(m_controller->dialPrefix(id));
        }
    });

    refreshAccounts();
    refreshState();
    refreshCalls();
    refreshActivity();
}

void SoftphoneWindow::showAndRaise(){show();raise();activateWindow();refreshAccounts();refreshState();refreshCalls();}

QString SoftphoneWindow::selectedAccountId() const
{
    if (m_phoneAccount && m_phoneAccount->currentIndex() >= 0) return m_phoneAccount->currentData().toString();
    if (m_account && m_account->currentIndex() >= 0) return m_account->currentData().toString();
    return m_controller->selectedAccountId();
}

void SoftphoneWindow::refreshAccounts()
{
    const QString wanted = m_controller->selectedAccountId().isEmpty() ? selectedAccountId() : m_controller->selectedAccountId();
    const auto list = m_controller->accounts();
    {
        const QSignalBlocker b(m_phoneAccount);
        m_phoneAccount->clear();
        for (const auto &a : list) {
            m_phoneAccount->addItem(QStringLiteral("%1 — %2").arg(a.name.isEmpty() ? a.identity : a.name, a.registrationText), a.id);
        }
        int idx = m_phoneAccount->findData(wanted);
        if (idx < 0 && m_phoneAccount->count()) idx = 0;
        if (idx >= 0) m_phoneAccount->setCurrentIndex(idx);
    }
    {
        const QSignalBlocker b(m_account); m_account->clear();
        for (const auto &a : list) m_account->addItem(QStringLiteral("%1 — %2 — %3").arg(a.name.isEmpty()?a.identity:a.name, a.identity, a.registrationText), a.id);
        int idx=m_account->findData(wanted); if(idx<0&&m_account->count())idx=0; if(idx>=0)m_account->setCurrentIndex(idx);
    }
    {
        const QSignalBlocker b(m_profileAccount); m_profileAccount->clear();
        for (const auto &a : list) m_profileAccount->addItem(QStringLiteral("%1 — %2").arg(a.name.isEmpty()?a.identity:a.name, a.identity), a.id);
        int idx=m_profileAccount->findData(wanted); if(idx<0&&m_profileAccount->count())idx=0; if(idx>=0)m_profileAccount->setCurrentIndex(idx);
    }
    if (m_phoneAccount->currentIndex() >= 0) m_controller->setSelectedAccountId(m_phoneAccount->currentData().toString());
    const bool have=m_phoneAccount->count()>0; m_destination->setEnabled(have); m_runtimeDialPrefix->setEnabled(have); m_callerId->setEnabled(have); m_startStop->setEnabled(have);
    loadProfileFields();
}

void SoftphoneWindow::accountSelectionChanged()
{
    QObject *senderObject = sender();
    QComboBox *source = m_phoneAccount;
    if (senderObject == m_profileAccount) source = m_profileAccount;
    else if (senderObject == m_account) source = m_account;
    const QString id = source && source->currentIndex() >= 0 ? source->currentData().toString() : QString();
    if (id.isEmpty()) return;
    m_controller->setSelectedAccountId(id);
    {
        const QSignalBlocker b(m_phoneAccount); int idx=m_phoneAccount->findData(id); if(idx>=0)m_phoneAccount->setCurrentIndex(idx);
    }
    {
        const QSignalBlocker b(m_account); int idx=m_account->findData(id); if(idx>=0)m_account->setCurrentIndex(idx);
    }
    {
        const QSignalBlocker b(m_profileAccount); int idx=m_profileAccount->findData(id); if(idx>=0)m_profileAccount->setCurrentIndex(idx);
    }
    loadProfileFields(); refreshState();
}

void SoftphoneWindow::refreshState()
{
    const QString id=selectedAccountId();
    m_registration->setText(id.isEmpty()?QStringLiteral("No SIP account configured"):m_controller->registrationText(id));
    m_audio->setText(m_controller->audioSummary());
    m_startStop->setText(!id.isEmpty() && m_controller->accountRegistrationEnabled(id) ? QStringLiteral("Disconnect Account") : QStringLiteral("Connect / Register"));
    const bool autoAudio=m_controller->audioAutoSwitch(); if(m_autoAudio->isChecked()!=autoAudio){const QSignalBlocker b(m_autoAudio);m_autoAudio->setChecked(autoAudio);}
    {
        const QSignalBlocker b(m_runtimeDialPrefix);
        m_runtimeDialPrefix->setText(id.isEmpty()?QString():m_controller->dialPrefix(id));
    }
}

void SoftphoneWindow::refreshCalls()
{
    const int previous=selectedCallId();const auto calls=m_controller->calls();m_calls->setRowCount(static_cast<int>(calls.size()));int selectedRow=-1;
    for(int row=0;row<static_cast<int>(calls.size());++row){const auto&c=calls.at(static_cast<std::size_t>(row));const QStringList values={QString::number(c.id),q(c.accountName).isEmpty()?q(c.accountId):q(c.accountName),c.direction==CallDirection::Incoming?QStringLiteral("IN"):QStringLiteral("OUT"),q(c.remoteUri),q(c.state),q(c.codecName),yesNo(c.mediaActive),yesNo(c.microphoneMuted),yesNo(c.foreground),c.estimatedMos>0.0?QString::number(c.estimatedMos,'f',2):QStringLiteral("—"),QString::number(rxLossPercent(c),'f',2),QString::number(c.rxJitterMs,'f',1),QString::number(c.rttMs,'f',1),qualityLabel(c)};for(int col=0;col<values.size();++col)m_calls->setItem(row,col,new QTableWidgetItem(values.at(col)));if(c.id==previous)selectedRow=row;}
    if(selectedRow>=0)m_calls->selectRow(selectedRow);else if(!calls.empty())m_calls->selectRow(static_cast<int>(calls.size())-1);
    if (m_phoneStatus) {
        const trunkmonkey::CallSnapshot *live = nullptr;
        for (const auto &call : calls) {
            if (!call.disconnected) { live = &call; if (call.foreground) break; }
        }
        m_phoneStatus->setText(live
            ? QStringLiteral("CALL #%1  •  %2  •  %3  •  %4")
                  .arg(live->id).arg(q(live->remoteUri), q(live->state),
                                     q(live->codecName).isEmpty() ? QStringLiteral("media pending") : q(live->codecName))
            : QStringLiteral("READY — No active call"));
    }
    populateCallCombos();refreshLadder();refreshSipLog();
}

void SoftphoneWindow::populateCallCombos(){const int oldLog=comboCallId(m_logCall),oldLadder=comboCallId(m_ladderCall);const auto calls=m_controller->calls();{const QSignalBlocker b(m_logCall);m_logCall->clear();m_logCall->addItem(QStringLiteral("All calls"),-1);for(const auto&c:calls)m_logCall->addItem(QStringLiteral("%1 — %2 — %3 — %4").arg(c.id).arg(q(c.accountName),q(c.remoteUri),q(c.state)),c.id);int idx=m_logCall->findData(oldLog);m_logCall->setCurrentIndex(idx>=0?idx:0);}{const QSignalBlocker b(m_ladderCall);m_ladderCall->clear();for(const auto&c:calls)m_ladderCall->addItem(QStringLiteral("%1 — %2 — %3 — %4").arg(c.id).arg(q(c.accountName),q(c.remoteUri),q(c.state)),c.id);int idx=m_ladderCall->findData(oldLadder);if(idx<0&&m_ladderCall->count())idx=m_ladderCall->count()-1;if(idx>=0)m_ladderCall->setCurrentIndex(idx);}}
void SoftphoneWindow::refreshSipLog(){m_sipLog->setPlainText(m_controller->sipLogText(comboCallId(m_logCall)));auto c=m_sipLog->textCursor();c.movePosition(QTextCursor::End);m_sipLog->setTextCursor(c);}
void SoftphoneWindow::refreshLadder(){m_ladder->setPlainText(m_controller->ladderText(comboCallId(m_ladderCall)));}
void SoftphoneWindow::refreshActivity(){m_activity->setPlainText(m_controller->activityText());auto c=m_activity->textCursor();c.movePosition(QTextCursor::End);m_activity->setTextCursor(c);}

void SoftphoneWindow::loadProfileFields()
{
    bool ok=false;const auto p=m_controller->accountProfile(selectedAccountId(),&ok);const QList<QWidget*> fields={m_profileName,m_domain,m_registrar,m_username,m_authUsername,m_password,m_displayName,m_outboundProxy,m_callerIdDomain,m_dialPrefix,m_stunServer,m_transport,m_identityMode,m_compatibility,m_localPort,m_regExpires,m_useIce,m_enableSrtp,m_savePassword};for(QWidget*w:fields)w->setEnabled(ok);if(!ok){m_profileName->clear();m_domain->clear();m_registrar->clear();m_username->clear();m_authUsername->clear();m_password->clear();m_displayName->clear();m_outboundProxy->clear();m_callerIdDomain->clear();m_dialPrefix->clear();m_stunServer->clear();return;}
    m_profileName->setText(q(p.name));m_domain->setText(q(p.sipDomain));m_registrar->setText(q(p.registrar));m_username->setText(q(p.username));m_authUsername->setText(q(p.authUsername));m_password->setText(q(p.password));m_displayName->setText(q(p.displayName));m_outboundProxy->setText(q(p.outboundProxy));m_callerIdDomain->setText(q(p.callerIdDomain));m_dialPrefix->setText(q(p.dialPrefix));m_stunServer->setText(q(p.stunServer));m_transport->setCurrentText(q(trunkmonkey::toString(p.transport)));m_identityMode->setCurrentText(q(trunkmonkey::toString(p.identityMode)));{const int compatIndex=m_compatibility->findData(q(trunkmonkey::toString(p.compatibility)));m_compatibility->setCurrentIndex(compatIndex>=0?compatIndex:0);}m_localPort->setValue(p.localSipPort?p.localSipPort:5060);m_regExpires->setValue(p.registrationExpires?static_cast<int>(p.registrationExpires):300);m_useIce->setChecked(p.useIce);m_enableSrtp->setChecked(p.enableSrtp);m_savePassword->setChecked(!p.password.empty());
}

void SoftphoneWindow::saveProfile(bool reregister)
{
    const QString accountId=selectedAccountId();if(accountId.isEmpty())return;
    for(const auto &call:m_controller->calls()){
        if(!call.disconnected&&q(call.accountId)==accountId){
            showError(QStringLiteral("SIP Account In Use"),QStringLiteral("Hang up active calls on this SIP account before editing its profile."));
            return;
        }
    }
    SipProfile p;p.name=s(m_profileName->text().trimmed());p.sipDomain=s(m_domain->text().trimmed());p.registrar=s(m_registrar->text().trimmed());p.username=s(m_username->text().trimmed());p.authUsername=s(m_authUsername->text().trimmed());p.password=s(m_password->text());p.displayName=s(m_displayName->text().trimmed());p.outboundProxy=s(m_outboundProxy->text().trimmed());p.callerIdDomain=s(m_callerIdDomain->text().trimmed());p.dialPrefix=s(m_dialPrefix->text().trimmed());p.stunServer=s(m_stunServer->text().trimmed());
    try{p.transport=trunkmonkey::transportFromString(s(m_transport->currentText()));p.identityMode=trunkmonkey::identityModeFromString(s(m_identityMode->currentText()));p.compatibility=trunkmonkey::sipCompatibilityFromString(s(m_compatibility->currentData().toString()));p.localSipPort=static_cast<std::uint16_t>(m_localPort->value());p.registrationExpires=static_cast<unsigned>(m_regExpires->value());p.useIce=m_useIce->isChecked();p.enableSrtp=m_enableSrtp->isChecked();trunkmonkey::ProfileStore::validate(p);}catch(const std::exception&e){showError(QStringLiteral("Invalid SIP Account"),QString::fromLocal8Bit(e.what()));return;}
    const bool wasEnabled=m_controller->accountRegistrationEnabled(accountId);QString error;emit profileSaveRequested(accountId,p,m_savePassword->isChecked());bool ok=false;(void)m_controller->accountProfile(accountId,&ok);if(!ok){showError(QStringLiteral("SIP Account Save Failed"),QStringLiteral("The selected WaffleHouse-Client SIP account could not be updated."));return;}if(reregister&&wasEnabled){m_controller->disconnectAccount(accountId,nullptr);if(!m_controller->connectAccount(accountId,&error))showError(QStringLiteral("SIP Re-registration Failed"),error);}refreshAccounts();refreshState();
}

void SoftphoneWindow::dial(){const QString id=selectedAccountId();if(id.isEmpty()||m_destination->text().trimmed().isEmpty())return;QString error;if(m_controller->dial(id,m_destination->text(),m_callerId->text(),&error)<0){showError(QStringLiteral("Call Failed"),error);return;}refreshCalls();}
int SoftphoneWindow::selectedCallId()const{const auto items=m_calls->selectedItems();if(items.isEmpty())return-1;bool ok=false;const int id=m_calls->item(items.first()->row(),0)->text().toInt(&ok);return ok?id:-1;}
int SoftphoneWindow::comboCallId(QComboBox*combo)const{return(!combo||combo->currentIndex()<0)?-1:combo->currentData().toInt();}
#define CALL_ACTION(method,title) do{const int id=selectedCallId();if(id<0)return;QString error;if(!m_controller->method(id,&error))showError(QStringLiteral(title),error);}while(0)
void SoftphoneWindow::answerSelected(){CALL_ACTION(answer,"Answer Failed");}void SoftphoneWindow::rejectSelected(){CALL_ACTION(reject,"Reject Failed");}void SoftphoneWindow::hangupSelected(){CALL_ACTION(hangup,"Hangup Failed");}void SoftphoneWindow::holdSelected(){CALL_ACTION(hold,"Hold Failed");}void SoftphoneWindow::resumeSelected(){CALL_ACTION(resume,"Resume Failed");}
#undef CALL_ACTION
void SoftphoneWindow::muteSelected(){const int id=selectedCallId();if(id<0)return;bool ok=false;const auto c=m_controller->call(id,&ok);if(!ok)return;QString error;if(!m_controller->setMuted(id,!c.microphoneMuted,&error))showError(QStringLiteral("Mute Failed"),error);}
void SoftphoneWindow::sendDtmf(){const int id=selectedCallId();if(id<0||m_dtmf->text().trimmed().isEmpty())return;QString error;if(!m_controller->sendDtmf(id,m_dtmf->text().trimmed(),&error))showError(QStringLiteral("DTMF Failed"),error);else m_dtmf->clear();}

void SoftphoneWindow::incomingCall(const QString &accountId,int id,const QString &remoteUri)
{
    m_controller->setSelectedAccountId(accountId);refreshAccounts();showAndRaise();m_tabs->setCurrentIndex(0);for(int row=0;row<m_calls->rowCount();++row)if(m_calls->item(row,0)&&m_calls->item(row,0)->text().toInt()==id){m_calls->selectRow(row);break;}QMessageBox box(this);box.setWindowTitle(QStringLiteral("Incoming SIP Call — %1").arg(appDisplayName()));box.setText(QStringLiteral("Account: %1\nIncoming call from:\n%2").arg(m_phoneAccount ? m_phoneAccount->currentText() : m_account->currentText(),remoteUri));auto*answer=box.addButton(QStringLiteral("Answer"),QMessageBox::AcceptRole);auto*reject=box.addButton(QStringLiteral("Reject"),QMessageBox::DestructiveRole);box.addButton(QStringLiteral("Ignore"),QMessageBox::RejectRole);box.exec();QString error;if(box.clickedButton()==answer){if(!m_controller->answer(id,&error))showError(QStringLiteral("Answer Failed"),error);}else if(box.clickedButton()==reject){if(!m_controller->reject(id,&error))showError(QStringLiteral("Reject Failed"),error);}
}

void SoftphoneWindow::showError(const QString&title,const QString&message){QMessageBox::warning(this,title,message.isEmpty()?QStringLiteral("Unknown error"):message);}
