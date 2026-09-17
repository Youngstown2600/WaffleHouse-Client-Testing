#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BACKEND="$ROOT/src/backend.h"
MAIN="$ROOT/src/mainwindow.cpp"
CLI="$ROOT/src/terminalui.cpp"
SOFT="$ROOT/src/softphonewindow.cpp"
fail() { echo "FAIL: $*" >&2; exit 1; }

grep -Fq 'QString accountLabel;' "$BACKEND" || fail 'shared accountLabel field missing'
grep -Fq 'm_accountLabelLabel = new QLabel(QStringLiteral("Account label:")' "$MAIN" || fail 'GUI AIM/IRC account label field missing'
grep -Fq 'value.accountLabel = m_accountLabel->text().trimmed();' "$MAIN" || fail 'GUI account label not saved from dialog'
grep -Fq 'settings.value(QStringLiteral("accountLabel"))' "$MAIN" || fail 'GUI account label persistence load missing'
grep -Fq 'settings.setValue(QStringLiteral("accountLabel"), value.accountLabel);' "$MAIN" || fail 'GUI account label persistence save missing'
grep -Fq 'cfg.accountLabel.trimmed()' "$MAIN" || fail 'GUI account label not used in account presentation'
grep -Fq '{QStringLiteral("accountlabel"), QStringLiteral("Account label")' "$CLI" || fail 'CLI account label field missing'
grep -Fq 'settings.accountLabel = field(QStringLiteral("accountlabel")).value.trimmed();' "$CLI" || fail 'CLI account label capture missing'
grep -Fq 'settings.setValue(QStringLiteral("accountLabel"), value.accountLabel);' "$CLI" || fail 'CLI account label persistence missing'

# Dial pad must not use style-dependent size hints.
grep -Fq 'constexpr int dialKeyWidth = 76;' "$SOFT" || fail 'uniform dial key width missing'
grep -Fq 'constexpr int dialKeyHeight = 58;' "$SOFT" || fail 'uniform dial key height missing'
grep -Fq 'key->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);' "$SOFT" || fail 'fixed dial key size policy missing'
grep -Fq 'keypad->setRowMinimumHeight(row, dialKeyHeight);' "$SOFT" || fail 'uniform keypad row geometry missing'
grep -Fq 'keypad->setColumnMinimumWidth(col, dialKeyWidth);' "$SOFT" || fail 'uniform keypad column geometry missing'

# Compact live-call bank: 4 + 4 buttons, then DTMF + diagnostics.
grep -Fq 'callButtons->addWidget(resume, 1, 0);' "$SOFT" || fail 'compact control row missing resume'
grep -Fq 'callButtons->addWidget(mute, 1, 1);' "$SOFT" || fail 'compact control row missing mute'
grep -Fq 'callButtons->addWidget(blindTransfer, 1, 2);' "$SOFT" || fail 'compact control row missing blind transfer'
grep -Fq 'callButtons->addWidget(attendedTransfer, 1, 3);' "$SOFT" || fail 'compact control row missing attended transfer'
grep -Fq 'QStringLiteral("Blind Xfer…")' "$SOFT" || fail 'compact blind-transfer label missing'
grep -Fq 'QStringLiteral("Attended Xfer…")' "$SOFT" || fail 'compact attended-transfer label missing'
grep -Fq 'button->setMaximumHeight(34);' "$SOFT" || fail 'compact control height cap missing'

echo 'WaffleHouse-Client 5.1r3 account-label + compact softphone regression: PASS'
