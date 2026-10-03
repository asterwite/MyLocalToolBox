// ToolBox Qt 客户端主窗口实现（纯本地，无网络依赖）
#include "mainwindow.h"
#include "theme.h"
#include "widgets.h"
#include "sysinfo.h"
#include "edgeproc.h"
#include "petwindow.h" // 云养联动：投喂/哄睡远程驱动桌宠动画
#include "localstore.h" // 积分/云养本地存储（exe\ToolBox\*.json）

#include <QApplication>
#include <QRandomGenerator>
#include <QClipboard>
#include <QDesktopServices>
#include <QDateTime>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QScrollArea>
#include <QSystemTrayIcon>
#include <QScrollBar>
#include <QSettings>
#include <QTextEdit>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QComboBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QFileDialog>
#include <QRegularExpression>
#include <QUuid>
#include <QCryptographicHash>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QScreen>
#include <QTextBrowser>
#include <QSpinBox>
#include <QDate>
#include <QDirIterator>
#include <QFileInfo>
#include <QKeySequence>
#include "shotoverlay.h"
#include "pickoverlay.h"
#include <QVBoxLayout>
#include <algorithm>
#include <QPushButton>
#include <QPainter>
#include <QColorDialog>
#include <QFileDialog>
#include <QSlider>
#include <QFile>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QComboBox>
#include <QProcess>
#include <QStandardPaths>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QButtonGroup>
#include <QStackedWidget>
#include <QKeySequenceEdit>
#include <QTreeWidget>
#include <QCheckBox>
#include <QTime>
#include <QDialog>
#include <windows.h>
#include <nlohmann/json.hpp>

// 应用元信息（编译日期/时间为宏注入，完整重新构建时自动刷新）
static const char* kQQGroup    = "963707322";

static const char* kBuildTime  = __TIME__;

// 根画布：同色系纵向渐变 + 主题色光晕 + 可选背景图片（页面/滚动区透明，透出此层）
class RootWidget : public QWidget {
public:
    RootWidget() {
        // 动态炫彩背景：色相缓慢流动，光晕漂移；设置里可关
        if (theme::bgAnimated()) {
            m_timer = new QTimer(this);
            connect(m_timer, &QTimer::timeout, this, [this] {
                m_phase += 0.03;
                update();
            });
            m_timer->start(33);
        }
    }

    void setAnimated(bool on) {
        theme::bgAnimated() = on;
        theme::save();
        if (on && !m_timer) {
            m_timer = new QTimer(this);
            connect(m_timer, &QTimer::timeout, this, [this] {
                m_phase += 0.03;
                update();
            });
        }
        if (on) m_timer->start(33);
        else m_timer->stop();
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const double t = m_phase;
        // 云白底色
        QLinearGradient base(0, 0, width(), height());
        base.setColorAt(0.0, QColor(0xff, 0xff, 0xff));
        base.setColorAt(1.0, QColor(0xed, 0xf1, 0xfa));
        p.fillRect(rect(), base);

        // 极光流：四个大光斑沿利萨如轨迹游走，色相持续旋转（辅助菜单风）
        struct Blob { double sx, sy, px, py, hueOff; };
        static const Blob blobs[] = {
            {0.30, 0.25, 0.45, 0.36, 0.00},
            {0.72, 0.22, 0.38, 0.47, 0.33},
            {0.24, 0.78, 0.42, 0.33, 0.55},
            {0.78, 0.74, 0.36, 0.44, 0.72},
        };
        for (const Blob& b : blobs) {
            const QPointF c(width() * (0.5 + 0.42 * std::sin(t * b.px + b.hueOff * 6.28)),
                            height() * (0.5 + 0.40 * std::cos(t * b.py + b.hueOff * 4.0)));
            QColor col = QColor::fromHsvF(std::fmod(t * 0.45 + b.hueOff, 1.0), 0.42, 0.97);
            col.setAlpha(46);
            QRadialGradient g(c, width() * 0.42);
            g.setColorAt(0.0, col);
            col.setAlpha(0);
            g.setColorAt(1.0, col);
            p.fillRect(rect(), g);
        }

        if (!theme::bgImagePath().isEmpty() && QFile::exists(theme::bgImagePath())) {
            QPixmap pm(theme::bgImagePath());
            if (!pm.isNull()) {
                p.setOpacity(theme::bgOpacity());
                p.drawPixmap(rect(), pm.scaled(size(), Qt::IgnoreAspectRatio,
                                               Qt::SmoothTransformation));
                p.setOpacity(1.0);
            }
        }
    }

    void showEvent(QShowEvent*) override {
        if (m_timer && theme::bgAnimated()) m_timer->start(33);
    }
    void hideEvent(QHideEvent*) override {
        if (m_timer) m_timer->stop(); // 窗口不可见时省电
    }

private:
    QTimer* m_timer = nullptr;
    double m_phase = 0.0;
};

static RootWidget* g_root = nullptr;

namespace tb {

using json = nlohmann::json;

// QSS 由 theme::qss() 按当前主题色生成

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    QSettings st("ToolBox", "ToolBoxQt");
    m_uid = st.value("uid", "").toString();
    if (m_uid.isEmpty()) {
        m_uid = "qt-" + QString::number(QRandomGenerator::global()->generate64(), 16);
        st.setValue("uid", m_uid);
    }

    auto* central = new RootWidget;
    g_root = central;
    central->setObjectName("root");
    auto* rootLayout = new QHBoxLayout(central);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    // ---------- 左侧导航（Fluent 风格：自绘导航按钮 + 悬停动画） ----------
    auto* side = new QWidget;
    side->setObjectName("side");
    side->setFixedWidth(216); // 208 放不下用户卡片第二行文字
    auto* sideLayout = new QVBoxLayout(side);
    sideLayout->setContentsMargins(12, 16, 12, 12);
    sideLayout->setSpacing(4);

