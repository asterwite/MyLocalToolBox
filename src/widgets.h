#pragma once
// 动态 UI 控件集（参照 Fluent/商业端设计语言）：
//   NavButton  —— 侧栏导航：悬停背景渐隐动画 + QPainter 线性图标 + 选中态主题色
//   AvatarLabel—— 用户头像圆点（取名字首字）
//   makeLiftCard —— 卡片投影 + 悬停抬升动画
//   fadeIn     —— 页面切换淡入
// 全部头文件实现，无 Q_OBJECT（lambda 连接），避免新增 moc 目标。
#include <QPushButton>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QVariantAnimation>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QEvent>
#include <QEnterEvent>
#include <QEasingCurve>
#include <QLabel>
#include <QColor>
#include <QAbstractNativeEventFilter>
#include <QKeySequence>
#include <algorithm>
#include <cmath>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace ui {

// ---------- 调色板（与 theme.h 保持一致的暗色语义） ----------
inline QColor textCol()    { return QColor(0x24, 0x28, 0x38); }
inline QColor dimCol()     { return QColor(0x7c, 0x82, 0x94); }
inline QColor surfaceCol() { return QColor(20, 26, 48, 14); }   // hover 面
inline QColor accentCol()  { return theme::accent(); }

// ---------- NavButton ----------
class NavButton : public QPushButton {
public:
    enum Icon { Home, Shop, Chat, Users, Gear, User, Tool, Paw, Todo, Calendar, Habit, Note, Vault, Disk, Media, Folder };

    NavButton(Icon ic, const QString& text, QWidget* parent = nullptr)
        : QPushButton(text, parent), m_icon(ic) {
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setFixedHeight(44);
        setObjectName("navbtn");
        setFlat(true);
        setAttribute(Qt::WA_Hover, true);
    }

protected:
    void enterEvent(QEnterEvent* e) override {
        animateTo(1.0);
        QPushButton::enterEvent(e);
    }
    void leaveEvent(QEvent* e) override {
        animateTo(0.0);
        QPushButton::leaveEvent(e);
    }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QRectF r = rect().adjusted(3, 3, -3, -3);

        QColor a = accentCol();
        if (isChecked()) {
            QColor fill = a;
            fill.setAlpha(38);
            p.setPen(Qt::NoPen);
            p.setBrush(fill);
            p.drawRoundedRect(r, 11, 11);
            // 左缘高亮胶囊（Fluent 选中指示）
            p.setBrush(a);
            p.drawRoundedRect(QRectF(r.left() + 2, r.center().y() - 8, 3, 16), 1.5, 1.5);
        } else if (m_hoverK > 0.01) {
            QColor fill = surfaceCol();
            fill.setAlpha(int(14 * m_hoverK));
            p.setPen(Qt::NoPen);
            p.setBrush(fill);
            p.drawRoundedRect(r, 11, 11);
        }

        QColor ic = isChecked() ? a
                                : QColor::fromRgbF(
                                      dimCol().redF() + (textCol().redF() - dimCol().redF()) * m_hoverK,
                                      dimCol().greenF() + (textCol().greenF() - dimCol().greenF()) * m_hoverK,
                                      dimCol().blueF() + (textCol().blueF() - dimCol().blueF()) * m_hoverK);

        // 图标 20x20，垂直居中，左侧留 14px
        QRectF ib(r.left() + 13, r.center().y() - 10, 20, 20);
        QPen pen(ic, 1.7);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        drawIcon(p, m_icon, ib);

        // 文字
        p.setPen(isChecked() ? ic : QColor::fromRgbF(ic.redF(), ic.greenF(), ic.blueF()));
        QFont f = font();
        f.setPixelSize(14);
        f.setBold(isChecked());
        p.setFont(f);
        QRectF tr = r.adjusted(46, 0, -6, 0);
        p.drawText(tr, Qt::AlignVCenter | Qt::AlignLeft, text());
    }

