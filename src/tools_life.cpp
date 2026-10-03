// 生活类大类页面：待办清单 / 日程（倒数日+每日提醒） / 习惯打卡
// 新增的大类导航页（page 3/4/5），数据存 ToolBox\{todos,schedule,habits}.json
#include "mainwindow.h"
#include "tools_common.h"
#include "localstore.h"
#include "petwindow.h"

#include <QApplication>
#include <QCalendarWidget>
#include <QCheckBox>
#include <QComboBox>
#include <QCursor>
#include <QDate>
#include <QDateTime>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSpinBox>
#include <QScrollArea>
#include <QSet>
#include <QSystemTrayIcon>
#include <QTime>
#include <QTimeEdit>
#include <QDateEdit>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>

namespace tb {

// ---------- 待办清单（todos.json） ----------
QWidget* MainWindow::buildTodoPage() {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* body = new QWidget;
    auto* v = new QVBoxLayout(body);
    v->setContentsMargins(0, 4, 12, 0);
    v->setSpacing(12);

    QVBoxLayout* lay;
    auto* card = toolCard("添加待办", lay);
    auto* row = new QHBoxLayout;
    m_todoInput = new QLineEdit;
    m_todoInput->setPlaceholderText("要做的事…（回车直接添加）");
    row->addWidget(m_todoInput, 1);
    m_todoPrio = new QComboBox;
    m_todoPrio->addItems({"普通", "重要", "紧急"});
    row->addWidget(m_todoPrio);
    auto* add = new QPushButton("添加");
    connect(add, &QPushButton::clicked, this, [this] {
        const QString text = m_todoInput->text().trimmed();
        if (text.isEmpty()) return;
        QJsonObject root = ls::loadJson("todos.json");
        QJsonArray arr = root.value("items").toArray();
        QJsonObject o;
        o.insert("id", (double)QDateTime::currentMSecsSinceEpoch());
        o.insert("text", text);
        o.insert("prio", m_todoPrio->currentIndex()); // 0普通 1重要 2紧急
        o.insert("done", false);
        o.insert("created", QDateTime::currentDateTime().toString("MM-dd hh:mm"));
        arr.prepend(o);
        root.insert("items", arr);
        ls::saveJson("todos.json", root);
        m_todoInput->clear();
        renderTodo();
    });
    connect(m_todoInput, &QLineEdit::returnPressed, add, &QPushButton::click);
    row->addWidget(add);
    lay->addLayout(row);
    v->addWidget(card);

    QVBoxLayout* lay2;
    auto* card2 = toolCard("进行中", lay2);
    m_todoList = new QListWidget;
    m_todoList->setMinimumHeight(200);
    lay2->addWidget(m_todoList, 1);
    auto* rowT = new QHBoxLayout;
    auto* clearDone = new QPushButton("清空已完成");
    clearDone->setObjectName("ghost");
    connect(clearDone, &QPushButton::clicked, this, [this] {
        QJsonObject root = ls::loadJson("todos.json");
        QJsonArray arr = root.value("items").toArray();
        QJsonArray keep;
        int removed = 0;
        for (const QJsonValue& v : arr) {
            if (v.toObject().value("done").toBool()) ++removed;
            else keep.append(v);
        }
        if (!removed) return;
        root.insert("items", keep);
        ls::saveJson("todos.json", root);
        renderTodo();
    });
    rowT->addWidget(clearDone);
    rowT->addStretch();
    lay2->addLayout(rowT);
    m_todoStat = new QLabel("");
    m_todoStat->setObjectName("dim");
    lay2->addWidget(m_todoStat);
    v->addWidget(card2);

    QVBoxLayout* lay3;
    auto* card3 = toolCard("已完成", lay3);
    m_todoDone = new QListWidget;
    m_todoDone->setMinimumHeight(110);
    lay3->addWidget(m_todoDone, 1);
    v->addWidget(card3, 1);

    // 勾选完成 / 取消完成
    connect(m_todoList, &QListWidget::itemChanged, this, [this](QListWidgetItem* it) {
        if (m_todoBusy) return;
        const qint64 id = it->data(Qt::UserRole).toLongLong();
        const bool done = it->checkState() == Qt::Checked;
        QJsonObject root = ls::loadJson("todos.json");
        QJsonArray arr = root.value("items").toArray();
        for (int i = 0; i < arr.size(); ++i) {
            QJsonObject o = arr[i].toObject();
            if ((qint64)o.value("id").toDouble() == id) { o.insert("done", done); arr[i] = o; break; }
        }
        root.insert("items", arr);
        ls::saveJson("todos.json", root);
        renderTodo();
    });
    // 删除：进行中右键删除；已完成列表双击删除
    m_todoList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_todoList, &QListWidget::customContextMenuRequested, this, [this](const QPoint&) {
        auto* it = m_todoList->currentItem();
        if (!it) return;
        QMenu menu(this);
        QAction* del = menu.addAction("删除");
        if (menu.exec(QCursor::pos()) == del) {
            const qint64 id = it->data(Qt::UserRole).toLongLong();
            QJsonObject root = ls::loadJson("todos.json");
            QJsonArray arr = root.value("items").toArray();
            QJsonArray keep;
            for (const QJsonValue& val : arr)
                if ((qint64)val.toObject().value("id").toDouble() != id) keep.append(val);
            root.insert("items", keep);
            ls::saveJson("todos.json", root);
            renderTodo();
        }
    });
    connect(m_todoDone, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) {
        const qint64 id = it->data(Qt::UserRole).toLongLong();
        QJsonObject root = ls::loadJson("todos.json");
        QJsonArray arr = root.value("items").toArray();
        QJsonArray keep;
        for (const QJsonValue& val : arr) {
            if ((qint64)val.toObject().value("id").toDouble() != id) keep.append(val);
        }
        root.insert("items", keep);
        ls::saveJson("todos.json", root);
        renderTodo();
    });
    m_todoDone->setToolTip("双击删除已完成的条目");

    scroll->setWidget(body);
    auto* page = new QWidget;
    auto* wrap = new QVBoxLayout(page);
    wrap->setContentsMargins(0, 0, 0, 0);
    wrap->addWidget(scroll);
    renderTodo();
    return page;
}

