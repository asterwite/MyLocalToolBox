// ToolBox Qt 客户端主窗口：纯本地应用
// 主页(我的工具箱：签到/帮助资源) + 工具箱 + 云养宠物 + 设置
#pragma once
#include <QMainWindow>
#include <QJsonArray>
#include <QJsonObject>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QGridLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVector>
#include <QMap>
#include <QPointer>
#include <QElapsedTimer>
#include <QQueue>
#include "notewindow.h"
#include "toast.h"
#include <functional>

class QCloseEvent;
class QLabel;
class QMenu;
class QVBoxLayout;
class QTextEdit;
class QListWidgetItem;
class QPushButton;
class QComboBox;
class QPlainTextEdit;
class QProcess;
class QNetworkAccessManager;
class ShotOverlay;
class PickOverlay;
class RulerOverlay;

namespace ui { class AvatarLabel; }
namespace ui { class HotkeyFilter; }
class QTreeWidget;
class QCheckBox;
class QProgressBar;
class QSpinBox;
class QDoubleSpinBox;
class QDateEdit;
class QTimeEdit;
class QTextBrowser;
class QEvent;
class QFile;
class QProgressBar;
class QNetworkReply;

namespace tb {

// 白噪音联动 API（实现 tools_life.cpp）
void playNoise(int kind);
void stopNoise();

inline const char* kAppVersion = "1.1.0.0";
inline const char* kAppAuthor  = "ToolBox Dev Team";
inline const char* kBuildDate  = __DATE__;
inline const char* kBuildTime  = __TIME__;
inline const char* kQQGroup    = "963707322";

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* e) override; // X = 隐藏到托盘

private slots:
    void switchPage(int index);
    void refreshHome();
    void doCheckin();
    void copyHwid();
    void refreshPet();
    void petBuy(const QString& id);
    void petUse(const QString& id, const QString& name);
    void petSleep(bool on);
    void petRename();
    void petStartTask(const QString& id);
    void petCancelTask();
    void petTestPoints();
    void taskTick();