private:
    void animateTo(double k) {
        if (!m_anim) {
            m_anim = new QVariantAnimation(this);
            connect(m_anim, &QVariantAnimation::valueChanged, this,
                    [this](const QVariant& v) {
                        m_hoverK = v.toDouble();
                        update();
                    });
        }
        m_anim->stop();
        m_anim->setDuration(150);
        m_anim->setStartValue(m_hoverK);
        m_anim->setEndValue(k);
        m_anim->setEasingCurve(QEasingCurve::OutQuad);
        m_anim->start();
    }

    static void drawIcon(QPainter& p, Icon ic, const QRectF& b) {
        const double x = b.left(), y = b.top();
        switch (ic) {
        case Home: {
            QPainterPath roof;
            roof.moveTo(x + 3, y + 9.5);
            roof.lineTo(x + 10, y + 3.2);
            roof.lineTo(x + 17, y + 9.5);
            p.drawPath(roof);
            p.drawRoundedRect(QRectF(x + 5, y + 9.5, 10, 7.3), 2, 2);
            break;
        }
        case Shop: { // 购物袋
            p.drawRoundedRect(QRectF(x + 4, y + 7, 12, 9.5), 2.5, 2.5);
            p.drawArc(QRectF(x + 7, y + 3, 6, 7), 0, 180 * 16);
            break;
        }
        case Chat: {
            QPainterPath bub;
            bub.addRoundedRect(QRectF(x + 3, y + 4, 14, 9.5), 4, 4);
            p.drawPath(bub);
            QPainterPath tail;
            tail.moveTo(x + 7, y + 13.5);
            tail.lineTo(x + 7, y + 17);
            tail.lineTo(x + 10.5, y + 13.5);
            p.drawPath(tail);
            for (int i = 0; i < 3; ++i)
                p.drawEllipse(QPointF(x + 7.5 + i * 2.8, y + 8.8), 0.9, 0.9);
            break;
        }
        case Users: {
            p.drawEllipse(QPointF(x + 7.5, y + 7.5), 2.8, 2.8);
            QPainterPath s1;
            s1.moveTo(x + 2.5, y + 16.5);
            s1.cubicTo(x + 2.5, y + 11.5, x + 12.5, y + 11.5, x + 12.5, y + 16.5);
            p.drawPath(s1);
            p.drawEllipse(QPointF(x + 14, y + 8), 2.2, 2.2);
            QPainterPath s2;
            s2.moveTo(x + 15, y + 12.6);
            s2.cubicTo(x + 18.5, y + 13.2, x + 17.8, y + 16.2, x + 17.8, y + 16.5);
            p.drawPath(s2);
            break;
        }
        case Gear: {
            QPointF c(x + 10, y + 10);
            p.drawEllipse(c, 3.1, 3.1);
            for (int i = 0; i < 8; ++i) {
                double ang = i * M_PI / 4.0;
                p.drawLine(QPointF(c.x() + std::cos(ang) * 5.2, c.y() + std::sin(ang) * 5.2),
                           QPointF(c.x() + std::cos(ang) * 7.3, c.y() + std::sin(ang) * 7.3));
            }
            break;
        }
        case User: {
            p.drawEllipse(QPointF(x + 10, y + 7), 3.2, 3.2);
            QPainterPath s;
            s.moveTo(x + 3.5, y + 17);
            s.cubicTo(x + 3.5, y + 11.6, x + 16.5, y + 11.6, x + 16.5, y + 17);
            p.drawPath(s);
            break;
        }
        case Tool: { // 滑杆（调节器）
            const double knobX[3] = {13.0, 7.0, 14.0};
            for (int i = 0; i < 3; ++i) {
                double yy = y + 4.6 + i * 5.4;
                p.drawLine(QPointF(x + 3, yy), QPointF(x + 17, yy));
                p.setBrush(ic);
                p.drawEllipse(QPointF(knobX[i], yy), 2.3, 2.3);
                p.setBrush(Qt::NoBrush);
            }
            break;
        }
        case Paw: { // 爪印：大肉垫 + 四趾
            p.drawEllipse(QPointF(x + 10, y + 12.6), 4.6, 3.7);
            p.drawEllipse(QPointF(x + 3.6, y + 8.2), 1.9, 2.3);
            p.drawEllipse(QPointF(x + 7.6, y + 4.6), 1.9, 2.3);
            p.drawEllipse(QPointF(x + 12.4, y + 4.6), 1.9, 2.3);
            p.drawEllipse(QPointF(x + 16.4, y + 8.2), 1.9, 2.3);
            break;
        }
        case Todo: { // 圆框对勾
            p.drawEllipse(QRectF(x + 3, y + 3, 14, 14));
            QPainterPath ck;
            ck.moveTo(x + 6.5, y + 10.2);
            ck.lineTo(x + 9, y + 12.8);
            ck.lineTo(x + 13.8, y + 7.4);
            p.drawPath(ck);
            break;
        }
        case Calendar: { // 日历
            p.drawRoundedRect(QRectF(x + 3, y + 5, 14, 12), 2.5, 2.5);
            p.drawLine(QPointF(x + 6.5, y + 3), QPointF(x + 6.5, y + 7));
            p.drawLine(QPointF(x + 13.5, y + 3), QPointF(x + 13.5, y + 7));
            p.drawLine(QPointF(x + 3, y + 9), QPointF(x + 17, y + 9));
            p.drawEllipse(QPointF(x + 7, y + 13), 1.1, 1.1);
            p.drawEllipse(QPointF(x + 10.5, y + 13), 1.1, 1.1);
            break;
        }
        case Habit: { // 小旗（打卡）
            p.drawLine(QPointF(x + 5, y + 3), QPointF(x + 5, y + 17));
            QPainterPath flag;
            flag.moveTo(x + 5, y + 4);
            flag.lineTo(x + 16, y + 6.5);
            flag.lineTo(x + 5, y + 10);
            flag.closeSubpath();
            p.drawPath(flag);
            break;
        }
        case Note: { // 笔记页：纸张 + 折角 + 笔线
            QPainterPath paper;
            paper.moveTo(x + 4.5, y + 3);
            paper.lineTo(x + 12.5, y + 3);
            paper.lineTo(x + 15.5, y + 6);
            paper.lineTo(x + 15.5, y + 17);
            paper.lineTo(x + 4.5, y + 17);
            paper.closeSubpath();
            p.drawPath(paper);
            p.drawLine(QPointF(x + 12.5, y + 3), QPointF(x + 12.5, y + 6));
            p.drawLine(QPointF(x + 12.5, y + 6), QPointF(x + 15.5, y + 6));
            p.drawLine(QPointF(x + 7, y + 10), QPointF(x + 13, y + 10));
            p.drawLine(QPointF(x + 7, y + 13.2), QPointF(x + 13, y + 13.2));
            break;
        }
        case Vault: { // 挂锁
            p.drawArc(QRectF(x + 6, y + 3, 8, 9), 0, 180 * 16);
            p.drawRoundedRect(QRectF(x + 4, y + 8.5, 12, 8.5), 2, 2);
            p.setBrush(ic);
            p.drawEllipse(QPointF(x + 10, y + 12.5), 1.4, 1.4);
            p.setBrush(Qt::NoBrush);
            break;
        }
        case Disk: { // 硬盘圆柱
            p.drawEllipse(QRectF(x + 4, y + 3, 12, 5));
            p.drawLine(QPointF(x + 4, y + 5.5), QPointF(x + 4, y + 14.5));
            p.drawLine(QPointF(x + 16, y + 5.5), QPointF(x + 16, y + 14.5));
            p.drawArc(QRectF(x + 4, y + 12, 12, 5), 180, 180);
            p.drawArc(QRectF(x + 6.5, y + 5.5, 7, 3.4), 0, 360 * 16);
            break;
        }
        case Media: { // 相机
            p.drawRoundedRect(QRectF(x + 3, y + 6, 14, 10), 2.5, 2.5);
            p.drawEllipse(QPointF(x + 10, y + 11), 3.2, 3.2);
            p.drawLine(QPointF(x + 7, y + 6), QPointF(x + 8.2, y + 3.8));
            p.drawLine(QPointF(x + 8.2, y + 3.8), QPointF(x + 11.8, y + 3.8));
            p.drawLine(QPointF(x + 11.8, y + 3.8), QPointF(x + 13, y + 6));
            break;
        }
        case Folder: { // 文件夹
            QPainterPath f;
            f.moveTo(x + 3, y + 5.5);
            f.lineTo(x + 8, y + 5.5);
            f.lineTo(x + 9.5, y + 7.5);
            f.lineTo(x + 17, y + 7.5);
            f.lineTo(x + 17, y + 15.5);
            f.lineTo(x + 3, y + 15.5);
            f.closeSubpath();
            p.drawPath(f);
            break;
        }
        }
    }

    Icon m_icon;
    double m_hoverK = 0.0;
    QVariantAnimation* m_anim = nullptr;
};

