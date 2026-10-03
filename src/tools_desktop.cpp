// 桌面效率：便签 / 截图 / 番茄钟 / 取色器 / 单位换算 / 快捷启动
// 由 mainwindow.cpp 模块化拆分（2026-10-03）：本文件实现 MainWindow 对应成员。
#include "mainwindow.h"
#include "theme.h"
#include "tools_common.h"
#include "localstore.h"
#include "petwindow.h"

#include <QApplication>
#include <QClipboard>
#include <QCheckBox>
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
#include <QTreeWidget>
#include <QUuid>
#include <QVBoxLayout>
#include "shotoverlay.h"
#include "pickoverlay.h"
#include "ruleroverlay.h"

#include <QPainter>
#include <algorithm>

namespace tb {

void MainWindow::saveNotes() {
    QJsonObject root;
    QJsonArray arr;
    for (auto it = m_notes.constBegin(); it != m_notes.constEnd(); ++it) {
        NoteWindow* w = it.value();
        QJsonObject o;
        o.insert("id", (double)it.key());
        o.insert("text", w->text());
        o.insert("color", w->m_color);
        o.insert("pinned", w->m_pinned);
        o.insert("x", w->x());
        o.insert("y", w->y());
        arr.append(o);
    }
    root.insert("items", arr);
    ls::saveJson("notes.json", root);
    if (m_noteList) renderNoteList();
}

void MainWindow::openNote(qint64 id) {
    NoteWindow* w = m_notes.value(id, nullptr);
    if (!w) return;
    w->show();
    w->raise();
    w->activateWindow();
}

QWidget* MainWindow::buildNotesTab() {
    // 首次进入：从 notes.json 载入既有便签（不自动显示）
    if (m_notes.isEmpty()) {
        const QJsonArray items = ls::loadJson("notes.json").value("items").toArray();
        for (const QJsonValue& v : items) {
            const QJsonObject o = v.toObject();
            const qint64 id = (qint64)o.value("id").toDouble();
            auto* w = new NoteWindow(id, o.value("text").toString(), o.value("color").toString("yellow"),
                                     QPoint(o.value("x").toInt(), o.value("y").toInt()),
                                     o.value("pinned").toBool(true));
            connect(w, &NoteWindow::changed, this, [this] { saveNotes(); });
            m_notes.insert(id, w);
        }
    }

    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* body = new QWidget;
    auto* v = new QVBoxLayout(body);
    v->setContentsMargins(0, 4, 12, 0);
    v->setSpacing(12);

    {
        QVBoxLayout* lay;
        auto* card = toolCard("桌面便签", lay);
        auto* tip = new QLabel("置顶小窗随手记，内容/位置/颜色自动保存到 ToolBox\\notes.json。右上 × 只是收起，不删数据。");
        tip->setObjectName("dim");
        tip->setWordWrap(true);
        lay->addWidget(tip);
        auto* row = new QHBoxLayout;
        auto* newBtn = new QPushButton("新建便签");
        connect(newBtn, &QPushButton::clicked, this, [this] {
            qint64 id = QDateTime::currentMSecsSinceEpoch();
            while (m_notes.contains(id)) ++id;
            QRect ag = QGuiApplication::primaryScreen()->availableGeometry();
            QPoint pos(ag.right() - 300 - (m_notes.size() % 6) * 26, ag.bottom() - 280 - (m_notes.size() % 6) * 22);
            auto* w = new NoteWindow(id, "", "yellow", pos, true);
            connect(w, &NoteWindow::changed, this, [this] { saveNotes(); });
            m_notes.insert(id, w);
            w->show();
            w->raise();
            saveNotes();
        });
        row->addWidget(newBtn);
        row->addStretch();
        lay->addLayout(row);
        v->addWidget(card);
    }

    QVBoxLayout* lay;
    auto* card = toolCard("我的便签", lay);
    m_noteList = new QListWidget;
    m_noteList->setMinimumHeight(220);
    lay->addWidget(m_noteList, 1);
    auto* row = new QHBoxLayout;
    auto* openBtn = new QPushButton("打开");
    auto* delBtn = new QPushButton("删除");
    delBtn->setObjectName("ghost");
    connect(openBtn, &QPushButton::clicked, this, [this] {
        auto* it = m_noteList->currentItem();
        if (it) openNote(it->data(Qt::UserRole).toLongLong());
    });
    connect(delBtn, &QPushButton::clicked, this, [this] {
        auto* it = m_noteList->currentItem();
        if (!it) return;
        const qint64 id = it->data(Qt::UserRole).toLongLong();
        if (QMessageBox::question(this, "删除便签", "确定删除这条便签？内容将不可恢复。") != QMessageBox::Yes) return;
        if (NoteWindow* w = m_notes.value(id, nullptr)) {
            w->deleteLater();
            m_notes.remove(id);
            saveNotes();
        }
    });
    row->addWidget(openBtn);
    row->addWidget(delBtn);
    row->addStretch();
    lay->addLayout(row);
    v->addWidget(card, 1);

    scroll->setWidget(body);
    auto* wrap = new QVBoxLayout;
    wrap->setContentsMargins(0, 0, 0, 0);
    wrap->addWidget(scroll);
    auto* page = new QWidget;
    page->setLayout(wrap);
    renderNoteList();
    return page;
}

void MainWindow::renderNoteList() {
    if (!m_noteList) return;
    m_noteList->clear();
    for (auto it = m_notes.constBegin(); it != m_notes.constEnd(); ++it) {
        NoteWindow* w = it.value();
        const QString first = w->text().section('\n', 0, 0).left(28);
        QString summary = first.isEmpty() ? QStringLiteral("(空白便签)") : first;
        static const QHash<QString, QString> colorZh = {{"yellow", "黄"}, {"green", "绿"}, {"blue", "蓝"}, {"pink", "粉"}};
        auto* it2 = new QListWidgetItem(QString("%1  ·  %2 便签").arg(summary, colorZh.value(w->m_color, w->m_color)));
        it2->setData(Qt::UserRole, it.key());
        m_noteList->addItem(it2);
    }
    if (m_notes.isEmpty()) m_noteList->addItem(QStringLiteral("还没有便签，点上面「新建便签」创建一条"));
}

void MainWindow::startShot(int delayMs) {
    if (delayMs > 0) {
        QTimer::singleShot(delayMs, this, [this] { startShot(0); });
        return;
    }
    if (m_overlay) m_overlay->deleteLater();
    m_overlay = new ShotOverlay();
    connect(m_overlay, &ShotOverlay::finished, this, &MainWindow::shotFinished);
    connect(m_overlay, &ShotOverlay::cancelled, m_overlay, &QObject::deleteLater);
    m_overlay->start();
}

void MainWindow::shotFinished(const QPixmap& pix) {
    m_shotPixmap = pix;
    QApplication::clipboard()->setPixmap(pix);
    if (m_overlay) m_overlay->deleteLater();
    if (m_shotPreview) {
        m_shotPreview->setPixmap(pix.scaledToHeight(280, Qt::SmoothTransformation));
        m_shotPreview->setFixedHeight(288);
    }
    if (m_shotState)
        m_shotState->setText(QString("已复制到剪贴板 · %1×%2 像素（保存或再次截图可覆盖）").arg(pix.width()).arg(pix.height()));
}

QWidget* MainWindow::buildShotTab() {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* body = new QWidget;
    auto* v = new QVBoxLayout(body);
    v->setContentsMargins(0, 4, 12, 0);
    v->setSpacing(12);

    QVBoxLayout* lay;
    auto* card = toolCard("截图（主显示器 · 选区截图）", lay);
    auto* tip = new QLabel("点击后屏幕变暗：拖拽选区 → Enter/双击确认，Esc 取消。截图自动进入剪贴板。");
    tip->setObjectName("dim");
    tip->setWordWrap(true);
    lay->addWidget(tip);

    auto* row = new QHBoxLayout;
    auto* now = new QPushButton("开始截图");
    connect(now, &QPushButton::clicked, this, [this] { startShot(0); });
    auto* delay = new QPushButton("3 秒后截图");
    delay->setObjectName("ghost");
    connect(delay, &QPushButton::clicked, this, [this] { startShot(3000); });
    row->addWidget(now);
    row->addWidget(delay);
    row->addStretch();
    lay->addLayout(row);

    m_shotState = new QLabel("");
    m_shotState->setObjectName("dim");
    lay->addWidget(m_shotState);

    m_shotPreview = new QLabel("暂无截图");
    m_shotPreview->setAlignment(Qt::AlignCenter);
    m_shotPreview->setMinimumHeight(60);
    m_shotPreview->setObjectName("dim");
    lay->addWidget(m_shotPreview);

    auto* row2 = new QHBoxLayout;
    auto* copy = new QPushButton("复制到剪贴板");
    copy->setObjectName("ghost");
    connect(copy, &QPushButton::clicked, this, [this] {
        if (!m_shotPixmap.isNull()) QApplication::clipboard()->setPixmap(m_shotPixmap);
    });
    auto* save = new QPushButton("保存为 PNG…");
    save->setObjectName("ghost");
    connect(save, &QPushButton::clicked, this, [this] {
        if (m_shotPixmap.isNull()) return;
        const QString f = QFileDialog::getSaveFileName(this, "保存截图", QDateTime::currentDateTime().toString("yyyyMMdd-hhmmss") + ".png", "PNG 图片 (*.png)");
        if (!f.isEmpty()) m_shotPixmap.save(f, "PNG");
    });
    row2->addWidget(copy);
    row2->addWidget(save);
    row2->addStretch();
    lay->addLayout(row2);
    v->addWidget(card);
    v->addStretch();

    scroll->setWidget(body);
    auto* page = new QWidget;
    auto* wrap = new QVBoxLayout(page);
    wrap->setContentsMargins(0, 0, 0, 0);
    wrap->addWidget(scroll);
    return page;
}

QWidget* MainWindow::buildPomoTab() {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* body = new QWidget;
    auto* v = new QVBoxLayout(body);
    v->setContentsMargins(0, 4, 12, 0);
    v->setSpacing(12);

    QVBoxLayout* lay;
    auto* card = toolCard("番茄钟", lay);
    auto* tip = new QLabel("专注节奏：工作倒计时结束会弹托盘通知，桌宠在场时还会提醒你休息；休息结束自动回到工作。今日完成数持久化。");
    tip->setObjectName("dim");
    tip->setWordWrap(true);
    lay->addWidget(tip);

    m_pomoBig = new QLabel("25:00");
    m_pomoBig->setObjectName("statNum");
    m_pomoBig->setAlignment(Qt::AlignCenter);
    QFont bf = m_pomoBig->font();
    bf.setPixelSize(52);
    bf.setBold(true);
    m_pomoBig->setFont(bf);
    lay->addWidget(m_pomoBig);

    m_pomoState = new QLabel("就绪 · 工作阶段");
    m_pomoState->setAlignment(Qt::AlignCenter);
    lay->addWidget(m_pomoState);

    auto* rowCfg = new QHBoxLayout;
    rowCfg->addStretch();
    rowCfg->addWidget(new QLabel("工作"));
    m_pomoWorkMin = new QSpinBox;
    m_pomoWorkMin->setRange(1, 120);
    m_pomoWorkMin->setValue(25);
    m_pomoWorkMin->setSuffix(" 分钟");
    rowCfg->addWidget(m_pomoWorkMin);
    rowCfg->addWidget(new QLabel("休息"));
    m_pomoRestMin = new QSpinBox;
    m_pomoRestMin->setRange(1, 60);
    m_pomoRestMin->setValue(5);
    m_pomoRestMin->setSuffix(" 分钟");
    rowCfg->addWidget(m_pomoRestMin);
    rowCfg->addStretch();
    lay->addLayout(rowCfg);

    auto* row = new QHBoxLayout;
    row->addStretch();
    auto* start = new QPushButton("开始");
    connect(start, &QPushButton::clicked, this, [this, start] {
        if (m_pomoRunning) {
            m_pomoRunning = false;
            m_pomoTimer->stop();
            start->setText("继续");
            m_pomoState->setText("已暂停 · " + QString(m_pomoWorking ? "工作阶段" : "休息阶段"));
        } else {
            if (m_pomoLeft <= 0)
                m_pomoLeft = (m_pomoWorking ? m_pomoWorkMin->value() : m_pomoRestMin->value()) * 60;
            m_pomoRunning = true;
            if (m_pomoNoiseCk && m_pomoNoiseCk->isChecked()) playNoise(0);
            if (!m_pomoTimer) {
                m_pomoTimer = new QTimer(this);
                connect(m_pomoTimer, &QTimer::timeout, this, &MainWindow::pomoTick);
            }
            m_pomoTimer->start(1000);
            start->setText("暂停");
        }
    });
    auto* reset = new QPushButton("重置");
    reset->setObjectName("ghost");
    connect(reset, &QPushButton::clicked, this, [this] {
        m_pomoTimer->stop();
        m_pomoRunning = false;
        m_pomoWorking = true;
        m_pomoLeft = m_pomoWorkMin->value() * 60;
        m_pomoBig->setText(QString("%1:00").arg(m_pomoWorkMin->value(), 2, 10, QChar('0')));
        m_pomoState->setText("就绪 · 工作阶段");
    });
    row->addWidget(start);
    row->addWidget(reset);
    row->addStretch();
    lay->addLayout(row);

    m_pomoCount = new QLabel("");
    m_pomoChart = new QLabel;
    m_pomoChart->setAlignment(Qt::AlignCenter);
    m_pomoChart->setMinimumHeight(90);
    m_pomoCount->setObjectName("dim");
    m_pomoCount->setAlignment(Qt::AlignCenter);
    lay->addWidget(m_pomoCount);

    // 完成统计（pomo.json：{history:{date:count}}，保留近 60 天）
    const QString today = QDate::currentDate().toString("yyyy-MM-dd");
    QJsonObject p = ls::loadJson("pomo.json");
    const QJsonObject hist = p.value("history").toObject();
    int todayN = hist.value(today).toInt();
    int weekN = 0;
    for (int d = 0; d < 7; ++d)
        weekN += hist.value(QDate::currentDate().addDays(-d).toString("yyyy-MM-dd")).toInt();
    m_pomoCount->setText(QString("今日 %1 个 · 近 7 天 %2 个番茄").arg(todayN).arg(weekN));
    lay->addWidget(m_pomoChart);

    lay->addWidget(m_pomoChart);
    renderPomoChart();

    v->addWidget(card);

    // ---------- 专注联动白噪音 ----------
    {
        QVBoxLayout* ln;
        auto* linkCard = toolCard("专注音效联动", ln);
        auto* ck = new QCheckBox("开始番茄钟自动播放雨声，进入休息自动停止");
        ln->addWidget(ck);
        auto* tip = new QLabel("勾选后无需手动开白噪音；也可在「截图与媒体 → 白噪音」手动选其他音效。");
        tip->setObjectName("dim");
        tip->setWordWrap(true);
        ln->addWidget(tip);
        m_pomoNoiseCk = ck;
        v->addWidget(linkCard);
    }

    // ---------- 倒计时 ----------
    {
        QVBoxLayout* lc;
        auto* cdCard = toolCard("倒计时（任意事项 · 到点提醒）", lc);
        m_cdBig = new QLabel("00:00");
        m_cdBig->setStyleSheet(QString("color:%1;").arg(theme::accent().name()));
        m_cdBig->setAlignment(Qt::AlignCenter);
        QFont cf = m_cdBig->font();
        cf.setPixelSize(34);
        cf.setBold(true);
        m_cdBig->setFont(cf);
        lc->addWidget(m_cdBig);
        auto* rowC = new QHBoxLayout;
        rowC->addStretch();
        auto* cdName = new QLineEdit;
        cdName->setPlaceholderText("事项（选填）");
        cdName->setFixedWidth(150);
        rowC->addWidget(cdName);
        rowC->addWidget(new QLabel("时长"));
        auto* mins = new QSpinBox;
        mins->setRange(1, 600);
        mins->setValue(10);
        mins->setSuffix(" 分钟");
        rowC->addWidget(mins);
        auto* go = new QPushButton("开 始");
        rowC->addWidget(go);
        auto* stopC = new QPushButton("停 止");
        stopC->setObjectName("ghost");
        stopC->setEnabled(false);
        rowC->addWidget(stopC);
        rowC->addStretch();
        lc->addLayout(rowC);
        v->addWidget(cdCard);
        connect(go, &QPushButton::clicked, this, [this, mins, go, stopC, cdName] {
            m_cdLeft = mins->value() * 60;
            m_cdBig->setText(QString("%1:%2").arg(m_cdLeft / 60, 2, 10, QChar('0')).arg(m_cdLeft % 60, 2, 10, QChar('0')));
            if (!m_cdTimer) {
                m_cdTimer = new QTimer(this);
                connect(m_cdTimer, &QTimer::timeout, this, [this, go, stopC, cdName] {
                    --m_cdLeft;
                    if (m_cdLeft <= 0) {
                        m_cdTimer->stop();
                        m_cdBig->setText("到点！");
                        go->setEnabled(true); stopC->setEnabled(false);
                        toast("倒计时", cdName->text().trimmed().isEmpty()
                                  ? QString("时间到！")
                                  : QString("%1 · 时间到！").arg(cdName->text().trimmed()), 5000);
                        pet::playRemote("超大伸懒腰", "时间到啦！");
                        return;
                    }
                    m_cdBig->setText(QString("%1:%2").arg(m_cdLeft / 60, 2, 10, QChar('0')).arg(m_cdLeft % 60, 2, 10, QChar('0')));
                });
            }
            m_cdTimer->start(1000);
            go->setEnabled(false); stopC->setEnabled(true);
        });
        connect(stopC, &QPushButton::clicked, this, [this, go, stopC] {
            if (m_cdTimer) m_cdTimer->stop();
            m_cdLeft = 0;
            m_cdBig->setText("00:00");
            go->setEnabled(true); stopC->setEnabled(false);
        });
    }

    // ---------- 秒表 ----------
    {
        QVBoxLayout* lay2;
        auto* swCard = toolCard("秒表（计次）", lay2);
        m_swBig = new QLabel("00:00.0");
        m_swBig->setStyleSheet(QString("color:%1;").arg(theme::accent().name()));
        m_swBig->setAlignment(Qt::AlignCenter);
        QFont sf = m_swBig->font();
        sf.setPixelSize(38);
        sf.setBold(true);
        m_swBig->setFont(sf);
        lay2->addWidget(m_swBig);
        auto* rowS = new QHBoxLayout;
        rowS->addStretch();
        auto* swGo = new QPushButton("开始");
        auto* swLap = new QPushButton("计 次");
        swLap->setEnabled(false);
        auto* swReset = new QPushButton("清 零");
        swReset->setObjectName("ghost");
        connect(swGo, &QPushButton::clicked, this, [this, swGo, swLap] {
            if (!m_swTimer) {
                m_swTimer = new QTimer(this);
                connect(m_swTimer, &QTimer::timeout, this, [this] {
                    const qint64 ms = m_swElapsed.elapsed();
                    m_swBig->setText(QString("%1:%2.%3").arg(ms / 60000, 2, 10, QChar('0'))
                                         .arg((ms / 1000) % 60, 2, 10, QChar('0'))
                                         .arg((ms % 1000) / 100));
                });
            }
            if (m_swRunning) {
                m_swTimer->stop();
                m_swRunning = false;
                swGo->setText("继续");
                swLap->setEnabled(false);
            } else {
                m_swElapsed.start();
                m_swTimer->start(100);
                m_swRunning = true;
                swGo->setText("暂 停");
                swLap->setEnabled(true);
            }
        });
        connect(swLap, &QPushButton::clicked, this, [this] {
            if (m_swLaps) m_swLaps->insertItem(0, new QListWidgetItem(m_swBig->text()));
        });
        connect(swReset, &QPushButton::clicked, this, [this, swGo, swLap] {
            if (m_swTimer) m_swTimer->stop();
            m_swRunning = false;
            swGo->setText("开 始");
            swLap->setEnabled(false);
            m_swBig->setText("00:00.0");
            if (m_swLaps) m_swLaps->clear();
        });
        rowS->addWidget(swGo);
        rowS->addWidget(swLap);
        rowS->addWidget(swReset);
        rowS->addStretch();
        lay2->addLayout(rowS);
        m_swLaps = new QListWidget;
        m_swLaps->setMaximumHeight(110);
        lay2->addWidget(m_swLaps);
        v->addWidget(swCard);
    }
    v->addStretch();
    scroll->setWidget(body);
    auto* page = new QWidget;
    auto* wrap = new QVBoxLayout(page);
    wrap->setContentsMargins(0, 0, 0, 0);
    wrap->addWidget(scroll);
    return page;
}

void MainWindow::pomoTick() {
    if (!m_pomoRunning) return;
    --m_pomoLeft;
    if (m_pomoLeft <= 0) { pomoFinish(m_pomoWorking); return; }
    m_pomoBig->setText(QString("%1:%2").arg(m_pomoLeft / 60, 2, 10, QChar('0')).arg(m_pomoLeft % 60, 2, 10, QChar('0')));
}

void MainWindow::pomoFinish(bool workDone) {
    m_pomoTimer->stop();
    m_pomoRunning = false;
    stopNoise(); // 专注结束自动停白噪音
    QJsonObject p = ls::loadJson("pomo.json");
    if (workDone) {
        const QString td = QDate::currentDate().toString("yyyy-MM-dd");
        QJsonObject hist = p.value("history").toObject();
        const int c = hist.value(td).toInt() + 1;
        hist.insert(td, c);
        // 裁剪到近 60 天
        const QString cutoff = QDate::currentDate().addDays(-60).toString("yyyy-MM-dd");
        QJsonObject pruned;
        for (auto it2 = hist.begin(); it2 != hist.end(); ++it2)
            if (it2.key() >= cutoff) pruned.insert(it2.key(), it2.value());
        p.insert("history", pruned);
        ls::saveJson("pomo.json", p);
        m_pomoCount->setText(QString("今日 %1 个 · 近 7 天 %2 个番茄").arg(c).arg([&] {
            int w = 0;
            for (int d = 0; d < 7; ++d)
                w += pruned.value(QDate::currentDate().addDays(-d).toString("yyyy-MM-dd")).toInt();
            return w;
        }()));
        m_pomoWorking = false;
        m_pomoLeft = m_pomoRestMin->value() * 60;
        m_pomoState->setText("工作完成！休息一下（自动开始）");
        toast("番茄钟", "一个番茄完成！休息 " + QString::number(m_pomoRestMin->value()) + " 分钟，起来活动一下～", 5000);
        pet::playRemote("超大伸懒腰", "番茄完成！起来活动一下吧～");
    } else {
        m_pomoWorking = true;
        m_pomoLeft = m_pomoWorkMin->value() * 60;
        m_pomoState->setText("休息结束，回到工作（自动开始）");
        toast("番茄钟", "休息结束，回到专注状态～", 4000);
        pet::playRemote("原地敲击桌面互动", "休息好啦，继续加油！");
    }
    m_pomoBig->setText(QString("%1:00").arg(m_pomoLeft / 60, 2, 10, QChar('0')));
    // 自动开始下一阶段
    m_pomoRunning = true;
    if (m_pomoTimer) m_pomoTimer->start(1000);
}

QWidget* MainWindow::buildPickTab() {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* body = new QWidget;
    auto* v = new QVBoxLayout(body);
    v->setContentsMargins(0, 4, 12, 0);
    v->setSpacing(12);

    QVBoxLayout* lay;
    auto* card = toolCard("屏幕取色器（主显示器）", lay);
    auto* tip = new QLabel("点击后移动鼠标：放大镜跟随，显示 HEX/RGB；点击或 Enter 取色，Esc 取消。取过的颜色保留在下方，点击即复制。");
    tip->setObjectName("dim");
    tip->setWordWrap(true);
    lay->addWidget(tip);

    auto* row = new QHBoxLayout;
    auto* go = new QPushButton("开始取色");
    connect(go, &QPushButton::clicked, this, [this] {
        if (m_picker) m_picker->deleteLater();
        m_picker = new PickOverlay();
        connect(m_picker, &PickOverlay::picked, this, [this](const QColor& c) {
            const QString hex = c.name();
            auto* it = new QListWidgetItem(hex);
            it->setBackground(c);
            it->setForeground(c.lightness() > 150 ? Qt::black : Qt::white);
            it->setData(Qt::UserRole, hex);
            m_colorList->insertItem(0, it);
            QApplication::clipboard()->setText(hex);
            if (m_colorList->count() > 24) m_colorList->takeItem(m_colorList->count() - 1);
            QJsonObject hist;
            QJsonArray arr;
            for (int i = 0; i < m_colorList->count(); ++i) arr.append(m_colorList->item(i)->data(Qt::UserRole).toString());
            hist.insert("colors", arr);
            ls::saveJson("color_history.json", hist);
        });
        connect(m_picker, &PickOverlay::cancelled, m_picker, &QObject::deleteLater);
        m_picker->start();
    });
    row->addWidget(go);
    row->addStretch();
    lay->addLayout(row);

    m_colorList = new QListWidget;
    m_colorList->setMinimumHeight(160);
    m_colorList->setFlow(QListView::LeftToRight);
    m_colorList->setWrapping(true);
    m_colorList->setSpacing(4);
    connect(m_colorList, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) {
        QApplication::clipboard()->setText(it->data(Qt::UserRole).toString());
    });
    lay->addWidget(m_colorList, 1);
    // 历史取色持久化（最多 24 条）
    for (const QJsonValue& val : ls::loadJson("color_history.json").value("colors").toArray()) {
        const QString hex = val.toString();
        QColor c(hex);
        if (!c.isValid()) continue;
        auto* it = new QListWidgetItem(hex);
        it->setBackground(c);
        it->setForeground(c.lightness() > 150 ? Qt::black : Qt::white);
        it->setData(Qt::UserRole, hex);
        m_colorList->addItem(it);
    }
    auto* rowHist = new QHBoxLayout;
    auto* clearHist = new QPushButton("清空历史");
    clearHist->setObjectName("ghost");
    connect(clearHist, &QPushButton::clicked, this, [this] {
        m_colorList->clear();
        ls::saveJson("color_history.json", QJsonObject());
    });
    rowHist->addWidget(clearHist);
    rowHist->addStretch();
    lay->addLayout(rowHist);
    auto* rowRuler = new QHBoxLayout;
    auto* ruler = new QPushButton("屏幕测距");
    ruler->setObjectName("ghost");
    ruler->setToolTip("拖拽测量屏幕上两点间的像素距离（对齐 UI 走查用）");
    connect(ruler, &QPushButton::clicked, this, [this] {
        if (m_ruler) m_ruler->deleteLater();
        m_ruler = new RulerOverlay();
        connect(m_ruler, &RulerOverlay::cancelled, m_ruler, &QObject::deleteLater);
        m_ruler->start();
    });
    rowRuler->addWidget(ruler);
    rowRuler->addStretch();
    lay->addLayout(rowRuler);
    auto* dim = new QLabel("取色后 HEX 已自动复制；点击色块可再次复制。");
    dim->setObjectName("dim");
    lay->addWidget(dim);