private:
    // ============================================================
    // MainWindow 结构导览（实现按领域分文件，本类只放声明）：
    //   mainwindow.cpp      壳/导航/主页/设置/云养 + 工具组页工厂（601 行）
    //   tools_system.cpp    系统信息/连点器/剪贴板/记账本/进程监控
    //   tools_desktop.cpp   便签/截图/番茄钟/取色器/单位换算/快捷启动
    //   tools_text.cpp      脱敏/加解密/开发工具箱/文本对比
    //   tools_intel.cpp     翻译/OCR
    //   tools_network.cpp   HTTP/汇率/网络诊断/下载器/天气/网速测试
    //   tools_files.cpp     快捷启动/重命名/重复/转码/关机/启动项/搜索/磁盘/图片
    //   tools_life.cpp      待办/日程/习惯/笔记/朗读/白噪音
    //   tools_vault.cpp     密码库
    //   (公共小构件 tools_common.h；全屏遮罩 shotoverlay/pickoverlay；通知 toast.h)
    // ============================================================
    QWidget* buildHomePage();
    QWidget* buildSettingsPage();
    QWidget* m_settingsPage = nullptr; // 设置页指针（重建设置页时按实际位置定位）
    QWidget* buildPetPage();
    // 工具组页工厂 + 六个领域大类页（26 个工具拆分归属）
    QWidget* buildGroupPage(const QString& title, const QString& sub,
                            const QVector<QPair<QString, QWidget* (MainWindow::*)()>>& tabs);
    QWidget* buildSysToolsPage();
    QWidget* buildLifeToolsPage();
    QWidget* buildTextToolsPage();
    QWidget* buildNetToolsPage();
    QWidget* buildMediaToolsPage();
    QWidget* buildFileToolsPage();
    QWidget* buildSysInfoTab();
    QWidget* buildClickerTab();
    QWidget* buildClipTab();
    QWidget* buildLedgerTab();
    void refreshLedger();
    void renderClipList();
    QWidget* buildEdgeTab();
    void edgeTick();
    void edgeKillAll(const QString& reason);
    void edgeLogLine(const QString& s);
    QWidget* buildNotesTab();
    QWidget* buildTransTab();
    QWidget* buildShotTab();
    QWidget* buildOcrTab();
    QWidget* buildMaskTab();
    QWidget* buildCryptoTab();
    QWidget* buildDevTab();
    QWidget* buildPomoTab();
    QWidget* buildPickTab();
    QWidget* buildUnitTab();
    QWidget* buildQuickTab();
    QWidget* buildDiffTab();
    QWidget* buildRenameTab();
    QWidget* buildDupTab();
    QWidget* buildTranscodeTab();
    QWidget* buildShutdownTab();
    QWidget* buildTtsTab();
    QWidget* buildStartupTab();
    QWidget* buildImgTab();
    QWidget* buildDiceTab();
    QWidget* buildDateTab();
    QWidget* buildPortTab();
    QWidget* buildSnippetTab();
    QWidget* buildWorldTab();
    bool eventFilter(QObject* obj, QEvent* e) override;
    QWidget* buildCleanupTab();
    QWidget* buildCheckTab();
    QWidget* buildUrlSafetyTab();
    QWidget* buildDownloadTab();
    QWidget* buildWeatherTab();
    QWidget* buildSpeedTab();
    QWidget* buildNoiseTab();
    QWidget* buildFileSearchTab();
    QWidget* buildVaultPage();
    QWidget* buildDiskPage();
    QWidget* buildHttpTab();
    // 生活类大类页（tools_life.cpp）
    QWidget* buildTodoPage();
    QWidget* buildSchedulePage();
    QWidget* buildHabitPage();
    QWidget* buildNotePage();
    void renderTodo();
    void renderSchedule();
    void scheduleTick();
    void renderHabits();
    void renderPaperList();
    void renderLedgerChart();
    // 联网工具（tools_network.cpp）
    QWidget* buildFxTab();
    QWidget* buildNetTab();
    void pomoTick();
    void pomoFinish(bool workDone);
    void saveNotes();
    void renderNoteList();
    void openNote(qint64 id);
    void startShot(int delayMs);
    void shotFinished(const QPixmap& pix);
    void runOcr(const QString& imgPath);
    void translateGo();
    QString aesProcess(const QString& mode, const QString& pass, const QString& data, QString* err);
    void showAbout();
    void refreshSettingsPage();
    void loadLedger();
    void saveLedger();
    QString myUid() const;
    void refreshCheckin();
    void refreshOverview();
    void applyGlobalHotkeys();

    // 外壳
    QStackedWidget* m_stack = nullptr;
    QSystemTrayIcon* m_tray = nullptr;
    QTimer m_remindTimer;          // 健康提醒节拍（30s 检查一次）
    qint64 m_remindSitLast = 0;    // 上次久坐提醒（ms epoch）
    qint64 m_remindWaterLast = 0;  // 上次喝水提醒
    QVector<QPushButton*> m_navButtons;
    QVector<int> m_navPages; // 按钮位置 → 页码（顺序与页码解耦）
    QLabel* m_userLabel = nullptr;
    QLabel* m_statusLabel = nullptr;
    ui::AvatarLabel* m_avatar = nullptr;
    QLabel* m_hwidEdit = nullptr;
    QLabel* m_overviewLabel = nullptr;
    QFrame* m_overviewCard = nullptr;

    // 主页
    QLabel* m_homeUser = nullptr;
    QLabel* m_homeLicense = nullptr;
    QLabel* m_checkinPts = nullptr;
    QLabel* m_checkinStreak = nullptr;
    QPushButton* m_checkinBtn = nullptr;
    bool m_checkinLoaded = false;
    bool m_checkinPending = false;

    // 各工具组页（每组一个局部 stack，不再共用 m_toolsStack）
    QProgressBar* m_sysCpuBar = nullptr;
    QLabel* m_sysCpuPct = nullptr;
    QProgressBar* m_sysRamBar = nullptr;
    QLabel* m_sysRamPct = nullptr;
    QLabel* m_sysRamDetail = nullptr;
    QLabel* m_sysUptime = nullptr;
    QTimer m_infoTimer;
    QTimer* m_clickerTimer = nullptr;
    ui::HotkeyFilter* m_hotkeyFilter = nullptr;
    QPushButton* m_clickerBtn = nullptr;
    QLabel* m_clickerCount = nullptr;
    QPoint m_clickPos;
    bool m_clickFixed = false;
    long long m_clicks = 0;
    QStringList m_clipHistory;
    QListWidget* m_clipList = nullptr;
    QLabel* m_clipHint = nullptr;
    struct LedgerRec {
        long long id = 0;
        QString type, category, note, date, time;
        double amount = 0;
    };
    QVector<LedgerRec> m_ledger;
    QListWidget* m_ledgerList = nullptr;
    QLabel* m_ledgerIn = nullptr;
    QLabel* m_ledgerOut = nullptr;
    QLabel* m_ledgerNet = nullptr;
    QLabel* m_ledgerChart = nullptr; // 本月每日支出条形图
    int m_ledgerMonthOffset = 0;     // 图表月份偏移（0=本月，负=过去）
    // 文本朗读（tools_life.cpp）
    QProcess* m_ttsProc = nullptr;
    // 下载器（tools_network.cpp）—— 多线程分块（Range），不支持分块的源自动回落单线程
    QVector<QNetworkReply*> m_dlReplies;  // 各分块请求
    QVector<qint64> m_dlBlockStart;       // 各块文件偏移
    QVector<qint64> m_dlBlockGot;         // 各块已下载字节
    QFile* m_dlFile = nullptr;
    qint64 m_dlTotal = 0;
    int m_dlDone = 0;   // 完成块数
    int m_dlBlocks = 0; // 总块数
    QMetaObject::Connection m_dlOpenConn; // toast「查看」→ 打开文件夹
    QProgressBar* m_dlBar = nullptr;
    QLabel* m_dlState = nullptr;
    qint64 m_dlLast = 0;      // 上次采样已下载字节
    QElapsedTimer m_dlClock;  // 速度采样
    // 断点续传 + 队列
    bool m_dlResume = false;
    QString m_dlSavedPart, m_dlSavedSide, m_dlLastPath;
    int m_dlMeterTick = 0;
    QQueue<QString> m_dlQueue;
    QLineEdit* m_dlQueueInput = nullptr;
    QListWidget* m_dlQueueList = nullptr;
    QPushButton* m_dlOpenDir = nullptr;
    void refreshQueueList();

    // Edge 看门狗
    QTimer* m_edgeTimer = nullptr;
    QLabel* m_edgeState = nullptr;
    QLabel* m_edgeCpu = nullptr;
    QLabel* m_edgeMem = nullptr;
    QLabel* m_edgeAction = nullptr;
    QTreeWidget* m_edgeTree = nullptr;
    QTextEdit* m_edgeLog = nullptr;
    QLineEdit* m_edgeProc = nullptr;
    QSpinBox* m_edgeCpuThr = nullptr;
    QDoubleSpinBox* m_edgeMemThr = nullptr;
    QSpinBox* m_edgeGap = nullptr;
    QCheckBox* m_edgeAuto = nullptr;
    QPushButton* m_edgeBtn = nullptr;
    QMap<quint32, QPair<quint64, quint64>> m_edgePrev; // pid -> (cpuTime 100ns, tickMs)
    quint64 m_edgeLastTick = 0;

    // 身份（本地：uid 即 HWID 标识）
    QString m_uid;

    // 云养宠物
    QProgressBar* m_petBar[4] = {nullptr, nullptr, nullptr, nullptr};
    QLabel* m_petTitle = nullptr;
    QLabel* m_petState = nullptr;
    QLabel* m_petPoints = nullptr;
    QPushButton* m_petSleepBtn = nullptr;
    QVBoxLayout* m_petShopLayout = nullptr;
    QVBoxLayout* m_petBagLayout = nullptr;
    QVBoxLayout* m_petTaskLayout = nullptr;
    QProgressBar* m_taskBar = nullptr;
    QLabel* m_taskLeft = nullptr;
    QWidget* m_petBagCard = nullptr;
    bool m_petLoaded = false;
    bool m_petSleeping = false;
    QString m_petName = "谭少小楠娘";
    QTimer m_petTimer;
    QTimer m_taskTimer;

    // 便签
    QMap<qint64, NoteWindow*> m_notes; // id → 便签窗（含隐藏的）
    QListWidget* m_noteList = nullptr;

    // 截图
    QPointer<ShotOverlay> m_overlay;
    QPixmap m_shotPixmap;
    QLabel* m_shotPreview = nullptr;
    QLabel* m_shotState = nullptr;
    QTimer m_shotDelay;

    // 文字识别
    QProcess* m_ocrProc = nullptr;
    QProgressBar* m_ocrBar = nullptr;
    QPlainTextEdit* m_ocrOut = nullptr;
    QLabel* m_ocrState = nullptr;

    // 翻译
    QNetworkAccessManager* m_net = nullptr;
    QPlainTextEdit* m_transIn = nullptr;
    QPlainTextEdit* m_transOut = nullptr;
    QLabel* m_transState = nullptr;
    QComboBox* m_transLang = nullptr;

    // 文本脱敏
    QPlainTextEdit* m_maskIn = nullptr;
    QPlainTextEdit* m_maskOut = nullptr;

    // 加解密
    QLineEdit* m_cryptoPass = nullptr;
    QPlainTextEdit* m_cryptoText = nullptr;
    QLabel* m_cryptoState = nullptr;

    // 番茄钟
    QTimer* m_pomoTimer = nullptr;
    QLabel* m_pomoBig = nullptr;
    QLabel* m_pomoState = nullptr;
    QLabel* m_pomoCount = nullptr;
    QLabel* m_pomoChart = nullptr;
    void renderPomoChart();
    QSpinBox* m_pomoWorkMin = nullptr;
    QSpinBox* m_pomoRestMin = nullptr;
    int m_pomoLeft = 0;      // 剩余秒
    bool m_pomoWorking = true;
    bool m_pomoRunning = false;

    // 取色器 / 屏幕测距
    QPointer<PickOverlay> m_picker;
    QPointer<RulerOverlay> m_ruler;
    QListWidget* m_colorList = nullptr;

    // 快捷启动
    QListWidget* m_quickList = nullptr;

    // 文本对比
    QPlainTextEdit* m_diffA = nullptr;
    QPlainTextEdit* m_diffB = nullptr;
    QTextBrowser* m_diffOut = nullptr;

    // 批量重命名 / 重复文件（实现见 tools_files.cpp）
    QLabel* m_dupState = nullptr;
    QProgressBar* m_fileBar = nullptr; // 文件类工具共用的忙碌条
    QTreeWidget* m_dupTree = nullptr;

    // 秒表（番茄钟页，实现见主文件 buildPomoTab）
    QTimer* m_swTimer = nullptr;
    QElapsedTimer m_swElapsed;
    QLabel* m_swBig = nullptr;
    QListWidget* m_swLaps = nullptr;
    bool m_swRunning = false;

    // 生活类大类页（实现见 tools_life.cpp）
    // 待办
    QLineEdit* m_todoInput = nullptr;
    QComboBox* m_todoPrio = nullptr;
    QListWidget* m_todoList = nullptr;
    QListWidget* m_todoDone = nullptr;
    QLabel* m_todoStat = nullptr;
    bool m_todoBusy = false;
    // 日程
    QLineEdit* m_cdName = nullptr;
    QDateEdit* m_cdDate = nullptr;
    QListWidget* m_cdList = nullptr;
    QLineEdit* m_remName = nullptr;
    QTimeEdit* m_remTime = nullptr;
    QListWidget* m_remList = nullptr;
    QTimer m_schedTimer; // 每日提醒节拍（30s）
    // 习惯打卡
    QVBoxLayout* m_habitArea = nullptr;
    QLabel* m_habitStat = nullptr;
    // 代码片段（tools_text.cpp）
    QListWidget* m_snipList = nullptr;
    QLineEdit* m_snipName = nullptr;
    QPlainTextEdit* m_snipEdit = nullptr;
    // 倒计时（番茄钟页）/ 世界时钟走秒闸门
    QTimer* m_worldTimer = nullptr;
    QPointer<QWidget> m_worldPage;
    QPointer<QWidget> m_weatherPage;
    QString m_weatherDefault;
    std::function<void(const QString&)> m_weatherQuery;
    QTimer* m_cdTimer = nullptr;
    // 全局快捷键 / 剪贴板搜索
    ui::HotkeyFilter* m_hkShot = nullptr;  // Ctrl+Alt+A 截图
    ui::HotkeyFilter* m_hkPet = nullptr;   // Ctrl+Alt+P 召唤桌宠
    QLineEdit* m_clipSearch = nullptr;
    QLabel* m_cdBig = nullptr;
    int m_cdLeft = 0;
    QCheckBox* m_pomoNoiseCk = nullptr;
    // 笔记
    QListWidget* m_paperList = nullptr;
    QLineEdit* m_paperTitle = nullptr;
    QPlainTextEdit* m_paperEdit = nullptr;
    QLabel* m_paperState = nullptr;
    QTimer m_paperSave;
    qint64 m_paperId = 0;
    // 磁盘助手（tools_files.cpp）
    QLabel* m_diskState = nullptr;
    QLabel* m_imgPreview = nullptr;
    QListWidget* m_diskDirs = nullptr;
    QListWidget* m_diskFiles = nullptr;
    // 密码库（tools_vault.cpp）
    bool m_vaultUnlocked = false;
    bool m_vaultExists = false;
    QString m_vaultKey;
    QJsonArray m_vaultItems;
    QLineEdit* m_vaultPass = nullptr;
    QLineEdit* m_vaultPass2 = nullptr;
    QLabel* m_vaultState = nullptr;
    QWidget* m_vaultBody = nullptr;
    QLineEdit* m_vaultSearch = nullptr;
    QListWidget* m_vaultList = nullptr;
    QTimer* m_vaultAutoLock = nullptr; // 10 分钟无操作自动锁定
};

} // namespace tb