    // 品牌行：Q 版徽章 + 名称 + 版本副标题
    auto* brandRow = new QHBoxLayout;
    brandRow->setContentsMargins(6, 2, 0, 2);
    brandRow->setSpacing(11);
    auto* brandIcon = new QLabel;
    brandIcon->setPixmap(QPixmap(":/res/char_badge256.png").scaled(
        38, 38, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    brandIcon->setFixedSize(38, 38);
    brandRow->addWidget(brandIcon);
    auto* brandText = new QVBoxLayout;
    brandText->setSpacing(1);
    auto* brand = new QLabel("我的工具箱");
    brand->setObjectName("brand");
    brandText->addWidget(brand);
    auto* brandVer = new QLabel("ToolBox · 桌面版");
    brandVer->setObjectName("dim");
    brandVer->setStyleSheet("font-size:10px;");
    brandText->addWidget(brandVer);
    brandRow->addLayout(brandText);
    brandRow->addStretch();
    sideLayout->addLayout(brandRow);
    sideLayout->addSpacing(12);
    auto* line = new QFrame;
    line->setObjectName("hline");
    line->setFixedHeight(1);
    sideLayout->addWidget(line);
    sideLayout->addSpacing(8);

    const struct { ui::NavButton::Icon icon; const char* name; int page; } nav[] = {
        {ui::NavButton::Home,     "我的工具箱", 0},
        {ui::NavButton::Tool,     "系统工具", 1},
        {ui::NavButton::Shop,     "效率生活", 2},
        {ui::NavButton::Chat,     "文本工具", 3},
        {ui::NavButton::Users,    "网络工具", 4},
        {ui::NavButton::Media,    "截图与媒体", 5},
        {ui::NavButton::Folder,   "文件工具", 6},
        {ui::NavButton::Paw,      "云养宠物", 7},
        {ui::NavButton::Todo,     "待办清单", 8},
        {ui::NavButton::Calendar, "日程", 9},
        {ui::NavButton::Habit,    "习惯打卡", 10},
        {ui::NavButton::Note,     "笔记", 11},
        {ui::NavButton::Vault,    "密码库", 12},
        {ui::NavButton::Gear,     "设置", 13},
    };
    const int navN = int(sizeof(nav) / sizeof(nav[0])); // 上界自适应，加导航项不用改这里
    for (int i = 0; i < navN; ++i) {
        auto* btn = new ui::NavButton(nav[i].icon, nav[i].name);
        sideLayout->addWidget(btn);
        m_navButtons.push_back(btn);
        m_navPages.push_back(nav[i].page);
        const int page = nav[i].page;
        connect(btn, &QPushButton::clicked, this, [this, page] { switchPage(page); });
    }
    sideLayout->addStretch();

    // 底部用户卡片
    auto* userCard = new QFrame;
    userCard->setObjectName("usercard");
    auto* ucLayout = new QHBoxLayout(userCard);
    ucLayout->setContentsMargins(10, 8, 10, 8);
    ucLayout->setSpacing(10);
    m_avatar = new ui::AvatarLabel;
    ucLayout->addWidget(m_avatar);
    auto* ucText = new QVBoxLayout;
    ucText->setSpacing(0);
    m_userLabel = new QLabel("本地用户");
    m_userLabel->setStyleSheet("font-size:13px; font-weight:bold; background:transparent;");
    ucText->addWidget(m_userLabel);
    m_statusLabel = new QLabel("数据保存在本机");
    m_statusLabel->setObjectName("dim");
    m_statusLabel->setStyleSheet("font-size:11px; background:transparent;");
    m_statusLabel->setToolTip("本地模式：所有数据保存在本机 ToolBox 文件夹，不上传任何服务器");
    ucText->addWidget(m_statusLabel);
    ucLayout->addLayout(ucText, 1);
    auto* chev = new QLabel("›");
    chev->setStyleSheet("color:#9aa0b0; font-size:18px; background:transparent;");
    ucLayout->addWidget(chev);
    sideLayout->addWidget(userCard);
    auto* aboutBtn = new QPushButton("关于 · 反馈");
    aboutBtn->setObjectName("flat");
    aboutBtn->setCursor(Qt::PointingHandCursor);
    sideLayout->addWidget(aboutBtn);
    connect(aboutBtn, &QPushButton::clicked, this, &MainWindow::showAbout);

    rootLayout->addWidget(side);

    // ---------- 页面栈 ----------
    m_stack = new QStackedWidget;
    rootLayout->addWidget(m_stack, 1);
    m_stack->addWidget(buildHomePage());      // 0
    m_stack->addWidget(buildSysToolsPage());  // 1
    m_stack->addWidget(buildLifeToolsPage()); // 2
    m_stack->addWidget(buildTextToolsPage()); // 3
    m_stack->addWidget(buildNetToolsPage());  // 4
    m_stack->addWidget(buildMediaToolsPage());// 5
    m_stack->addWidget(buildFileToolsPage()); // 6
    m_stack->addWidget(buildPetPage());       // 7
    m_stack->addWidget(buildTodoPage());      // 8
    m_stack->addWidget(buildSchedulePage());  // 9
    m_stack->addWidget(buildHabitPage());     // 10
    m_stack->addWidget(buildNotePage());      // 11
    m_stack->addWidget(buildVaultPage());     // 12
    m_settingsPage = buildSettingsPage();
    m_stack->addWidget(m_settingsPage);       // 13

    m_navButtons[0]->setChecked(true);

    // 云养页每分钟自动刷新（状态随时间衰减）
    connect(&m_petTimer, &QTimer::timeout, this, [this] {
        if (m_stack->currentIndex() == 7) refreshPet();
    });
    m_petTimer.start(60000);
    // 任务栏 1 秒节拍：进度条 + 到点自动结算
    connect(&m_taskTimer, &QTimer::timeout, this, &MainWindow::taskTick);
    m_taskTimer.start(1000);

    // 调试/直达参数：--tools 打开工具箱，--ledger 直达记账标签
    const QStringList args = QApplication::arguments();
    if (args.contains("--tools")) switchPage(1);  // 系统工具页
    if (args.contains("--pet")) switchPage(7);
    for (int i = 0; i < args.size(); ++i) {
        if (args[i] == "--edgeproc" && i + 1 < args.size()) m_edgeProc->setText(args[i + 1]);
        if (args[i] == "--edgecpu" && i + 1 < args.size()) m_edgeCpuThr->setValue(args[i + 1].toInt());
        if (args[i] == "--edgemem" && i + 1 < args.size()) m_edgeMemThr->setValue(args[i + 1].toDouble());
        if (args[i] == "--edgegap" && i + 1 < args.size()) m_edgeGap->setValue(args[i + 1].toInt());
        if (args.contains("--about"))
            QTimer::singleShot(0, this, [this] { showAbout(); });
        if (args[i] == "--edgewatch")
            QTimer::singleShot(0, this, [this] { if (m_edgeBtn) m_edgeBtn->click(); });
    }

    setCentralWidget(central);
    qApp->setStyleSheet(theme::qss());
    setMinimumSize(1000, 660);
    // 恢复上次窗口大小与位置（无记录或超出屏幕则用默认）
    {
        QSettings gs("ToolBox", "ToolBoxQt");
        const QRect saved = gs.value("ui/geometry").toRect();
        QRect ag = QGuiApplication::primaryScreen()->availableGeometry();
        if (saved.isValid() && ag.intersects(saved) && saved.width() >= minimumWidth()
            && saved.height() >= minimumHeight())
            setGeometry(saved);
        else
            resize(1180, 840); // 14 个导航项 + 底部卡片需要更高的侧栏
    }
    refreshHome();

    // 剪贴板历史：应用运行期间持续记录（去重，最多 100 条，随设置持久化）
    {
        QSettings cst("ToolBox", "ToolBoxQt");
        m_clipHistory = cst.value("clipHistory").toStringList();
    }
    connect(QApplication::clipboard(), &QClipboard::dataChanged, this, [this] {
        QString t = QApplication::clipboard()->text();
        if (t.isEmpty()) return;
        if (t.size() > 4000) t = t.left(4000) + "…"; // 超长截断，防注册表膨胀
        if (!m_clipHistory.isEmpty() && m_clipHistory.first() == t) return;
        m_clipHistory.removeAll(t);
        m_clipHistory.prepend(t);
        while (m_clipHistory.size() > 100) m_clipHistory.removeLast();
        QSettings cst("ToolBox", "ToolBoxQt");
        cst.setValue("clipHistory", m_clipHistory);
        if (m_clipList) renderClipList();
    });

    // 系统信息 2s 刷新（CPU/内存占用 + 开机时长）
    m_infoTimer.setParent(this);
    connect(&m_infoTimer, &QTimer::timeout, this, [this] {
        static sysinfo::CpuUsage cu;
        int pct = cu.sample();
        if (pct >= 0 && m_sysCpuBar) {
            m_sysCpuBar->setValue(pct);
            m_sysCpuPct->setText(QString::number(pct) + " %");
        }
        sysinfo::MemInfo mem = sysinfo::memory();
        if (m_sysRamBar) {
            m_sysRamBar->setValue(mem.usedPct());
            m_sysRamPct->setText(QString::number(mem.usedPct()) + " %");
            m_sysRamDetail->setText(QString("已用 %1 GB / 共 %2 GB · 可用 %3 GB")
                                        .arg(QString::number(mem.totalGB - mem.freeGB, 'f', 1))
                                        .arg(QString::number(mem.totalGB, 'f', 1))
                                        .arg(QString::number(mem.freeGB, 'f', 1)));
        }
        if (m_sysUptime) m_sysUptime->setText(sysinfo::uptime());
    });
    m_infoTimer.start(2000);

    // ---------- 托盘：X 关闭主窗口 = 隐藏到托盘，退出走托盘菜单 ----------
    m_tray = new QSystemTrayIcon(QIcon(":/res/char_badge256.png"), this);
    m_tray->setToolTip("我的工具箱 · 本地版");
    auto* trayMenu = new QMenu(this);
    trayMenu->addAction("显示主程序", this, [this] {
        showNormal(); raise(); activateWindow();
    });
    trayMenu->addAction("召唤桌宠", this, [] { pet::summonPet(); });
    trayMenu->addSeparator();
    trayMenu->addAction("退出", this, [] { qApp->quit(); });
    m_tray->setContextMenu(trayMenu);
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason r) {
        if (r == QSystemTrayIcon::Trigger || r == QSystemTrayIcon::DoubleClick) {
            if (isVisible()) hide(); else { showNormal(); raise(); activateWindow(); }
        }
    });
    m_tray->show();

    // ---------- 健康提醒节拍：久坐/喝水，桌宠在场走气泡+动作，隐藏时走托盘通知 ----------
    m_remindSitLast = m_remindWaterLast = QDateTime::currentMSecsSinceEpoch();
    connect(&m_remindTimer, &QTimer::timeout, this, [this] {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const QJsonObject u = ls::petUi();
        const bool petUp = pet::s_activePet && pet::s_activePet->isVisible();
        // 桌宠被哄睡时不打断睡眠（间隔顺延，醒来后到点照常提醒）
        if (petUp && ls::petState().value("sleeping").toBool()) return;
        if (u.value("remindSit").toBool()) {
            const qint64 iv = qint64(u.value("remindSitMin").toInt(45)) * 60000;
            if (iv > 0 && now - m_remindSitLast >= iv) {
                m_remindSitLast = now;
                if (petUp) pet::playRemote("超大伸懒腰", "久坐啦～起来伸个懒腰吧！");
                else m_tray->showMessage("健康提醒", "久坐啦～起来活动活动！",
                                         QSystemTrayIcon::Information, 4000);
            }
        }
        if (u.value("remindWater").toBool()) {
            const qint64 iv = qint64(u.value("remindWaterMin").toInt(60)) * 60000;
            if (iv > 0 && now - m_remindWaterLast >= iv) {
                m_remindWaterLast = now;
                if (petUp) pet::playRemote("鲸鱼吐泡泡特效", "该喝水啦～咕嘟咕嘟！");
                else m_tray->showMessage("健康提醒", "该喝水啦～补充水分！",
                                         QSystemTrayIcon::Information, 4000);
            }
        }
    });
    m_remindTimer.start(30000);

    // ---------- 每日提醒节拍（日程页，30s 粒度，lastFired 防重） ----------
    connect(&m_schedTimer, &QTimer::timeout, this, &MainWindow::scheduleTick);
    m_schedTimer.start(30000);

    // ---------- 全局快捷键（组合键可在设置页修改） ----------
    applyGlobalHotkeys();
}

void MainWindow::closeEvent(QCloseEvent* e) {
    // X = 隐藏到托盘（首次提示一次），进程与桌宠继续运行
    QSettings gs("ToolBox", "ToolBoxQt");
    gs.setValue("ui/geometry", geometry());
    hide();
    QSettings st("ToolBox", "ToolBoxQt");
    if (!st.value("tray/hintShown").toBool()) {
        st.setValue("tray/hintShown", true);
        m_tray->showMessage("我的工具箱仍在运行",
                            "已最小化到托盘，双击托盘图标或右键可恢复；退出请用托盘菜单。",
                            QSystemTrayIcon::Information, 4000);
    }
    e->ignore();
}

void MainWindow::switchPage(int index) {
    // 离开工具箱页自动停止连点器，避免合成点击继续落在其他页面输入框上
    if (index != 1 && m_clickerTimer && m_clickerTimer->isActive()) {
        m_clickerTimer->stop();
        if (m_clickerBtn) m_clickerBtn->setText("开始连点");
    }
    m_stack->setCurrentIndex(index);
    for (int i = 0; i < (int)m_navButtons.size(); ++i)
        m_navButtons[i]->setChecked(i < m_navPages.size() && m_navPages[i] == index);
    ui::fadeIn(m_stack->widget(index)); // 页面切换淡入
    if (index == 0) refreshHome();
    if (index == 1) refreshLedger();
    if (index == 2) refreshPet();
}

// ---------------- 网络 ----------------

QString MainWindow::myUid() const { return m_uid; }

void MainWindow::copyHwid() {
    QApplication::clipboard()->setText(m_uid);
}

void MainWindow::applyGlobalHotkeys() {
    QSettings st("ToolBox", "ToolBoxQt");
    auto reg = [this, &st](ui::HotkeyFilter** slot, UINT id, const QString& key,
                      std::function<void()> fn) {
        if (!*slot) {
            *slot = new ui::HotkeyFilter;
            (*slot)->id = id;
            QCoreApplication::instance()->installNativeEventFilter(*slot);
        }
        HWND hwnd = (HWND)winId();
        UnregisterHotKey(hwnd, id);
        const QKeySequence ks(st.value(key).toString());
        UINT mods = 0, vk = 0;
        if (ui::keySeqToNative(ks, &mods, &vk) && RegisterHotKey(hwnd, id, mods, vk))
            (*slot)->onHotkey = std::move(fn);
        else
            (*slot)->onHotkey = nullptr; // 无效或被占用：不响应，设置页会显示
    };
    reg(&m_hkShot, 2, "hotkeys/shot", [this] { startShot(0); });
    reg(&m_hkPet, 3, "hotkeys/pet", [] { pet::summonPet(); });
}