void MainWindow::renderTodo() {
    if (!m_todoList) return;
    m_todoBusy = true;
    m_todoList->clear();
    m_todoDone->clear();
    const QJsonArray items = ls::loadJson("todos.json").value("items").toArray();
    int open = 0, done = 0;
    for (const QJsonValue& val : items) {
        const QJsonObject o = val.toObject();
        const QString text = o.value("text").toString();
        const qint64 id = (qint64)o.value("id").toDouble();
        const bool doneFlag = o.value("done").toBool();
        static const char* prioTag[] = {"", "[重要] ", "[紧急] "};
        QColor col = Qt::white;
        if (o.value("prio").toInt() == 2) col = QColor(0xd9, 0x53, 0x4f);
        else if (o.value("prio").toInt() == 1) col = QColor(0xc7, 0x78, 0x1a);
        if (!doneFlag) {
            ++open;
            auto* it = new QListWidgetItem(QString(prioTag[o.value("prio").toInt()]) + text);
            it->setForeground(col);
            it->setData(Qt::UserRole, id);
            it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
            it->setCheckState(Qt::Unchecked);
            m_todoList->addItem(it);
        } else {
            ++done;
            auto* it = new QListWidgetItem("✓ " + text);
            it->setForeground(QColor(0x8a, 0x90, 0xa0));
            it->setData(Qt::UserRole, id);
            m_todoDone->addItem(it);
        }
    }
    m_todoStat->setText(QString("进行中 %1 · 已完成 %2 · 勾选完成，双击已完成的删除").arg(open).arg(done));
    m_todoBusy = false;
}

// ---------- 日程：倒数日 + 每日提醒（schedule.json） ----------
QWidget* MainWindow::buildSchedulePage() {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* body = new QWidget;
    auto* v = new QVBoxLayout(body);
    v->setContentsMargins(0, 4, 12, 0);
    v->setSpacing(12);

    QVBoxLayout* lay;
    auto* card = toolCard("倒数日（考试、生日、DDL…）", lay);
    auto* colsCal = new QHBoxLayout;
    QCalendarWidget* cal = new QCalendarWidget;
    cal->setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);
    cal->setMinimumHeight(200);
    colsCal->addWidget(cal, 1);
    auto* calInfo = new QVBoxLayout;
    auto* calInfoLab = new QLabel("点击日期查看");
    calInfoLab->setObjectName("dim");
    calInfoLab->setWordWrap(true);
    calInfoLab->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    calInfo->addWidget(calInfoLab, 1);
    colsCal->addLayout(calInfo, 1);
    lay->addLayout(colsCal);
    connect(cal, &QCalendarWidget::clicked, this, [this, calInfoLab](const QDate& d) {
        const qint64 n = QDate::currentDate().daysTo(d);
        QString when = n == 0 ? "就是今天"
            : n > 0 ? QString("还有 %1 天").arg(n)
                    : QString("已过 %1 天").arg(-n);
        // 该日期上的倒数事件
        QStringList hits;
        for (const QJsonValue& val : ls::loadJson("schedule.json").value("dates").toArray()) {
            const QJsonObject o = val.toObject();
            if (QDate::fromString(o.value("date").toString(), "yyyy-MM-dd") == d)
                hits << o.value("name").toString();
        }
        calInfoLab->setText(QString("%1\n%2%3")
            .arg(d.toString("yyyy年M月d日 dddd"), when,
                 hits.isEmpty() ? QString() : "\n事件：" + hits.join("、")));
    });
    auto* row = new QHBoxLayout;
    m_cdName = new QLineEdit;
    m_cdName->setPlaceholderText("事件名称");
    row->addWidget(m_cdName, 1);
    m_cdDate = new QDateEdit(QDate::currentDate().addDays(7));
    m_cdDate->setCalendarPopup(true);
    m_cdDate->setDisplayFormat("yyyy-MM-dd");
    row->addWidget(m_cdDate);
    auto* add = new QPushButton("添加");
    connect(add, &QPushButton::clicked, this, [this] {
        const QString n = m_cdName->text().trimmed();
        if (n.isEmpty()) return;
        QJsonObject root = ls::loadJson("schedule.json");
        QJsonArray arr = root.value("dates").toArray();
        QJsonObject o;
        o.insert("name", n);
        o.insert("date", m_cdDate->date().toString("yyyy-MM-dd"));
        arr.append(o);
        root.insert("dates", arr);
        ls::saveJson("schedule.json", root);
        m_cdName->clear();
        renderSchedule();
    });
    row->addWidget(add);
    lay->addLayout(row);
    m_cdList = new QListWidget;
    m_cdList->setMinimumHeight(150);
    m_cdList->setToolTip("双击删除");
    lay->addWidget(m_cdList, 1);
    connect(m_cdList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) {
        const qint64 id = it->data(Qt::UserRole).toLongLong();
        QJsonObject root = ls::loadJson("schedule.json");
        QJsonArray arr = root.value("dates").toArray();
        QJsonArray keep;
        for (const QJsonValue& val : arr)
            if ((qint64)val.toObject().value("id").toDouble() != id) keep.append(val);
        root.insert("dates", keep);
        ls::saveJson("schedule.json", root);
        renderSchedule();
    });
    v->addWidget(card);

    QVBoxLayout* lay2;
    auto* card2 = toolCard("每日提醒（到点托盘+桌宠提醒）", lay2);
    auto* row2 = new QHBoxLayout;
    m_remName = new QLineEdit;
    m_remName->setPlaceholderText("提醒内容（如：背单词）");
    row2->addWidget(m_remName, 1);
    m_remTime = new QTimeEdit(QTime(9, 0));
    m_remTime->setDisplayFormat("hh:mm");
    row2->addWidget(m_remTime);
    auto* add2 = new QPushButton("添加");
    connect(add2, &QPushButton::clicked, this, [this] {
        const QString n = m_remName->text().trimmed();
        if (n.isEmpty()) return;
        QJsonObject root = ls::loadJson("schedule.json");
        QJsonArray arr = root.value("reminders").toArray();
        QJsonObject o;
        o.insert("id", (double)QDateTime::currentMSecsSinceEpoch());
        o.insert("name", n);
        o.insert("time", m_remTime->time().toString("hh:mm"));
        o.insert("lastFired", "");
        arr.append(o);
        root.insert("reminders", arr);
        ls::saveJson("schedule.json", root);
        m_remName->clear();
        renderSchedule();
    });
    row2->addWidget(add2);
    lay2->addLayout(row2);
    m_remList = new QListWidget;
    m_remList->setMinimumHeight(150);
    m_remList->setToolTip("双击删除");
    lay2->addWidget(m_remList, 1);
    connect(m_remList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) {
        const qint64 id = it->data(Qt::UserRole).toLongLong();
        QJsonObject root = ls::loadJson("schedule.json");
        QJsonArray arr = root.value("reminders").toArray();
        QJsonArray keep;
        for (const QJsonValue& val : arr)
            if ((qint64)val.toObject().value("id").toDouble() != id) keep.append(val);
        root.insert("reminders", keep);
        ls::saveJson("schedule.json", root);
        renderSchedule();
    });
    v->addWidget(card2);
    v->addStretch();

    scroll->setWidget(body);
    auto* page = new QWidget;
    auto* wrap = new QVBoxLayout(page);
    wrap->setContentsMargins(0, 0, 0, 0);
    wrap->addWidget(scroll);
    renderSchedule();
    return page;
}