// ---------- 头像 ----------
class AvatarLabel : public QLabel {
public:
    explicit AvatarLabel(QWidget* parent = nullptr) : QLabel(parent) {
        setFixedSize(32, 32);
        setAlignment(Qt::AlignCenter);
        m_guestBadge = QPixmap(":/res/char_badge256.png");
    }
    // 名字为空 = 游客态，显示 Q 版徽章
    void setName(const QString& name) {
        m_name = name.isEmpty() ? QString() : name.left(1).toUpper();
        update();
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QRectF r = rect().adjusted(1, 1, -1, -1);
        if (m_name.isEmpty()) { // 游客：Q 版徽章
            if (!m_guestBadge.isNull())
                p.drawPixmap(r.toRect(), m_guestBadge);
            return;
        }
        QLinearGradient g(r.topLeft(), r.bottomLeft());
        QColor a = accentCol();
        g.setColorAt(0.0, a.lighter(115));
        g.setColorAt(1.0, a.darker(115));
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        p.drawEllipse(r);
        p.setPen(QPen(QColor(255, 255, 255, 70), 1.2));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(r);
        p.setPen(Qt::white);
        QFont f = font();
        f.setPixelSize(14);
        f.setBold(true);
        p.setFont(f);
        p.drawText(rect(), Qt::AlignCenter, m_name);
    }
private:
    QString m_name;
    QPixmap m_guestBadge;
};

