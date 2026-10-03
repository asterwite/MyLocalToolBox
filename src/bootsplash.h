#pragma once
// 启动过场动画：参照 dsh-boot-animation 的"全窗口过场 + 点击跳过"形式，
// 用 Q 版看板娘 + 淡入实现（Qt Widgets 原生绘制，无视频解码依赖）。
// 时间轴：0-0.9s 形象淡入+缩放回弹，0.5s 起标题渐显，随后上下浮动，
// 2.6s 自动淡出；任意点击/按键立即淡出。结束后回调 onFinished。
#include <QWidget>
#include <QPixmap>
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QRadialGradient>
#include <QTimer>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QFont>
#include <algorithm>
#include <cmath>
#include <functional>

namespace boot {

class BootSplash : public QWidget {
public:
    std::function<void()> onFinished; // 整体淡出结束后调用（切主窗口）

    BootSplash() : m_char(":/res/char_full.png") {
        setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
        setAttribute(Qt::WA_TranslucentBackground);
        setCursor(Qt::PointingHandCursor);
        QRect ag = QGuiApplication::primaryScreen()->availableGeometry();
        QSize sz(880, 660);
        setGeometry(ag.center().x() - sz.width() / 2, ag.center().y() - sz.height() / 2,
                    sz.width(), sz.height());
        m_t0.start();
        auto* t = new QTimer(this);
        connect(t, &QTimer::timeout, this, [this] {
            update();
            if (!m_closing && m_t0.elapsed() > kAutoCloseMs) beginClose();
            if (m_closing && m_t0.elapsed() - m_closeAt > kFadeOutMs) {
                if (onFinished) onFinished();
                close();
            }
        });
        t->start(16);
    }

protected:
    void mousePressEvent(QMouseEvent*) override { beginClose(); }
    void keyPressEvent(QKeyEvent*) override { beginClose(); }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);

        const double t = m_t0.elapsed();
        double globalAlpha = 1.0;
        if (m_closing)
            globalAlpha = std::max(0.0, 1.0 - double(t - m_closeAt) / kFadeOutMs);
        else
            globalAlpha = std::min(1.0, t / 260.0); // 面板本身快速淡入
        p.setOpacity(globalAlpha);

        // ---------- 圆角过场卡片 ----------
        QRectF r = rect().adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath card;
        card.addRoundedRect(r, 26, 26);
        p.setClipPath(card);

        QLinearGradient bg(r.topLeft(), r.bottomLeft());
        bg.setColorAt(0.0, QColor(0x14, 0x16, 0x1f));
        bg.setColorAt(1.0, QColor(0x0e, 0x0f, 0x16));
        p.fillPath(card, bg);

        // 顶部主题色光晕
        QRadialGradient glow(QPointF(r.width() * 0.5, -r.height() * 0.12), r.width() * 0.85);
        QColor ac = accentGlow();
        ac.setAlpha(52);
        glow.setColorAt(0.0, ac);
        ac.setAlpha(0);
        glow.setColorAt(1.0, ac);
        p.fillRect(r, glow);

        // ---------- 看板娘：淡入 + 缩放回弹 + 浮动 ----------
        double k = std::clamp(t / 900.0, 0.0, 1.0);
        double ease = 1.0 - std::pow(1.0 - k, 3.0);           // ease-out cubic
        double scale = 0.90 + 0.12 * ease - 0.02 * k;         // 0.90 -> 1.02 -> 1.00 回弹
        double floatY = k >= 1.0 ? std::sin(t / 640.0) * 6.0 : 0.0;

        double targetH = r.height() * 0.60;
        double w = m_char.width() * (targetH / m_char.height()) * scale;
        double h = targetH * scale;
        double cx = r.center().x();
        double baseY = r.height() * 0.66;                     // 站在标题区上方
        QRectF target(cx - w / 2, baseY - h + floatY, w, h);
        p.setOpacity(globalAlpha * ease);
        p.drawPixmap(target, m_char, QRectF(m_char.rect()));

        // ---------- 标题 / 副标题 ----------
        double tk = std::clamp((t - 500.0) / 800.0, 0.0, 1.0);
        double tEase = 1.0 - std::pow(1.0 - tk, 3.0);
        if (tk > 0) {
            p.setOpacity(globalAlpha * tEase);
            p.setPen(QColor(0xea, 0xec, 0xf2));
            QFont f = font();
            f.setFamily("Microsoft YaHei");
            f.setPixelSize(34);
            f.setBold(true);
            p.setFont(f);
            double ty = r.height() * 0.70 + (1.0 - tEase) * 14.0;
            p.drawText(QRectF(r.x(), ty, r.width(), 48), Qt::AlignHCenter, "我的工具箱");
            p.setPen(QColor(0x8a, 0x8f, 0xa0));
            f.setPixelSize(14);
            f.setBold(false);
            p.setFont(f);
            p.drawText(QRectF(r.x(), ty + 50, r.width(), 24), Qt::AlignHCenter,
                       "ToolBox · 本地优先 · 一切尽在掌握");
        }

        // ---------- 加载点 ----------
        p.setOpacity(globalAlpha);
        double dotY = r.height() * 0.90;
        for (int i = 0; i < 3; ++i) {
            double ph = t / 420.0 - i * 0.55;
            double dy = std::abs(std::sin(ph)) * -7.0;
            QColor c = accentGlow();
            c.setAlpha(90 + int(120 * std::clamp(std::sin(ph) + 0.4, 0.0, 1.0)));
            p.setBrush(c);
            p.setPen(Qt::NoPen);
            p.drawEllipse(QPointF(cx + (i - 1) * 22.0, dotY + dy), 5.0, 5.0);
        }

        // ---------- 跳过提示 ----------
        double sk = std::clamp((t - 900.0) / 500.0, 0.0, 1.0);
        if (sk > 0) {
            p.setOpacity(globalAlpha * sk * 0.7);
            p.setPen(QColor(0x8a, 0x8f, 0xa0));
            QFont f2 = font();
            f2.setFamily("Microsoft YaHei");
            f2.setPixelSize(11);
            p.setFont(f2);
            p.drawText(QRectF(r.right() - 200, r.bottom() - 30, 184, 20),
                       Qt::AlignRight | Qt::AlignVCenter, "点击任意位置跳过");
        }
        p.setClipping(false);
    }

private:
    void beginClose() {
        if (m_closing) return;
        m_closing = true;
        m_closeAt = m_t0.elapsed();
    }

    static QColor accentGlow() {
        // 与主题色预设保持一致（蓝为默认）
        return QColor(0x4e, 0x9a, 0xff);
    }

    static constexpr double kAutoCloseMs = 2600.0;
    static constexpr double kFadeOutMs = 350.0;

    QPixmap m_char;
    QElapsedTimer m_t0;
    bool m_closing = false;
    double m_closeAt = 0;
};

} // namespace boot
