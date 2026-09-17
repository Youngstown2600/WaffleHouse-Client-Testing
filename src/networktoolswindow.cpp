#include "networktoolswindow.h"
#include "buildfeatures.h"
#include "appbranding.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QSslSocket>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTabWidget>
#include <QThread>
#include <QUrl>
#include <QVBoxLayout>

namespace {
#ifdef Q_OS_MACOS
QString shellQuote(QString value)
{
    value.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QLatin1Char('\'') + value + QLatin1Char('\'');
}
#endif

bool socketConnect(QSslSocket &socket, const QString &host, quint16 port, bool tls, QString &error)
{
    if (tls) {
        socket.connectToHostEncrypted(host, port);
        if (!socket.waitForEncrypted(12000)) { error = socket.errorString(); return false; }
    } else {
        socket.connectToHost(host, port);
        if (!socket.waitForConnected(10000)) { error = socket.errorString(); return false; }
    }
    return true;
}


}

NetworkToolsWindow::NetworkToolsWindow(QWidget *parent) : QWidget(parent)
{
    setWindowTitle(QStringLiteral("Network Tools — %1").arg(appDisplayName()));
    resize(850, 620);
    auto *layout = new QVBoxLayout(this);
    auto *intro = new QLabel(QStringLiteral("Optional network tools. Only features compiled into this build appear here."), this);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    m_tabs = new QTabWidget(this);
    layout->addWidget(m_tabs, 1);
    if (BuildFeatures::Gopher || BuildFeatures::Gemini) addBrowserTab();
    if (BuildFeatures::Mosh) addMoshTab();
    if (m_tabs->count() == 0) {
        auto *empty = new QLabel(QStringLiteral("No auxiliary network tools were compiled into this build."), this);
        empty->setAlignment(Qt::AlignCenter);
        layout->addWidget(empty);
    }
}

void NetworkToolsWindow::showAndRaise() { show(); raise(); activateWindow(); }

void NetworkToolsWindow::addBrowserTab()
{
    auto *page = new QWidget(m_tabs);
    auto *v = new QVBoxLayout(page);
    auto *row = new QHBoxLayout;
    auto *url = new QLineEdit(page);
    url->setPlaceholderText(BuildFeatures::Gemini && BuildFeatures::Gopher
                                ? QStringLiteral("gopher://host/1/ or gemini://host/")
                                : BuildFeatures::Gemini ? QStringLiteral("gemini://host/") : QStringLiteral("gopher://host/1/"));
    auto *go = new QPushButton(QStringLiteral("Fetch"), page);
    row->addWidget(url, 1); row->addWidget(go); v->addLayout(row);
    auto *output = new QPlainTextEdit(page); output->setReadOnly(true); v->addWidget(output, 1);
    connect(go, &QPushButton::clicked, page, [url, output, go] {
        const QUrl target = QUrl::fromUserInput(url->text().trimmed());
        if (!target.isValid() || target.host().isEmpty()) { output->setPlainText(QStringLiteral("Invalid URL.")); return; }
        const QString scheme = target.scheme().toCaseFolded();
        if ((scheme == QStringLiteral("gopher") && !BuildFeatures::Gopher) ||
            (scheme == QStringLiteral("gemini") && !BuildFeatures::Gemini) ||
            (scheme != QStringLiteral("gopher") && scheme != QStringLiteral("gemini"))) {
            output->setPlainText(QStringLiteral("That scheme is not enabled in this build.")); return;
        }
        go->setEnabled(false); output->setPlainText(QStringLiteral("Fetching %1 …").arg(target.toString()));
        QThread *worker = QThread::create([target, scheme, output, go] {
            QString result, error;
            if (scheme == QStringLiteral("gopher")) {
                QSslSocket socket;
                const quint16 port = target.port(70);
                if (!socketConnect(socket, target.host(), port, false, error)) result = QStringLiteral("Connection failed: %1").arg(error);
                else {
                    QString selector = QUrl::fromPercentEncoding(target.path(QUrl::FullyEncoded).toUtf8());
                    if (selector.startsWith(QLatin1Char('/'))) selector.remove(0, 1);
                    if (!selector.isEmpty() && QStringLiteral("0123456789+gITdsh;Mcip").contains(selector.front())) selector.remove(0, 1);
                    socket.write(selector.toUtf8() + QByteArray("\r\n")); socket.waitForBytesWritten(3000);
                    QByteArray data;
                    while (socket.waitForReadyRead(1500)) {
                        data += socket.readAll();
                    }
                    data += socket.readAll();
                    result = QString::fromUtf8(data);
                }
            } else {
                QSslSocket socket;
                socket.connectToHostEncrypted(target.host(), target.port(1965));
                if (!socket.waitForEncrypted(12000)) result = QStringLiteral("Gemini TLS failed: %1\nWaffleHouse-Client does not silently ignore certificate errors.").arg(socket.errorString());
                else {
                    socket.write(target.toString(QUrl::FullyEncoded).toUtf8() + QByteArray("\r\n")); socket.waitForBytesWritten(3000);
                    QByteArray data;
                    while (socket.waitForReadyRead(2000)) {
                        data += socket.readAll();
                    }
                    data += socket.readAll();
                    result = QString::fromUtf8(data);
                }
            }
            QMetaObject::invokeMethod(output, [output, go, result] { output->setPlainText(result); go->setEnabled(true); }, Qt::QueuedConnection);
        });
        QObject::connect(worker, &QThread::finished, worker, &QObject::deleteLater); worker->start();
    });
    const QString browserLabel = BuildFeatures::Gopher && BuildFeatures::Gemini
        ? QStringLiteral("Gopher / Gemini")
        : (BuildFeatures::Gopher ? QStringLiteral("Gopher") : QStringLiteral("Gemini"));
    m_tabs->addTab(page, browserLabel);
}

