#pragma once
// 桌面宠物（素材与交互体系完整复刻自 dsh-pet，CC BY-NC 4.0）：
//  - 素材：pet-frames/<动作名>/NNN.webp 帧序列（由 dsh-pet 的 webm+alpha 提取），manifest.json 索引
//  - 动作分类与权重完全对齐 dsh-pet config：小动作/玩耍/吃什么/时节/文字 + 待机/转向/走路/拖拽/点击回应
//  - 物理参数对齐：gravity 1400(px/s²)、restitution 0.78、groundFriction 2.5、天花板反弹开启
//  - 16ms 精密定时器 + delta-time 积分；帧动画按各动作自身 fps 播放（循环/单次）
//  - 拖拽甩出：重力 + 转体 + 墙地反弹 + 落地压扁 + 晕眩吐槽
//  - 位置、大小与开关持久化；随主程序自动启动
#include <QWidget>
#include <QPixmap>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QElapsedTimer>
#include <QMenu>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QLabel>
#include <QSlider>
#include <QVBoxLayout>
#include <QScreen>
#include <QUrl>
#include <QMouseEvent>
#include <QRandomGenerator>
#include <QSettings>
#include <QDateTime>
#include <QImage>
#include <QPointer>
#include <QThreadPool>
#include <QJsonDocument>
#include "localstore.h" // 桌宠设置本地化（exe\ToolBox\pet_ui.json，不写 C 盘）
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <QFile>
#include <QColor>
#include <QHash>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace pet {

class PetWindow;
inline PetWindow* s_activePet = nullptr; // 云养桥：主窗口投喂/哄睡时远程驱动桌宠（inline 变量，多 TU 安全）

inline void playRemote(const QString& action, const QString& bubble);
inline void setRemoteSleeping(bool on);
inline void setRemoteWork(const QString& action);

class PetWindow : public QWidget {
public:
    ~PetWindow() override {
        if (s_activePet == this) s_activePet = nullptr;
    }

    // 云养联动：主窗口投喂物品时远程点播对应动画 + 气泡
    void remotePlay(const QString& action, const QString& bubble) {
        if (!isVisible()) return; // 桌宠隐藏时免空播（气泡/动画状态无人看）
        if (!m_anims.contains(action)) return;
        playManual(action);
        if (!bubble.isEmpty()) {
            m_bubble = bubble;
            m_bubbleUntil = m_t0.elapsed() + 2600;
        }
    }

    // 素材热重载：云养页导入/切换自定义模型后由桥调用
    void reloadAssets() {
        m_frameCache.clear();
        m_cacheOrder.clear();
        m_spPix.clear();
        m_anims.clear();
        loadSprites();
        enterIdle();
        update();
    }

    // 云养联动：睡觉=沉眠循环稳态；叫醒=播「打瞌睡被惊醒」过渡后回待机
    void setCloudSleep(bool on) {
        if (m_cloudSleep == on) return;
        m_cloudSleep = on;
        if (m_thrown || m_dragging) return;
        m_bubble = on ? QString("Zzz……晚安～") : QString("睡醒啦！精神满满！");
        m_bubbleUntil = m_t0.elapsed() + 2400;
        m_walking = false;
        if (!on && playManual("打瞌睡被惊醒")) return; // 惊醒动画播完自动 enterIdle 回待机
        enterIdle();
    }

    // 云养联动：打工任务（循环播放任务动画直到任务结束/放弃）
    void setCloudWork(const QString& action) {
        if (m_cloudWork == action) return;
        m_cloudWork = action;
        if (m_thrown || m_dragging) return;
        enterIdle();
    }

    std::function<void()> onShowMain; // 菜单"显示主程序"

    PetWindow() {
        s_activePet = this;
        setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
        setAttribute(Qt::WA_TranslucentBackground);
        setCursor(Qt::PointingHandCursor);
        setMouseTracking(true);

        QPixmap src(":/res/char_full.png");
        for (int s = 0; s < 3; ++s) {
            m_char[s] = src.scaledToHeight(kHeights[s], Qt::SmoothTransformation);
            m_charFlip[s] = m_char[s].transformed(QTransform().scale(-1, 1));
        }
        loadSprites();
        applySize();

        QJsonObject uiSt = ls::petUi();
        if (uiSt.isEmpty()) { // 迁移：本地无配置时读一次旧注册表
            QSettings old("ToolBox", "ToolBoxQt");
            uiSt.insert("walk", old.value("pet/walk", true).toBool());
            uiSt.insert("speak", old.value("pet/autospeak", true).toBool());
            uiSt.insert("autoact", old.value("pet/autoact", true).toBool());
            uiSt.insert("enabled", old.value("pet/enabled", true).toBool());
            uiSt.insert("size", old.value("pet/size", 1).toInt());
            const QVariant ox = old.value("pet/x");
            const QVariant oy = old.value("pet/y");
            if (ox.isValid()) uiSt.insert("x", ox.toInt());
            if (oy.isValid()) uiSt.insert("y", oy.toInt());
            ls::savePetUi(uiSt);
        }
        m_walkEnabled = uiSt.value("walk").toBool();
        m_hover = uiSt.value("hover").toBool();
        m_autoSpeak = uiSt.value("speak").toBool();
        m_autoAct = uiSt.value("autoact").toBool();
        m_autoStart = uiSt.value("enabled").toBool();
        m_sizeIdx = qBound(0, uiSt.value("size").toInt(1), 2);
        m_customW = qBound(0, uiSt.value("w").toInt(0), 520);
        if (m_customW > 0 && m_customW < 180) m_customW = 180;
        applySize();
        QRect ag = QGuiApplication::primaryScreen()->availableGeometry();
        if (uiSt.contains("x") && uiSt.contains("y")) {
            move(qBound(ag.left(), uiSt.value("x").toInt(), ag.right() - width()),
                 qBound(ag.top(), uiSt.value("y").toInt(), ag.bottom() - height()));
        } else {
            move(ag.right() - width() - 60, ag.bottom() - height());
        }

        m_cloudSleep = ls::petState().value("sleeping").toBool(); // 跨重启恢复睡觉态（MainWindow 构造早于本窗口，桥同步不可靠，这里自己读）
        m_nextSpeak = 6000 + QRandomGenerator::global()->bounded(6000);
        m_nextAct = 9000 + QRandomGenerator::global()->bounded(8000);
        enterIdle();

        m_t0.start();
        auto* t = new QTimer(this);
        t->setTimerType(Qt::PreciseTimer);
        connect(t, &QTimer::timeout, this, [this] { tick(); });
        t->start(16);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);