void MainWindow::renderSchedule() {
    if (!m_cdList) return;
    const QDate today = QDate::currentDate();
    m_cdList->clear();
    const QJsonArray dates = ls::loadJson("schedule.json").value("dates").toArray();
    // 按剩余天数升序（QJsonValueRef 不可 swap，提取到临时数组排）
    QVector<QPair<QDate, QJsonObject>> rows;
    for (const QJsonValue& val : dates) {
        const QJsonObject o = val.toObject();
        rows.append({QDate::fromString(o.value("date").toString(), "yyyy-MM-dd"), o});
    }
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& rowP : rows) {
        const QJsonObject o = rowP.second;
        const qint64 id = (qint64)o.value("id").toDouble();
        const QDate d = rowP.first;
        const qint64 days = today.daysTo(d);
        QString when;
        QColor col = Qt::white;
        if (days == 0) { when = "就是今天！"; col = QColor(0xd9, 0x53, 0x4f); }
        else if (days > 0) when = QString("还有 %1 天").arg(days);
        else { when = QString("已过 %1 天").arg(-days); col = QColor(0x9a, 0xa0, 0xb0); }
        auto* it = new QListWidgetItem(QString("%1 · %2 · %3").arg(o.value("name").toString(),
                                        d.toString("yyyy-MM-dd"), when));
        it->setForeground(col);
        it->setData(Qt::UserRole, id);
        m_cdList->addItem(it);
    }
    m_remList->clear();
    const QJsonArray rems = ls::loadJson("schedule.json").value("reminders").toArray();
    for (const QJsonValue& val : rems) {
        const QJsonObject o = val.toObject();
        auto* it = new QListWidgetItem(QString("%1  每天 %2").arg(o.value("name").toString(), o.value("time").toString()));
        it->setData(Qt::UserRole, (qint64)o.value("id").toDouble());
        m_remList->addItem(it);
    }
}

void MainWindow::scheduleTick() {
    const QTime now = QTime::currentTime();
    const QString hhmm = now.toString("hh:mm");
    const QString today = QDate::currentDate().toString("yyyy-MM-dd");
    QJsonObject root = ls::loadJson("schedule.json");
    // 倒数日临近提醒：当天或前一天，每天一次（9 点后首次命中时）
    if (now.hour() >= 9 && root.value("lastCountdownNotify").toString() != today) {
        const QDate td = QDate::currentDate();
        for (const QJsonValue& val : root.value("dates").toArray()) {
            const QJsonObject o = val.toObject();
            const qint64 n = td.daysTo(QDate::fromString(o.value("date").toString(), "yyyy-MM-dd"));
            if (n == 0 || n == 1) {
                root.insert("lastCountdownNotify", today);
                ls::saveJson("schedule.json", root);
                toast("倒数日", QString("「%1」%2！").arg(o.value("name").toString(),
                      n == 0 ? "就是今天" : "明天就到了"), 6000);
                break;
            }
        }
    }
    QJsonArray arr = root.value("reminders").toArray();
    bool changed = false;
    for (int i = 0; i < arr.size(); ++i) {
        QJsonObject o = arr[i].toObject();
        if (o.value("time").toString() != hhmm || o.value("lastFired").toString() == today) continue;
        o.insert("lastFired", today);
        arr[i] = o;
        changed = true;
        const QString name = o.value("name").toString();
        toast("每日提醒", name, 6000);
        pet::playRemote("超大伸懒腰", "叮！该做啦：" + name);
    }
    if (changed) { root.insert("reminders", arr); ls::saveJson("schedule.json", root); }
}

// ---------- 习惯打卡（habits.json，打卡送积分） ----------
QWidget* MainWindow::buildHabitPage() {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* body = new QWidget;
    auto* v = new QVBoxLayout(body);
    v->setContentsMargins(0, 4, 12, 0);
    v->setSpacing(12);

    QVBoxLayout* lay;
    auto* card = toolCard("添加习惯", lay);
    auto* row = new QHBoxLayout;
    auto* name = new QLineEdit;
    name->setPlaceholderText("习惯名称（如：背单词、喝水、锻炼）");
    row->addWidget(name, 1);
    auto* add = new QPushButton("添加");
    connect(add, &QPushButton::clicked, this, [this, name] {
        const QString n = name->text().trimmed();
        if (n.isEmpty()) return;
        QJsonObject root = ls::loadJson("habits.json");
        QJsonArray arr = root.value("habits").toArray();
        QJsonObject o;
        o.insert("id", (double)QDateTime::currentMSecsSinceEpoch());
        o.insert("name", n);
        o.insert("created", QDate::currentDate().toString("yyyy-MM-dd"));
        o.insert("log", QJsonArray());
        arr.append(o);
        root.insert("habits", arr);
        ls::saveJson("habits.json", root);
        name->clear();
        renderHabits();
    });
    connect(name, &QLineEdit::returnPressed, add, &QPushButton::click);
    row->addWidget(add);
    lay->addLayout(row);
    v->addWidget(card);

    QVBoxLayout* lay2;
    auto* card2 = toolCard("今日打卡（每完成一个 +5 积分）", lay2);
    m_habitArea = new QVBoxLayout;
    m_habitArea->setSpacing(6);
    lay2->addLayout(m_habitArea);
    m_habitStat = new QLabel("");
    m_habitStat->setObjectName("dim");
    lay2->addWidget(m_habitStat);
    v->addWidget(card2);
    v->addStretch();

    scroll->setWidget(body);
    auto* page = new QWidget;
    auto* wrap = new QVBoxLayout(page);
    wrap->setContentsMargins(0, 0, 0, 0);
    wrap->addWidget(scroll);
    renderHabits();
    return page;
}