void MainWindow::refreshOverview() {
    if (!m_overviewLabel) return;
    QStringList lines;
    // 最近倒数日
    QDate best; QString bestName;
    for (const QJsonValue& v : ls::loadJson("schedule.json").value("dates").toArray()) {
        const QJsonObject o = v.toObject();
        const QDate d = QDate::fromString(o.value("date").toString(), "yyyy-MM-dd");
        const qint64 n = QDate::currentDate().daysTo(d);
        if (n >= 0 && (!best.isValid() || n < QDate::currentDate().daysTo(best))) {
            best = d; bestName = o.value("name").toString();
        }
    }
    if (best.isValid()) lines << QString("⏳ 最近的倒数日：「%1」还有 %2 天").arg(bestName).arg(QDate::currentDate().daysTo(best));
    // 今日待办
    int openTodo = 0;
    for (const QJsonValue& v : ls::loadJson("todos.json").value("items").toArray())
        if (!v.toObject().value("done").toBool()) ++openTodo;
    lines << QString("📋 待办进行中：%1 件").arg(openTodo);
    // 今日番茄
    const int pm = ls::loadJson("pomo.json").value("history").toObject()
                       .value(QDate::currentDate().toString("yyyy-MM-dd")).toInt();
    lines << QString("🍅 今日专注：%1 个番茄").arg(pm);
    m_overviewLabel->setText(lines.join("\n"));
}

void MainWindow::refreshHome() {
    refreshOverview();
    refreshCheckin();
}

void MainWindow::doCheckin() {
    QJsonObject p = ls::profile();
    const QString today = QDate::currentDate().toString("yyyy-MM-dd");
    if (p.value("lastCheckin").toString() == today) {
        QMessageBox::information(this, "签到", "今天已经签到过啦");
        return;
    }
    const QString yesterday = QDate::currentDate().addDays(-1).toString("yyyy-MM-dd");
    const int streak = p.value("lastCheckin").toString() == yesterday ? p.value("streak").toInt() + 1 : 1;
    const int gain = std::min(10 + (streak - 1) * 2, 20);
    const int points = p.value("points").toInt() + gain;
    p.insert("streak", streak);
    p.insert("points", points);
    p.insert("lastCheckin", today);
    ls::saveProfile(p);
    QMessageBox::information(this, "签到成功",
        QString("积分 +%1，当前 %2 分，连续 %3 天").arg(gain).arg(points).arg(streak));
    m_checkinPts->setText(QString::number(points));
    m_checkinStreak->setText(QString::number(streak) + " 天");
    m_checkinBtn->setText("今日已签到");
    m_checkinBtn->setEnabled(false);
}

void MainWindow::refreshCheckin() {
    if (m_checkinLoaded) return;
    m_checkinLoaded = true;
    const QJsonObject p = ls::profile();
    const bool checkedToday = p.value("lastCheckin").toString() == QDate::currentDate().toString("yyyy-MM-dd");
    m_checkinPts->setText(QString::number(p.value("points").toInt()));
    m_checkinStreak->setText(QString::number(p.value("streak").toInt()) + " 天");
    m_checkinBtn->setText(checkedToday ? "今日已签到" : "立即签到");
    m_checkinBtn->setEnabled(!checkedToday);
}

QWidget* MainWindow::buildHomePage() {
    auto* page = new QWidget;
    auto* outer = new QVBoxLayout(page);
    outer->setContentsMargins(24, 20, 24, 20);
    outer->setSpacing(12);

    // Hero：主题色渐变横幅 + Q 版看板娘
    auto* hero = new QFrame;
    hero->setObjectName("hero");
    hero->setFixedHeight(150);
    auto* heroL = new QHBoxLayout(hero);
    heroL->setContentsMargins(24, 16, 20, 0);
    heroL->setSpacing(12);
    auto* heroText = new QVBoxLayout;
    heroText->setSpacing(4);
    auto* heroHi = new QLabel("欢迎回来");
    heroHi->setObjectName("heroSub");
    heroText->addWidget(heroHi);
    auto* heroTitle = new QLabel("我的工具箱");
    heroTitle->setObjectName("heroTitle");
    heroText->addWidget(heroTitle);
    auto* heroSub = new QLabel("本地优先 · 数据保存在本机 · 一切尽在掌握");
    heroSub->setObjectName("heroSub");
    heroText->addWidget(heroSub);
    heroText->addStretch();
    heroL->addLayout(heroText, 1);
    auto* mascot = new QLabel;
    QPixmap mp = QPixmap(":/res/char_full.png").scaledToHeight(134, Qt::SmoothTransformation);
    mascot->setPixmap(mp);
    mascot->setFixedSize(mp.size());
    mascot->setAlignment(Qt::AlignBottom | Qt::AlignHCenter);
    heroL->addWidget(mascot);
    outer->addWidget(hero);
    ui::LiftFilter::apply(hero, 26, 8, 150);

    // 我的账户（含 HWID）
    auto* accCard = new QFrame;
    accCard->setObjectName("card");
    auto* accL = new QVBoxLayout(accCard);
    accL->setContentsMargins(20, 16, 20, 16);
    accL->setSpacing(10);
    auto* accTitle = new QLabel("我的账户");
    accTitle->setObjectName("author");
    accL->addWidget(accTitle);
    m_homeUser = new QLabel("本地用户");
    m_homeUser->setStyleSheet("font-size:18px; font-weight:bold;");
    accL->addWidget(m_homeUser);
    m_homeLicense = new QLabel("数据保存在本机，离线可用");
    m_homeLicense->setObjectName("dim");
    accL->addWidget(m_homeLicense);
    auto* hwidRow = new QHBoxLayout;
    hwidRow->setSpacing(10);
    hwidRow->addWidget(new QLabel("本机 HWID"));
    m_hwidEdit = new QLabel(myUid());
    m_hwidEdit->setObjectName("dim");
    hwidRow->addWidget(m_hwidEdit, 1);
    auto* hwidCopy = new QPushButton("复制");
    hwidCopy->setObjectName("ghost");
    connect(hwidCopy, &QPushButton::clicked, this, &MainWindow::copyHwid);
    hwidRow->addWidget(hwidCopy);
    accL->addLayout(hwidRow);
    outer->addWidget(accCard);
    ui::LiftFilter::apply(accCard);

    // 每日签到（积分 / 连续签到 / 签到按钮）
    auto* ckCard = new QFrame;
    ckCard->setObjectName("card");
    auto* ckL = new QVBoxLayout(ckCard);
    ckL->setContentsMargins(20, 16, 20, 16);
    ckL->setSpacing(10);
    auto* ckTitle = new QLabel("每日签到");
    ckTitle->setObjectName("author");
    ckL->addWidget(ckTitle);
    auto* ckRow = new QHBoxLayout;
    ckRow->setSpacing(34);
    auto* ptsBox = new QVBoxLayout;
    m_checkinPts = new QLabel("0");
    m_checkinPts->setObjectName("statNum");
    ptsBox->addWidget(m_checkinPts);
    auto* ptsCap = new QLabel("我的积分");
    ptsCap->setObjectName("dim");
    ptsBox->addWidget(ptsCap);
    ckRow->addLayout(ptsBox);
    auto* stkBox = new QVBoxLayout;
    m_checkinStreak = new QLabel("0 天");
    m_checkinStreak->setObjectName("statNum");
    stkBox->addWidget(m_checkinStreak);
    auto* stkCap = new QLabel("连续签到");
    stkCap->setObjectName("dim");
    stkBox->addWidget(stkCap);
    ckRow->addLayout(stkBox);
    ckRow->addStretch();
    m_checkinBtn = new QPushButton("立即签到");
    connect(m_checkinBtn, &QPushButton::clicked, this, &MainWindow::doCheckin);
    ckRow->addWidget(m_checkinBtn);
    ckL->addLayout(ckRow);
    outer->addWidget(ckCard);
    ui::LiftFilter::apply(ckCard);

    // 帮助与资源
    auto* helpCard = new QFrame;
    helpCard->setObjectName("card");
    auto* helpL = new QVBoxLayout(helpCard);
    helpL->setContentsMargins(20, 16, 20, 16);
    auto* helpTitle = new QLabel("帮助与资源");
    helpTitle->setObjectName("author");
    helpL->addWidget(helpTitle);
    auto* helpRow = new QHBoxLayout;
    helpRow->setSpacing(10);
    for (const QString& name : {QString("官网首页"), QString("更新日志"), QString("问题反馈"), QString("使用文档")}) {
        auto* btn = new QPushButton(name);
        connect(btn, &QPushButton::clicked, this, [this, name] {
            QMessageBox::information(this, name, "该链接暂未开放，后续版本会提供，敬请期待。");
        });
        helpRow->addWidget(btn);
    }
    helpL->addLayout(helpRow);
    outer->addWidget(helpCard);

    // 今日概览：最近倒数日 / 今日待办 / 今日番茄（进主页时刷新）
    {
        auto* ovCard = new QFrame;
        ovCard->setObjectName("card");
        auto* ovL = new QVBoxLayout(ovCard);
        ovL->setContentsMargins(20, 16, 20, 16);
        ovL->setSpacing(6);
        auto* ovTitle = new QLabel("今日概览");
        ovTitle->setObjectName("author");
        ovL->addWidget(ovTitle);
        m_overviewLabel = new QLabel;
        m_overviewLabel->setWordWrap(true);
        m_overviewLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        ovL->addWidget(m_overviewLabel);
        outer->addWidget(ovCard);
        m_overviewCard = ovCard;
    }


    outer->addStretch();
    return page;
}

QWidget* MainWindow::buildGroupPage(const QString& title, const QString& sub,
                                    const QVector<QPair<QString, QWidget* (MainWindow::*)()>>& tabs) {
    auto* page = new QWidget;
    auto* outer = new QVBoxLayout(page);
    outer->setContentsMargins(24, 20, 24, 20);
    outer->setSpacing(12);
    auto* t = new QLabel(title);
    t->setObjectName("heroTitle");
    outer->addWidget(t);
    auto* s = new QLabel(sub);
    s->setObjectName("dim");
    outer->addWidget(s);

    auto* split = new QHBoxLayout;
    split->setSpacing(14);
    auto* navScroll = new QScrollArea;
    navScroll->setWidgetResizable(true);
    navScroll->setFixedWidth(156);
    navScroll->setFrameShape(QFrame::NoFrame);
    navScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* navBody = new QWidget;
    auto* nav = new QVBoxLayout(navBody);
    nav->setContentsMargins(0, 0, 6, 0);
    nav->setSpacing(3);
    auto* group = new QButtonGroup(page);
    auto* stack = new QStackedWidget;
    QSettings gset("ToolBox", "ToolBoxQt");
    const QString keyLast = "ui/grouptab/" + title;
    const int lastTab = gset.value(keyLast, 0).toInt();
    for (int i = 0; i < tabs.size(); ++i) {
        auto* b = new QPushButton(tabs[i].first);
        b->setObjectName("tabbtn");
        b->setCheckable(true);
        b->setChecked(i == lastTab);
        b->setStyleSheet("QPushButton{text-align:left;padding-left:16px;}");
        group->addButton(b, i);
        nav->addWidget(b);
        stack->addWidget((this->*tabs[i].second)());
    }
    stack->setCurrentIndex(qBound(0, lastTab, tabs.size() - 1));
    nav->addStretch();
    navScroll->setWidget(navBody);
    split->addWidget(navScroll);
    split->addWidget(stack, 1);
    outer->addLayout(split, 1);
    connect(group, &QButtonGroup::idClicked, this, [this, stack, title](int id) {
        if (m_clickerTimer && m_clickerTimer->isActive()) { // 切走工具前停连点器
            m_clickerTimer->stop();
            if (m_clickerBtn) m_clickerBtn->setText("开始连点");
        }
        stack->setCurrentIndex(id);
        QSettings gset("ToolBox", "ToolBoxQt");
        gset.setValue("ui/grouptab/" + title, id);
        ui::fadeIn(stack->widget(id));
    });
    return page;
}