    // 颜色转换器：HEX ↔ RGB ↔ HSL
    {
        QVBoxLayout* lc;
        auto* cc = toolCard("颜色转换器", lc);
        auto* rowC = new QHBoxLayout;
        rowC->addWidget(new QLabel("HEX"));
        auto* hex = new QLineEdit("#4E9AFF");
        hex->setFixedWidth(110);
        rowC->addWidget(hex);
        auto* swatch = new QLabel;
        swatch->setFixedSize(28, 28);
        swatch->setStyleSheet("border:1px solid rgba(20,26,48,.2);border-radius:6px;background:#4E9AFF;");
        rowC->addWidget(swatch);
        auto* rgbOut = new QLabel;
        rgbOut->setObjectName("dim");
        rowC->addWidget(rgbOut, 1);
        lc->addLayout(rowC);
        auto* hslOut = new QLabel;
        hslOut->setObjectName("dim");
        lc->addWidget(hslOut);
        auto convert = [hex, swatch, rgbOut, hslOut] {
            QColor c(hex->text().trimmed());
            if (!c.isValid()) { rgbOut->setText("格式：#RRGGBB"); hslOut->setText(""); return; }
            swatch->setStyleSheet(QString("border:1px solid rgba(20,26,48,.2);border-radius:6px;background:%1;").arg(c.name()));
            int h, sat, l, v;
            c.getHsv(&h, &sat, &v);
            c.getHsl(&h, &sat, &l);
            rgbOut->setText(QString("RGB(%1, %2, %3)").arg(c.red()).arg(c.green()).arg(c.blue()));
            hslOut->setText(QString("HSL(%1°, %2%, %3%)").arg(h).arg(int(sat / 255.0 * 100)).arg(int(l / 255.0 * 100)));
        };
        connect(hex, &QLineEdit::textChanged, this, convert);
        convert();
        v->addWidget(cc);
    }
    v->addWidget(card);
    v->addStretch();