// 从今天（或昨天，容忍今天没打）往回数连续打卡天数
static int habitStreak(const QJsonArray& log, const QString& today) {
    QSet<QString> days;
    for (const QJsonValue& v : log) days.insert(v.toString());
    int streak = 0;
    QDate d = QDate::fromString(today, "yyyy-MM-dd");
    if (!days.contains(today)) d = d.addDays(-1);
    while (days.contains(d.toString("yyyy-MM-dd"))) {
        ++streak;
        d = d.addDays(-1);
    }
    return streak;
}

void MainWindow::renderHabits() {
    if (!m_habitArea) return;
    // 清空动态行（widget 与 layout 都要处理）
    while (QLayoutItem* it = m_habitArea->takeAt(0)) {
        if (QWidget* w = it->widget()) w->deleteLater();
        delete it;
    }
    const QString today = QDate::currentDate().toString("yyyy-MM-dd");
    QJsonObject root = ls::loadJson("habits.json");
    QJsonArray arr = root.value("habits").toArray();
    int doneToday = 0;
    for (int i = 0; i < arr.size(); ++i) {
        const QJsonObject o = arr[i].toObject();
        const QJsonArray log = o.value("log").toArray();
        bool done = false;
        for (const QJsonValue& v : log)
            if (v.toString() == today) { done = true; break; }
        if (done) ++doneToday;
        const int streak = habitStreak(log, today);
        auto* rowW = new QFrame;
        rowW->setObjectName("card");
        auto* h = new QHBoxLayout(rowW);
        h->setContentsMargins(14, 8, 14, 8);
        auto* lab = new QLabel(QString("%1   ·   连续 %2 天").arg(o.value("name").toString()).arg(streak));
        h->addWidget(lab, 1);
        auto* btn = new QPushButton(done ? "今日已打卡 ✓" : "打卡 +5 分");
        btn->setEnabled(!done);
        if (done) btn->setStyleSheet("color:#189d63;");
        const int idx = i;
        connect(btn, &QPushButton::clicked, this, [this, idx] {
            QJsonObject r2 = ls::loadJson("habits.json");
            QJsonArray a2 = r2.value("habits").toArray();
            QJsonObject ob = a2[idx].toObject();
            QJsonArray lg = ob.value("log").toArray();
            const QString td = QDate::currentDate().toString("yyyy-MM-dd");
            for (const QJsonValue& v : lg) if (v.toString() == td) return; // 已打卡
            lg.append(td);
            ob.insert("log", lg);
            a2[idx] = ob;
            r2.insert("habits", a2);
            ls::saveJson("habits.json", r2);
            // 打卡奖励积分
            QJsonObject prof = ls::profile();
            prof.insert("points", prof.value("points").toInt() + 5);
            ls::saveProfile(prof);
            renderHabits();
        });
        h->addWidget(btn);
        auto* del = new QPushButton("删除");
        del->setObjectName("ghost");
        const QString hname = o.value("name").toString();
        connect(del, &QPushButton::clicked, this, [this, idx, hname] {
            if (QMessageBox::question(this, "删除习惯",
                    QString("删除习惯「%1」？历史打卡记录一并删除。").arg(hname)) != QMessageBox::Yes) return;
            QJsonObject r2 = ls::loadJson("habits.json");
            QJsonArray a2 = r2.value("habits").toArray();
            QJsonArray keep;
            for (const QJsonValue& v : a2)
                if ((qint64)v.toObject().value("id").toDouble() != (qint64)a2[idx].toObject().value("id").toDouble())
                    keep.append(v);
            r2.insert("habits", keep);
            ls::saveJson("habits.json", r2);
            renderHabits();
        });
        h->addWidget(del);
        m_habitArea->addWidget(rowW);
    }
    m_habitStat->setText(QString("共 %1 个习惯 · 今日已完成 %2 个 · 连续天数断了会重新计，坚持就是胜利").arg(arr.size()).arg(doneToday));
}

