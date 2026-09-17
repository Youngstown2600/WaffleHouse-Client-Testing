#pragma once

#include <QWidget>

class QTabWidget;

class NetworkToolsWindow final : public QWidget {
    Q_OBJECT
public:
    explicit NetworkToolsWindow(QWidget *parent = nullptr);
    void showAndRaise();

private:
    void addBrowserTab();
    void addMoshTab();
    QTabWidget *m_tabs = nullptr;
};