    scroll->setWidget(body);
    auto* page = new QWidget;
    auto* wrap = new QVBoxLayout(page);
    wrap->setContentsMargins(0, 0, 0, 0);
    wrap->addWidget(scroll);
    return page;
}

QWidget* MainWindow::buildUnitTab() {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* body = new QWidget;
    auto* v = new QVBoxLayout(body);
    v->setContentsMargins(0, 4, 12, 0);
    v->setSpacing(12);

    QVBoxLayout* lay;
    auto* card = toolCard("单位换算（本地计算）", lay);
    lay->addWidget(new QLabel("类别"));
    auto* cat = new QComboBox;
    cat->addItems({"长度", "重量", "温度", "数据大小", "面积", "速度"});
    lay->addWidget(cat);
    lay->addWidget(new QLabel("换算"));
    auto* row = new QHBoxLayout;
    auto* val = new QLineEdit;
    val->setPlaceholderText("数值");
    row->addWidget(val, 1);
    auto* from = new QComboBox;
    row->addWidget(from, 1);
    row->addWidget(new QLabel("→"));
    auto* to = new QComboBox;
    row->addWidget(to, 1);
    auto* go = new QPushButton("换算");
    row->addWidget(go);
    lay->addLayout(row);
    auto* out = new QLabel("");
    out->setObjectName("postTitle");
    out->setWordWrap(true);
    lay->addWidget(out);

    // 单位表：相对基准系数（温度特殊处理）
    const struct { QStringList units; QList<double> k; } tbl[6] = {
        {{"毫米", "厘米", "米", "千米", "英寸", "英尺", "英里"}, {0.001, 0.01, 1, 1000, 0.0254, 0.3048, 1609.344}},
        {{"毫克", "克", "千克", "吨", "盎司", "磅"}, {0.001, 1, 1000, 1e6, 28.3495, 453.592}},
        {{"摄氏度", "华氏度", "开尔文"}, {1, 1, 1}},
        {{"比特", "字节", "KB", "MB", "GB", "TB"}, {0.125, 1, 1024, 1048576, 1073741824.0, 1099511627776.0}},
        {{"平方米", "平方千米", "平方英尺", "亩", "公顷"}, {1, 1e6, 0.092903, 666.667, 10000}},
        {{"米/秒", "千米/时", "英里/时", "节"}, {1, 0.277778, 0.44704, 0.514444}},
    };
    auto fill = [tbl, cat, from, to] {
        const int i = cat->currentIndex();
        from->clear(); to->clear();
        from->addItems(tbl[i].units); to->addItems(tbl[i].units);
        to->setCurrentIndex(1);
    };
    fill();
    connect(cat, &QComboBox::currentIndexChanged, this, fill);
    connect(go, &QPushButton::clicked, this, [tbl, cat, from, to, val, out] {
        bool ok; const double x = val->text().toDouble(&ok);
        if (!ok) { out->setText("请输入合法数值"); return; }
        const int i = cat->currentIndex();
        const int a = from->currentIndex(), b = to->currentIndex();
        double r;
        if (i == 2) { // 温度：先转摄氏
            double c = a == 0 ? x : a == 1 ? (x - 32) * 5 / 9 : x - 273.15;
            r = b == 0 ? c : b == 1 ? c * 9 / 5 + 32 : c + 273.15;
        } else {
            r = x * tbl[i].k[a] / tbl[i].k[b];
        }
        out->setText(QString("%1 %2 = %3 %4").arg(val->text(), from->currentText(),
                       QString::number(r, 'g', 10), to->currentText()));
    });
    connect(val, &QLineEdit::returnPressed, go, &QPushButton::click);
    v->addWidget(card);
    v->addStretch();

    scroll->setWidget(body);
    auto* page = new QWidget;
    auto* wrap = new QVBoxLayout(page);
    wrap->setContentsMargins(0, 0, 0, 0);
    wrap->addWidget(scroll);
    return page;
}