// ---------- 卡片投影 + 悬停抬升 ----------
class LiftFilter : public QObject {
public:
    LiftFilter(QGraphicsDropShadowEffect* eff, QObject* parent)
        : QObject(parent), m_eff(eff) {}
    bool eventFilter(QObject* obj, QEvent* ev) override {
        if (ev->type() == QEvent::HoverEnter || ev->type() == QEvent::HoverLeave) {
            bool enter = ev->type() == QEvent::HoverEnter;
            auto* anim = new QVariantAnimation(obj);
            QObject::connect(anim, &QVariantAnimation::valueChanged, obj,
                             [this](const QVariant& v) {
                                 double k = v.toDouble();
                                 m_eff->setBlurRadius(m_baseBlur + k * 12);
                                 m_eff->setYOffset(m_baseY + k * 4);
                             });
            anim->setDuration(180);
            anim->setStartValue(0.0);
            anim->setEndValue(enter ? 1.0 : 0.0);
            anim->setEasingCurve(QEasingCurve::OutQuad);
            anim->start(QVariantAnimation::DeleteWhenStopped);
            ((QWidget*)obj)->setCursor(enter ? Qt::PointingHandCursor : Qt::ArrowCursor);
        }
        return QObject::eventFilter(obj, ev);
    }
    static void apply(QWidget* card, int blur = 20, int yoff = 6, int alpha = 120) {
        auto* eff = new QGraphicsDropShadowEffect(card);
        eff->setBlurRadius(blur);
        eff->setOffset(0, yoff);
        eff->setColor(QColor(0, 0, 0, alpha));
        card->setGraphicsEffect(eff);
        auto* f = new LiftFilter(eff, card);
        f->m_baseBlur = blur;
        f->m_baseY = yoff;
        card->installEventFilter(f);
        card->setAttribute(Qt::WA_Hover, true);
    }

private:
    QGraphicsDropShadowEffect* m_eff;
    int m_baseBlur = 20, m_baseY = 6;
};