// ---------- 笔记（notes_data.json：左侧列表 + 右侧编辑，自动保存） ----------
QWidget* MainWindow::buildNotePage() {
    auto* h = new QHBoxLayout;
    h->setSpacing(14);

    // 左：笔记列表
    auto* listCard = new QFrame;
    listCard->setObjectName("card");
    auto* lv = new QVBoxLayout(listCard);
    lv->setContentsMargins(12, 12, 12, 12);
    lv->setSpacing(6);
    auto* ltitle = new QLabel("我的笔记");
    ltitle->setObjectName("author");
    lv->addWidget(ltitle);
    m_paperList = new QListWidget;
    m_paperList->setFixedWidth(210);
    lv->addWidget(m_paperList, 1);
    auto* rowN = new QHBoxLayout;
    auto* newBtn = new QPushButton("新建");
    connect(newBtn, &QPushButton::clicked, this, [this] {
        QJsonObject root = ls::loadJson("notes_data.json");
        QJsonArray arr = root.value("items").toArray();
        QJsonObject o;
        const qint64 id = QDateTime::currentMSecsSinceEpoch();
        o.insert("id", (double)id);
        o.insert("title", "新笔记");
        o.insert("content", "");
        o.insert("updated", QDateTime::currentDateTime().toString("MM-dd hh:mm"));
        arr.prepend(o);
        root.insert("items", arr);
        ls::saveJson("notes_data.json", root);
        renderPaperList();
        m_paperId = id;
        m_paperTitle->setText("新笔记");
        m_paperEdit->clear();
        m_paperState->setText("已创建");
    });
    auto* delBtn = new QPushButton("删除");
    delBtn->setObjectName("ghost");
    connect(delBtn, &QPushButton::clicked, this, [this] {
        auto* it = m_paperList->currentItem();
        if (!it) return;
        const qint64 id = it->data(Qt::UserRole).toLongLong();
        if (QMessageBox::question(this, "删除笔记", "确定删除这篇笔记？不可恢复。") != QMessageBox::Yes) return;
        QJsonObject root = ls::loadJson("notes_data.json");
        QJsonArray arr = root.value("items").toArray();
        QJsonArray keep;
        for (const QJsonValue& val : arr)
            if ((qint64)val.toObject().value("id").toDouble() != id) keep.append(val);
        root.insert("items", keep);
        ls::saveJson("notes_data.json", root);
        renderPaperList();
        if (m_paperId == id) { m_paperId = 0; m_paperTitle->clear(); m_paperEdit->clear(); }
    });
    auto* expBtn = new QPushButton("导出TXT");
    expBtn->setObjectName("ghost");
    connect(expBtn, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, "选择导出位置");
        if (dir.isEmpty()) return;
        int n = 0;
        for (const QJsonValue& val : ls::loadJson("notes_data.json").value("items").toArray()) {
            const QJsonObject o = val.toObject();
            const QString safeTitle = o.value("title").toString().trimmed()
                                          .replace(QRegularExpression("[\/:*?\"<>|]"), "_");
            const QString name = QString("%1/%2.txt").arg(dir).arg(safeTitle.isEmpty() ? "未命名" : safeTitle);
            QFile f(name);
            if (f.open(QIODevice::WriteOnly)) {
                f.write(o.value("content").toString().toUtf8());
                ++n;
            }
        }
        if (n) toast("笔记", QString("导出了 %1 篇笔记").arg(n));
    });
    rowN->addWidget(expBtn);
    rowN->addWidget(delBtn);
    lv->addLayout(rowN);
    h->addWidget(listCard);

    // 右：编辑区
    auto* editCard = new QFrame;
    editCard->setObjectName("card");
    auto* ev = new QVBoxLayout(editCard);
    ev->setContentsMargins(14, 12, 14, 12);
    ev->setSpacing(6);
    auto* rowT = new QHBoxLayout;
    m_paperTitle = new QLineEdit;
    m_paperTitle->setPlaceholderText("笔记标题…");
    rowT->addWidget(m_paperTitle, 1);
    m_paperState = new QLabel("");
    m_paperState->setObjectName("dim");
    rowT->addWidget(m_paperState);
    ev->addLayout(rowT);
    m_paperEdit = new QPlainTextEdit;
    m_paperEdit->setPlaceholderText("正文…（输入后 0.8 秒自动保存）");
    ev->addWidget(m_paperEdit, 1);
    h->addWidget(editCard, 1);

    // 列表选择 → 载入
    connect(m_paperList, &QListWidget::itemSelectionChanged, this, [this] {
        auto* it = m_paperList->currentItem();
        if (!it) return;
        const qint64 id = it->data(Qt::UserRole).toLongLong();
        for (const QJsonValue& val : ls::loadJson("notes_data.json").value("items").toArray()) {
            const QJsonObject o = val.toObject();
            if ((qint64)o.value("id").toDouble() == id) {
                m_paperId = id;
                m_paperTitle->setText(o.value("title").toString());
                m_paperEdit->setPlainText(o.value("content").toString());
                m_paperState->setText("更新于 " + o.value("updated").toString());
                break;
            }
        }
    });
    // 自动保存（去抖）
    m_paperSave.setSingleShot(true);
    m_paperSave.setInterval(800);
    connect(&m_paperSave, &QTimer::timeout, this, [this] {
        if (m_paperId == 0) return;
        QJsonObject root = ls::loadJson("notes_data.json");
        QJsonArray arr = root.value("items").toArray();
        for (int i = 0; i < arr.size(); ++i) {
            QJsonObject o = arr[i].toObject();
            if ((qint64)o.value("id").toDouble() == m_paperId) {
                o.insert("title", m_paperTitle->text().trimmed().isEmpty() ? "无标题" : m_paperTitle->text().trimmed());
                o.insert("content", m_paperEdit->toPlainText());
                o.insert("updated", QDateTime::currentDateTime().toString("MM-dd hh:mm"));
                arr[i] = o;
                break;
            }
        }
        root.insert("items", arr);
        ls::saveJson("notes_data.json", root);
        m_paperState->setText("已保存 " + QDateTime::currentDateTime().toString("hh:mm:ss"));
    });
    connect(m_paperTitle, &QLineEdit::textChanged, &m_paperSave, qOverload<>(&QTimer::start));
    connect(m_paperEdit, &QPlainTextEdit::textChanged, &m_paperSave, qOverload<>(&QTimer::start));

    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(24, 20, 24, 20);
    v->setSpacing(12);
    auto* title = new QLabel("笔记");
    title->setObjectName("heroTitle");
    v->addWidget(title);
    auto* sub = new QLabel("长文笔记，自动保存 · 左侧新建/删除，选中即编辑");
    sub->setObjectName("dim");
    v->addWidget(sub);
    v->addLayout(h, 1);
    renderPaperList();
    return page;
}

void MainWindow::renderPaperList() {
    if (!m_paperList) return;
    m_paperList->clear();
    const QJsonArray items = ls::loadJson("notes_data.json").value("items").toArray();
    for (const QJsonValue& val : items) {
        const QJsonObject o = val.toObject();
        auto* it = new QListWidgetItem(QString("%1\n  %2").arg(o.value("title").toString(), o.value("updated").toString()));
        it->setData(Qt::UserRole, (qint64)o.value("id").toDouble());
        m_paperList->addItem(it);
    }
    if (items.isEmpty()) m_paperList->addItem("还没有笔记，点「新建」开始");
}

