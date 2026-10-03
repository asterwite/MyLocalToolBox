#pragma once
// 屏幕测距：全屏透明覆盖，拖拽画线并显示像素长度（Esc 退出）
#include <QWidget>
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QScreen>
#include <QGuiApplication>

class RulerOverlay : public QWidget {
    Q_OBJECT
public:
    QPoint m_p0, m_p1;
    bool m_dragging = false;

    RulerOverlay() {
        setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
        setAttribute(Qt::WA_TranslucentBackground);
        setCursor(Qt::CrossCursor);
        setGeometry(QGuiApplication::primaryScreen()->geometry());
    }
    void start() { show(); raise(); activateWindow(); setFocus(); }

signals:
    void cancelled();

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), QColor(255, 255, 255, 1)); // 接收鼠标的最小着色
        if (m_dragging || (!m_p0.isNull() && !m_p1.isNull())) {
            QPen pen(QColor(230, 60, 60), 2);
            pen.setStyle(Qt::DashLine);
            p.setPen(pen);
            p.drawLine(m_p0, m_p1);
            for (const QPoint& pt : {m_p0, m_p1}) {
                p.setBrush(QColor(230, 60, 60));
                p.drawEllipse(pt, 4, 4);
            }
            const int dx = m_p1.x() - m_p0.x(), dy = m_p1.y() - m_p0.y();
            const QString tip = QString("ΔX %1  ΔY %2  长度 %3 px")
                .arg(dx).arg(dy).arg(int(std::hypot(dx, dy)));
            QFont f = p.font();
            f.setPixelSize(13);
            p.setFont(f);
            QRect tr = p.fontMetrics().boundingRect(tip).adjusted(-8, -5, 8, 5);
            tr.translate((m_p0.x() + m_p1.x()) / 2 - tr.center().x(),
                         (m_p0.y() + m_p1.y()) / 2 - 34);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(20, 20, 20, 200));
            p.drawRoundedRect(tr, 5, 5);
            p.setPen(Qt::white);
            p.drawText(tr, Qt::AlignCenter, tip);
        } else {
            p.setPen(QColor(80, 90, 110, 200));
            QFont f = p.font();
            f.setPixelSize(14);
            p.setFont(f);
            p.drawText(rect(), Qt::AlignHCenter | Qt::AlignTop,
                       QStringLiteral("按住拖拽测距 · Esc 退出"));
        }
    }

    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton) {
            m_p0 = m_p1 = e->position().toPoint();
            m_dragging = true;
            update();
        }
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        if (m_dragging) {
            m_p1 = e->position().toPoint();
            update();
        }
    }
    void mouseReleaseEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton) m_dragging = false;
        update();
    }
    void keyPressEvent(QKeyEvent* e) override {
        if (e->key() == Qt::Key_Escape) { emit cancelled(); close(); }
    }
};