void NetworkToolsWindow::addMoshTab()
{
    auto *page = new QWidget(m_tabs); auto *v = new QVBoxLayout(page); auto *form = new QFormLayout;
    auto *host = new QLineEdit(page); auto *user = new QLineEdit(page); auto *port = new QSpinBox(page); port->setRange(1,65535); port->setValue(22);
    form->addRow(QStringLiteral("Host:"), host); form->addRow(QStringLiteral("User:"), user); form->addRow(QStringLiteral("SSH port:"), port); v->addLayout(form);
    auto *note = new QLabel(QStringLiteral("Mosh is launched through the system mosh client in a terminal. This keeps Mosh's own UDP roaming and terminal handling intact."), page); note->setWordWrap(true); v->addWidget(note);
    auto *launch = new QPushButton(QStringLiteral("Launch Mosh"), page); v->addWidget(launch); v->addStretch(1);
    connect(launch, &QPushButton::clicked, page, [page, host, user, port] {
        const QString mosh = QStandardPaths::findExecutable(QStringLiteral("mosh"));
        if (mosh.isEmpty()) { QMessageBox::warning(page, QStringLiteral("Mosh"), QStringLiteral("The system 'mosh' executable was not found. Install Mosh or leave this module out of the next build.")); return; }
        QString destination = host->text().trimmed(); if (!user->text().trimmed().isEmpty()) destination = user->text().trimmed() + QLatin1Char('@') + destination;
        if (destination.isEmpty()) return;
        const QStringList moshArgs{QStringLiteral("--ssh=ssh -p %1").arg(port->value()), destination};
#ifdef Q_OS_MACOS
        QString command = shellQuote(mosh) + QLatin1Char(' ') + shellQuote(moshArgs.at(0)) + QLatin1Char(' ') + shellQuote(destination);
        command.replace(QStringLiteral("\""), QStringLiteral("\\\""));
        QProcess::startDetached(QStringLiteral("osascript"), {QStringLiteral("-e"), QStringLiteral("tell application \"Terminal\" to do script %1").arg(QStringLiteral("\"") + command + QStringLiteral("\""))});
#elif defined(Q_OS_WIN)
        QProcess::startDetached(QStringLiteral("cmd.exe"), {QStringLiteral("/c"), QStringLiteral("start"), QStringLiteral("\"WaffleHouse-Client Mosh\""), mosh, moshArgs.at(0), destination});
#else
        QString terminal;
        for (const QString &candidate : {QStringLiteral("x-terminal-emulator"), QStringLiteral("xfce4-terminal"), QStringLiteral("gnome-terminal"), QStringLiteral("konsole"), QStringLiteral("xterm")}) {
            terminal = QStandardPaths::findExecutable(candidate); if (!terminal.isEmpty()) break;
        }
        if (!terminal.isEmpty()) QProcess::startDetached(terminal, {QStringLiteral("-e"), mosh, moshArgs.at(0), destination});
        else QProcess::startDetached(mosh, moshArgs);
#endif
    });
    m_tabs->addTab(page, QStringLiteral("Mosh"));
}