QWidget* MainWindow::buildSysToolsPage() {
    return buildGroupPage("系统工具", "硬件状态 · 自动化 · 进程与电源管理", {
        {"系统信息", &MainWindow::buildSysInfoTab},
        {"连点器", &MainWindow::buildClickerTab},
        {"进程监控器", &MainWindow::buildEdgeTab},
        {"启动项", &MainWindow::buildStartupTab},
        {"定时关机", &MainWindow::buildShutdownTab},
    });
}

QWidget* MainWindow::buildLifeToolsPage() {
    return buildGroupPage("效率生活", "记账 · 便签 · 专注 · 剪贴板", {
        {"剪贴板历史", &MainWindow::buildClipTab},
        {"记账本", &MainWindow::buildLedgerTab},
        {"便签", &MainWindow::buildNotesTab},
        {"番茄钟", &MainWindow::buildPomoTab},
        {"文本朗读", &MainWindow::buildTtsTab},
        {"随机决策", &MainWindow::buildDiceTab},
        {"日期计算", &MainWindow::buildDateTab},
        {"世界时钟", &MainWindow::buildWorldTab},
    });
}

QWidget* MainWindow::buildTextToolsPage() {
    return buildGroupPage("文本工具", "脱敏 · 加解密 · 开发辅助 · 对比", {
        {"文本脱敏", &MainWindow::buildMaskTab},
        {"加解密", &MainWindow::buildCryptoTab},
        {"开发工具箱", &MainWindow::buildDevTab},
        {"代码片段", &MainWindow::buildSnippetTab},
        {"文本对比", &MainWindow::buildDiffTab},
    });
}

QWidget* MainWindow::buildNetToolsPage() {
    return buildGroupPage("网络工具", "翻译 · 汇率 · 诊断 · 测速 · 下载", {
        {"翻译", &MainWindow::buildTransTab},
        {"汇率", &MainWindow::buildFxTab},
        {"网络诊断", &MainWindow::buildNetTab},
        {"网速测试", &MainWindow::buildSpeedTab},
        {"端口查看", &MainWindow::buildPortTab},
        {"批量测活", &MainWindow::buildCheckTab},
        {"网址安全", &MainWindow::buildUrlSafetyTab},
        {"HTTP测试", &MainWindow::buildHttpTab},
        {"下载器", &MainWindow::buildDownloadTab},
        {"天气", &MainWindow::buildWeatherTab},
    });
}

QWidget* MainWindow::buildMediaToolsPage() {
    return buildGroupPage("截图与媒体", "截图 · OCR · 取色 · 专注音效", {
        {"截图", &MainWindow::buildShotTab},
        {"文字识别", &MainWindow::buildOcrTab},
        {"取色器", &MainWindow::buildPickTab},
        {"图片工具", &MainWindow::buildImgTab},
        {"白噪音", &MainWindow::buildNoiseTab},
    });
}

QWidget* MainWindow::buildFileToolsPage() {
    return buildGroupPage("文件工具", "搜索 · 整理 · 重复清理 · 空间分析", {
        {"快捷启动", &MainWindow::buildQuickTab},
        {"批量重命名", &MainWindow::buildRenameTab},
        {"重复文件", &MainWindow::buildDupTab},
        {"文件转码", &MainWindow::buildTranscodeTab},
        {"文件搜索", &MainWindow::buildFileSearchTab},
        {"磁盘助手", &MainWindow::buildDiskPage},
        {"系统清理", &MainWindow::buildCleanupTab},
    });
}

