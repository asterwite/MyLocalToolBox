#pragma once
// 全屏截图遮罩：主屏暗化 + 橡皮筋选区；Enter/双击确认，Esc 取消
#include <QWidget>
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QScreen>
#include <QGuiApplication>

class ShotOverlay : public QWidget {
    Q_OBJECT
public:
    QPixmap m_shot;    // 全屏底图（含 DPR）
    QRect m_sel;
    QPoint m_p0;
    bool m_selecting = false;

    ShotOverlay() {
        setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
        setCursor(Qt::CrossCursor);
        QScreen* sc = QGuiApplication::primaryScreen();
        m_shot = sc->grabWindow(0);
        setGeometry(sc->geometry());
    }

    void start() { show(); raise(); activateWindow(); setFocus(); }

signals:
    void finished(const QPixmap& crop);
    void cancelled();

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.drawPixmap(0, 0, m_shot);
        p.fillRect(rect(), QColor(0, 0, 0, 110)); // 整体暗化
        if (!m_sel.isNull() && m_sel.width() > 1 && m_sel.height() > 1) {
            const qreal dpr = devicePixelRatioF();
            // 选区内重画原亮度
            p.drawPixmap(m_sel, m_shot,
                         QRectF(m_sel.x() * dpr, m_sel.y() * dpr, m_sel.width() * dpr, m_sel.height() * dpr));
            QPen pen(QColor(0, 170, 255), 2);
            p.setPen(pen);
            p.drawRect(m_sel);
            p.setPen(QColor(0, 170, 255));
            p.setBrush(QColor(0, 170, 255, 200));
            QFont f = p.font();
            f.setPixelSize(12);
            p.setFont(f);
            const QString tip = QString("%1 × %2").arg(m_sel.width()).arg(m_sel.height());
            QRect tr = p.fontMetrics().boundingRect(tip).adjusted(-6, -3, 6, 3);
            tr.moveTopLeft(m_sel.bottomRight() + QPoint(-tr.width(), 6));
            p.drawRect(tr);
            p.drawText(tr, Qt::AlignCenter, tip);
        } else {
            p.setPen(QColor(255, 255, 255, 200));
            QFont f = p.font();
            f.setPixelSize(14);
            p.setFont(f);
            p.drawText(rect(), Qt::AlignHCenter | Qt::AlignTop,
                       QStringLiteral("拖拽选择截图区域  ·  Enter 或双击确认  ·  Esc 取消"));
        }
    }

    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton) {
            m_p0 = e->position().toPoint();
            m_sel = QRect();
            m_selecting = true;
            update();
        }
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        if (m_selecting) {
            m_sel = QRect(m_p0, e->position().toPoint()).normalized();
            update();
        }
    }

    void mouseReleaseEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton) {
            m_selecting = false;
            m_sel = QRect(m_p0, e->position().toPoint()).normalized();
            update();
        }
    }

    void mouseDoubleClickEvent(QMouseEvent*) override { confirm(); }

    void keyPressEvent(QKeyEvent* e) override {
        if (e->key() == Qt::Key_Escape) {
            emit cancelled();
            close();
        } else if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
            confirm();
        }
    }

    void confirm() {
        if (m_sel.isNull() || m_sel.width() < 2 || m_sel.height() < 2) return;
        const qreal dpr = devicePixelRatioF();
        QPixmap crop = m_shot.copy(QRect(m_sel.x() * dpr, m_sel.y() * dpr,
                                         m_sel.width() * dpr, m_sel.height() * dpr));
        crop.setDevicePixelRatio(dpr);
        emit finished(crop);
        close();
    }
};