        double t = m_t0.elapsed();
        double hop = 0, sx = 1.0, sy = 1.0;

        if (t < m_squashUntil) { sx = 1.12; sy = 0.84; }
        if (t < m_bounceUntil)
            hop = -std::abs(std::sin((m_bounceUntil - t) / 620.0 * M_PI)) * 12.0;

        // ---------- 帧动画模式 ----------
        if (!m_spPix.isEmpty()) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0, 0, 0, 45));
            p.drawEllipse(QPointF(width() / 2.0, height() - 5.0),
                          width() * 0.22, 5.0 * (1.0 - qMin(1.0, std::abs(hop) / 30.0)));
            p.translate(width() / 2.0, height() - 4.0 + hop);
            p.scale(sx, sy); // 抛掷不转体，保持直立（对齐 dsh-pet）
            int idx = qBound(0, m_spIdx, m_spPix.size() - 1);
            const QImage& fr = m_spPix[idx];
            // 帧统一按窗口尺寸缩放绘制（窗口也是 16:9，三档大小都完整显示）
            p.drawImage(QRect(-width() / 2, -height() + 2, width(), height()), fr);
            p.resetTransform();
        } else {
            // ---------- 立绘程序化动画（素材缺失时的回落） ----------
            int sz = m_sizeIdx;
            const QPixmap& pm = m_dir < 0 ? m_charFlip[sz] : m_char[sz];
            int cw = pm.width(), chh = pm.height();
            double bob = (m_walking || m_thrown || m_falling)
                             ? std::abs(std::sin(t / 140.0)) * 3.0
                             : std::sin(t / 650.0) * 4.0;
            if (t < m_squashUntil) { sx = 1.12; sy = 0.84; }
            double pokeHop = 0;
            if (t < m_bounceUntil)
                pokeHop = -std::abs(std::sin((m_bounceUntil - t) / 620.0 * M_PI)) * 14.0;
            hop += pokeHop;
            double shadowSquash = 1.0 - qMin(1.0, std::abs(hop) / 40.0);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0, 0, 0, 55));
            p.drawEllipse(QPointF(width() / 2.0, height() - 6.0),
                          cw * 0.32, 7.0 * shadowSquash);
            p.translate(width() / 2.0, height() - 6.0 + hop);
            p.scale(sx, sy);
            p.drawPixmap(QRect(-cw / 2, -chh + int(bob), cw, chh), pm);
            p.resetTransform();
        }

        // 气泡
        if (t < m_bubbleUntil && !m_bubble.isEmpty()) {
            QFont f = font();
            f.setPixelSize(12);
            p.setFont(f);
            QFontMetrics fm(f);
            int tw = fm.horizontalAdvance(m_bubble) + 20;
            QRectF bubble(width() / 2.0 - tw / 2.0, 8.0, tw, 26);
            QPainterPath bp;
            bp.addRoundedRect(bubble, 10, 10);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 255, 255, 235));
            p.drawPath(bp);
            QPainterPath tail;
            tail.moveTo(QPointF(bubble.center().x() - 5, bubble.bottom() - 1));
            tail.lineTo(QPointF(bubble.center().x() + 5, bubble.bottom() - 1));
            tail.lineTo(QPointF(bubble.center().x(), bubble.bottom() + 8));
            p.drawPath(tail);
            p.setPen(QColor(40, 44, 60));
            p.drawText(bubble, Qt::AlignCenter, m_bubble);
        }

        updateClickMask(t, hop);
    }

    // 像素级点击范围：窗口蒙版 = 当前帧不透明区域 ∪ 气泡 ∪ 影子（透明处点击穿透到底下窗口）
    void updateClickMask(double t, double hop) {
        if (++m_maskTick % 2) return; // 隔帧刷新，省一半开销
        QRegion r;
        if (!m_spPix.isEmpty()) {
            const int idx = qBound(0, m_spIdx, m_spPix.size() - 1);
            const QImage& fr0 = m_spPix[idx];
            const QImage fr = fr0.format() == QImage::Format_ARGB32
                                  ? fr0 : fr0.convertToFormat(QImage::Format_ARGB32);
            const int fw = fr.width(), fh = fr.height();
            QRegion frame;
            // 逐行扫不透明段（alpha>10，含半透明描边），每段水平 ±3、垂直 ±2 膨胀——
            // 严格贴 alpha 会把发丝/柔边漏成点击穿透（视觉角色比可点区大一圈）
            for (int y = 0; y < fh; ++y) {
                const QRgb* src = reinterpret_cast<const QRgb*>(fr.constScanLine(y));
                int run = -1;
                for (int x = 0; x <= fw; ++x) {
                    const bool op = x < fw && qAlpha(src[x]) > 10;
                    if (op && run < 0) run = x;
                    else if (!op && run >= 0) {
                        frame += QRect(run - 3, y - 2, (x - run) + 6, 5);
                        run = -1;
                    }
                }
            }
            // 帧画在窗口 (0, hop-2) 处（水平铺满整窗），蒙版跟随跳动偏移
            r += frame.translated(0, height() - 2 - fh + int(hop));
            r += QRegion(QRect(width() / 2 - int(width() * 0.22), height() - 12,
                               int(width() * 0.44), 12)); // 影子
        } else {
            const QPixmap& pm = m_dir < 0 ? m_charFlip[m_sizeIdx] : m_char[m_sizeIdx];
            if (!pm.mask().isNull())
                r += QRegion(pm.mask()).translated(width() / 2 - pm.width() / 2,
                                                   height() - 6 - pm.height());
            else
                r += QRegion(rect());
        }
        if (t < m_bubbleUntil && !m_bubble.isEmpty()) {
            QFont f = font();
            f.setPixelSize(12);
            QFontMetrics fm(f);
            const int tw = fm.horizontalAdvance(m_bubble) + 20;
            r += QRegion(width() / 2 - tw / 2, 8, tw, 26 + 8);
        }
        setMask(r);
    }

    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton) {
            m_dragging = true;
            m_moved = false;
            m_thrown = false;
            // 不清 m_placed：单击不能把悬空放置的桌宠按回地面；拖拽松手时会重新判定
            m_pressPos = e->globalPosition().toPoint();
            m_dragOff = m_pressPos - pos();
            m_samples.clear();
            m_samples.append({m_t0.elapsed(), m_pressPos});
        }
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        if (!m_dragging) return;
        QPoint gp = e->globalPosition().toPoint();
        QPoint np = gp - m_dragOff;
        if ((np - pos()).manhattanLength() > 10) m_moved = true; // 阈值放宽，鼠标轻抖不误判为拖拽
        if (m_moved) {
            if (!m_dragAnimOn) { // 进入拖拽：悬空反馈动画
                m_dragAnimOn = true;
                playLoop("被鼠标拖拽悬空反馈");
            }
            m_falling = false;
            move(np);
        }
        m_samples.append({m_t0.elapsed(), gp});
        while (m_samples.size() > 2 && m_t0.elapsed() - m_samples.first().first > 250)
            m_samples.removeFirst();
    }

    void mouseReleaseEvent(QMouseEvent* e) override {
        if (e->button() != Qt::LeftButton) return;
        bool wasDragAnim = m_dragAnimOn;
        m_dragging = false;
        m_dragAnimOn = false;
        if (m_moved) {
            double vx = 0, vy = 0;
            if (m_samples.size() >= 2) {
                qint64 t0 = m_samples.first().first;
                qint64 t1 = m_samples.last().first;
                if (t1 - t0 > 20) {
                    vx = double(e->globalPosition().toPoint().x() - m_samples.first().second.x()) / double(t1 - t0);
                    vy = double(e->globalPosition().toPoint().y() - m_samples.first().second.y()) / double(t1 - t0);
                }
            }
            double speed = std::sqrt(vx * vx + vy * vy);
            if (!m_hover && speed > 3.2) { // 只有真用力甩才触发抛掷（原 0.7 导致放置必须等手完全静止）
                double cap = 4.5;
                if (speed > cap) {
                    vx *= cap / speed;
                    vy *= cap / speed;
                }
                m_vx = vx;
                m_vy = vy;
                m_thrown = true;
                m_placed = false;
                m_bounces = 0;
                m_spOneShot = false;
                return;
            }
            // 直接放置（对齐 dsh-pet）：松手即停在原位，无需等手静止
            m_thrown = false;
            m_falling = false;
            m_vx = 0;
            m_vy = 0;
            QRect agl = QGuiApplication::primaryScreen()->availableGeometry();
            m_placed = (y() + height() < agl.bottom() - 3); // 悬空放置才标记
        } else {
            clickRespond();
        }
    }

    void contextMenuEvent(QContextMenuEvent* e) override {
        QMenu menu(this);
        QAction* showMain = menu.addAction("显示主程序");
        QMenu* actMenu = menu.addMenu("动作");
        buildActionMenu(actMenu);
        menu.addSeparator();
        QAction* walk = menu.addAction("自动走动");
        walk->setCheckable(true);
        walk->setChecked(m_walkEnabled);
        QAction* hoverAct = menu.addAction("悬停模式（放到空中不坠落）");
        hoverAct->setCheckable(true);
        hoverAct->setChecked(m_hover);
        QAction* speakAct = menu.addAction("自动说话");
        speakAct->setCheckable(true);
        speakAct->setChecked(m_autoSpeak);
        QAction* autoAct = menu.addAction("自动播放动作");
        autoAct->setCheckable(true);
        autoAct->setChecked(m_autoAct);
        QMenu* sizeMenu = menu.addMenu("大小");
        QAction* szAct[3];
        const char* szName[3] = {"小 (240)", "中 (320)", "大 (420)"};
        for (int i = 0; i < 3; ++i) {
            szAct[i] = sizeMenu->addAction(szName[i]);
            szAct[i]->setCheckable(true);
            szAct[i]->setChecked(m_sizeIdx == i && m_customW <= 0);
        }
        QAction* custSzAct = sizeMenu->addAction("自定义大小…");
        custSzAct->setCheckable(true);
        custSzAct->setChecked(m_customW > 0);
        QAction* autostart = menu.addAction("下次启动仍显示");
        autostart->setCheckable(true);
        autostart->setChecked(m_autoStart);
        menu.addSeparator();
        QAction* authorAct = menu.addAction("关于原作者 · dsh-pet");
        authorAct->setStatusTip("https://github.com/PC2005-cloud/dsh-pet");
        QAction* quitPet = menu.addAction("退出桌宠");
        QAction* chosen = menu.exec(e->globalPos());
        if (!chosen) return;
        if (chosen == showMain) {
            if (onShowMain) onShowMain();
        } else if (chosen == authorAct) {
            QDesktopServices::openUrl(QUrl("https://github.com/PC2005-cloud/dsh-pet"));
        } else if (chosen == walk) {
            m_walkEnabled = walk->isChecked();
            saveSettings();
        } else if (chosen == hoverAct) {
            m_hover = hoverAct->isChecked();
            if (m_hover) {
                m_thrown = false;
                m_falling = false;
                speak("悬浮模式开启，把我放到哪儿我就在哪儿～");
            } else if (y() + height() < QGuiApplication::primaryScreen()->availableGeometry().bottom()) {
                m_falling = true; // 关掉悬空：自然坠落回地面
                m_placed = false;
            }
            saveSettings();
        } else if (chosen == speakAct) {
            m_autoSpeak = speakAct->isChecked();
            saveSettings();
        } else if (chosen == autoAct) {
            m_autoAct = autoAct->isChecked();
            saveSettings();
        } else if (chosen == autostart) {
            m_autoStart = autostart->isChecked();
            saveSettings();
        } else if (chosen == quitPet) {
            saveSettings();
            close();
        } else if (chosen == custSzAct) {
            QDialog dlg(this);
            dlg.setWindowTitle("自定义大小");
            auto* v = new QVBoxLayout(&dlg);
            v->addWidget(new QLabel("桌宠宽度（180 - 520 px，16:9 帧自动适配）：", &dlg));
            auto* sl = new QSlider(Qt::Horizontal, &dlg);
            sl->setRange(180, 520);
            sl->setValue(m_customW > 0 ? m_customW : (m_sizeIdx == 0 ? 240 : m_sizeIdx == 1 ? 320 : 420));
            auto* valLab = new QLabel(QString::number(sl->value()) + " px", &dlg);
            QObject::connect(sl, &QSlider::valueChanged, &dlg, [valLab](int v2) {
                valLab->setText(QString::number(v2) + " px");
            });
            v->addWidget(sl);
            v->addWidget(valLab);
            auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
            QObject::connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
            QObject::connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
            v->addWidget(bb);
            if (dlg.exec() == QDialog::Accepted) {
                m_customW = sl->value();
                applySize();
                saveSettings();
            }
            return;
        } else {
            for (int i = 0; i < 3; ++i) {
                if (chosen == szAct[i]) {
                    m_sizeIdx = i;
                    m_customW = 0;
                    applySize();
                    if (m_hover) {
                        QRect agc = QGuiApplication::primaryScreen()->availableGeometry();
                        move(x(), qBound(agc.top(), qMin(y() + height(), agc.bottom() + 1) - height(), agc.bottom() - height() + 1));
                    } else {
                        settleToGround();
                    }
                    saveSettings();
                    break;
                }
            }
        }
    }

