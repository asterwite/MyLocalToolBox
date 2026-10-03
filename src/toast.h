#pragma once
// 右下角浮动通知：任务栏上方浮现、上浮渐隐后自动关闭；多条堆叠；点击可打开链接
#include "theme.h"
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QTimer>
#include <QVariantAnimation>
#include <QPainter>
#include <QGuiApplication>
#include <QScreen>
#include <QUrl>
#include <QDesktopServices>

class ToastWidget : public QWidget {
    Q_OBJECT
public:
    ToastWidget(const QString& title, const QString& text, int durationMs, bool clickable, QWidget* parent = nullptr)
        : QWidget(nullptr) {
        setWindowFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint);
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setFixedWidth(336);

        auto* v = new QVBoxLayout(this);
        v->setContentsMargins(14, 10, 14, 12);
        auto* h = new QHBoxLayout;
        auto* tv = new QVBoxLayout;
        tv->setSpacing(3);
        auto* t = new QLabel(title);
        t->setStyleSheet("color:#23263a;font-size:13px;font-weight:bold;background:transparent;");
        tv->addWidget(t);
        if (!text.isEmpty()) {
            auto* body = new QLabel(text);
            body->setStyleSheet("color:rgba(38,44,66,.80);font-size:12px;background:transparent;");
            body->setWordWrap(true);
            tv->addWidget(body);
        }
        h->addLayout(tv, 1);
        if (clickable) {
            auto* go = new QPushButton("查看");
            go->setCursor(Qt::PointingHandCursor);
            go->setStyleSheet("QPushButton{background:rgba(20,26,48,.07);color:#23263a;border:0;border-radius:8px;padding:4px 12px;font-size:12px;}"
                              "QPushButton:hover{background:rgba(20,26,48,.14);}");
            connect(go, &QPushButton::clicked, this, [this] { emit clicked(); fadeOut(); });
            h->addWidget(go);
        }
        auto* close = new QPushButton("×");
        close->setFixedSize(22, 22);
        close->setCursor(Qt::PointingHandCursor);
        close->setStyleSheet("QPushButton{background:transparent;color:rgba(38,44,66,.55);border:0;font-size:15px;}"
                             "QPushButton:hover{color:#23263a;background:rgba(20,26,48,.07);border-radius:11px;}");
        connect(close, &QPushButton::clicked, this, &ToastWidget::fadeOut);
        h->addWidget(close);
        v->addLayout(h);

        m_timer.setSingleShot(true);
        m_timer.setInterval(durationMs);
        connect(&m_timer, &QTimer::timeout, this, &ToastWidget::fadeOut);
    }

    void popup() {
        show();
        // 定位：主屏右下，任务栏上方（availableGeometry 底边即任务栏上缘）
        QScreen* sc = QGuiApplication::primaryScreen();
        const QRect ag = sc->availableGeometry();
        m_targetY = ag.bottom() - height() - m_margin;
        const int x = ag.right() - width() - m_margin;
        const int fromY = ag.bottom() + 8;
        move(x, fromY);
        // 浮现动画：从下往上升 + 渐入
        auto* anim = new QVariantAnimation(this);
        anim->setDuration(280);
        anim->setStartValue(0.0);
        anim->setEndValue(1.0);
        anim->setEasingCurve(QEasingCurve::OutCubic);
        connect(anim, &QVariantAnimation::valueChanged, this, [this, x, fromY](const QVariant& v2) {
            const double k = v2.toDouble();
            move(x, int(fromY + (m_targetY - fromY) * k));
            setWindowOpacity(k);
        });
        connect(anim, &QVariantAnimation::finished, this, [this, x] { move(x, m_targetY); });
        anim->start(QVariantAnimation::DeleteWhenStopped);
        m_timer.start();
    }

    // 关闭后其他 toast 上移补位（MainWindow 层负责）
    static void setMargin(int m) { m_margin = m; }

signals:
    void closed(ToastWidget* self);
    void clicked();

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        // 玻璃卡片：深色半透明 + 高光描边
        p.setPen(QPen(QColor(20, 26, 48, 32), 1));
        p.setBrush(QColor(255, 255, 255, 244));
        p.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 12, 12);
        // 左侧主题色竖条
        p.setPen(Qt::NoPen);
        p.setBrush(theme::accent());
        p.drawRoundedRect(QRect(6, 10, 3, height() - 20), 1, 1);
    }

private:
    void fadeOut() {
        m_timer.stop();
        auto* anim = new QVariantAnimation(this);
        anim->setDuration(240);
        anim->setStartValue(1.0);
        anim->setEndValue(0.0);
        connect(anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
            setWindowOpacity(v.toDouble());
            move(x(), y() - int(v.toDouble() * 14)); // 上浮渐隐
        });
        connect(anim, &QVariantAnimation::finished, this, [this] {
            emit closed(this);
            deleteLater();
        });
        anim->start(QVariantAnimation::DeleteWhenStopped);
    }

    int m_targetY = 0;
    QTimer m_timer;
    inline static int m_margin = 12;
};

// 管理堆叠：新 toast 依次往上排，旧的上移补位；聚合「有 toast 被点击」事件
class ToastHost : public QObject {
    Q_OBJECT
public:
    explicit ToastHost(QObject* parent = nullptr) : QObject(parent) {}

signals:
    void hostClicked();

public:
    void show(const QString& title, const QString& text, int durationMs = 3200, bool clickable = false) {
        auto* w = new ToastWidget(title, text, durationMs, clickable);
        m_toasts.append(w);
        connect(w, &ToastWidget::clicked, this, [this] { emit hostClicked(); });
        connect(w, &ToastWidget::closed, this, [this](ToastWidget* self) {
            m_toasts.removeAll(self);
            restack();
        });
        restack();
        w->popup();
    }

    void restack() {
        // 从最底（最新）往上重排
        QScreen* sc = QGuiApplication::primaryScreen();
        const QRect ag = sc->availableGeometry();
        int y = ag.bottom() - 12;
        for (int i = m_toasts.size() - 1; i >= 0; --i) {
            ToastWidget* w = m_toasts[i];
            y -= (w->height() + 10);
            if (w->isVisible()) w->move(w->x(), y);
        }
    }

private:
    QList<ToastWidget*> m_toasts;
};

inline ToastHost* toastHost() {
    static ToastHost host;
    return &host;
}

inline void toast(const QString& title, const QString& text = QString(), int durationMs = 3200, bool clickable = false) {
    toastHost()->show(title, text, durationMs, clickable);
}