// ---------- 全局热键（RegisterHotKey + WM_HOTKEY） ----------
class HotkeyFilter : public QAbstractNativeEventFilter {
public:
    std::function<void()> onHotkey;
    UINT id = 1;
    bool nativeEventFilter(const QByteArray& type, void* message, qintptr* result) override {
        if (type == "windows_generic_MSG") {
            MSG* msg = static_cast<MSG*>(message);
            if (msg && msg->message == WM_HOTKEY && msg->wParam == (WPARAM)id) {
                // 防抖：个别环境下一次按键会投递两条 WM_HOTKEY
                DWORD now = GetTickCount();
                if (now - m_lastFire < 250) return true;
                m_lastFire = now;
                if (onHotkey) { onHotkey(); return true; }
            }
        }
        return false;
    }

private:
    DWORD m_lastFire = 0;
};

// QKeySequence -> RegisterHotKey 的修饰键 + VK 码。
// 规则：F 功能键可单键使用；其余按键必须带 Ctrl/Alt/Shift/Win 修饰，避免全局拦截普通输入。
inline bool keySeqToNative(const QKeySequence& ks, UINT* mods, UINT* vk) {
    if (ks.isEmpty()) return false;
    int combined = ks[0].toCombined();
    Qt::KeyboardModifiers m(Qt::KeyboardModifierMask & combined);
    int key = combined & ~Qt::KeyboardModifierMask;
    UINT mod = MOD_NOREPEAT; // 防止按住重复触发
    if (m & Qt::ShiftModifier)  mod |= MOD_SHIFT;
    if (m & Qt::ControlModifier) mod |= MOD_CONTROL;
    if (m & Qt::AltModifier)    mod |= MOD_ALT;
    if (m & Qt::MetaModifier)   mod |= MOD_WIN;
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24) {
        *vk = UINT(VK_F1 + (key - Qt::Key_F1));
        *mods = mod;
        return true;
    }
    if (mod == MOD_NOREPEAT) return false;
    if ((key >= Qt::Key_A && key <= Qt::Key_Z) || (key >= Qt::Key_0 && key <= Qt::Key_9)) {
        *vk = UINT(key); // ASCII 区间 Qt 键值与 VK 码一致
        *mods = mod;
        return true;
    }
    UINT guess = UINT(key) & 0xFF;
    if (guess) { *vk = guess; *mods = mod; return true; }
    return false;
}

// ---------- 整卡可点击（含子控件透传） ----------
class ClickFilter : public QObject {
public:
    explicit ClickFilter(QObject* parent = nullptr) : QObject(parent) {}
    std::function<void()> onClick;
    bool eventFilter(QObject* obj, QEvent* ev) override {
        if (ev->type() == QEvent::MouseButtonPress && onClick) onClick();
        return QObject::eventFilter(obj, ev);
    }
};
inline void makeClickable(QWidget* w, std::function<void()> fn) {
    auto* f = new ClickFilter(w);
    f->onClick = std::move(fn);
    w->installEventFilter(f);
    for (auto* c : w->findChildren<QWidget*>())
        c->installEventFilter(f);
    w->setCursor(Qt::PointingHandCursor);
}

// ---------- 页面淡入 ----------
inline void fadeIn(QWidget* w, int ms = 240) {
    if (!w) return;
    auto* eff = new QGraphicsOpacityEffect(w);
    eff->setOpacity(0.0);
    w->setGraphicsEffect(eff);
    auto* a = new QPropertyAnimation(eff, "opacity", w);
    a->setDuration(ms);
    a->setStartValue(0.0);
    a->setEndValue(1.0);
    a->setEasingCurve(QEasingCurve::OutCubic);
    QObject::connect(a, &QPropertyAnimation::finished, w, [w] {
        w->setGraphicsEffect(nullptr); // 淡入后移除，避免与卡片投影嵌套
    });
    a->start(QAbstractAnimation::DeleteWhenStopped);
}

} // namespace ui
