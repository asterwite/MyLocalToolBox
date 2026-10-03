// 系统工具：系统信息 / 连点器 / 剪贴板历史 / 记账本 / 进程监控器
// 由 mainwindow.cpp 模块化拆分（2026-10-03）：本文件实现 MainWindow 对应成员。
#include "mainwindow.h"
#include "tools_common.h"
#include "localstore.h"
#include "theme.h"
#include "widgets.h"
#include "sysinfo.h"
#include "edgeproc.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDate>
#include <QDateTime>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProgressBar>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QScreen>
#include <QScrollBar>
#include <QSpinBox>
#include <QSystemTrayIcon>
#include <QTextBrowser>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QCheckBox>
#include <QButtonGroup>
#include <QKeySequenceEdit>
#include <QTreeWidget>
#include <QUuid>
#include <QVBoxLayout>
#include "petwindow.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>

namespace tb {
static void sendClick(bool right) {
    INPUT in[2] = {};
    in[0].type = INPUT_MOUSE;
    in[1].type = INPUT_MOUSE;
    if (right) {
        in[0].mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
        in[1].mi.dwFlags = MOUSEEVENTF_RIGHTUP;
    } else {
        in[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        in[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    }
    SendInput(2, in, sizeof(INPUT));
}

// ---------- 工具组页工厂：左侧小导航 + 右内容（各领域大类页共用） ----------

QWidget* MainWindow::buildSysInfoTab() {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* body = new QWidget;
    auto* grid = new QGridLayout(body);
    grid->setContentsMargins(0, 4, 0, 0);
    grid->setSpacing(12);
    for (int i = 0; i < 2; ++i) grid->setColumnStretch(i, 1);

    // 处理器
    {
        auto* card = new QFrame;
        card->setObjectName("card");
        auto* lay = new QVBoxLayout(card);
        lay->setContentsMargins(18, 14, 18, 14);
        lay->setSpacing(7);
        auto* t = new QLabel("处理器");
        t->setObjectName("author");
        lay->addWidget(t);
        auto* name = new QLabel(sysinfo::cpuName());
        name->setObjectName("postTitle");
        name->setWordWrap(true);
        lay->addWidget(name);
        lay->addWidget(new QLabel(QString("逻辑核心 %1 · %2")
                                      .arg(sysinfo::cpuCores())
                                      .arg(QSysInfo::currentCpuArchitecture())));
        auto* row = new QHBoxLayout;
        m_sysCpuBar = new QProgressBar;
        m_sysCpuBar->setTextVisible(false);
        m_sysCpuBar->setRange(0, 100);
        row->addWidget(m_sysCpuBar, 1);
        m_sysCpuPct = new QLabel("-");
        row->addWidget(m_sysCpuPct);
        lay->addLayout(row);
        auto* cap = new QLabel("当前占用");
        cap->setObjectName("dim");
        lay->addWidget(cap);
        grid->addWidget(card, 0, 0);
    }
    // 内存
    {
        auto* card = new QFrame;
        card->setObjectName("card");
        auto* lay = new QVBoxLayout(card);
        lay->setContentsMargins(18, 14, 18, 14);
        lay->setSpacing(7);
        auto* t = new QLabel("内存");
        t->setObjectName("author");
        lay->addWidget(t);
        sysinfo::MemInfo mem = sysinfo::memory();
        auto* total = new QLabel(QString("%1 GB").arg(QString::number(mem.totalGB, 'f', 1)));
        total->setObjectName("statNum");
        lay->addWidget(total);
        auto* row = new QHBoxLayout;
        m_sysRamBar = new QProgressBar;
        m_sysRamBar->setTextVisible(false);
        m_sysRamBar->setRange(0, 100);
        row->addWidget(m_sysRamBar, 1);
        m_sysRamPct = new QLabel("-");
        row->addWidget(m_sysRamPct);
        lay->addLayout(row);
        m_sysRamDetail = new QLabel("采样中…");
        m_sysRamDetail->setObjectName("dim");
        lay->addWidget(m_sysRamDetail);
        grid->addWidget(card, 0, 1);
    }
    // 显卡
    {
        auto* card = new QFrame;
        card->setObjectName("card");
        auto* lay = new QVBoxLayout(card);
        lay->setContentsMargins(18, 14, 18, 14);
        lay->setSpacing(7);
        auto* t = new QLabel("显卡");
        t->setObjectName("author");
        lay->addWidget(t);
        QStringList gpus = sysinfo::gpus();
        if (gpus.isEmpty()) gpus << "未检测到";
        for (const QString& g : gpus) {
            auto* l = new QLabel(g);
            l->setWordWrap(true);
            lay->addWidget(l);
        }
        grid->addWidget(card, 1, 0);
    }
    // 系统
    {
        auto* card = new QFrame;
        card->setObjectName("card");
        auto* lay = new QVBoxLayout(card);
        lay->setContentsMargins(18, 14, 18, 14);
        lay->setSpacing(7);
        auto* t = new QLabel("系统");
        t->setObjectName("author");
        lay->addWidget(t);
        lay->addWidget(new QLabel(sysinfo::osInfo()));
        auto* host = new QLabel("主机名：" + sysinfo::hostName());
        host->setObjectName("dim");
        lay->addWidget(host);
        auto* board = new QLabel("主板/机型：" + sysinfo::boardInfo());
        board->setObjectName("dim");
        board->setWordWrap(true);
        lay->addWidget(board);
        auto* upRow = new QHBoxLayout;
        auto* upCap = new QLabel("已开机");
        upRow->addWidget(upCap);
        m_sysUptime = new QLabel("-");
        m_sysUptime->setObjectName("statNum");
        upRow->addWidget(m_sysUptime);
        upRow->addStretch();
        lay->addLayout(upRow);
        grid->addWidget(card, 1, 1);
    }
    // 硬盘
    {
        auto* card = new QFrame;
        card->setObjectName("card");
        auto* lay = new QVBoxLayout(card);
        lay->setContentsMargins(18, 14, 18, 14);
        lay->setSpacing(9);
        auto* t = new QLabel("硬盘");
        t->setObjectName("author");
        lay->addWidget(t);
        for (const sysinfo::Disk& d : sysinfo::disks()) {
            double usedPct = 100.0 * (d.totalGB - d.freeGB) / d.totalGB;
            auto* l = new QLabel(QString("%1 %2 GB 可用 / 共 %3 GB")
                                     .arg(d.drive)
                                     .arg(QString::number(d.freeGB, 'f', 0))
                                     .arg(QString::number(d.totalGB, 'f', 0)));
            l->setObjectName("dim");
            lay->addWidget(l);
            auto* bar = new QProgressBar;
            bar->setTextVisible(false);
            bar->setRange(0, 100);
            bar->setValue(int(usedPct));
            lay->addWidget(bar);
        }
        grid->addWidget(card, 2, 0, 1, 2);
    }
    // 显示
    {
        auto* card = new QFrame;
        card->setObjectName("card");
        auto* lay = new QVBoxLayout(card);
        lay->setContentsMargins(18, 14, 18, 14);
        lay->setSpacing(7);
        auto* t = new QLabel("显示");
        t->setObjectName("author");
        lay->addWidget(t);
        lay->addWidget(new QLabel(sysinfo::screenInfo()));
        grid->addWidget(card, 3, 0, 1, 2);
    }
    // 电池（笔记本场景）
    {
        auto* card = new QFrame;
        card->setObjectName("card");
        auto* lay = new QVBoxLayout(card);
        lay->setContentsMargins(18, 14, 18, 14);
        lay->setSpacing(7);
        auto* t = new QLabel("电池");
        t->setObjectName("author");
        lay->addWidget(t);
        const sysinfo::BatteryInfo bi = sysinfo::battery();
        if (!bi.present) {
            lay->addWidget(new QLabel("未检测到电池（台式机）"));
        } else {
            auto* pct = new QLabel(QString("%1%").arg(bi.percent));
            pct->setObjectName("statNum");
            lay->addWidget(pct);
            auto* bar = new QProgressBar;
            bar->setRange(0, 100);
            bar->setValue(bi.percent);
            bar->setTextVisible(false);
            lay->addWidget(bar);
            lay->addWidget(new QLabel(bi.charging ? "正在充电" : "使用电池供电"));
        }
        grid->addWidget(card, 3, 1, 1, 2);
    }
    grid->setRowStretch(5, 1);
    scroll->setWidget(body);
    return scroll;
}

QWidget* MainWindow::buildClickerTab() {
    auto* card = new QFrame;
    card->setObjectName("card");
    auto* lay = new QVBoxLayout(card);
    lay->setContentsMargins(22, 18, 22, 18);
    lay->setSpacing(12);
    auto* t = new QLabel("连点器");
    t->setObjectName("heroTitle");
    lay->addWidget(t);
    auto* hint = new QLabel("点击作用于鼠标当前位置；设置固定位置后自动移动到目标坐标再点击。仅供自有软件/测试场景使用。");
    hint->setObjectName("dim");
    hint->setWordWrap(true);
    lay->addWidget(hint);

    auto* form = new QGridLayout;
    form->setHorizontalSpacing(16);
    form->setVerticalSpacing(10);
    int r = 0;
    form->addWidget(new QLabel("鼠标按键"), r, 0);
    auto* keyCombo = new QComboBox;
    keyCombo->addItems({"左键", "右键"});
    keyCombo->setFixedWidth(140);
    form->addWidget(keyCombo, r++, 1);
    form->addWidget(new QLabel("点击间隔（秒）"), r, 0);
    auto* intervalSec = new QDoubleSpinBox;
    intervalSec->setRange(0.05, 600.0);
    intervalSec->setDecimals(2);
    intervalSec->setSingleStep(0.10);
    intervalSec->setValue(0.10);
    intervalSec->setSuffix(" 秒");
    intervalSec->setFixedWidth(140);
    form->addWidget(intervalSec, r++, 1);
    form->addWidget(new QLabel("次数上限（0 = 无限）"), r, 0);
    auto* limit = new QSpinBox;
    limit->setRange(0, 999999);
    limit->setValue(0);
    limit->setFixedWidth(140);
    form->addWidget(limit, r++, 1);
    form->addWidget(new QLabel("固定位置"), r, 0);
    auto* posRow = new QHBoxLayout;
    posRow->setSpacing(10);
    auto* pickBtn = new QPushButton("选取位置 (3秒)");
    pickBtn->setObjectName("ghost");
    posRow->addWidget(pickBtn);
    auto* posLabel = new QLabel("跟随鼠标");
    posLabel->setObjectName("dim");
    posRow->addWidget(posLabel);
    posRow->addStretch();
    form->addLayout(posRow, r++, 1);
    form->addWidget(new QLabel("启动热键（全局）"), r, 0);
    auto* hkRow = new QHBoxLayout;
    hkRow->setSpacing(10);
    auto* seqEdit = new QKeySequenceEdit(QKeySequence("F6"));
    seqEdit->setFixedWidth(160);
    hkRow->addWidget(seqEdit);
    auto* hkStatus = new QLabel("按热键开始/停止连点，应用在后台同样生效");
    hkStatus->setObjectName("dim");
    hkRow->addWidget(hkStatus);
    hkRow->addStretch();
    form->addLayout(hkRow, r++, 1);
    lay->addLayout(form);

    auto* runRow = new QHBoxLayout;
    m_clickerBtn = new QPushButton("开始连点");
    m_clickerBtn->setFixedWidth(160);
    runRow->addWidget(m_clickerBtn);
    m_clickerCount = new QLabel("已点击 0 次");
    m_clickerCount->setObjectName("dim");
    runRow->addWidget(m_clickerCount);
    runRow->addStretch();
    lay->addLayout(runRow);
    lay->addStretch();

    m_clickerTimer = new QTimer(this);
    connect(m_clickerTimer, &QTimer::timeout, this, [this, keyCombo, limit] {
        if (m_clickFixed) SetCursorPos(m_clickPos.x(), m_clickPos.y());
        sendClick(keyCombo->currentIndex() == 1);
        ++m_clicks;
        m_clickerCount->setText(QString("已点击 %1 次").arg(m_clicks));
        if (limit->value() > 0 && m_clicks >= limit->value()) {
            m_clickerTimer->stop();
            m_clickerBtn->setText("开始连点");
        }
    });
    std::function<void()> toggle = [this, intervalSec]() {
        fprintf(stderr, "[hotkey] toggle executed, running=%d\n", m_clickerTimer->isActive());
        fflush(stderr);
        if (m_clickerTimer->isActive()) {
            m_clickerTimer->stop();
            m_clickerBtn->setText("开始连点");
        } else {
            m_clicks = 0;
            m_clickerCount->setText("已点击 0 次");
            m_clickerTimer->start(qBound(20, int(intervalSec->value() * 1000), 600000));
            m_clickerBtn->setText("停止");
        }
    };
    connect(m_clickerBtn, &QPushButton::clicked, this, [toggle] { toggle(); });

    // 注册/更新全局热键（成功后才绑定回调；过滤器只安装一次）
    auto applyHotkey = [this, seqEdit, hkStatus, toggle]() {
        if (!m_hotkeyFilter) {
            m_hotkeyFilter = new ui::HotkeyFilter;
            QCoreApplication::instance()->installNativeEventFilter(m_hotkeyFilter);
        }
        HWND hwnd = (HWND)winId();
        UnregisterHotKey(hwnd, 1);
        UINT mods = 0, vk = 0;
        if (!ui::keySeqToNative(seqEdit->keySequence(), &mods, &vk)) {
            hkStatus->setText("热键需包含 Ctrl/Alt/Shift，或直接使用 F 功能键");
            m_hotkeyFilter->onHotkey = nullptr;
            return;
        }
        if (!RegisterHotKey(hwnd, 1, mods, vk)) {
            hkStatus->setText("热键注册失败，可能被其他程序占用");
            m_hotkeyFilter->onHotkey = nullptr;
            return;
        }
        m_hotkeyFilter->onHotkey = toggle;
        hkStatus->setText("热键已生效（全局）：按下开始，再按停止");
    };
    connect(seqEdit, &QKeySequenceEdit::keySequenceChanged, this, [applyHotkey] { applyHotkey(); });
    QTimer::singleShot(0, this, [applyHotkey] { applyHotkey(); });
    connect(pickBtn, &QPushButton::clicked, this, [this, pickBtn, posLabel] {
        pickBtn->setEnabled(false);
        posLabel->setText("3 秒后捕获鼠标位置…");
        QTimer::singleShot(3000, this, [this, pickBtn, posLabel] {
            m_clickPos = QCursor::pos();
            m_clickFixed = true;
            posLabel->setText(QString("固定位置：%1, %2").arg(m_clickPos.x()).arg(m_clickPos.y()));
            pickBtn->setEnabled(true);
        });
    });
    return card;
}

void MainWindow::renderClipList() {
    if (!m_clipList) return;
    m_clipList->clear();
    const QString q = m_clipSearch ? m_clipSearch->text().trimmed() : QString();
    for (const QString& t : m_clipHistory) {
        if (!q.isEmpty() && !t.contains(q, Qt::CaseInsensitive)) continue;
        QString shown = t.simplified();
        if (shown.size() > 90) shown = shown.left(90) + "…";
        if (t.contains('\n')) shown.prepend("[多行] ");
        m_clipList->addItem(shown);
    }
    if (m_clipList->count() == 0)
        m_clipList->addItem(q.isEmpty() ? "暂无记录 · 复制任意文本会自动记录在这里" : "无匹配记录");
}

QWidget* MainWindow::buildClipTab() {
    auto* card = new QFrame;
    card->setObjectName("card");
    auto* lay = new QVBoxLayout(card);
    lay->setContentsMargins(22, 18, 22, 18);
    lay->setSpacing(12);
    auto* t = new QLabel("剪贴板历史");
    t->setObjectName("heroTitle");
    lay->addWidget(t);
    auto* hint = new QLabel("应用运行期间自动记录文本（最多 100 条，仅本机保存）· 点击条目复制");
    hint->setObjectName("dim");
    hint->setWordWrap(true);
    lay->addWidget(hint);

    auto* btnRow = new QHBoxLayout;
    auto* delBtn = new QPushButton("删除选中");
    delBtn->setObjectName("ghost");
    btnRow->addWidget(delBtn);
    auto* clearBtn = new QPushButton("清空全部");
    clearBtn->setObjectName("ghost");
    btnRow->addWidget(clearBtn);
    m_clipHint = new QLabel("");
    m_clipHint->setObjectName("dim");
    btnRow->addWidget(m_clipHint);
    btnRow->addStretch();
    lay->addLayout(btnRow);

    m_clipSearch = new QLineEdit;
    m_clipSearch->setPlaceholderText("搜索剪贴板历史…");
    m_clipSearch->setClearButtonEnabled(true);
    connect(m_clipSearch, &QLineEdit::textChanged, this, [this] { renderClipList(); });
    lay->addWidget(m_clipSearch);
    m_clipList = new QListWidget;
    connect(m_clipList, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) {
        int row = m_clipList->row(it);
        if (row < 0 || row >= m_clipHistory.size()) return;
        QApplication::clipboard()->setText(m_clipHistory[row]);
        m_clipHint->setText("已复制到剪贴板");
        QTimer::singleShot(1500, this, [this] { m_clipHint->setText(""); });
    });
    connect(delBtn, &QPushButton::clicked, this, [this] {
        int row = m_clipList->currentRow();
        if (row < 0 || row >= m_clipHistory.size()) return;
        m_clipHistory.removeAt(row);
        QSettings cst("ToolBox", "ToolBoxQt");
        cst.setValue("clipHistory", m_clipHistory);
        renderClipList();
    });
    connect(clearBtn, &QPushButton::clicked, this, [this] {
        m_clipHistory.clear();
        QSettings cst("ToolBox", "ToolBoxQt");
        cst.setValue("clipHistory", m_clipHistory);
        renderClipList();
    });
    lay->addWidget(m_clipList, 1);
    renderClipList();
    return card;
}

QWidget* MainWindow::buildLedgerTab() {
    auto* page = new QWidget;
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(0, 4, 0, 0);
    lay->setSpacing(12);

    // 汇总
    auto* sum = new QFrame;
    sum->setObjectName("card");
    auto* sumL = new QHBoxLayout(sum);
    sumL->setContentsMargins(20, 14, 20, 14);
    sumL->setSpacing(40);
    auto addStat = [&](const QString& cap, QLabel** out, const QString& color) {
        auto* box = new QVBoxLayout;
        auto* v = new QLabel("¥0.00");
        v->setStyleSheet(QString("font-size:20px; font-weight:bold; color:%1;").arg(color));
        *out = v;
        auto* c = new QLabel(cap);
        c->setObjectName("dim");
        box->addWidget(v);
        box->addWidget(c);
        sumL->addLayout(box);
    };
    addStat("本月收入", &m_ledgerIn, "#189d63");
    addStat("本月支出", &m_ledgerOut, "#ff5d5d");
    addStat("本月结余", &m_ledgerNet, "#242838");
    sumL->addStretch();
    lay->addWidget(sum);

    // 录入
    auto* form = new QFrame;
    form->setObjectName("card");
    auto* formL = new QHBoxLayout(form);
    formL->setContentsMargins(20, 14, 20, 14);
    formL->setSpacing(10);
    auto* typeCombo = new QComboBox;
    typeCombo->addItems({"支出", "收入"});
    formL->addWidget(typeCombo);
    auto* amount = new QDoubleSpinBox;
    amount->setRange(0.01, 9999999);
    amount->setDecimals(2);
    amount->setPrefix("¥ ");
    amount->setFixedWidth(130);
    formL->addWidget(amount);
    auto* cat = new QComboBox;
    cat->addItems({"餐饮", "交通", "购物", "娱乐", "居家", "工资", "红包", "其他"});
    formL->addWidget(cat);
    auto* note = new QLineEdit;
    note->setPlaceholderText("备注（可选）");
    formL->addWidget(note, 1);
    auto* addBtn = new QPushButton("添加记录");
    formL->addWidget(addBtn);
    lay->addWidget(form);

    // 记录列表
    auto* recCard = new QFrame;
    recCard->setObjectName("card");
    auto* recL = new QVBoxLayout(recCard);
    recL->setContentsMargins(20, 14, 20, 14);
    recL->setSpacing(10);
    auto* recRow = new QHBoxLayout;
    auto* recT = new QLabel("记录");
    recT->setObjectName("author");
    recRow->addWidget(recT);
    recRow->addStretch();
    auto* delBtn = new QPushButton("删除选中");
    delBtn->setObjectName("ghost");
    recRow->addWidget(delBtn);
    recL->addLayout(recRow);
    m_ledgerList = new QListWidget;
    recL->addWidget(m_ledgerList, 1);
    auto* chartHead = new QHBoxLayout;
    auto* prevM = new QPushButton("‹ 上月");
    prevM->setObjectName("flat");
    auto* nextM = new QPushButton("下月 ›");
    nextM->setObjectName("flat");
    chartHead->addWidget(prevM);
    chartHead->addStretch();
    chartHead->addWidget(nextM);
    recL->addLayout(chartHead);
    connect(prevM, &QPushButton::clicked, this, [this] { if (m_ledgerMonthOffset > -24) --m_ledgerMonthOffset; renderLedgerChart(); });
    auto* expCsv = new QPushButton("导出 CSV");
    expCsv->setObjectName("flat");
    chartHead->addWidget(expCsv);
    connect(expCsv, &QPushButton::clicked, this, [this] {
        if (m_ledger.isEmpty()) { QMessageBox::information(this, "导出", "还没有账目记录"); return; }
        const QString f = QFileDialog::getSaveFileName(this, "导出账目 CSV",
            "ToolBox账目.csv", "CSV (*.csv)");
        if (f.isEmpty()) return;
        QFile file(f);
        if (!file.open(QIODevice::WriteOnly)) { QMessageBox::warning(this, "导出", "无法创建文件"); return; }
        file.write("\xEF\xBB\xBF"); // BOM：Excel 打开中文不乱码
        auto esc = [](QString v) { v.replace("\"", "\"\""); if (v.contains(',') || v.contains('"') || v.contains('\n')) v = "\"" + v + "\""; return v; };
        file.write("日期,时间,类型,分类,金额,备注\r\n");
        for (const auto& r : m_ledger)
            file.write(QString("%1,%2,%3,%4,%5,%6\r\n")
                .arg(r.date, r.time, r.type == "income" ? "收入" : "支出", esc(r.category))
                .arg(QString::number(r.amount, 'f', 2), esc(r.note)).toUtf8());
        file.close();
        toast("记账本", QString("导出了 %1 条账目").arg(m_ledger.size()));
    });
    connect(nextM, &QPushButton::clicked, this, [this] { if (m_ledgerMonthOffset < 0) ++m_ledgerMonthOffset; renderLedgerChart(); });
    m_ledgerChart = new QLabel;
    m_ledgerChart->setAlignment(Qt::AlignCenter);
    m_ledgerChart->setMinimumHeight(120);
    recL->addWidget(m_ledgerChart);
    lay->addWidget(recCard, 1);

    connect(addBtn, &QPushButton::clicked, this, [this, typeCombo, amount, cat, note] {
        double v = amount->value();
        if (v <= 0) {
            QMessageBox::information(this, "记账", "请输入有效金额");
            return;
        }
        QJsonObject rec;
        rec.insert("id", double(QDateTime::currentMSecsSinceEpoch()));
        rec.insert("type", typeCombo->currentIndex() == 1 ? "income" : "expense");
        rec.insert("amount", std::round(v * 100) / 100.0);
        rec.insert("category", cat->currentText());
        rec.insert("note", note->text().trimmed());
        rec.insert("date", QDate::currentDate().toString("yyyy-MM-dd"));
        rec.insert("time", QTime::currentTime().toString("HH:mm"));
        QJsonArray arr = ls::ledgerRecords();
        arr.prepend(rec);
        ls::saveLedgerRecords(arr);
        refreshLedger();
        amount->setValue(0.01);
        note->clear();
    });
    connect(delBtn, &QPushButton::clicked, this, [this] {
        auto* it = m_ledgerList->currentItem();
        if (!it) return;
        long long id = it->data(Qt::UserRole).toLongLong();
        QJsonArray arr = ls::ledgerRecords();
        QJsonArray keep;
        for (const auto& v : arr)
            if ((long long)v.toObject().value("id").toDouble() != id)
                keep.append(v);
        ls::saveLedgerRecords(keep);
        refreshLedger();
    });
    refreshLedger();
    return page;
}

void MainWindow::refreshLedger() {
    {
        m_ledger.clear();
        QJsonArray arr = ls::ledgerRecords();
        double income = 0, expense = 0;
        QString month = QDate::currentDate().toString("yyyy-MM");
        if (m_ledgerList) m_ledgerList->clear();
        for (const auto& v : arr) {
            QJsonObject r = v.toObject();
            LedgerRec rec;
            rec.id = (long long)r.value("id").toDouble();
            rec.type = r.value("type").toString();
            rec.amount = r.value("amount").toDouble();
            rec.category = r.value("category").toString();
            rec.note = r.value("note").toString();
            rec.date = r.value("date").toString();
            rec.time = r.value("time").toString();
            m_ledger.push_back(rec);
            if (rec.date.startsWith(month)) {
                if (rec.type == "income") income += rec.amount;
                else expense += rec.amount;
            }
            QString line = QString("%1  %2  ·  %3%4 元  %5")
                               .arg(rec.date)
                               .arg(rec.time.isEmpty() ? QString("--:--") : rec.time)
                               .arg(rec.type == "income" ? QString("+") : QString("-"))
                               .arg(QString::number(rec.amount, 'f', 2))
                               .arg(rec.category + (rec.note.isEmpty() ? QString("") : QString(" · ") + rec.note));
            auto* it = new QListWidgetItem(line);
            it->setData(Qt::UserRole, (double)rec.id);
            it->setForeground(rec.type == "income" ? QColor("#189d63") : QColor("#eaecf2"));
            if (m_ledgerList) m_ledgerList->addItem(it);
        }
        if (m_ledgerList && m_ledgerList->count() == 0)
            m_ledgerList->addItem("还没有记录 · 添加第一笔记账吧（数据保存在本机）");
        if (m_ledgerIn) m_ledgerIn->setText(QString("¥%1").arg(QString::number(income, 'f', 2)));
        if (m_ledgerOut) m_ledgerOut->setText(QString("¥%1").arg(QString::number(expense, 'f', 2)));
        if (m_ledgerNet) m_ledgerNet->setText(QString("¥%1").arg(QString::number(income - expense, 'f', 2)));
        renderLedgerChart();
    }
}

// 本月每日支出条形图（QPainter 自绘，随账本刷新）

void MainWindow::renderLedgerChart() {
    if (!m_ledgerChart) return;
    const QDate monthDate = QDate::currentDate().addMonths(m_ledgerMonthOffset);
    const QString month = monthDate.toString("yyyy-MM");
    double day[32] = {0};
    double maxV = 0;
    for (const auto& rec : m_ledger) {
        if (rec.type != "expense" || !rec.date.startsWith(month)) continue;
        bool ok; const int d = rec.date.section('-', 2, 2).toInt(&ok);
        if (!ok || d < 1 || d > 31) continue;
        day[d] += rec.amount;
        if (day[d] > maxV) maxV = day[d];
    }
    const int W = 640, H = 130, pad = 26, bw = (W - pad * 2) / 31;
    QPixmap pm(W, H);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QFont f = p.font();
    f.setPixelSize(11);
    p.setFont(f);
    // 基线与刻度
    p.setPen(QColor(90, 100, 115));
    p.drawLine(pad, H - 22, W - pad, H - 22);
    bool any = false;
    for (int d = 1; d <= 31; ++d) {
        if (day[d] <= 0) continue;
        any = true;
        const int h = int((day[d] / maxV) * (H - 46));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 170, 255, 190));
        p.drawRoundedRect(pad + (d - 1) * bw + 1, H - 22 - h, bw - 2, h, 2, 2);
        p.setPen(QColor(150, 160, 175));
        p.drawText(QRect(pad + (d - 1) * bw - 4, H - 20, bw + 8, 14), Qt::AlignCenter, QString::number(d));
    }
    if (any) {
        p.setPen(QColor(140, 150, 165));
        p.drawText(QRect(pad, 4, W - pad * 2, 16), Qt::AlignLeft,
                   QString("%1 每日支出 · 最高单日 ¥%2").arg(monthDate.toString("yyyy年M月"), QString::number(maxV, 'f', 2)));
    } else {
        p.setPen(QColor(140, 150, 165));
        p.drawText(pm.rect(), Qt::AlignCenter, QString("%1 暂无支出记录").arg(monthDate.toString("yyyy年M月")));
    }
    m_ledgerChart->setPixmap(pm.scaled(m_ledgerChart->width() > 0 ? m_ledgerChart->width() : W, H,
                                       Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

// ---------------- 进程监控器（参考 Edge-Monitor：自动终止后台异常进程，默认盯防 msedge.exe） ----------------

QWidget* MainWindow::buildEdgeTab() {
    auto* page = new QWidget;
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(0, 4, 0, 0);
    lay->setSpacing(12);

    // 状态卡
    auto* sum = new QFrame;
    sum->setObjectName("card");
    auto* sumL = new QHBoxLayout(sum);
    sumL->setContentsMargins(20, 14, 20, 14);
    sumL->setSpacing(36);
    auto addStat = [&](const QString& cap, QLabel** out, const QString& color) {
        auto* box = new QVBoxLayout;
        auto* v = new QLabel("-");
        v->setStyleSheet(QString("font-size:19px; font-weight:bold; color:%1;").arg(color));
        *out = v;
        auto* c = new QLabel(cap);
        c->setObjectName("dim");
        box->addWidget(v);
        box->addWidget(c);
        sumL->addLayout(box);
    };
    addStat("监控状态", &m_edgeState, "#242838");
    addStat("监控进程 CPU", &m_edgeCpu, "#242838");
    addStat("监控进程内存", &m_edgeMem, "#242838");
    addStat("上次动作", &m_edgeAction, "#189d63");
    sumL->addStretch();
    lay->addWidget(sum);

    // 设置卡
    auto* cfg = new QFrame;
    cfg->setObjectName("card");
    auto* form = new QGridLayout(cfg);
    form->setContentsMargins(20, 14, 20, 14);
    form->setHorizontalSpacing(16);
    form->setVerticalSpacing(10);
    int r = 0;
    form->addWidget(new QLabel("监控进程名"), r, 0);
    m_edgeProc = new QLineEdit;
    m_edgeProc->setText("msedge.exe");
    m_edgeProc->setFixedWidth(180);
    form->addWidget(m_edgeProc, r++, 1);
    form->addWidget(new QLabel("CPU 阈值 (%)"), r, 0);
    m_edgeCpuThr = new QSpinBox;
    m_edgeCpuThr->setRange(0, 100);
    m_edgeCpuThr->setValue(30);
    m_edgeCpuThr->setFixedWidth(180);
    form->addWidget(m_edgeCpuThr, r++, 1);
    form->addWidget(new QLabel("内存阈值 (GB)"), r, 0);
    m_edgeMemThr = new QDoubleSpinBox;
    m_edgeMemThr->setRange(0.1, 64.0);
    m_edgeMemThr->setDecimals(1);
    m_edgeMemThr->setValue(2.0);
    m_edgeMemThr->setFixedWidth(180);
    form->addWidget(m_edgeMemThr, r++, 1);
    form->addWidget(new QLabel("检查间隔 (秒)"), r, 0);
    m_edgeGap = new QSpinBox;
    m_edgeGap->setRange(2, 3600);
    m_edgeGap->setValue(5);
    m_edgeGap->setFixedWidth(180);
    form->addWidget(m_edgeGap, r++, 1);
    form->addWidget(new QLabel("自动清理"), r, 0);
    m_edgeAuto = new QCheckBox("监控进程无可见窗口且超过阈值时自动终止");
    m_edgeAuto->setChecked(true);
    form->addWidget(m_edgeAuto, r++, 1);
    lay->addWidget(cfg);

    // 按钮行
    auto* btnRow = new QHBoxLayout;
    m_edgeBtn = new QPushButton("启动监控");
    m_edgeBtn->setFixedWidth(150);
    btnRow->addWidget(m_edgeBtn);
    auto* killNow = new QPushButton("立即清理后台进程");
    killNow->setObjectName("ghost");
    btnRow->addWidget(killNow);
    btnRow->addStretch();
    lay->addLayout(btnRow);

    // 进程列表
    m_edgeTree = new QTreeWidget;
    m_edgeTree->setColumnCount(5);
    m_edgeTree->setHeaderLabels({"PID", "进程", "内存", "CPU", "状态"});
    m_edgeTree->setRootIsDecorated(false);
    m_edgeTree->setFixedHeight(190);
    lay->addWidget(m_edgeTree);

    // 日志
    auto* logCard = new QFrame;
    logCard->setObjectName("card");
    auto* logL = new QVBoxLayout(logCard);
    logL->setContentsMargins(20, 14, 20, 14);
    logL->setSpacing(8);
    auto* logT = new QLabel("清理日志");
    logT->setObjectName("author");
    logL->addWidget(logT);
    m_edgeLog = new QTextEdit;
    m_edgeLog->setReadOnly(true);
    m_edgeLog->setFixedHeight(120);
    m_edgeLog->setPlaceholderText("清理动作会记录在这里");
    logL->addWidget(m_edgeLog);
    lay->addWidget(logCard);
    lay->addStretch();

    // 监控启停
    m_edgeTimer = new QTimer(this);
    connect(m_edgeTimer, &QTimer::timeout, this, [this] { edgeTick(); });
    connect(m_edgeBtn, &QPushButton::clicked, this, [this] {
        if (m_edgeTimer->isActive()) {
            m_edgeTimer->stop();
            m_edgeBtn->setText("启动监控");
            m_edgeState->setText("已停止");
        } else {
            m_edgeLastTick = 0;
            m_edgePrev.clear();
            m_edgeTimer->start(m_edgeGap->value() * 1000);
            m_edgeBtn->setText("停止监控");
            edgeTick();
        }
    });
    connect(killNow, &QPushButton::clicked, this, [this] { edgeKillAll("手动清理"); });
    return page;
}

void MainWindow::edgeTick() {
    QString name = m_edgeProc->text().trimmed();
    if (name.isEmpty()) return;
    if (!name.contains('.')) name += ".exe";
    QList<edgeproc::ProcInfo> list = edgeproc::enumerate(name);
    quint64 now = GetTickCount64();
    double cores = qMax(1, QThread::idealThreadCount());

    double totalCpu = 0, totalMemMB = 0;
    int bgCount = 0;
    QMap<quint32, QPair<quint64, quint64>> prev;
    m_edgeTree->clear();
    for (const edgeproc::ProcInfo& pi : list) {
        double cpu = 0;
        if (m_edgeLastTick && m_edgePrev.contains(pi.pid)) {
            quint64 dT = now - m_edgeLastTick;
            quint64 dP = pi.cpuTime - m_edgePrev[pi.pid].first;
            // dP 单位为 100ns：1 个核心跑满 dT 毫秒累计 dT*10000；按整机归一化（100% = 全部核心）
            if (dT > 0)
                cpu = qBound(0.0, double(dP) / (double(dT) * 10000.0 * cores) * 100.0, 100.0);
        }
        totalCpu += cpu;
        totalMemMB += double(pi.memWS) / (1024.0 * 1024.0);
        if (!pi.hasWindow) ++bgCount;
        prev.insert(pi.pid, {pi.cpuTime, now});
        auto* it = new QTreeWidgetItem;
        it->setText(0, QString::number(pi.pid));
        it->setText(1, pi.name);
        it->setText(2, QString::number(double(pi.memWS) / (1024.0 * 1024.0), 'f', 1) + " MB");
        it->setText(3, QString::number(cpu, 'f', 1) + " %");
        it->setText(4, pi.hasWindow ? "有窗口" : "后台");
        it->setForeground(4, pi.hasWindow ? QColor("#189d63") : QColor("#7c8294"));
        m_edgeTree->addTopLevelItem(it);
    }
    m_edgePrev = prev;
    m_edgeLastTick = now;

    m_edgeCpu->setText(QString::number(totalCpu, 'f', 1) + " %");
    m_edgeMem->setText(QString::number(totalMemMB / 1024.0, 'f', 2) + " GB");
    m_edgeState->setText(m_edgeTimer->isActive()
                             ? QString("监控中 · %1 个进程").arg(list.size())
                             : QString("已停止 · %1 个进程").arg(list.size()));

    // 自动清理判定（同 Edge-Monitor）：目标进程全部在后台运行，且 CPU 或内存超过阈值
    if (m_edgeTimer->isActive() && m_edgeAuto->isChecked() && !list.isEmpty() && bgCount == list.size()) {
        bool abnormal = totalCpu > double(m_edgeCpuThr->value()) ||
                        totalMemMB / 1024.0 > m_edgeMemThr->value();
        if (abnormal)
            edgeKillAll(QString("自动清理：CPU %1% / 内存 %2 GB 超阈值")
                            .arg(QString::number(totalCpu, 'f', 0))
                            .arg(QString::number(totalMemMB / 1024.0, 'f', 2)));
    }
}

void MainWindow::edgeKillAll(const QString& reason) {
    QString name = m_edgeProc->text().trimmed();
    if (name.isEmpty()) return;
    if (!name.contains('.')) name += ".exe";
    QList<edgeproc::ProcInfo> list = edgeproc::enumerate(name);
    QStringList pids;
    for (const edgeproc::ProcInfo& pi : list) {
        if (pi.hasWindow) continue; // 有可见窗口的进程不动
        if (edgeproc::killPid(pi.pid)) pids << QString::number(pi.pid);
    }
    QString line = QString("[%1] %2：%3")
                       .arg(QTime::currentTime().toString("hh:mm:ss"))
                       .arg(reason)
                       .arg(pids.isEmpty() ? QString("无后台进程可终止")
                                           : QString("终止 %1 个后台进程 (PID %2)")
                                                 .arg(pids.size())
                                                 .arg(pids.join(", ")));
    edgeLogLine(line);
    m_edgeAction->setText(pids.isEmpty() ? QString("无可清理") : QString("终止 %1 个").arg(pids.size()));
    m_edgeAction->setStyleSheet(pids.isEmpty()
                                    ? "font-size:19px; font-weight:bold; color:#242838;"
                                    : "font-size:19px; font-weight:bold; color:#ff5d5d;");
}

void MainWindow::edgeLogLine(const QString& s) {
    if (!m_edgeLog) return;
    QString nl(QChar(10));
    QStringList lines = m_edgeLog->toPlainText().split(nl);
    lines.prepend(s);
    while (lines.size() > 200) lines.removeLast();
    m_edgeLog->setPlainText(lines.join(nl));
}

// ---------------- 关于 ----------------

} // namespace tb
