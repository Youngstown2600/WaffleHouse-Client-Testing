#pragma once

#include "ansiterminal.h"

#include <QWidget>
#include <QFont>

class QKeyEvent;
class QMouseEvent;
class QPaintEvent;
class QResizeEvent;

class AnsiTerminalWidget : public QWidget {
    Q_OBJECT
public:
    explicit AnsiTerminalWidget(QWidget *parent = nullptr);
    void feed(const QString &text);
    void clearScreen();
    int terminalColumns() const { return m_model.columns(); }
    int terminalRows() const { return m_model.rows(); }

    // BBS geometry is a protocol property, not an accident of the Qt window size.
    // When autoFit is true the model/NAWS dimensions stay exact and the monospace
    // font is resized to fit those cells into the available widget rectangle.
    void setTerminalGeometry(int columns, int rows, bool autoFit = true);
    bool autoFitEnabled() const { return m_autoFit; }

signals:
    void terminalBytes(const QByteArray &bytes);
    void terminalSizeChanged(int columns, int rows);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    void updateGeometryFromPixels();
    void fitFontToViewport();
    void updateCellMetrics();
    QByteArray keySequence(QKeyEvent *event) const;

    AnsiTerminalModel m_model{80, 24};
    QFont m_font;
    int m_cellWidth = 8;
    int m_cellHeight = 16;
    int m_targetColumns = 80;
    int m_targetRows = 24;
    bool m_autoFit = true;
};
