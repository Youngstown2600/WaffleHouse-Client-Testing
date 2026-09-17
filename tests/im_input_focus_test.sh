#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CPP="$ROOT/src/chatwindow.cpp"
HDR="$ROOT/src/chatwindow.h"

grep -q 'm_transcript->setFocusPolicy(Qt::ClickFocus)' "$CPP"
grep -q 'void ChatWindow::showEvent(QShowEvent \*event)' "$CPP"
grep -q 'QTimer::singleShot(0, this' "$CPP"
grep -q 'm_messageEdit->setFocus(Qt::OtherFocusReason)' "$CPP"
grep -q 'm_initialInputFocusApplied' "$HDR"

echo 'IM composer initial-focus regression: PASS'