// ---------- 文本朗读（Windows SAPI 本地语音，离线） ----------
QWidget* MainWindow::buildTtsTab() {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* body = new QWidget;
    auto* v = new QVBoxLayout(body);
    v->setContentsMargins(0, 4, 12, 0);
    v->setSpacing(12);

    QVBoxLayout* lay;
    auto* card = toolCard("文本朗读（Windows 本地语音 · 离线）", lay);
    auto* tip = new QLabel("用系统自带的语音引擎朗读文本（中文系统自带中文女声）。点「停止」可中断。");
    tip->setObjectName("dim");
    tip->setWordWrap(true);
    lay->addWidget(tip);

    auto* row = new QHBoxLayout;
    row->addWidget(new QLabel("语速"));
    auto* rate = new QComboBox;
    rate->addItems({"慢速", "正常", "快速"});
    rate->setCurrentIndex(1);
    row->addWidget(rate);
    auto* speak = new QPushButton("朗 读");
    row->addWidget(speak);
    auto* stop = new QPushButton("停 止");
    stop->setObjectName("ghost");
    stop->setEnabled(false);
    row->addWidget(stop);
    row->addStretch();
    lay->addLayout(row);
    auto* state = new QLabel("");
    state->setObjectName("dim");
    lay->addWidget(state);

    auto* text = new QPlainTextEdit;
    text->setPlaceholderText("粘贴要朗读的文本…（长文也能读，停止可中断）");
    lay->addWidget(text, 1);
    v->addWidget(card);
    v->addStretch();

    connect(speak, &QPushButton::clicked, this, [this, text, rate, speak, stop, state] {
        const QString t = text->toPlainText().trimmed();
        if (t.isEmpty()) { state->setText("先输入文本"); return; }
        if (!m_ttsProc) m_ttsProc = new QProcess(this);
        if (m_ttsProc->state() != QProcess::NotRunning) { state->setText("正在朗读中，先点停止"); return; }
        const int r = rate->currentIndex() == 0 ? -3 : rate->currentIndex() == 2 ? 3 : 0;
        // 脚本文本经环境变量传入，避免引号转义问题
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("TBTEXT", t);
        env.insert("TBRATE", QString::number(r));
        m_ttsProc->setProcessEnvironment(env);
        connect(m_ttsProc, &QProcess::finished, this, [speak, stop, state] {
            stop->setEnabled(false);
            speak->setEnabled(true);
            state->setText("朗读结束");
        });
        m_ttsProc->start("powershell", {"-NoProfile", "-Command",
            "$ErrorActionPreference='Stop';"
            "Add-Type -AssemblyName System.Speech;"
            "$s=New-Object System.Speech.Synthesis.SpeechSynthesizer;"
            "$s.Rate=[int]$env:TBRATE;"
            "$s.Speak($env:TBTEXT)"});
        if (m_ttsProc->state() == QProcess::NotRunning) {
            state->setText("启动失败：" + QString::fromLocal8Bit(m_ttsProc->readAllStandardError()).left(100));
            return;
        }
        speak->setEnabled(false);
        stop->setEnabled(true);
        state->setText("朗读中…");
    });
    connect(stop, &QPushButton::clicked, this, [this, speak, stop, state] {
        if (m_ttsProc && m_ttsProc->state() != QProcess::NotRunning) m_ttsProc->kill();
        speak->setEnabled(true);
        stop->setEnabled(false);
        state->setText("已停止");
    });

    scroll->setWidget(body);
    auto* page = new QWidget;
    auto* wrap = new QVBoxLayout(page);
    wrap->setContentsMargins(0, 0, 0, 0);
    wrap->addWidget(scroll);
    return page;
}

// ---------- 白噪音：程序化生成雨/海浪/白噪/篝火 WAV，winmm PlaySound 循环 ----------
namespace {
// 生成 3 秒 44.1kHz 16bit 单声道 WAV 到内存
QByteArray makeNoiseWav(int kind) {
    const int rate = 44100, sec = 3;
    const int n = rate * sec;
    QByteArray data;
    data.reserve(44 + n * 2);
    auto put16 = [&data](quint16 v) { data.append(char(v & 0xFF)).append(char(v >> 8)); };
    auto put32 = [&data](quint32 v) {
        for (int i = 0; i < 4; ++i) data.append(char((v >> (i * 8)) & 0xFF));
    };
    auto putStr = [&data](const char* s, int len) { data.append(s, len); };
    putStr("RIFF", 4); put32(quint32(36 + n * 2)); putStr("WAVE", 4);
    putStr("fmt ", 4); put32(16); put16(1); put16(1);
    put32(quint32(rate)); put32(quint32(rate * 2)); put16(2); put16(16);
    putStr("data", 4); put32(quint32(n * 2));
    QRandomGenerator rng(kind * 7919 + 41);
    double lp = 0; // 低通状态
    for (int i = 0; i < n; ++i) {
        const double white = rng.generateDouble() * 2.0 - 1.0;
        double v = 0;
        switch (kind) {
        case 0: // 雨声：白噪 + 一阶低通（沙沙）
            lp += 0.18 * (white - lp);
            v = lp * 0.55 + white * 0.12;
            break;
        case 1: { // 海浪：低通噪声 × 慢速幅度起伏
            lp += 0.08 * (white - lp);
            const double swell = 0.45 + 0.55 * std::sin(2.0 * 3.14159265 * 0.22 * i / rate - 1.57);
            v = lp * swell * 0.95;
            break;
        }
        case 2: // 白噪
            v = white * 0.55;
            break;
        case 3: { // 篝火：底噪 + 稀疏爆裂
            lp += 0.25 * (white - lp);
            v = lp * 0.35;
            if (rng.generateDouble() < 0.0009) v += (rng.generateDouble() * 2 - 1) * 0.85;
            break;
        }
        }
        put16(quint16(qBound(-1.0, v, 1.0) * 32000));
    }
    return data;
}
} // namespace

// PlaySound SND_MEMORY 要求音频数据在播放期间常驻
static QByteArray s_wav;
static int s_playing = -1;
// 供番茄钟等外部联动的播放 API（同步白噪音页按钮状态）
static QPushButton* g_noisePlay = nullptr;
static QPushButton* g_noiseStop = nullptr;
void playNoise(int kind) {
    s_wav = makeNoiseWav(kind);
    if (!PlaySoundW(reinterpret_cast<LPCWSTR>(s_wav.constData()), nullptr,
                    SND_MEMORY | SND_ASYNC | SND_LOOP)) return;
    s_playing = kind;
    if (g_noisePlay) g_noisePlay->setEnabled(false);
    if (g_noiseStop) g_noiseStop->setEnabled(true);
}
void stopNoise() {
    PlaySoundW(nullptr, nullptr, 0);
    s_playing = -1;
    if (g_noisePlay) g_noisePlay->setEnabled(true);
    if (g_noiseStop) g_noiseStop->setEnabled(false);
}