private:
    // ---------- 帧动画素材（dsh-pet 提取） ----------
    struct SpriteAnim {
        qreal fps = 20.0;
        QStringList files;
    };
    QMap<QString, SpriteAnim> m_anims;
    QStringList m_customClips; // 自定义模型片段名（菜单「自定义动作」+ 适配源）

    // 素材诊断日志（中文路径/webp 插件问题曾导致动画静默回落到立绘）
    static void petLog(const QString& line) {
        QFile lg(QCoreApplication::applicationDirPath() + "/pet.log");
        if (lg.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
            lg.write((QDateTime::currentDateTime().toString("MM-dd hh:mm:ss ") + line + "\n").toUtf8());
    }

    void loadSprites() {
        QDir base = QDir(QCoreApplication::applicationDirPath());
        QStringList roots = {base.filePath("pet-frames"),
                             base.filePath("../pet-frames")};
        for (const QString& root : roots) {
            QFile f(root + "/manifest.json");
            if (!f.open(QIODevice::ReadOnly)) continue; // 关键：先 open 再读
            QByteArray raw = f.readAll();
            f.close();
            QJsonDocument doc = QJsonDocument::fromJson(raw);
            QJsonObject o = doc.object();
            for (auto it = o.begin(); it != o.end(); ++it) {
                QJsonObject a = it.value().toObject();
                SpriteAnim sa;
                sa.fps = a.value("fps").toDouble(20.0);
                for (const QJsonValue& v : a.value("frames").toArray())
                    sa.files << root + "/" + v.toString();
                m_anims.insert(it.key(), sa);
            }
            if (!m_anims.isEmpty()) {
                petLog(QString("loaded anims=%1 bytes=%2 root=%3").arg(m_anims.size()).arg(raw.size()).arg(root));
                break;
            }
        }
        if (m_anims.isEmpty())
            petLog("loaded anims=0 (manifest missing or unparsable)");
        applyCustomModel();
    }

    // ---------- 自定义桌宠模型（ToolBox/custom_pet：model.json + <clip>/NNN.webp） ----------
    // 混合运行：映射过的内部动作换成自定义片段，未映射的保持默认帧，保证桌宠永远可用。
    void applyCustomModel() {
        m_customClips.clear();
        const QJsonObject m = ls::customModel();
        if (!m.value("enabled").toBool()) return;
        const QString base = ls::customPetDir() + "/";
        QMap<QString, SpriteAnim> custom;
        const QJsonObject clips = m.value("clips").toObject();
        for (auto it = clips.begin(); it != clips.end(); ++it) {
            const QJsonObject co = it.value().toObject();
            SpriteAnim sa;
            sa.fps = co.value("fps").toDouble(24.0);
            for (const QJsonValue& v : co.value("frames").toArray())
                sa.files << base + v.toString();
            if (!sa.files.isEmpty()) custom.insert(it.key(), sa);
        }
        if (custom.isEmpty()) return;
        m_customClips = custom.keys();
        for (auto it = custom.begin(); it != custom.end(); ++it)
            m_anims.insert(it.key(), it.value()); // 片段本身也可在动作菜单直接点播
        const QJsonObject map = m.value("map").toObject();
        auto bind = [&](const char* role, const QStringList& canonical) {
            const QString clip = map.value(role).toString();
            if (clip.isEmpty() || !custom.contains(clip)) return;
            for (const QString& c : canonical) m_anims.insert(c, custom.value(clip));
        };
        bind("idle", {"待机呼吸休闲"});
        bind("walk", {"原地漂浮踏步", "螃蟹走路", "原地左转奔跑"});
        bind("sleep", {"原地小憩沉眠"});
        bind("drag", {"被鼠标拖拽悬空反馈"});
        bind("feed", {"大口吃零食"});
        QStringList clicks;
        for (auto it = m_anims.begin(); it != m_anims.end(); ++it)
            if (it.key().startsWith("点击回应-")) clicks << it.key();
        bind("click", clicks);
    }

    bool playLoop(const QString& action) { return playSprite(action, true); }

    QSize targetFrameSize() const {
        int tw = qMin(420, qRound(width() * devicePixelRatioF())); // 素材按大档原生 420x236 提取，1:1 直出
        return QSize(tw, tw * 9 / 16);
    }

    void cacheInsert(const QString& action, QList<QImage> frames) {
        m_frameCache.insert(action, std::move(frames));
        m_cacheOrder.removeAll(action);
        m_cacheOrder.append(action);
        while (m_cacheOrder.size() > 3) // LRU：留最近 3 个动作（大档约 285MB / 中档 165MB 上限）
            m_frameCache.remove(m_cacheOrder.takeFirst());
    }

    void swapIn(const QString& action, bool loop) {
        m_spPix = m_frameCache.value(action);
        m_spFps = m_anims.value(action).fps;
        m_spLoop = loop;
        m_spIdx = 0;
        m_spAcc = 0;
        m_spName = action;
        m_spMode = true;
    }

    // 播放请求：缓存命中立即切换；未命中则后台线程解码（期间保持当前动画，UI 不冻结），
    // 帧到位后若该动作仍是待播目标则自动换上。点击回应的 Q弹/气泡等即时反馈不依赖帧到位。
    bool playSprite(const QString& action, bool loop) {
        if (!m_anims.contains(action)) {
            if (!m_loggedFail.contains(action)) {
                m_loggedFail.insert(action);
                petLog("play fail(no anim): " + action);
            }
            return false;
        }
        if (!loop) m_walking = false; // 单次动作原地表演
        if (m_frameCache.contains(action)) {
            m_pendingAction.clear();
            swapIn(action, loop);
            return true;
        }
        m_pendingAction = action;
        m_pendingLoop = loop;
        const QSize tsz = targetFrameSize();
        const QString key = action + "@" + QString::number(tsz.width());
        if (!m_loadingKeys.contains(key)) {
            m_loadingKeys.insert(key);
            const QStringList files = m_anims.value(action).files;
            QPointer<PetWindow> guard(this);
            QThreadPool::globalInstance()->start([guard, files, tsz, action, key] {
                QList<QImage> frames;
                for (const QString& f : files) {
                    QImage img(f);
                    if (img.isNull()) { // 通常是缺 qwebp.dll 插件
                        QMetaObject::invokeMethod(guard, [guard, f] {
                            if (guard) {
                                if (!guard->m_loggedFail.contains(f)) {
                                    guard->m_loggedFail.insert(f);
                                    petLog("play fail(decode): " + f);
                                }
                                guard->m_loadingKeys.clear(); // 解码坏掉时允许重试并让 pending 落回
                            }
                        }, Qt::QueuedConnection);
                        return;
                    }
                    if (img.size() != tsz)
                        img = img.scaled(tsz, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                    frames << std::move(img);
                }
                QMetaObject::invokeMethod(guard, [guard, action, key, tsz, frames = std::move(frames)]() mutable {
                    if (!guard) return;
                    guard->m_loadingKeys.remove(key);
                    if (tsz != guard->targetFrameSize()) return; // 加载期间换了档位，丢弃
                    guard->cacheInsert(action, std::move(frames));
                    if (guard->m_pendingAction == action)
                        guard->swapIn(action, guard->m_pendingLoop);
                }, Qt::QueuedConnection);
            });
        }
        return true;
    }

    // 按分类组织动作菜单（覆盖全部素材；余额/工作状态在 dsh-pet 为事件触发，这里也可点播）
    void buildActionMenu(QMenu* actMenu) {
        struct Cat { const char* title; QStringList keys; };
        const Cat cats[] = {
            {"小动作", {"悠闲哼歌", "超大伸懒腰", "原地敲击桌面互动", "原地重力下蹲压缩", "哈欠连天",
                        "原地小憩沉眠", "女仆屈膝礼仪", "被吓一跳", "小幅度原地360度旋转展示",
                        "偷吃零食被抓住", "用鲸鱼尾巴拍打地面", "打瞌睡被惊醒", "照镜子", "东张西望",
                        "整体换装试色", "轻快记录", "写代码", "摇扇纳凉", "晨间刷牙"}},
            {"玩耍", {"原地专心玩魔方", "原地蹲下玩玩具汽车", "鲸鱼吐泡泡特效", "原地跳跃抓碎头顶物品",
                      "玩游戏气急败坏", "玩水枪", "小提琴演奏", "蓝鲸现世", "优雅女仆舞", "轻快摇摆舞",
                      "可爱宅舞", "吹气球", "动物环绕", "放风筝", "拆礼物", "变鸽子", "扑克魔术",
                      "抽陀螺", "吹笛子", "蝴蝶蜜蜂环绕头顶开花", "撸猫", "凭空生花", "骑木马",
                      "三球抛接", "踢毽子", "下五子棋", "荡秋千"}},
            {"吃什么", {"吃白饭", "大口吃零食", "吃Token", "吃早餐", "吃午餐", "吃晚餐",
                        "吃冰淇淋融化", "吃大闸蟹", "吃糖葫芦", "吃长寿面", "吃西瓜", "涮火锅"}},
            {"时节", {"被落叶淹没", "中秋赏月吃月饼", "堆雪人", "放烟花", "吃粽子", "吃年糕",
                      "吃青团", "吃腊八粥", "吃重阳糕", "收红包", "写福字", "穿针乞巧", "舞狮头",
                      "讨糖南瓜灯", "插茱萸赏菊", "放河灯", "萌化小幽灵", "装点圣诞树", "放孔明灯",
                      "吃汤圆", "吃饺子"}},
            {"碎碎念与文字", {"是啊，吃什么", "深度思考碎碎念", "碎碎念-擦桌碎碎念",
                              "碎碎念-发呆碎碎念", "碎碎念-对屏碎碎念"}},
            {"余额", {"余额-分文不剩", "余额-数金皱眉", "余额-袋空如洗",
                      "余额-金袋叮当", "余额-钱袋如常", "余额-钱袋满溢"}},
            {"工作状态", {"工作状态-原地踱步张望", "工作状态-垂头叹气冒汗", "工作状态-忙碌点按",
                          "工作状态-思考冒泡", "工作状态-清点归档", "工作状态-雀跃庆祝"}},
            {"移动与转向", {"原地漂浮踏步", "螃蟹走路", "原地左转奔跑"}},
            {"互动", {"点击回应-开心跃动", "点击回应-害羞惊讶", "点击回应-傲娇生气",
                      "点击回应-挠痒咯咯笑", "点击回应-元气挥手", "被鼠标拖拽悬空反馈"}},
        };
        for (const Cat& c : cats) {
            QMenu* sub = actMenu->addMenu(c.title);
            for (const QString& a : c.keys) {
                if (!m_anims.contains(a)) continue;
                QAction* act = sub->addAction(a);
                connect(act, &QAction::triggered, this, [this, a] {
                    playManual(a); // 单次播放，播完回待机
                });
            }
        }
        // 自定义模型片段（导入后可直接点播）
        if (!m_customClips.isEmpty()) {
            QMenu* cm = actMenu->addMenu("自定义动作");
            for (const QString& c : m_customClips) {
                QAction* a = cm->addAction(c);
                connect(a, &QAction::triggered, this, [this, c] { playManual(c); });
            }
        }
        // 未归类的动作兜底
        QMenu* other = actMenu->addMenu("更多");
        for (auto it = m_anims.begin(); it != m_anims.end(); ++it) {
            bool known = false;
            for (const Cat& c : cats)
                if (c.keys.contains(it.key())) { known = true; break; }
            if (!known) {
                QAction* act = other->addAction(it.key());
                connect(act, &QAction::triggered, this, [this, a = it.key()] {
                    playManual(a);
                });
            }
        }
        if (other->isEmpty()) delete other;
    }

    // ---------- 帧动画状态 ----------
    QList<QImage> m_spPix;
    int m_maskTick = 0;
    QHash<QString, QList<QImage>> m_frameCache; // LRU 帧缓存（动作名 → 已解码帧，QImage 可跨线程解码）
    QStringList m_cacheOrder;
    QSet<QString> m_loadingKeys; // 在途后台加载（动作@宽度）
    QString m_pendingAction;     // 待切换动作（帧到位时仍是它才换上）
    bool m_pendingLoop = false;
    qreal m_spFps = 20.0;
    int m_spIdx = 0;
    double m_spAcc = 0;
    bool m_spLoop = true;
    bool m_spMode = false;   // 当前用帧动画渲染
    bool m_spOneShot = false; // 单次播放（结束回待机）
    bool m_dragAnimOn = false;
    QString m_spName;
    QSet<QString> m_loggedFail;

    // 点击回应（随机五选一，同 dsh-pet）
    void clickRespond() {
        const QStringList clicks = {"点击回应-开心跃动", "点击回应-害羞惊讶",
                                    "点击回应-傲娇生气", "点击回应-挠痒咯咯笑",
                                    "点击回应-元气挥手"};
        QStringList avail;
        for (const QString& c : clicks)
            if (m_anims.contains(c)) avail << c;
        if (avail.isEmpty()) { m_bounceUntil = m_t0.elapsed() + 620; return; }
        QString pick = avail[QRandomGenerator::global()->bounded(avail.size())];
        m_squashUntil = m_t0.elapsed() + 170; // 即时 Q 弹反馈，不等帧解码
        m_bounceUntil = m_t0.elapsed() + 620;
        playManual(pick);
        m_bubble = QString::fromUtf8(u8"\u8036\uff01"); // 默认
        if (pick.contains("开心")) m_bubble = "耶！开心！";
        else if (pick.contains("害羞")) m_bubble = "呀！！好害羞…";
        else if (pick.contains("傲娇")) m_bubble = "哼！生气了！";
        else if (pick.contains("挠痒")) m_bubble = "咯咯咯～好痒！";
        else if (pick.contains("挥手")) m_bubble = "你好呀～！";
        m_bubbleUntil = m_t0.elapsed() + 2400;
    }

    // ---------- 状态机 ----------
    void enterIdle() {
        m_walking = false;
        m_spOneShot = false; // 回循环待机必须清单次标志（one-shot 播放中途切 loop 会残留，卡死自动排程）
        if (!m_cloudWork.isEmpty() && playLoop(m_cloudWork)) return; // 打工中：循环任务动画
        if (m_cloudSleep && playLoop("原地小憩沉眠")) return; // 云养睡觉：待机姿态换成打盹
        if (playLoop("待机呼吸休闲")) return;
        m_spMode = false;
    }

    void startWalk() {
        QRect ag = QGuiApplication::primaryScreen()->availableGeometry();
        m_dir = QRandomGenerator::global()->bounded(2) ? 1 : -1;
        // 三种步态：漂浮(短) / 螃蟹(中) / 奔跑(长)
        const QStringList moves = {"原地漂浮踏步", "螃蟹走路", "原地左转奔跑"};
        QStringList avail;
        for (const QString& mv : moves)
            if (m_anims.contains(mv)) avail << mv;
        QString mv = avail.isEmpty() ? QString() : avail[QRandomGenerator::global()->bounded(avail.size())];
        int dist = 60 + QRandomGenerator::global()->bounded(mv == "原地左转奔跑" ? 260 : 180);
        int target = x() + m_dir * dist;
        m_targetX = qBound(ag.left(), target, ag.right() - width());
        if (m_targetX == x()) { m_idleUntil = m_t0.elapsed() + 4000; return; }
        m_walking = true;
        m_speed = mv == "原地左转奔跑" ? 2 : 1; // px per 16.7ms
        if (!playLoop(mv)) m_spMode = false;    // 有素材播走路帧，否则立绘跳动
        else m_dir = m_dir;                     // 帧动画自带朝向
    }

    void tick() {
        double t = m_t0.elapsed();
        double dt = qBound(1.0, t - m_lastTick, 100.0);
        m_lastTick = t;
        QRect ag = QGuiApplication::primaryScreen()->availableGeometry();
        int groundY = ag.bottom() - height() + 1;

        if (m_dragging) { update(); return; }

        if (m_thrown && !m_hover) { // 抛掷物理（gravity 1400 / restitution 0.78 / friction 2.5，对齐 dsh-pet）
            m_vy += 0.0014 * dt;               // 1400 px/s²
            int nx = x() + int(m_vx * dt);
            int ny = y() + int(m_vy * dt);
            if (nx < ag.left()) {
                nx = ag.left();
                m_vx = -m_vx * 0.78;
                ++m_bounces;
            }
            if (nx > ag.right() - width()) {
                nx = ag.right() - width();
                m_vx = -m_vx * 0.78;
                ++m_bounces;
            }
            if (ny < ag.top()) { // 天花板反弹
                ny = ag.top();
                m_vy = -m_vy * 0.78;
            }
            if (ny >= groundY) {
                ny = groundY;
                if (m_vy > 0.3) {
                    m_vy = -m_vy * 0.78;
                    ++m_bounces;
                    m_squashUntil = t + 170;
                } else {
                    m_vy = 0;
                    m_vx *= std::exp(-2.5 * dt / 1000.0); // groundFriction 2.5/s
                }
            }
            move(nx, ny);
            if (ny >= groundY - 1 && std::abs(m_vy) < 0.25 && std::abs(m_vx) < 0.05) {
                m_thrown = false;
                m_bubble = m_bounces >= 2 ? QString("晕了……别扔我啦！") : QString("哎呀！");
                m_bubbleUntil = t + 2200;
                m_bounces = 0;
                m_nextAct = t + 6000;
                m_nextSpeak = t + 5000;
                enterIdle();
            }
            update();
            return;
        }

        if (m_falling && !m_hover) { // 轻放后自然下落
            int ny = y() + int(m_fallV * dt / 16.7);
            m_fallV += 0.18 * dt;
            if (ny >= groundY) {
                ny = groundY;
                m_falling = false;
                m_bounceUntil = t + 300;
                m_squashUntil = t + 150;
            }
            move(x(), ny);
        }

        // 帧动画推进（循环 / 单次结束回待机）：dt(ms)/1000 × fps = 本帧应推进的帧数
        if (!m_spPix.isEmpty()) {
            m_spAcc += dt * m_spFps / 1000.0;
            while (m_spAcc >= 1.0) {
                m_spAcc -= 1.0;
                ++m_spIdx;
            }
            if (m_spIdx >= m_spPix.size()) {
                if (m_spLoop) {
                    m_spIdx = 0;
                } else {
                    m_spOneShot = false;
                    enterIdle();
                }
            }
        }

        // 走动移动
        if (m_walking) {
            int nx = x() + int(m_dir * m_speed * dt / 16.7);
            if (m_dir > 0 ? nx >= m_targetX : nx <= m_targetX) {
                nx = m_targetX;
                m_walking = false;
                m_idleUntil = t + 3000 + QRandomGenerator::global()->bounded(5000);
                enterIdle();
            }
            nx = qBound(ag.left(), nx, ag.right() - width());
            move(nx, (m_hover || m_placed) ? y() : groundY);
        } else {
            // 待机排程：仅循环（待机）态调度，单次动作/打工任务播放中不被自动打断；
            // 云养睡觉是稳态：不闲聊、不做随机动作、不走路（沉眠循环不被打断）
            if (!m_spOneShot && m_cloudWork.isEmpty()) {
                if (m_autoSpeak && !m_cloudSleep && t > m_nextSpeak) {
                    speak(pickIdleTalk());
                    m_nextSpeak = t + 9000 + QRandomGenerator::global()->bounded(7000);
                }
                if (m_autoAct && !m_cloudSleep && t > m_nextAct) {
                    playRandomAction();
                }
                else if (m_walkEnabled && !m_cloudSleep && t > m_idleUntil && (m_hover || y() + height() >= groundY - 2)) startWalk(); // 直接放置在空中时不走动
            }
            if (y() + height() >= groundY - 1) m_placed = false; // 回到地面即清除放置态
            if (!m_hover && !m_placed && y() != groundY) move(x(), groundY);
        }
        update();
    }

    void applySize() {
        // 帧动画素材为 16:9（源 640x360），立绘回落为竖版
        m_frameCache.clear(); // 尺寸变化后按新窗口尺寸重新缩放入缓存
        m_cacheOrder.clear();
        m_loadingKeys.clear();
        m_pendingAction.clear();
        m_spPix.clear();
        if (!m_anims.isEmpty()) {
            if (m_spMode && m_spOneShot) {
                m_spOneShot = false; // 换档打断单次动作，直接回新档待机（否则帧列表空窗会卡住状态机）
                enterIdle();
            } else if (m_spMode && !m_spName.isEmpty()) {
                playLoop(m_spName); // 待机/走路等循环动画立即按新档重载，否则旧档帧会一直画到重启
            }
        }
        if (!m_anims.isEmpty()) {
            const int w = m_customW > 0 ? m_customW : (m_sizeIdx == 0 ? 240 : m_sizeIdx == 1 ? 320 : 420);
            resize(w, w * 9 / 16);
        } else {
            int h = kHeights[m_sizeIdx];
            resize(m_char[m_sizeIdx].width() + 20, int(h * 1.35));
        }
    }

    void settleToGround() {
        QRect ag = QGuiApplication::primaryScreen()->availableGeometry();
        move(x(), ag.bottom() - height() + 1);
    }

    void saveSettings() {
        QJsonObject uiSt;
        uiSt.insert("x", x());
        uiSt.insert("y", y());
        uiSt.insert("size", m_sizeIdx);
        uiSt.insert("w", m_customW);
        uiSt.insert("walk", m_walkEnabled);
        uiSt.insert("hover", m_hover);
        uiSt.insert("speak", m_autoSpeak);
        uiSt.insert("autoact", m_autoAct);
        uiSt.insert("enabled", m_autoStart);
        ls::savePetUi(uiSt);
    }

    void speak(const QString& s) {
        m_bubble = s;
        m_bubbleUntil = m_t0.elapsed() + 2400;
    }

    QString pickIdleTalk() {
        QStringList lines;
        for (const QJsonValue& v : ls::petUi().value("talks").toArray()) {
            const QString t = v.toString().trimmed();
            if (!t.isEmpty()) lines << t; // 设置页自定义台词（每行一条），实时生效
        }
        if (lines.isEmpty()) {
            const char* defs[] = {"在忙什么呀～", "要不要休息一下", "工具箱里有好多工具哦",
                                  "陪你一起摸鱼～", "窗外天色真好", "无聊…陪我玩玩嘛",
                                  "记得保存文件哦", "嘿嘿，我最可爱"};
            for (const char* d : defs) lines << QString::fromUtf8(d);
        }
        return lines[QRandomGenerator::global()->bounded(lines.size())];
    }

    // 手动点播（右键菜单/点击回应）：单次播放 + 顺延自动排程，避免播到一半被打断
    bool playManual(const QString& a) {
        if (!playSprite(a, false)) return false;
        m_spOneShot = true;
        qint64 now = m_t0.elapsed();
        m_nextAct = now + 12000 + QRandomGenerator::global()->bounded(9000);
        m_nextSpeak = now + 9000 + QRandomGenerator::global()->bounded(7000);
        m_idleUntil = now + 3000 + QRandomGenerator::global()->bounded(5000);
        return true;
    }

    // 自动播放动作：按 dsh-pet 分类权重随机（小动作 20 / 玩耍 20 / 吃什么 16 / 时节 14 / 文字 10）
    void playRandomAction() {
        struct Cat { int weight; QStringList keys; };
        static const Cat cats[] = {
            {20, {"悠闲哼歌", "超大伸懒腰", "原地敲击桌面互动", "原地重力下蹲压缩", "哈欠连天",
                  "原地小憩沉眠", "女仆屈膝礼仪", "被吓一跳", "小幅度原地360度旋转展示",
                  "偷吃零食被抓住", "用鲸鱼尾巴拍打地面", "打瞌睡被惊醒", "照镜子", "整体换装试色",
                  "轻快记录", "写代码", "摇扇纳凉", "晨间刷牙"}},
            {20, {"原地专心玩魔方", "原地蹲下玩玩具汽车", "鲸鱼吐泡泡特效", "原地跳跃抓碎头顶物品",
                  "玩游戏气急败坏", "玩水枪", "小提琴演奏", "蓝鲸现世", "优雅女仆舞", "轻快摇摆舞",
                  "可爱宅舞", "吹气球", "动物环绕", "放风筝", "拆礼物", "变鸽子", "扑克魔术",
                  "抽陀螺", "吹笛子", "蝴蝶蜜蜂环绕头顶开花", "撸猫", "凭空生花", "骑木马",
                  "三球抛接", "踢毽子", "下五子棋", "荡秋千"}},
            {16, {"吃白饭", "大口吃零食", "吃Token", "吃早餐", "吃午餐", "吃晚餐", "吃冰淇淋融化",
                  "吃大闸蟹", "吃糖葫芦", "吃长寿面", "吃西瓜", "涮火锅"}},
            {14, {"被落叶淹没", "中秋赏月吃月饼", "堆雪人", "放烟花", "吃粽子", "吃年糕", "吃青团",
                  "吃腊八粥", "吃重阳糕", "收红包", "写福字", "穿针乞巧", "舞狮头", "讨糖南瓜灯",
                  "插茱萸赏菊", "放河灯", "萌化小幽灵", "装点圣诞树", "放孔明灯", "吃汤圆", "吃饺子"}},
            {10, {"是啊，吃什么", "深度思考碎碎念", "碎碎念-擦桌碎碎念",
                  "碎碎念-发呆碎碎念", "碎碎念-对屏碎碎念"}},
        };
        // 分类级权重抽取：无可用动作的分类不计入权重池（此前按可用动作数累加 acc，
        // 导致前 4 个小动作吃掉全部权重区间，其余分类永远轮不到）
        QStringList pools[5];
        int total = 0;
        for (int i = 0; i < 5; ++i) {
            for (const QString& k : cats[i].keys)
                if (m_anims.contains(k)) pools[i] << k;
            if (!pools[i].isEmpty()) total += cats[i].weight;
        }
        if (total == 0) { m_nextAct = m_t0.elapsed() + 15000; return; }
        int roll = QRandomGenerator::global()->bounded(total);
        int acc = 0;
        for (int i = 0; i < 5; ++i) {
            if (pools[i].isEmpty()) continue;
            acc += cats[i].weight;
            if (roll < acc) {
                const QString& k = pools[i][QRandomGenerator::global()->bounded(pools[i].size())];
                m_walking = false;
                playSprite(k, false); // 单次播放，播完回待机
                m_spOneShot = true;
                if (k.startsWith("吃")) speak("啊呜啊呜～");
                else if (k.contains("碎碎念")) speak("碎碎念中…");
                break;
            }
        }
        m_nextAct = m_t0.elapsed() + 12000 + QRandomGenerator::global()->bounded(9000);
    }

    static constexpr int kHeights[3] = {104, 144, 196};

    QPixmap m_char[3], m_charFlip[3];
    QElapsedTimer m_t0;
    double m_lastTick = 0;
    int m_sizeIdx = 1;
    int m_dir = 1;
    bool m_walkEnabled = true;
    bool m_hover = false;      // 悬停模式：忽略甩出，走动原地滑行
    bool m_placed = false;     // 悬空放置中（直接放置后不回落地面）
    int m_customW = 0;         // 自定义宽度（0=用三档预设；帧按窗口宽缩放，天然支持任意尺寸）
    bool m_autoSpeak = true;
    bool m_autoAct = true;
    bool m_autoStart = true;
    bool m_walking = false;
    bool m_falling = false;
    bool m_dragging = false;
    bool m_moved = false;
    bool m_thrown = false;
    bool m_cloudSleep = false; // 云养睡觉状态（待机姿态联动）
    QString m_cloudWork;       // 云养打工任务动画（非空时循环播放并门控自动行为）
    int m_speed = 1;
    int m_targetX = 0;
    double m_fallV = 2;
    double m_vx = 0, m_vy = 0;
    int m_bounces = 0;
    qint64 m_squashUntil = 0;
    qint64 m_idleUntil = 0;
    qint64 m_bubbleUntil = 0;
    qint64 m_bounceUntil = 0;
    qint64 m_nextSpeak = 0;
    qint64 m_nextAct = 0;
    QPoint m_pressPos;
    QPoint m_dragOff;
    QList<QPair<qint64, QPoint>> m_samples;
    QString m_bubble;
};

inline void playRemote(const QString& action, const QString& bubble) {
    if (s_activePet) s_activePet->remotePlay(action, bubble);
}
inline void setRemoteSleeping(bool on) {
    if (s_activePet) s_activePet->setCloudSleep(on);
}
inline void setRemoteWork(const QString& action) {
    if (s_activePet) s_activePet->setCloudWork(action);
}
inline void summonPet() {
    if (s_activePet) {
        s_activePet->show();
        s_activePet->raise();
        s_activePet->activateWindow();
    }
}
inline void reloadPetAssets() {
    if (s_activePet) s_activePet->reloadAssets();
}

} // namespace pet