QWidget* MainWindow::buildQuickTab() {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* body = new QWidget;
    auto* v = new QVBoxLayout(body);
    v->setContentsMargins(0, 4, 12, 0);
    v->setSpacing(12);

    const QJsonArray saved = ls::loadJson("quick.json").value("items").toArray();

    QVBoxLayout* lay;
    auto* card = toolCard("快捷启动（双击条目打开）", lay);
    auto* tip = new QLabel("把常用程序/文件夹/网址放这里：程序和文件夹用「浏览」选，网址直接输入 http(s):// 开头的地址，回车用默认方式打开。");
    tip->setObjectName("dim");
    tip->setWordWrap(true);
    lay->addWidget(tip);

    auto* rowName = new QHBoxLayout;
    rowName->addWidget(new QLabel("名称"));
    auto* name = new QLineEdit;
    name->setPlaceholderText("显示名称");
    rowName->addWidget(name, 1);
    lay->addLayout(rowName);
    auto* rowPath = new QHBoxLayout;
    rowPath->addWidget(new QLabel("路径"));
    auto* path = new QLineEdit;
    path->setPlaceholderText("exe / 文件夹路径 或 http(s):// 网址");
    rowPath->addWidget(path, 1);
    auto* browse = new QPushButton("浏览…");
    browse->setObjectName("ghost");
    connect(browse, &QPushButton::clicked, this, [this, path, name] {
        const QString f = QFileDialog::getOpenFileName(this, "选择程序或文件");
        if (!f.isEmpty()) {
            path->setText(f);
            if (name->text().trimmed().isEmpty())
                name->setText(QFileInfo(f).completeBaseName());
        }
    });
    rowPath->addWidget(browse);
    lay->addLayout(rowPath);

    m_quickList = new QListWidget;
    m_quickList->setMinimumHeight(220);
    lay->addWidget(m_quickList, 1);

    auto persist = [this] {
        QJsonArray arr;
        for (int i = 0; i < m_quickList->count(); ++i) {
            QJsonObject o;
            o.insert("name", m_quickList->item(i)->text().section("  →  ", 0, 0));
            o.insert("path", m_quickList->item(i)->data(Qt::UserRole).toString());
            arr.append(o);
        }
        QJsonObject root; root.insert("items", arr);
        ls::saveJson("quick.json", root);
    };
    auto openItem = [](QListWidgetItem* it) {
        const QString p = it->data(Qt::UserRole).toString();
        if (p.startsWith("http://") || p.startsWith("https://"))
            QDesktopServices::openUrl(QUrl(p));
        else
            QDesktopServices::openUrl(QUrl::fromLocalFile(p));
    };
    connect(m_quickList, &QListWidget::itemDoubleClicked, this, openItem);
    connect(m_quickList, &QListWidget::itemDoubleClicked, this, [persist] { /* 打开不改数据 */ });

    auto* row = new QHBoxLayout;
    auto* add = new QPushButton("添加");
    connect(add, &QPushButton::clicked, this, [this, name, path, persist] {
        const QString p = path->text().trimmed();
        const QString n = name->text().trimmed().isEmpty() ? p : name->text().trimmed();
        if (p.isEmpty()) return;
        auto* it = new QListWidgetItem(n + "  →  " + (p.size() > 46 ? "…" + p.right(46) : p));
        it->setData(Qt::UserRole, p);
        m_quickList->addItem(it);
        path->clear(); name->clear();
        persist();
    });
    auto* open = new QPushButton("打开选中");
    connect(open, &QPushButton::clicked, this, [this, openItem] {
        auto* it = m_quickList->currentItem();
        if (it) openItem(it);
    });
    auto* del = new QPushButton("删除选中");
    del->setObjectName("ghost");
    connect(del, &QPushButton::clicked, this, [this, persist] {
        auto* it = m_quickList->currentItem();
        if (!it) return;
        delete m_quickList->takeItem(m_quickList->row(it));
        persist();
    });
    row->addWidget(add);
    row->addWidget(open);
    row->addWidget(del);
    row->addStretch();
    lay->addLayout(row);
    v->addWidget(card);
    v->addStretch();

    for (const QJsonValue& val : saved) {
        const QJsonObject o = val.toObject();
        const QString p = o.value("path").toString();
        auto* it = new QListWidgetItem(o.value("name").toString() + "  →  " + (p.size() > 46 ? "…" + p.right(46) : p));
        it->setData(Qt::UserRole, p);
        m_quickList->addItem(it);
    }

    scroll->setWidget(body);
    auto* page = new QWidget;
    auto* wrap = new QVBoxLayout(page);
    wrap->setContentsMargins(0, 0, 0, 0);
    wrap->addWidget(scroll);
    return page;
}

