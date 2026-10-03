#pragma once
// 全屏取色遮罩：鼠标跟随放大镜，点击/Enter 取色，Esc 取消
#include <QWidget>
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QScreen>
#include <QGuiApplication>

class PickOverlay : public QWidget {
    Q_OBJECT
public:
    QImage m_img; // 全屏底图（设备像素，用于取样）
    QPoint m_pos;

    PickOverlay() {
        setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
        setCursor(Qt::BlankCursor);
        setMouseTracking(true);
        QScreen* sc = QGuiApplication::primaryScreen();
        m_img = sc->grabWindow(0).toImage();
        setGeometry(sc->geometry());
    }

    void start() { show(); raise(); activateWindow(); setFocus(); }

signals:
    void picked(const QColor& c);
    void cancelled();

protected:
    QColor sample(const QPoint& logical) const {
        const qreal dpr = devicePixelRatioF();
        const int x = qBound(0, int(logical.x() * dpr), m_img.width() - 1);
        const int y = qBound(0, int(logical.y() * dpr), m_img.height() - 1);
        return m_img.pixel(x, y);
    }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.drawImage(rect(), m_img); // 原亮度底图
        const QColor c = sample(m_pos);
        // 放大镜：光标周围 15x15 源像素，每格 13px
        const int n = 15, cell = 13, half = n / 2;
        const qreal dpr = devicePixelRatioF();
        const int sx = int(m_pos.x() * dpr) - half, sy = int(m_pos.y() * dpr) - half;
        const int boxW = n * cell, boxH = n * cell;
        int bx = m_pos.x() + 24, by = m_pos.y() + 24;
        if (bx + boxW > width() - 8) bx = m_pos.x() - boxW - 24;
        if (by + boxH + 34 > height() - 8) by = m_pos.y() - boxH - 34 - 24;
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j) {
                const int px = qBound(0, sx + i, m_img.width() - 1);
                const int py = qBound(0, sy + j, m_img.height() - 1);
                p.fillRect(bx + i * cell, by + j * cell, cell, cell, m_img.pixel(px, py));
            }
        p.setPen(QPen(QColor(255, 255, 255), 2));
        p.setBrush(Qt::NoBrush);
        p.drawRect(bx + half * cell - 1, by + half * cell - 1, cell + 2, cell + 2); // 中心格高亮
        p.drawRect(bx - 1, by - 1, boxW + 2, boxH + 2);
        // 色值条
        const QString tip = QString("#%1%2%3   RGB(%4, %5, %6)")
                                .arg(c.red(), 2, 16, QChar('0')).arg(c.green(), 2, 16, QChar('0'))
                                .arg(c.blue(), 2, 16, QChar('0')).arg(c.red()).arg(c.green()).arg(c.blue());
        QFont f = p.font();
        f.setPixelSize(13);
        p.setFont(f);
        QRect tr = p.fontMetrics().boundingRect(tip).adjusted(-10, -5, 10, 5);
        tr.moveTopLeft(QPoint(bx, by + boxH + 4));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(20, 20, 20, 220));
        p.drawRoundedRect(tr, 4, 4);
        p.setPen(Qt::white);
        p.drawText(tr, Qt::AlignCenter, tip);
        // 光标十字
        p.setPen(QPen(QColor(255, 60, 60), 1));
        p.drawLine(m_pos.x() - 12, m_pos.y(), m_pos.x() + 12, m_pos.y());
        p.drawLine(m_pos.x(), m_pos.y() - 12, m_pos.x(), m_pos.y() + 12);
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        m_pos = e->position().toPoint();
        update();
    }

    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton) {
            emit picked(sample(e->position().toPoint()));
            close();
        }
    }

    void keyPressEvent(QKeyEvent* e) override {
        if (e->key() == Qt::Key_Escape) {
            emit cancelled();
            close();
        } else if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
            emit picked(sample(m_pos));
            close();
        }
    }
};