void MainWindow::showAbout() {
    QDialog dlg(this);
    dlg.setWindowTitle("关于 我的工具箱");
    dlg.setFixedWidth(430);
    auto* lay = new QVBoxLayout(&dlg);
    lay->setContentsMargins(28, 24, 28, 20);
    lay->setSpacing(6);

    auto* icon = new QLabel;
    icon->setPixmap(QPixmap(":/res/char_badge256.png").scaled(
        84, 84, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    icon->setAlignment(Qt::AlignHCenter);
    lay->addWidget(icon);

    auto* name = new QLabel("我的工具箱");
    name->setObjectName("heroTitle");
    name->setAlignment(Qt::AlignHCenter);
    lay->addWidget(name);

    auto* ver = new QLabel(QString("ToolBox · 桌面版 v%1").arg(kAppVersion));
    ver->setObjectName("aboutVer");
    ver->setAlignment(Qt::AlignHCenter);
    lay->addWidget(ver);
    lay->addSpacing(10);

    auto addLine = [&](const QString& cap, const QString& val) {
        auto* l = new QLabel(QString("%1： %2").arg(cap, val));
        l->setWordWrap(true);
        lay->addWidget(l);
    };
    addLine("开发者", kAppAuthor);
    addLine("编译日期", kBuildDate);
    addLine("编译时间", kBuildTime);
    addLine("运行环境", QString("Qt %1 (MinGW)").arg(QT_VERSION_STR));
    addLine("桌宠素材", "dsh-pet (CC BY-NC 4.0)");
    lay->addSpacing(10);

    auto* contact = new QLabel("联系与交流");
    contact->setObjectName("author");
    lay->addWidget(contact);
    auto* qqRow = new QHBoxLayout;
    auto* qq = new QLabel(QString("QQ 交流群：%1").arg(kQQGroup));
    qq->setTextInteractionFlags(Qt::TextSelectableByMouse);
    qqRow->addWidget(qq);
    auto* copyBtn = new QPushButton("复制群号");
    copyBtn->setObjectName("ghost");
    connect(copyBtn, &QPushButton::clicked, copyBtn, [copyBtn] {
        QApplication::clipboard()->setText(kQQGroup);
        copyBtn->setText("已复制");
        QTimer::singleShot(1500, copyBtn, [copyBtn] { copyBtn->setText("复制群号"); });
    });
    qqRow->addWidget(copyBtn);
    qqRow->addStretch();
    lay->addLayout(qqRow);
    lay->addSpacing(10);

    auto* close = new QPushButton("关闭");
    connect(close, &QPushButton::clicked, &dlg, &QDialog::accept);
    lay->addWidget(close);

    dlg.exec();
}

// ---------------- 设置页（外观：主题色 / 背景色 / 背景图片） ----------------

QWidget* MainWindow::buildSettingsPage() {
    auto* page = new QWidget;
    auto* outer = new QVBoxLayout(page);
    outer->setContentsMargins(24, 20, 24, 20);
    outer->setSpacing(12);

    outer->addWidget(new QLabel("外观设置（仅作用于本 Qt 客户端，独立于 imgui 桌面端保存）"));

    // 主题色预设
    outer->addWidget(new QLabel("主题色"));
    auto* swRow = new QHBoxLayout;
    swRow->setSpacing(10);
    for (int i = 0; i < theme::presetCount(); ++i) {
        QColor c = theme::presets()[i];
        auto* sw = new QPushButton;
        sw->setFixedSize(34, 34);
        sw->setCursor(Qt::PointingHandCursor);
        QString hex = c.name(QColor::HexRgb);
        bool sel = theme::accent() == c;
        sw->setStyleSheet(QString("background:%1; border-radius:17px; border:3px solid rgba(20,26,48,%2);")
                              .arg(hex).arg(sel ? "230" : "0"));
        connect(sw, &QPushButton::clicked, this, [this, c] {
            theme::accent() = c;
            theme::save();
            qApp->setStyleSheet(theme::qss());
            update();
            refreshSettingsPage();
        });
        swRow->addWidget(sw);
    }
    swRow->addStretch();
    outer->addLayout(swRow);

    outer->addWidget(new QLabel("字体大小"));
    auto* fontRow = new QHBoxLayout;
    auto* fontLab = new QLabel(QString::number(theme::uiFontPx()) + " px");
    auto* fontSl = new QSlider(Qt::Horizontal);
    fontSl->setRange(11, 18);
    fontSl->setValue(theme::uiFontPx());
    fontSl->setFixedWidth(220);
    fontRow->addWidget(fontSl);
    fontRow->addWidget(fontLab);
    fontRow->addStretch();
    outer->addLayout(fontRow);
    connect(fontSl, &QSlider::valueChanged, fontLab, [fontLab](int v) {
        fontLab->setText(QString::number(v) + " px"); // 拖动中只更新数字
    });
    connect(fontSl, &QSlider::sliderReleased, this, [fontSl] {
        theme::uiFontPx() = fontSl->value();
        theme::save();
        qApp->setStyleSheet(theme::qss()); // 松手才整体重刷
    });

    auto* animCk = new QCheckBox("动态炫彩背景（渐变流动，关掉则静止）");
    animCk->setChecked(theme::bgAnimated());
    connect(animCk, &QCheckBox::toggled, this, [this](bool on) {
        if (g_root) g_root->setAnimated(on);
    });
    outer->addWidget(animCk);

    auto* note = new QLabel("界面背景色随主题色自动适配，无需手动设置");
    note->setObjectName("dim");
    outer->addWidget(note);

    // 背景图片
    outer->addWidget(new QLabel("背景图片"));
    auto* imgRow = new QHBoxLayout;
    auto* pickImg = new QPushButton("选择图片...");
    connect(pickImg, &QPushButton::clicked, this, [this] {
        QString f = QFileDialog::getOpenFileName(this, "选择背景图片", QString(),
                                                 "图片文件 (*.png *.jpg *.jpeg *.bmp)");
        if (f.isEmpty()) return;
        theme::bgImagePath() = f;
        theme::save();
        update();
    });
    imgRow->addWidget(pickImg);
    auto* rmImg = new QPushButton("移除图片");
    connect(rmImg, &QPushButton::clicked, this, [this] {
        theme::bgImagePath().clear();
        theme::save();
        update();
    });
    imgRow->addWidget(rmImg);
    imgRow->addWidget(new QLabel("浓度"));
    auto* slider = new QSlider(Qt::Horizontal);
    slider->setRange(0, 100);
    slider->setValue(int(theme::bgOpacity() * 100));
    slider->setFixedWidth(180);
    connect(slider, &QSlider::valueChanged, this, [this](int v) {
        theme::bgOpacity() = v / 100.0;
        theme::save();
        update();
    });
    imgRow->addWidget(slider);
    imgRow->addStretch();
    outer->addLayout(imgRow);

    // ---------- 桌宠台词 ----------
    outer->addWidget(new QLabel("桌宠台词（自动说话随机挑选，每行一条；留空恢复默认，保存即生效）"));
    auto* talkEdit = new QPlainTextEdit;
    const QJsonArray curTalks = ls::petUi().value("talks").toArray();
    for (const QJsonValue& v : curTalks) talkEdit->appendPlainText(v.toString());
    talkEdit->setPlaceholderText(QString::fromUtf8("在忙什么呀～\n要不要休息一下\n（留空使用默认台词）"));
    talkEdit->setFixedHeight(110);
    outer->addWidget(talkEdit);
    auto* talkRow = new QHBoxLayout;
    auto* talkSave = new QPushButton("保存台词");
    talkSave->setObjectName("ghost");
    auto* talkTip = new QLabel("");
    talkTip->setObjectName("dim");
    talkRow->addWidget(talkSave);
    talkRow->addWidget(talkTip, 1);
    outer->addLayout(talkRow);
    connect(talkSave, &QPushButton::clicked, this, [this, talkEdit, talkTip] {
        QJsonObject u = ls::petUi();
        QJsonArray arr;
        const QStringList ls2 = talkEdit->toPlainText().split(QLatin1Char('\n'));
        for (const QString& line : ls2) {
            const QString t = line.trimmed();
            if (!t.isEmpty()) arr.append(t);
        }
        u.insert("talks", arr);
        ls::savePetUi(u);
        talkTip->setText(arr.isEmpty() ? "已保存（当前使用默认台词）"
                                       : QString("已保存 %1 条台词，桌宠立即开始使用").arg(arr.size()));
    });

    // ---------- 启动 ----------
    outer->addWidget(new QLabel("启动"));
    const QString lnkPath = QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation)
                            + "/Startup/ToolBoxQt.lnk";
    auto* autoRow = new QHBoxLayout;
    auto* autoChk = new QCheckBox("开机自动启动（写入当前用户的「启动」文件夹，随文件夹位置更新）");
    autoChk->setChecked(QFile::exists(lnkPath));
    autoRow->addWidget(autoChk);
    autoRow->addStretch();
    outer->addLayout(autoRow);
    connect(autoChk, &QCheckBox::toggled, this, [this, lnkPath, autoChk](bool on) {
        if (on) {
            const QString exe = QDir::toNativeSeparators(
                QCoreApplication::applicationDirPath() + "/ToolBoxQt.exe");
            const QString workdir = QDir::toNativeSeparators(QCoreApplication::applicationDirPath());
            const QString ps = QString(
                "$ws = New-Object -ComObject WScript.Shell;"
                "$s = $ws.CreateShortcut('%1');"
                "$s.TargetPath = '%2';"
                "$s.WorkingDirectory = '%3';"
                "$s.Save()").arg(QDir::toNativeSeparators(lnkPath), exe, workdir);
            QProcess::execute("powershell", {"-NoProfile", "-Command", ps});
            if (!QFile::exists(lnkPath)) {
                QMessageBox::warning(this, "开机自启", "快捷方式创建失败，请检查权限");
                autoChk->setChecked(false);
                return;
            }
            QMessageBox::information(this, "开机自启", "已开启：登录 Windows 后将自动运行本程序");
        } else {
            QFile::remove(lnkPath);
        }
    });

    // ---------- 数据备份：导出/导入全部本地 json ----------
    outer->addWidget(new QLabel("数据备份"));
    auto* bkRow = new QHBoxLayout;
    auto* bkOut = new QPushButton("导出数据到文件夹…");
    bkOut->setObjectName("ghost");
    auto* bkIn = new QPushButton("从文件夹导入…");
    bkIn->setObjectName("ghost");
    auto* bkState = new QLabel("");
    bkState->setObjectName("dim");
    bkRow->addWidget(bkOut);
    bkRow->addWidget(bkIn);
    bkRow->addWidget(bkState, 1);
    outer->addLayout(bkRow);
    connect(bkOut, &QPushButton::clicked, this, [this, bkState] {
        const QString dir = QFileDialog::getExistingDirectory(this, "选择导出位置");
        if (dir.isEmpty()) return;
        const QString dst = dir + "/ToolBox-backup-" + QDateTime::currentDateTime().toString("yyyyMMdd-hhmmss");
        if (!QDir().mkpath(dst)) { bkState->setText("无法创建导出目录"); return; }
        int n = 0;
        QDirIterator it(ls::dataDir(), {"*.json"}, QDir::Files);
        while (it.hasNext()) {
            const QString f = it.next();
            if (QFile::copy(f, dst + "/" + QFileInfo(f).fileName())) ++n;
        }
        bkState->setText(QString("已导出 %1 个数据文件到 %2").arg(n).arg(dst));
        toast("数据备份", QString("导出完成 · %1 个文件").arg(n));
    });
    connect(bkIn, &QPushButton::clicked, this, [this, bkState] {
        const QString dir = QFileDialog::getExistingDirectory(this, "选择备份文件夹（含导出的 json）");
        if (dir.isEmpty()) return;
        int n = 0;
        QDirIterator it(dir, {"*.json"}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString f = it.next();
            const QString rel = QDir(dir).relativeFilePath(f);
            const QString dst = ls::dataDir() + "/" + rel;
            QDir().mkpath(QFileInfo(dst).absolutePath());
            QFile::remove(dst);
            if (QFile::copy(f, dst)) ++n;
        }
        if (n == 0) { bkState->setText("该文件夹里没有 json 数据文件"); return; }
        bkState->setText(QString("已导入 %1 个文件，重启程序后生效").arg(n));
        QMessageBox::information(this, "数据备份", QString("已导入 %1 个文件。\n重启 ToolBox 后生效。").arg(n));
    });

    outer->addStretch();

    // 内容多（快捷键/备份/台词…），套滚动区保证任何字号都够得着底部
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(page);
    return scroll;
}

void MainWindow::refreshSettingsPage() {
    // 按实际位置重建设置页（索引自适应，杜绝页码迁移后的错位崩溃）
    if (!m_settingsPage) return;
    const int idx = m_stack->indexOf(m_settingsPage);
    if (idx < 0) return;
    m_stack->removeWidget(m_settingsPage);
    m_settingsPage->deleteLater();
    m_settingsPage = buildSettingsPage();
    m_stack->insertWidget(idx, m_settingsPage);
    m_stack->setCurrentIndex(idx);
}

// ---------------- 云养宠物 ----------------

namespace {

// 物品 → 桌宠动画（对应素材库里的吃相/玩耍动作）
QString petItemAnim(const QString& id) {
    if (id == "rice") return "吃白饭";
    if (id == "bento") return "吃午餐";
    if (id == "crab") return "吃大闸蟹";
    if (id == "token") return "吃Token";
    if (id == "watermelon") return "吃西瓜";
    if (id == "hotpot") return "涮火锅";
    if (id == "nip" || id == "yarn") return "撸猫";
    if (id == "cube") return "原地专心玩魔方";
    if (id == "blanket") return "原地小憩沉眠";
    return "大口吃零食";
}

QString petItemBubble(const QString& id) {
    if (id == "coffee") return "精神来了！";
    if (id == "milktea") return "快乐水到账～";
    if (id == "water") return "咕嘟咕嘟～";
    if (id == "juice") return "维C 补充！";
    if (id == "blanket") return "Zzz……";
    if (id == "nip" || id == "yarn" || id == "cube") return "好耶！玩起来！";
    if (id == "hotpot") return "涮火锅咯～满足！";
    if (id == "crab") return "大闸蟹！横着走！";
    return "啊呜啊呜～好吃！";
}

QString petEffectText(const QJsonObject& it) {
    QStringList parts;
    const struct { const char* k; const char* n; } m[] = {
        {"hunger", "饱腹"}, {"thirst", "解渴"}, {"energy", "精力"}, {"mood", "心情"},
    };
    for (const auto& e : m) {
        const int v = it.value(e.k).toInt();
        if (v) parts << QString("%1%2 %3").arg(v > 0 ? "+" : "").arg(v).arg(e.n);
    }
    return parts.join("  ");
}

// 本地商店目录（原服务端 /api/pet/shop 内容原样本地化）
QJsonObject petShopCatalog() {
    static const QJsonObject cat = [] {
        auto mk = [](const char* id, const char* name, int price, const char* desc,
                     int hunger = 0, int thirst = 0, int energy = 0, int mood = 0) {
            QJsonObject o;
            o.insert("id", id);
            o.insert("name", name);
            o.insert("price", price);
            o.insert("desc", desc);
            if (hunger) o.insert("hunger", hunger);
            if (thirst) o.insert("thirst", thirst);
            if (energy) o.insert("energy", energy);
            if (mood) o.insert("mood", mood);
            return o;
        };
        return QJsonObject{
            {"foods", QJsonArray{
                mk("rice", "香喷喷饭团", 10, "管饱的基础款", 22),
                mk("bento", "元气便当", 25, "妈妈的味道", 35, 0, 8),
                mk("crab", "大闸蟹", 45, "横着走的美味", 45, 0, 0, 12),
                mk("token", "Token 餐包", 30, "AI 的精神食粮", 20, 0, 0, 20),
                mk("watermelon", "冰镇西瓜", 15, "夏天的仪式感", 10, 18, 0, 6),
                mk("hotpot", "小火锅", 60, "满足感拉满，就是有点咸", 60, -8, 0, 15),
            }},
            {"drinks", QJsonArray{
                mk("water", "矿泉水", 5, "生命之源", 0, 30),
                mk("juice", "鲜榨橙汁", 16, "维C 满满", 0, 25, 0, 8),
                mk("milktea", "三分糖奶茶", 18, "快乐水", 0, 32, 0, 10),
                mk("coffee", "热美式", 20, "续命标配", 0, 8, 30),
            }},
            {"toys", QJsonArray{
                mk("nip", "猫薄荷", 12, "上头一整天", 0, 0, -10, 20),
                mk("yarn", "毛线球", 22, "玩物丧志，但快乐", 0, 0, 0, 25),
                mk("cube", "魔方", 28, "还原那一刻超爽", 0, 0, -6, 22),
                mk("blanket", "小毯子", 35, "秒睡神器", 0, 0, 40),
            }},
        };
    }();
    return cat;
}

QJsonObject petItemById(const QString& id, QString* catOut = nullptr) {
    const QJsonObject cat = petShopCatalog();
    for (const char* k : {"foods", "drinks", "toys"}) {
        for (const QJsonValue& v : cat.value(k).toArray()) {
            if (v.toObject().value("id").toString() == id) {
                if (catOut) *catOut = QString(k);
                return v.toObject();
            }
        }
    }
    return {};
}

// 本地任务目录（宠物打工：时长/奖励/对应动画），奖励约 10 积分/分钟
QJsonArray petTaskCatalog() {
    static const QJsonArray arr = [] {
        auto mk = [](const char* id, const char* name, int dur, int reward,
                     const char* anim, const char* desc) {
            QJsonObject o;
            o.insert("id", id);
            o.insert("name", name);
            o.insert("dur", dur);
            o.insert("reward", reward);
            o.insert("anim", anim);
            o.insert("desc", desc);
            return o;
        };
        return QJsonArray{
            mk("think", "深度思考", 30, 5, "深度思考碎碎念", "发呆也是生产力"),
            mk("record", "轻快记录", 60, 8, "轻快记录", "记笔记小能手"),
            mk("dance", "直播宅舞", 90, 12, "可爱宅舞", "观众礼物收到手软"),
            mk("code", "写代码", 120, 20, "写代码", "接了个外包，别催"),
            mk("violin", "小提琴演出", 150, 25, "小提琴演奏", "音乐厅巡演"),
        };
    }();
    return arr;
}

QJsonObject petTaskById(const QString& id) {
    for (const QJsonValue& v : petTaskCatalog())
        if (v.toObject().value("id").toString() == id) return v.toObject();
    return {};
}

// 清空动态行布局：addLayout 的子布局里 it->widget() 取不到控件，必须递归 deleteLater，
// 否则旧标签/按钮变成孤儿控件残留原位，和新行重叠
void clearPetRows(QLayout* lay) {
    while (QLayoutItem* it = lay->takeAt(0)) {
        if (QWidget* w = it->widget())
            w->deleteLater();
        else if (QLayout* sub = it->layout())
            clearPetRows(sub);
        delete it;
    }
}

} // namespace

QWidget* MainWindow::buildPetPage() {
    auto* page = new QWidget;
    auto* outer = new QVBoxLayout(page);
    outer->setContentsMargins(0, 4, 0, 0);
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* body = new QWidget;
    auto* lay = new QVBoxLayout(body);
    lay->setContentsMargins(24, 18, 24, 24);
    lay->setSpacing(14);
    scroll->setWidget(body);
    outer->addWidget(scroll);

    // ---- 状态卡 ----
    auto* stCard = new QFrame;
    stCard->setObjectName("card");
    auto* stL = new QVBoxLayout(stCard);
    stL->setContentsMargins(20, 16, 20, 16);
    stL->setSpacing(12);
    auto* head = new QHBoxLayout;
    m_petTitle = new QLabel("桌面宠物 · 云养");
    m_petTitle->setStyleSheet("font-size:17px; font-weight:bold;");
    head->addWidget(m_petTitle);
    head->addStretch();
    auto* summonBtn = new QPushButton("召唤桌宠");
    summonBtn->setObjectName("flat");
    head->addWidget(summonBtn);
    connect(summonBtn, &QPushButton::clicked, this, [this] {
        pet::summonPet();
        if (m_petState) m_petState->setText("桌宠已召唤，正在陪你工作～");
    });
    auto* renameBtn = new QPushButton("改名");
    renameBtn->setObjectName("flat");
    head->addWidget(renameBtn);
    connect(renameBtn, &QPushButton::clicked, this, &MainWindow::petRename);
    m_petPoints = new QLabel("积分 -");
    m_petPoints->setStyleSheet("font-size:14px; font-weight:bold; color:#ffc857;");
    head->addWidget(m_petPoints);
    auto* refreshBtn = new QPushButton("刷新");
    refreshBtn->setObjectName("flat");
    head->addWidget(refreshBtn);
    connect(refreshBtn, &QPushButton::clicked, this, &MainWindow::refreshPet);
    stL->addLayout(head);

    const struct { const char* name; const char* color; } bars[4] = {
        {"饱腹", "#ff9f43"}, {"解渴", "#4fc3f7"}, {"精力", "#b388ff"}, {"心情", "#f06292"},
    };
    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(12);
    for (int i = 0; i < 4; ++i) {
        auto* cap = new QLabel(bars[i].name);
        cap->setObjectName("dim");
        cap->setFixedWidth(30);
        grid->addWidget(cap, i / 2, (i % 2) * 3);
        auto* bar = new QProgressBar;
        bar->setRange(0, 100);
        bar->setValue(70);
        bar->setFormat("%p");
        bar->setFixedHeight(16);
        bar->setStyleSheet(QString(
            "QProgressBar{background:rgba(20,26,48,.08);border:none;border-radius:8px;font-size:10px;}"
            "QProgressBar::chunk{background:%1;border-radius:8px;}").arg(bars[i].color));
        grid->addWidget(bar, i / 2, (i % 2) * 3 + 1);
        m_petBar[i] = bar;
    }
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(3, 1);
    stL->addLayout(grid);

    auto* stateRow = new QHBoxLayout;
    m_petState = new QLabel("连接中…");
    stateRow->addWidget(m_petState, 1);
    m_petSleepBtn = new QPushButton("哄睡");
    m_petSleepBtn->setCursor(Qt::PointingHandCursor);
    connect(m_petSleepBtn, &QPushButton::clicked, this, [this] { petSleep(!m_petSleeping); });
    stateRow->addWidget(m_petSleepBtn);
    stL->addLayout(stateRow);
    lay->addWidget(stCard);

    // ---- 自定义模型卡（导入帧文件夹 + 动作适配） ----
    {
        auto* custCard = new QFrame;
        custCard->setObjectName("card");
        auto* cl = new QVBoxLayout(custCard);
        cl->setContentsMargins(20, 16, 20, 16);
        cl->setSpacing(10);
        auto* custTitle = new QLabel("自定义桌宠模型");
        custTitle->setStyleSheet("font-size:15px; font-weight:bold;");
        cl->addWidget(custTitle);
        auto* custHint = new QLabel(
            "导入后可与默认动作混合运行：映射过的动作播自定义片段，其余保持默认。\n"
            "文件夹格式：每个动作一个子文件夹，内含 000.webp、001.webp…（可用 tools/extract_pet_frames.py 从 webm 提取）");
        custHint->setObjectName("dim");
        custHint->setWordWrap(true);
        cl->addWidget(custHint);

        auto* enRow = new QHBoxLayout;
        auto* enChk = new QCheckBox("启用自定义模型");
        enChk->setChecked(ls::customModel().value("enabled").toBool());
        enRow->addWidget(enChk);
        enRow->addStretch();
        auto* importBtn = new QPushButton("导入模型文件夹…");
        auto* resetBtn = new QPushButton("恢复默认");
        resetBtn->setObjectName("ghost");
        enRow->addWidget(importBtn);
        enRow->addWidget(resetBtn);
        cl->addLayout(enRow);

        const struct { const char* key; const char* name; } roles[6] = {
            {"idle", "待机"}, {"walk", "走路"}, {"sleep", "睡觉"},
            {"drag", "拖拽悬空"}, {"click", "点击回应"}, {"feed", "吃喝投喂"},
        };
        auto* mapGrid = new QGridLayout;
        QComboBox* combo[6] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
        auto applyChange = [this](const char* key, const QString& clip, bool enabled) {
            QJsonObject m = ls::customModel();
            QJsonObject map = m.value("map").toObject();
            if (clip.isEmpty()) map.remove(key);
            else map.insert(key, clip);
            m.insert("map", map);
            m.insert("enabled", enabled);
            ls::saveCustomModel(m);
            pet::reloadPetAssets();
        };
        for (int i = 0; i < 6; ++i) {
            auto* cap = new QLabel(roles[i].name);
            cap->setObjectName("dim");
            cap->setFixedWidth(60);
            mapGrid->addWidget(cap, i / 2, (i % 2) * 2);
            combo[i] = new QComboBox;
            combo[i]->setMinimumWidth(150);
            mapGrid->addWidget(combo[i], i / 2, (i % 2) * 2 + 1);
            const char* key = roles[i].key;
            QComboBox* cb = combo[i];
            connect(cb, &QComboBox::currentIndexChanged, this,
                    [this, applyChange, key, enChk, cb](int idx) {
                if (idx < 0) return;
                applyChange(key, idx == 0 ? QString() : cb->itemText(idx), enChk->isChecked());
            });
        }
        cl->addLayout(mapGrid);
        lay->addWidget(custCard);

        // 组合框填充/当前值
        auto refreshCombos = [this, combo, roles, enChk]() {
            const QJsonObject m = ls::customModel();
            QStringList names = m.value("clips").toObject().keys();
            std::sort(names.begin(), names.end());
            const QJsonObject map = m.value("map").toObject();
            for (int i = 0; i < 6; ++i) {
                combo[i]->blockSignals(true);
                combo[i]->clear();
                combo[i]->addItem("使用默认");
                combo[i]->addItems(names);
                const QString cur = map.value(roles[i].key).toString();
                const int idx = cur.isEmpty() ? 0 : names.indexOf(cur) + 1;
                combo[i]->setCurrentIndex(idx < 0 ? 0 : idx);
                combo[i]->blockSignals(false);
            }
            enChk->blockSignals(true);
            enChk->setChecked(m.value("enabled").toBool());
            enChk->blockSignals(false);
        };

        connect(enChk, &QCheckBox::toggled, this, [this, applyChange](bool on) {
            QJsonObject m = ls::customModel();
            m.insert("enabled", on);
            ls::saveCustomModel(m);
            pet::reloadPetAssets();
        });
        connect(importBtn, &QPushButton::clicked, this, [this, refreshCombos]() {
            const QString dir = QFileDialog::getExistingDirectory(
                this, "选择模型文件夹（每个动作一个子文件夹，内含 webp 帧序列）");
            if (dir.isEmpty()) return;
            QDir d(dir);
            QStringList subs = d.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
            if (subs.isEmpty()) subs << "."; // 所选文件夹本身就是单动作帧夹
            QJsonObject clips;
            int total = 0;
            for (const QString& sub : subs) {
                const QString srcPath = sub == "." ? dir : dir + "/" + sub;
                QDir sd(srcPath);
                const QStringList webps = sd.entryList({"*.webp"}, QDir::Files, QDir::Name);
                if (webps.size() < 2) continue;
                const QString clipName = sub == "." ? QFileInfo(dir).fileName() : sub;
                const QString dstDir = ls::customPetDir() + "/" + clipName;
                QDir().mkpath(dstDir);
                QJsonArray frames;
                for (const QString& f : webps) {
                    const QString dst = dstDir + "/" + f;
                    QFile::remove(dst);
                    if (QFile::copy(sd.filePath(f), dst)) {
                        frames.append(clipName + "/" + f);
                        ++total;
                    }
                }
                if (!frames.isEmpty()) {
                    QJsonObject c;
                    c.insert("fps", 24.0);
                    c.insert("frames", frames);
                    clips.insert(clipName, c);
                }
            }
            if (clips.isEmpty()) {
                QMessageBox::information(this, "导入模型",
                    "未找到 webp 帧序列。每个动作子文件夹里至少要有 2 张 webp\n"
                    "（webm 源视频请先用 tools/extract_pet_frames.py 提取）");
                return;
            }
            QJsonObject m = ls::customModel();
            m.insert("clips", clips);
            m.insert("name", QFileInfo(dir).fileName());
            if (!m.contains("map")) m.insert("map", QJsonObject());
            ls::saveCustomModel(m);
            refreshCombos();
            pet::reloadPetAssets();
            QMessageBox::information(this, "导入模型",
                QString("已导入 %1 个动作、共 %2 帧，可在下方做动作适配。").arg(clips.size()).arg(total));
        });
        connect(resetBtn, &QPushButton::clicked, this, [this, refreshCombos]() {
            if (QMessageBox::question(this, "恢复默认",
                "停用自定义模型并清除动作适配？（导入的片段文件保留）") != QMessageBox::Yes)
                return;
            QJsonObject m = ls::customModel();
            m.remove("map");
            m.insert("enabled", false);
            ls::saveCustomModel(m);
            refreshCombos();
            pet::reloadPetAssets();
        });
        refreshCombos();
    }

    // ---- 健康提醒卡（久坐/喝水，到点桌宠提醒，桌宠隐藏走托盘通知） ----
    {
        auto* remCard = new QFrame;
        remCard->setObjectName("card");
        auto* rl = new QVBoxLayout(remCard);
        rl->setContentsMargins(20, 16, 20, 16);
        rl->setSpacing(10);
        auto* remTitle = new QLabel("健康提醒");
        remTitle->setStyleSheet("font-size:15px; font-weight:bold;");
        rl->addWidget(remTitle);
        auto* remHint = new QLabel("到点后桌宠会提醒你（隐藏时走托盘气泡）。间隔修改即时生效。");
        remHint->setObjectName("dim");
        remHint->setWordWrap(true);
        rl->addWidget(remHint);

        const QJsonObject ru = ls::petUi();
        auto* sitRow = new QHBoxLayout;
        auto* sitChk = new QCheckBox("久坐提醒");
        sitChk->setChecked(ru.value("remindSit").toBool());
        sitRow->addWidget(sitChk);
        sitRow->addStretch();
        sitRow->addWidget(new QLabel("间隔(分钟)"));
        auto* sitMin = new QSpinBox;
        sitMin->setRange(5, 240);
        sitMin->setValue(ru.value("remindSitMin").toInt(45));
        sitRow->addWidget(sitMin);
        rl->addLayout(sitRow);

        auto* watRow = new QHBoxLayout;
        auto* watChk = new QCheckBox("喝水提醒");
        watChk->setChecked(ru.value("remindWater").toBool());
        watRow->addWidget(watChk);
        watRow->addStretch();
        watRow->addWidget(new QLabel("间隔(分钟)"));
        auto* watMin = new QSpinBox;
        watMin->setRange(5, 240);
        watMin->setValue(ru.value("remindWaterMin").toInt(60));
        watRow->addWidget(watMin);
        rl->addLayout(watRow);
        lay->addWidget(remCard);

        auto saveRem = [this](const char* key, const char* minKey, bool on, int minutes) {
            QJsonObject m = ls::petUi();
            m.insert(key, on);
            m.insert(minKey, minutes);
            ls::savePetUi(m);
        };
        connect(sitChk, &QCheckBox::toggled, this, [this, saveRem, sitMin](bool on) {
            saveRem("remindSit", "remindSitMin", on, sitMin->value());
            m_remindSitLast = QDateTime::currentMSecsSinceEpoch(); // 改设置重新计时
        });
        connect(sitMin, &QSpinBox::valueChanged, this, [saveRem, sitChk, sitMin](int v) {
            saveRem("remindSit", "remindSitMin", sitChk->isChecked(), v);
        });
        connect(watChk, &QCheckBox::toggled, this, [this, saveRem, watMin](bool on) {
            saveRem("remindWater", "remindWaterMin", on, watMin->value());
            m_remindWaterLast = QDateTime::currentMSecsSinceEpoch();
        });
        connect(watMin, &QSpinBox::valueChanged, this, [saveRem, watChk, watMin](int v) {
            saveRem("remindWater", "remindWaterMin", watChk->isChecked(), v);
        });
    }

    // ---- 任务栏卡 ----
    auto* taskCard = new QFrame;
    taskCard->setObjectName("card");
    auto* taskL = new QVBoxLayout(taskCard);
    taskL->setContentsMargins(20, 14, 20, 14);
    taskL->setSpacing(8);
    auto* taskHead = new QHBoxLayout;
    auto* taskTitle = new QLabel("任务栏（让宠物打工赚积分）");
    taskTitle->setStyleSheet("font-size:14px; font-weight:bold;");
    taskHead->addWidget(taskTitle);
    taskHead->addStretch();
    auto* testBtn = new QPushButton("测试加积分");
    testBtn->setObjectName("flat");
    testBtn->setToolTip("测试用：直接 +100 积分");
    testBtn->setCursor(Qt::PointingHandCursor);
    taskHead->addWidget(testBtn);
    connect(testBtn, &QPushButton::clicked, this, &MainWindow::petTestPoints);
    taskL->addLayout(taskHead);
    auto* taskHint = new QLabel("打工中会一直播放对应动画；关掉软件任务也照常计时，下次打开自动结算");
    taskHint->setObjectName("dim");
    taskL->addWidget(taskHint);
    m_petTaskLayout = new QVBoxLayout;
    m_petTaskLayout->setSpacing(6);
    taskL->addLayout(m_petTaskLayout);
    lay->addWidget(taskCard);

    // ---- 背包卡 ----
    m_petBagCard = new QFrame;
    m_petBagCard->setObjectName("card");
    auto* bagL = new QVBoxLayout(m_petBagCard);
    bagL->setContentsMargins(20, 14, 20, 14);
    bagL->setSpacing(8);
    auto* bagTitle = new QLabel("背包（点击使用，桌宠会当场表演吃相）");
    bagTitle->setStyleSheet("font-size:14px; font-weight:bold;");
    bagL->addWidget(bagTitle);
    m_petBagLayout = new QVBoxLayout;
    m_petBagLayout->setSpacing(6);
    bagL->addLayout(m_petBagLayout);
    lay->addWidget(m_petBagCard);

    // ---- 商店卡 ----
    auto* shopCard = new QFrame;
    shopCard->setObjectName("card");
    auto* shopL = new QVBoxLayout(shopCard);
    shopL->setContentsMargins(20, 14, 20, 14);
    shopL->setSpacing(8);
    auto* shopTitle = new QLabel("积分商店（积分来自每日签到）");
    shopTitle->setStyleSheet("font-size:14px; font-weight:bold;");
    shopL->addWidget(shopTitle);
    m_petShopLayout = new QVBoxLayout;
    m_petShopLayout->setSpacing(6);
    shopL->addLayout(m_petShopLayout);
    lay->addWidget(shopCard);
    lay->addStretch();

    return page;
}

void MainWindow::refreshPet() {
    const QJsonObject st = ls::petState();
    const int vals[4] = {st.value("hunger").toInt(), st.value("thirst").toInt(),
                         st.value("energy").toInt(), st.value("mood").toInt()};
    for (int i = 0; i < 4; ++i)
        if (m_petBar[i]) m_petBar[i]->setValue(vals[i]);
    m_petSleeping = st.value("sleeping").toBool();
    pet::setRemoteSleeping(m_petSleeping); // 跨重启恢复睡觉态（幂等：状态相同直接 return，不重复气泡）
    m_petName = st.value("name").toString("谭少小楠娘");
    m_petTitle->setText(m_petName + " · 云养");
    m_petPoints->setText(QString::number(ls::profile().value("points").toInt()) + " 积分");
    m_petSleepBtn->setText(m_petSleeping ? "叫醒" : "哄睡");
    QString s;
    if (m_petSleeping) s = "Zzz…… 睡着呢（精力恢复中，其他消耗减慢）";
    else if (vals[0] < 20 || vals[1] < 20) s = "饿/渴得难受，快投喂一点什么吧！";
    else if (vals[2] < 20) s = "没电了……让它睡一会吧";
    else if (vals[3] < 25) s = "心情低落，陪它玩玩嘛";
    else if (vals[0] > 75 && vals[1] > 75 && vals[2] > 60 && vals[3] > 60)
        s = "吃好喝好，状态极佳！";
    else s = "状态还不错～";
    m_petState->setText("「" + m_petName + "」" + s);
    // 快饿扁/渴扁了：桌宠主动卖惨
    if (!m_petSleeping && (vals[0] < 15 || vals[1] < 15))
        pet::playRemote("深度思考碎碎念", vals[0] < 15 ? "饿饿…饭饭…QAQ" : "渴渴…QAQ");

    // 任务栏（本地）
    clearPetRows(m_petTaskLayout);
    m_taskBar = nullptr;
    m_taskLeft = nullptr;
    const QJsonObject task = st.value("task").toObject();
    if (!task.isEmpty()) {
        const QJsonObject def = petTaskById(task.value("id").toString());
        if (!def.isEmpty()) {
            auto* row = new QHBoxLayout;
            auto* nm = new QLabel(def.value("name").toString() + " · 打工中");
            nm->setStyleSheet("font-weight:bold; color:#ffc857;");
            row->addWidget(nm);
            auto* bar = new QProgressBar;
            bar->setRange(0, 100);
            bar->setTextVisible(false);
            bar->setFixedHeight(14);
            bar->setStyleSheet("QProgressBar{background:rgba(20,26,48,.08);border:none;border-radius:7px;}"
                               "QProgressBar::chunk{background:#ffc857;border-radius:7px;}");
            row->addWidget(bar, 1);
            auto* left = new QLabel;
            left->setObjectName("dim");
            row->addWidget(left);
            auto* cancel = new QPushButton("放弃");
            cancel->setCursor(Qt::PointingHandCursor);
            connect(cancel, &QPushButton::clicked, this, &MainWindow::petCancelTask);
            row->addWidget(cancel);
            m_petTaskLayout->addLayout(row);
            m_taskBar = bar;
            m_taskLeft = left;
        }
    } else {
        auto* empty = new QLabel("没有进行中的任务，接一个吧：");
        empty->setObjectName("dim");
        m_petTaskLayout->addWidget(empty);
        for (const QJsonValue& v : petTaskCatalog()) {
            const QJsonObject t = v.toObject();
            auto* row = new QHBoxLayout;
            auto* nm = new QLabel(t.value("name").toString());
            nm->setStyleSheet("font-weight:bold;");
            row->addWidget(nm);
            auto* desc = new QLabel(t.value("desc").toString());
            desc->setObjectName("dim");
            row->addWidget(desc, 1);
            const int dur = t.value("dur").toInt();
            auto* durL = new QLabel(dur % 60 == 0 ? QString("%1 分钟").arg(dur / 60)
                                                  : QString("%1 秒").arg(dur));
            durL->setObjectName("dim");
            row->addWidget(durL);
            auto* reward = new QLabel("+" + QString::number(t.value("reward").toInt()) + " 分");
            reward->setStyleSheet("color:#ffc857; font-weight:bold;");
            row->addWidget(reward);
            auto* go = new QPushButton("开工");
            go->setCursor(Qt::PointingHandCursor);
            const QString id = t.value("id").toString();
            connect(go, &QPushButton::clicked, this, [this, id] { petStartTask(id); });
            row->addWidget(go);
            m_petTaskLayout->addLayout(row);
        }
    }

    // 背包（本地）
    clearPetRows(m_petBagLayout);
    const QJsonObject bag = st.value("bag").toObject();
    if (bag.isEmpty()) {
        auto* empty = new QLabel("背包空空，去下面商店买点吃的吧");
        empty->setObjectName("dim");
        m_petBagLayout->addWidget(empty);
    }
    for (auto bit = bag.begin(); bit != bag.end(); ++bit) {
        const QJsonObject def = petItemById(bit.key());
        if (def.isEmpty()) continue;
        auto* row = new QHBoxLayout;
        auto* nm = new QLabel(QString("%1 ×%2").arg(def.value("name").toString()).arg(bit.value().toInt()));
        nm->setStyleSheet("font-weight:bold;");
        row->addWidget(nm);
        row->addStretch();
        auto* use = new QPushButton("使用");
        use->setCursor(Qt::PointingHandCursor);
        const QString id = bit.key();
        const QString name = def.value("name").toString();
        connect(use, &QPushButton::clicked, this, [this, id, name] { petUse(id, name); });
        row->addWidget(use);
        m_petBagLayout->addLayout(row);
    }

    // 商店（本地静态目录）
    clearPetRows(m_petShopLayout);
    const struct { const char* key; const char* title; } cats[] = {
        {"foods", "吃的"}, {"drinks", "喝的"}, {"toys", "玩的"},
    };
    for (const auto& c : cats) {
        auto* sec = new QLabel(QString("—— %1 ——").arg(c.title));
        sec->setObjectName("dim");
        sec->setAlignment(Qt::AlignCenter);
        m_petShopLayout->addWidget(sec);
        for (const QJsonValue& v : petShopCatalog().value(c.key).toArray()) {
            const QJsonObject it = v.toObject();
            auto* row = new QHBoxLayout;
            auto* nm = new QLabel(it.value("name").toString());
            nm->setStyleSheet("font-weight:bold;");
            row->addWidget(nm);
            auto* desc = new QLabel(it.value("desc").toString());
            desc->setObjectName("dim");
            row->addWidget(desc, 1);
            auto* eff = new QLabel(petEffectText(it));
            eff->setStyleSheet("color:#3dc97e; font-size:11px;");
            row->addWidget(eff);
            auto* price = new QLabel(QString::number(it.value("price").toInt()) + " 分");
            price->setStyleSheet("color:#ffc857; font-weight:bold;");
            row->addWidget(price);
            auto* buy = new QPushButton("购买");
            buy->setCursor(Qt::PointingHandCursor);
            const QString id = it.value("id").toString();
            connect(buy, &QPushButton::clicked, this, [this, id] { petBuy(id); });
            row->addWidget(buy);
            m_petShopLayout->addLayout(row);
        }
    }
}

void MainWindow::petBuy(const QString& id) {
    const QJsonObject def = petItemById(id);
    if (def.isEmpty()) return;
    const int price = def.value("price").toInt();
    QJsonObject prof = ls::profile();
    if (prof.value("points").toInt() < price) {
        QMessageBox::information(this, "云养宠物", "积分不够啦，先去签到攒积分吧");
        return;
    }
    prof.insert("points", prof.value("points").toInt() - price);
    ls::saveProfile(prof);
    QJsonObject st = ls::petState();
    QJsonObject bag = st.value("bag").toObject();
    bag.insert(id, bag.value(id).toInt() + 1);
    st.insert("bag", bag);
    ls::savePetState(st);
    pet::playRemote("点击回应-开心跃动", "买好" + def.value("name").toString() + "啦，快投喂我！");
    refreshPet();
}

void MainWindow::petUse(const QString& id, const QString& name) {
    QString cat;
    const QJsonObject def = petItemById(id, &cat);
    if (def.isEmpty()) return;
    QJsonObject st = ls::petState();
    QJsonObject bag = st.value("bag").toObject();
    if (bag.value(id).toInt() <= 0) {
        QMessageBox::information(this, "云养宠物", "背包里没有它，先去买一个吧");
        return;
    }
    bag.insert(id, bag.value(id).toInt() - 1);
    if (bag.value(id).toInt() <= 0) bag.remove(id);
    st.insert("bag", bag);
    if (st.value("sleeping").toBool() && cat != "toys") st.insert("sleeping", false); // 吃喝自动叫醒
    st.insert("hunger", (int)ls::clampStat(st.value("hunger").toDouble() + def.value("hunger").toDouble()));
    st.insert("thirst", (int)ls::clampStat(st.value("thirst").toDouble() + def.value("thirst").toDouble()));
    st.insert("energy", (int)ls::clampStat(st.value("energy").toDouble() + def.value("energy").toDouble()));
    st.insert("mood", (int)ls::clampStat(st.value("mood").toDouble() + def.value("mood").toDouble()));
    ls::savePetState(st);
    m_petSleeping = st.value("sleeping").toBool();
    // 桌宠当场表演
    pet::setRemoteSleeping(m_petSleeping);
    pet::playRemote(petItemAnim(id), petItemBubble(id));
    const bool isPlay = (cat == "toys");
    m_petState->setText(QString("刚%1了%2，看着它多开心～").arg(isPlay ? "玩" : "吃", name));
    refreshPet();
}

void MainWindow::petSleep(bool on) {
    QJsonObject st = ls::petState();
    st.insert("sleeping", on);
    st.insert("updated", double(QDateTime::currentMSecsSinceEpoch()));
    ls::savePetState(st);
    m_petSleeping = on;
    m_petSleepBtn->setText(on ? "叫醒" : "哄睡");
    pet::setRemoteSleeping(on);
    refreshPet();
}

void MainWindow::petRename() {
    bool ok = false;
    const QString nm = QInputDialog::getText(this, "给宠物改名", "新名字（16 字以内）：",
                                             QLineEdit::Normal, m_petName, &ok).trimmed();
    if (!ok || nm.isEmpty() || nm == m_petName) return;
    QJsonObject st = ls::petState();
    st.insert("name", nm);
    ls::savePetState(st);
    m_petName = nm;
    m_petTitle->setText(nm + " · 云养");
    m_petState->setText("「" + nm + "」很喜欢新名字！");
    pet::playRemote("点击回应-开心跃动", "以后叫我「" + nm + "」！");
    refreshPet();
}

void MainWindow::taskTick() {
    if (m_stack->currentIndex() != 6) return;
    const QJsonObject st = ls::petState();
    const QJsonObject task = st.value("task").toObject();
    if (task.isEmpty()) return;
    const QJsonObject def = petTaskById(task.value("id").toString());
    if (def.isEmpty()) return;
    const qint64 start = qint64(task.value("start").toDouble());
    const qint64 endMs = start + qint64(task.value("dur").toInt()) * 1000;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now >= endMs) { // 到点结算（关软件重开也会在这里补结算）
        const int reward = def.value("reward").toInt();
        QJsonObject prof = ls::profile();
        prof.insert("points", prof.value("points").toInt() + reward);
        ls::saveProfile(prof);
        QJsonObject st2 = ls::petState();
        st2.remove("task");
        ls::savePetState(st2);
        pet::setRemoteWork("");
        pet::playRemote("点击回应-开心跃动", QString("任务完成！+%1 积分！").arg(reward));
        refreshPet();
        return;
    }
    if (m_taskBar) {
        const qint64 span = std::max<qint64>(1, endMs - start);
        m_taskBar->setValue(int(100 * (now - start) / span));
        const int remain = int(std::max<qint64>(0, (endMs - now) / 1000));
        if (m_taskLeft)
            m_taskLeft->setText(QString("剩余 %1:%2").arg(remain / 60).arg(remain % 60, 2, 10, QChar('0')));
    }
}