QWidget* MainWindow::buildNoiseTab() {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* body = new QWidget;
    auto* v = new QVBoxLayout(body);
    v->setContentsMargins(0, 4, 12, 0);
    v->setSpacing(12);

    QVBoxLayout* lay;
    auto* card = toolCard("白噪音（程序生成 · 本地播放 · 学习/助眠）", lay);
    auto* tip = new QLabel("四种生成音效循环播放，配合番茄钟专注。播放走系统音频，切换页面不影响；程序退出自动停止。");
    tip->setObjectName("dim");
    tip->setWordWrap(true);
    lay->addWidget(tip);
    auto* row = new QHBoxLayout;
    row->addWidget(new QLabel("音效"));
    auto* kind = new QComboBox;
    kind->addItems({"雨声", "海浪", "白噪", "篝火"});
    row->addWidget(kind);
    auto* play = new QPushButton("播 放");
    row->addWidget(play);
    auto* stop = new QPushButton("停 止");
    stop->setObjectName("ghost");
    stop->setEnabled(false);
    row->addWidget(stop);
    row->addStretch();
    lay->addLayout(row);
    auto* state = new QLabel("");
    state->setObjectName("dim");
    lay->addWidget(state);
    v->addWidget(card);
    v->addStretch();

    connect(play, &QPushButton::clicked, this, [this, kind, play, stop, state] {
        const int k = kind->currentIndex();
        if (s_playing == k) { state->setText("这个音效已在播放"); return; }
        s_wav = makeNoiseWav(k);
        // SND_MEMORY|SND_ASYNC|SND_LOOP：循环播放，数据必须常驻
        if (!PlaySoundW(reinterpret_cast<LPCWSTR>(s_wav.constData()), nullptr,
                        SND_MEMORY | SND_ASYNC | SND_LOOP)) {
            state->setText("播放失败（音频设备被占用？）");
            return;
        }
        s_playing = k;
        g_noisePlay = play; g_noiseStop = stop;
        play->setEnabled(false);
        stop->setEnabled(true);
        state->setText(QString("正在播放：%1（切页面、最小化都不停）").arg(kind->currentText()));
    });
    connect(stop, &QPushButton::clicked, this, [this, play, stop, state] {
        PlaySoundW(nullptr, nullptr, 0);
        s_playing = -1;
        g_noisePlay = play; g_noiseStop = stop;
        play->setEnabled(true);
        stop->setEnabled(false);
        state->setText("已停止");
    });

    scroll->setWidget(body);
    auto* page = new QWidget;
    auto* wrap = new QVBoxLayout(page);
    wrap->setContentsMargins(0, 0, 0, 0);
    wrap->addWidget(scroll);
    return page;
}

// ---------- 随机决策：骰子 / 硬币 / 随机数 / 名单抽签 ----------
QWidget* MainWindow::buildDiceTab() {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* body = new QWidget;
    auto* v = new QVBoxLayout(body);
    v->setContentsMargins(0, 4, 12, 0);
    v->setSpacing(12);

    QVBoxLayout* lay;
    auto* card = toolCard("随机决策（本地随机）", lay);
    auto* big = new QLabel("?");
    big->setAlignment(Qt::AlignCenter);
    QFont bf = big->font();
    bf.setPixelSize(44);
    bf.setBold(true);
    big->setFont(bf);
    lay->addWidget(big);

    auto* rowD = new QHBoxLayout;
    rowD->addStretch();
    auto* dice = new QPushButton("掷骰子");
    auto* coin = new QPushButton("抛硬币");
    rowD->addWidget(dice);
    rowD->addWidget(coin);
    rowD->addStretch();
    lay->addLayout(rowD);

    auto* rowR = new QHBoxLayout;
    rowR->addStretch();
    rowR->addWidget(new QLabel("随机数"));
    auto* lo = new QSpinBox;
    lo->setRange(-999999, 999999);
    lo->setValue(1);
    auto* hi = new QSpinBox;
    hi->setRange(-999999, 999999);
    hi->setValue(100);
    auto* gen = new QPushButton("生成");
    rowR->addWidget(lo);
    rowR->addWidget(new QLabel("到"));
    rowR->addWidget(hi);
    rowR->addWidget(gen);
    rowR->addStretch();
    lay->addLayout(rowR);

    lay->addWidget(new QLabel("名单抽签（每行一个，随机抽出，不重复）"));
    auto* names = new QPlainTextEdit;
    names->setPlaceholderText("张三\n李四\n王五…");
    names->setFixedHeight(96);
    lay->addWidget(names);
    auto* rowL = new QHBoxLayout;
    rowL->addWidget(new QLabel("抽"));
    auto* pickN = new QSpinBox;
    pickN->setRange(1, 50);
    pickN->setValue(1);
    rowL->addWidget(pickN);
    rowL->addWidget(new QLabel("人"));
    auto* pick = new QPushButton("抽 签");
    rowL->addWidget(pick);
    rowL->addStretch();
    lay->addLayout(rowL);
    auto* out = new QLabel("");
    out->setObjectName("postTitle");
    out->setWordWrap(true);
    lay->addWidget(out);
    v->addWidget(card);
    v->addStretch();

    connect(dice, &QPushButton::clicked, this, [big] {
        big->setText(QString("🎲 %1").arg(QRandomGenerator::global()->bounded(1, 7)));
    });
    connect(coin, &QPushButton::clicked, this, [big] {
        big->setText(QRandomGenerator::global()->bounded(2) ? "🪙 正面" : "🪙 反面");
    });
    connect(gen, &QPushButton::clicked, this, [big, lo, hi] {
        if (lo->value() > hi->value()) { big->setText("区间反了"); return; }
        big->setText(QString::number(QRandomGenerator::global()->bounded(lo->value(), hi->value() + 1)));
    });
    connect(pick, &QPushButton::clicked, this, [names, pickN, out] {
        QStringList pool;
        for (const QString& l : names->toPlainText().split('\n', Qt::SkipEmptyParts))
            if (!l.trimmed().isEmpty()) pool << l.trimmed();
        if (pool.isEmpty()) { out->setText("先填名单"); return; }
        if (pickN->value() > pool.size()) { out->setText("要抽的人数超过名单大小"); return; }
        QStringList picked;
        for (int i = 0; i < pickN->value(); ++i) {
            const int k = QRandomGenerator::global()->bounded(pool.size());
            picked << pool.takeAt(k);
        }
        out->setText("🎉 " + picked.join("、"));
    });

    scroll->setWidget(body);
    auto* page = new QWidget;
    auto* wrap = new QVBoxLayout(page);
    wrap->setContentsMargins(0, 0, 0, 0);
    wrap->addWidget(scroll);
    return page;
}