void MainWindow::renderPomoChart() {
    if (!m_pomoChart) return;
    const int W = 420, H = 90;
    QPixmap pm(W, H);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QFont f = p.font();
    f.setPixelSize(10);
    p.setFont(f);
    QJsonObject hist = ls::loadJson("pomo.json").value("history").toObject();
    int maxV = 1;
    int vals[7] = {0};
    for (int d = 0; d < 7; ++d) {
        vals[d] = hist.value(QDate::currentDate().addDays(-(6 - d)).toString("yyyy-MM-dd")).toInt();
        if (vals[d] > maxV) maxV = vals[d];
    }
    const int bw = (W - 40) / 7;
    for (int d = 0; d < 7; ++d) {
        const QDate day = QDate::currentDate().addDays(-(6 - d));
        const int h2 = int(double(vals[d]) / maxV * 44);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(theme::accent()));
        p.drawRoundedRect(20 + d * bw, 58 - h2, bw - 8, h2, 3, 3);
        p.setPen(QColor(120, 130, 148));
        p.drawText(QRect(20 + d * bw - 4, 62, bw + 2, 14), Qt::AlignCenter,
                   d == 6 ? "今天" : day.toString("M/d"));
        if (vals[d] > 0) {
            p.setPen(QColor(90, 100, 118));
            p.drawText(QRect(20 + d * bw - 4, 40 - h2, bw + 2, 14), Qt::AlignCenter,
                       QString::number(vals[d]));
        }
    }
    m_pomoChart->setPixmap(pm);
}

} // namespace tb