void MainWindow::petStartTask(const QString& id) {
    const QJsonObject def = petTaskById(id);
    if (def.isEmpty()) return;
    if (m_petSleeping) {
        QMessageBox::information(this, "任务栏", "宠物在睡觉，先叫醒它吧");
        return;
    }
    QJsonObject st = ls::petState();
    if (!st.value("task").toObject().isEmpty()) {
        QMessageBox::information(this, "任务栏", "已经有任务在进行中啦");
        return;
    }
    QJsonObject task;
    task.insert("id", id);
    task.insert("start", double(QDateTime::currentMSecsSinceEpoch()));
    task.insert("dur", def.value("dur").toInt());
    st.insert("task", task);
    ls::savePetState(st);
    pet::setRemoteWork(def.value("anim").toString());
    pet::playRemote(def.value("anim").toString(), "开工啦！");
    refreshPet();
}

void MainWindow::petCancelTask() {
    QJsonObject st = ls::petState();
    if (st.value("task").toObject().isEmpty()) return;
    st.remove("task");
    ls::savePetState(st);
    pet::setRemoteWork("");
    pet::playRemote("被吓一跳", "罢工了…积分也没了");
    refreshPet();
}

void MainWindow::petTestPoints() {
    QJsonObject prof = ls::profile();
    prof.insert("points", prof.value("points").toInt() + 100);
    ls::saveProfile(prof);
    pet::playRemote("点击回应-元气挥手", "测试积分 +100！");
    refreshPet();
}

// ==================== 工具箱扩展：便签 / 翻译 / 截图 / OCR ====================

} // namespace tb