// ---------- 日期计算：间隔天数 / 加减天数 / 年内序号 ----------
QWidget* MainWindow::buildDateTab() {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* body = new QWidget;
    auto* v = new QVBoxLayout(body);
    v->setContentsMargins(0, 4, 12, 0);
    v->setSpacing(12);

    QVBoxLayout* lay;
    auto* card = toolCard("日期计算（本地）", lay);
    lay->addWidget(new QLabel("两日期间隔"));
    auto* rowA = new QHBoxLayout;
    auto* d1 = new QDateEdit(QDate::currentDate());
    auto* d2 = new QDateEdit(QDate::currentDate().addDays(30));
    for (QDateEdit* de : {d1, d2}) { de->setCalendarPopup(true); de->setDisplayFormat("yyyy-MM-dd"); }
    rowA->addWidget(d1);
    rowA->addWidget(d2);
    auto* diff = new QPushButton("算间隔");
    rowA->addWidget(diff);
    lay->addLayout(rowA);
    auto* outA = new QLabel("");
    outA->setObjectName("postTitle");
    lay->addWidget(outA);

    lay->addWidget(new QLabel("日期加减天数"));
    auto* rowB = new QHBoxLayout;
    auto* d3 = new QDateEdit(QDate::currentDate());
    d3->setCalendarPopup(true);
    d3->setDisplayFormat("yyyy-MM-dd");
    rowB->addWidget(d3);
    auto* delta = new QSpinBox;
    delta->setRange(-9999, 9999);
    delta->setValue(100);
    delta->setPrefix(delta->value() >= 0 ? "+" : "");
    rowB->addWidget(delta);
    auto* calc = new QPushButton("计算");
    rowB->addWidget(calc);
    lay->addLayout(rowB);
    auto* outB = new QLabel("");
    outB->setObjectName("postTitle");
    lay->addWidget(outB);

    auto* todayInfo = new QLabel(QString("今天是 %1 · 本年第 %2 天 · 第 %3 周（ISO）")
        .arg(QDate::currentDate().toString("yyyy-MM-dd ddd"))
        .arg(QDate::currentDate().dayOfYear())
        .arg(QDate::currentDate().weekNumber()));
    todayInfo->setObjectName("dim");
    lay->addWidget(todayInfo);
    v->addWidget(card);
    v->addStretch();

    connect(diff, &QPushButton::clicked, this, [d1, d2, outA] {
        const int n = d1->date().daysTo(d2->date());
        outA->setText(QString("相差 %1 天（%2 → %3）").arg(n).arg(d1->date().toString("yyyy-MM-dd"), d2->date().toString("yyyy-MM-dd")));
    });
    connect(calc, &QPushButton::clicked, this, [d3, delta, outB] {
        const QDate r = d3->date().addDays(delta->value());
        outB->setText(QString("%1 %2 %3 天 = %4（%5）")
            .arg(d3->date().toString("yyyy-MM-dd"))
            .arg(delta->value() >= 0 ? "+" : "").arg(delta->value())
            .arg(r.toString("yyyy-MM-dd"), r.toString("dddd")));
    });

    scroll->setWidget(body);
    auto* page = new QWidget;
    auto* wrap = new QVBoxLayout(page);
    wrap->setContentsMargins(0, 0, 0, 0);
    wrap->addWidget(scroll);
    return page;
}

// ---------- 世界时钟：常用时区当前时间，1 秒刷新 ----------
QWidget* MainWindow::buildWorldTab() {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* body = new QWidget;
    auto* v = new QVBoxLayout(body);
    v->setContentsMargins(0, 4, 12, 0);
    v->setSpacing(12);

    QVBoxLayout* lay;
    auto* card = toolCard("世界时钟", lay);
    auto* big = new QLabel;
    big->setAlignment(Qt::AlignCenter);
    QFont bf = big->font();
    bf.setPixelSize(30);
    bf.setBold(true);
    big->setFont(bf);
    lay->addWidget(big);

    const struct { const char* city; const char* tz; } zones[] = {
        {"北京", "Asia/Shanghai"}, {"东京", "Asia/Tokyo"}, {"新加坡", "Asia/Singapore"},
        {"伦敦", "Europe/London"}, {"巴黎", "Europe/Paris"}, {"莫斯科", "Europe/Moscow"},
        {"纽约", "America/New_York"}, {"洛杉矶", "America/Los_Angeles"}, {"悉尼", "Australia/Sydney"},
    };
    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(18);
    QVector<QPair<QLabel*, QTimeZone>> rows;
    int col = 0, row = 0;
    for (const auto& z : zones) {
        const QTimeZone tz(z.tz);
        if (!tz.isValid()) continue;
        auto* cell = new QVBoxLayout;
        auto* city = new QLabel(z.city);
        city->setObjectName("dim");
        city->setAlignment(Qt::AlignCenter);
        auto* t = new QLabel("--:--");
        t->setAlignment(Qt::AlignCenter);
        QFont tf = t->font();
        tf.setPixelSize(17);
        tf.setBold(true);
        t->setFont(tf);
        cell->addWidget(city);
        cell->addWidget(t);
        auto* wrapW = new QWidget;
        wrapW->setLayout(cell);
        grid->addWidget(wrapW, row, col % 3);
        if (++col % 3 == 0) ++row;
        rows.append({t, tz});
    }
    lay->addLayout(grid);
    auto* dateLab = new QLabel;
    dateLab->setObjectName("dim");
    dateLab->setAlignment(Qt::AlignCenter);
    lay->addWidget(dateLab);
    v->addWidget(card);
    v->addStretch();

    auto tick = [big, dateLab, rows]() {
        const QDateTime now = QDateTime::currentDateTime();
        big->setText(QTime::currentTime().toString("hh:mm:ss"));
        dateLab->setText(QDate::currentDate().toString("yyyy年M月d日 dddd"));
        for (const auto& r : rows) {
            const QDateTime local = now.toTimeZone(r.second);
            r.first->setText(local.toString("hh:mm"));
        }
    };
    auto* timer = new QTimer(card);
    connect(timer, &QTimer::timeout, card, tick);
    tick();

    scroll->setWidget(body);
    auto* page = new QWidget;
    auto* wrap = new QVBoxLayout(page);
    wrap->setContentsMargins(0, 0, 0, 0);
    wrap->addWidget(scroll);
    // 只在页面可见时走秒（QStackedWidget 切走会触发 Hide）
    page->installEventFilter(this);
    m_worldTimer = timer;
    m_worldPage = page;
    return page;
}

bool MainWindow::eventFilter(QObject* obj, QEvent* e) {
    if (m_worldPage && obj == m_worldPage) {
        if (e->type() == QEvent::Show && m_worldTimer) m_worldTimer->start(1000);
        else if (e->type() == QEvent::Hide && m_worldTimer) m_worldTimer->stop();
    }
    if (m_weatherPage && obj == m_weatherPage && e->type() == QEvent::Show) {
        const auto q = m_weatherQuery;   // 天气 tab 首次显示才发默认城市查询
        const QString city = m_weatherDefault;
        m_weatherPage.clear();           // 只触发一次
        if (q) QTimer::singleShot(200, this, [q, city] { q(city); });
    }
    return QMainWindow::eventFilter(obj, e);
}

} // namespace tb
