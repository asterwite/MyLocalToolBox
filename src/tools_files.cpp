// 文件工具：批量重命名 / 重复文件查找 / 编码转换
// 由 mainwindow.cpp 模块化拆分：本文件实现 MainWindow 的文件类工具箱 tab。
#include "mainwindow.h"
#include "tools_common.h"
#include "localstore.h"

#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QProgressBar>
#include <QRegularExpression>
#include <QSpinBox>
#include <QStringDecoder>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace tb {

// ---------- 批量重命名：查找替换 + 前后缀 + 序号，预览后执行 ----------
QWidget* MainWindow::buildRenameTab() {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* body = new QWidget;
    auto* v = new QVBoxLayout(body);
    v->setContentsMargins(0, 4, 12, 0);
    v->setSpacing(12);

    QVBoxLayout* lay;
    auto* card = toolCard("批量重命名（只处理所选目录下的普通文件，不递归）", lay);
    auto* tip = new QLabel("先选目录 → 设置规则 → 预览（新名标绿色）→ 确认执行。规则按顺序叠加：先查找替换，再加前后缀，最后按需改序号。");
    tip->setObjectName("dim");
    tip->setWordWrap(true);
    lay->addWidget(tip);

    auto* rowDir = new QHBoxLayout;
    auto* dir = new QLineEdit;
    dir->setPlaceholderText("文件夹路径");
    rowDir->addWidget(dir, 1);
    auto* browse = new QPushButton("选择目录…");
    connect(browse, &QPushButton::clicked, this, [dir] {
        const QString d = QFileDialog::getExistingDirectory(nullptr, "选择要重命名的目录");
        if (!d.isEmpty()) dir->setText(d);
    });
    rowDir->addWidget(browse);
    lay->addLayout(rowDir);

    auto* grid = new QGridLayout;
    auto* findEd = new QLineEdit;
    findEd->setPlaceholderText("查找（字面量）");
    auto* replEd = new QLineEdit;
    replEd->setPlaceholderText("替换为（可留空=删除）");
    grid->addWidget(findEd, 0, 0);
    grid->addWidget(replEd, 0, 1);
    auto* prefix = new QLineEdit;
    prefix->setPlaceholderText("添加前缀");
    auto* suffix = new QLineEdit;
    suffix->setPlaceholderText("添加后缀（扩展名前）");
    grid->addWidget(prefix, 1, 0);
    grid->addWidget(suffix, 1, 1);
    lay->addLayout(grid);

    auto* rowNum = new QHBoxLayout;
    auto* ckNum = new QCheckBox("重命名为序号（保留扩展名）");
    auto* numStart = new QSpinBox;
    numStart->setRange(0, 99999);
    numStart->setValue(1);
    numStart->setPrefix("起始 ");
    numStart->setEnabled(false);
    connect(ckNum, &QCheckBox::toggled, numStart, &QSpinBox::setEnabled);
    rowNum->addWidget(ckNum);
    rowNum->addWidget(numStart);
    rowNum->addStretch();
    lay->addLayout(rowNum);

    auto* list = new QListWidget;
    list->setMinimumHeight(220);
    lay->addWidget(list, 1);

    // 应用规则到单个文件名（返回完整新名）
    auto applyRule = [findEd, replEd, prefix, suffix, ckNum, numStart](const QString& old, int idx) {
        if (ckNum->isChecked())
            return QString("%1%2").arg(numStart->value() + idx).arg(QFileInfo(old).suffix().isEmpty() ? "" : "." + QFileInfo(old).suffix());
        QString base = QFileInfo(old).completeBaseName();
        const QString ext = QFileInfo(old).suffix();
        if (!findEd->text().isEmpty()) base.replace(findEd->text(), replEd->text());
        base = prefix->text() + base + suffix->text();
        return ext.isEmpty() ? base : base + "." + ext;
    };

    auto* rowBtn = new QHBoxLayout;
    auto* prev = new QPushButton("预 览");
    connect(prev, &QPushButton::clicked, this, [this, dir, list, applyRule] {
        list->clear();
        const QDir d(dir->text());
        if (!d.exists()) { list->addItem("目录不存在"); return; }
        const QFileInfoList files = d.entryInfoList(QDir::Files, QDir::Name);
        int n = 0;
        for (const QFileInfo& fi : files) {
            const QString nn = applyRule(fi.fileName(), n);
            auto* it = new QListWidgetItem(nn == fi.fileName() ? fi.fileName()
                : fi.fileName() + "  →  " + nn);
            it->setForeground(nn == fi.fileName() ? QPalette().color(QPalette::WindowText) : QColor(0, 160, 60));
            list->addItem(it);
            ++n;
        }
        if (n == 0) list->addItem("(目录下没有文件)");
    });
    auto* run = new QPushButton("执行重命名");
    run->setObjectName("ghost");
    connect(run, &QPushButton::clicked, this, [this, dir, list, applyRule] {
        const QDir d(dir->text());
        if (!d.exists()) { QMessageBox::warning(this, "批量重命名", "目录不存在"); return; }
        if (QMessageBox::question(this, "批量重命名", "按当前规则重命名目录下所有文件？\n" + dir->text()) != QMessageBox::Yes) return;
        const QFileInfoList files = d.entryInfoList(QDir::Files, QDir::Name);
        int ok = 0, skip = 0, fail = 0;
        int n = 0;
        for (const QFileInfo& fi : files) {
            const QString nn = applyRule(fi.fileName(), n);
            ++n;
            if (nn == fi.fileName()) { ++skip; continue; }
            if (QFile::rename(fi.absoluteFilePath(), d.absoluteFilePath(nn))) ++ok;
            else ++fail;
        }
        QMessageBox::information(this, "批量重命名",
            QString("完成：成功 %1 · 无需改 %2 · 失败 %3").arg(ok).arg(skip).arg(fail));
    });
    rowBtn->addWidget(prev);
    rowBtn->addWidget(run);
    rowBtn->addStretch();
    lay->addLayout(rowBtn);
    v->addWidget(card);
    v->addStretch();

    scroll->setWidget(body);
    auto* page = new QWidget;
    auto* wrap = new QVBoxLayout(page);
    wrap->setContentsMargins(0, 0, 0, 0);
    wrap->addWidget(scroll);
    return page;
}

// ---------- 重复文件查找：大小分组 → MD5 精确比对，支持删除 ----------
QWidget* MainWindow::buildDupTab() {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* body = new QWidget;
    auto* v = new QVBoxLayout(body);
    v->setContentsMargins(0, 4, 12, 0);
    v->setSpacing(12);

    QVBoxLayout* lay;
    auto* card = toolCard("重复文件查找（先比大小再比 MD5，同内容分组）", lay);
    auto* tip = new QLabel("扫描目录（含子目录）里内容完全相同的文件，勾选多余的删除。删除不可恢复，请看清路径。");
    tip->setObjectName("dim");
    tip->setWordWrap(true);
    lay->addWidget(tip);

    auto* rowDir = new QHBoxLayout;
    auto* dir = new QLineEdit;
    dir->setPlaceholderText("文件夹路径");
    rowDir->addWidget(dir, 1);
    auto* browse = new QPushButton("选择目录…");
    connect(browse, &QPushButton::clicked, this, [dir] {
        const QString d = QFileDialog::getExistingDirectory(nullptr, "选择扫描目录");
        if (!d.isEmpty()) dir->setText(d);
    });
    rowDir->addWidget(browse);
    auto* scan = new QPushButton("扫 描");
    rowDir->addWidget(scan);
    lay->addLayout(rowDir);

    m_dupState = new QLabel("");
    m_dupState->setObjectName("dim");
    lay->addWidget(m_dupState);
    m_fileBar = new QProgressBar;
    m_fileBar->setTextVisible(false);
    m_fileBar->setFixedHeight(6);
    m_fileBar->hide();
    lay->addWidget(m_fileBar);

    m_dupTree = new QTreeWidget;
    m_dupTree->setColumnCount(2);
    m_dupTree->setHeaderLabels({"文件（勾选要删除的）", "大小"});
    m_dupTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    lay->addWidget(m_dupTree, 1);

    auto* row = new QHBoxLayout;
    auto* keepNew = new QPushButton("自动保留最新");
    keepNew->setObjectName("ghost");
    keepNew->setEnabled(false);
    connect(keepNew, &QPushButton::clicked, this, [this] {
        // 每组勾选除最新外的所有副本（最新 = 修改时间最大者）
        for (int g = 0; g < m_dupTree->topLevelItemCount(); ++g) {
            QTreeWidgetItem* grp = m_dupTree->topLevelItem(g);
            QTreeWidgetItem* newest = nullptr;
            QDateTime newestT;
            for (int i = 0; i < grp->childCount(); ++i) {
                QTreeWidgetItem* c = grp->child(i);
                const QDateTime mt = QFileInfo(c->text(0)).lastModified();
                if (!newest || mt > newestT) { newestT = mt; newest = c; }
            }
            for (int i = 0; i < grp->childCount(); ++i) {
                QTreeWidgetItem* c = grp->child(i);
                c->setCheckState(0, c == newest ? Qt::Unchecked : Qt::Checked);
            }
        }
    });
    row->addWidget(keepNew);
    auto* del = new QPushButton("删除勾选项");
    del->setObjectName("ghost");
    connect(del, &QPushButton::clicked, this, [this] {
        QStringList picked;
        QTreeWidgetItemIterator it(m_dupTree, QTreeWidgetItemIterator::Checked);
        for (; *it; ++it) {
            QTreeWidgetItem* item = *it;
            if (item->parent()) picked << item->text(0);
        }
        if (picked.isEmpty()) { m_dupState->setText("先勾选要删除的文件"); return; }
        if (QMessageBox::question(this, "删除重复文件",
                QString("确定删除勾选的 %1 个文件？不可恢复！").arg(picked.size())) != QMessageBox::Yes) return;
        int ok = 0;
        for (const QString& p : picked)
            if (QFile::remove(p)) ++ok;
        m_dupState->setText(QString("已删除 %1 / %2 个文件").arg(ok).arg(picked.size()));
        // 从树里移除已删项
        QTreeWidgetItemIterator it2(m_dupTree, QTreeWidgetItemIterator::Checked);
        QList<QTreeWidgetItem*> doomed;
        for (; *it2; ++it2)
            if ((*it2)->parent() && QFile::exists((*it2)->text(0)) == false) doomed << *it2;
        for (QTreeWidgetItem* item : doomed) {
            item->parent()->removeChild(item);
            if (item->parent()->childCount() <= 1) { // 组里只剩一个就不算重复
                QTreeWidgetItem* grp = item->parent();
                QTreeWidgetItem* last = grp->takeChildren().value(0);
                delete grp;
                if (last) { last->setCheckState(0, Qt::Unchecked); m_dupTree->addTopLevelItem(last); }
            }
        }
    });
    row->addWidget(del);
    row->addStretch();
    lay->addLayout(row);
    v->addWidget(card);
    v->addStretch();

    connect(scan, &QPushButton::clicked, this, [this, dir, keepNew] {
        const QDir d(dir->text());
        if (!d.exists()) { m_dupState->setText("目录不存在"); return; }
        m_dupTree->clear();
        m_dupState->setText("扫描中…");
        QApplication::processEvents();
        // 1) 收集全部文件按大小分组
        QHash<qint64, QStringList> bySize;
        QDirIterator it(d.absolutePath(), QDir::Files, QDirIterator::Subdirectories);
        int total = 0;
        while (it.hasNext()) {
            bySize[QFileInfo(it.next()).size()] << it.filePath();
            if (++total % 400 == 0) { m_dupState->setText(QString("扫描中… %1 个文件").arg(total)); QApplication::processEvents(); }
        }
        // 2) 大小相同的组再比 MD5
        int groups = 0;
        qint64 waste = 0;
        for (auto sit = bySize.constBegin(); sit != bySize.constEnd(); ++sit) {
            if (sit.value().size() < 2) continue;
            QHash<QByteArray, QStringList> byMd5;
            for (const QString& p : sit.value()) {
                QFile f(p);
                QCryptographicHash h(QCryptographicHash::Md5);
                if (!f.open(QIODevice::ReadOnly)) continue;
                while (!f.atEnd()) h.addData(f.read(65536));
                byMd5[h.result().toHex()].append(p);
            }
            for (auto mit = byMd5.constBegin(); mit != byMd5.constEnd(); ++mit) {
                if (mit.value().size() < 2) continue;
                ++groups;
                waste += qint64(sit.key()) * (mit.value().size() - 1);
                auto* grp = new QTreeWidgetItem(m_dupTree, {QString("重复组 · %1 份内容相同").arg(mit.value().size()),
                                                            QString::number(sit.key()) + " B"});
                grp->setFirstColumnSpanned(true);
                for (const QString& p : mit.value()) {
                    auto* child = new QTreeWidgetItem(grp, {p, QFileInfo(p).size() < 1024
                        ? QString::number(QFileInfo(p).size()) + " B"
                        : QString::number(QFileInfo(p).size() / 1024.0, 'f', 1) + " KB"});
                    child->setCheckState(0, Qt::Unchecked);
                }
                grp->setExpanded(true);
            }
        }
        m_fileBar->hide();
        keepNew->setEnabled(groups > 0);
        m_dupState->setText(QString("共 %1 个文件，发现 %2 组重复，可释放约 %3 MB")
                                .arg(total).arg(groups).arg(QString::number(waste / 1048576.0, 'f', 2)));
    });

    scroll->setWidget(body);
    auto* page = new QWidget;
    auto* wrap = new QVBoxLayout(page);
    wrap->setContentsMargins(0, 0, 0, 0);
    wrap->addWidget(scroll);
    return page;
}

// ---------- 编码转换：GB18030 ↔ UTF-8（PowerShell .NET 引擎，本机离线） ----------
QWidget* MainWindow::buildTranscodeTab() {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* body = new QWidget;
    auto* v = new QVBoxLayout(body);
    v->setContentsMargins(0, 4, 12, 0);
    v->setSpacing(12);

    QVBoxLayout* lay;
    auto* card = toolCard("文本文件编码转换（GB18030 ↔ UTF-8）", lay);
    auto* tip = new QLabel("老的 txt/csv 常是 GBK/GB18030，现代编辑器默认 UTF-8。选文件自动检测：合法 UTF-8 显示「UTF-8」，否则按 GB18030 处理。转换写到新文件，不覆盖原文件。");
    tip->setObjectName("dim");
    tip->setWordWrap(true);
    lay->addWidget(tip);

    auto* rowDir = new QHBoxLayout;
    auto* file = new QLineEdit;
    file->setPlaceholderText("文本文件路径（.txt/.csv/.md/…）");
    rowDir->addWidget(file, 1);
    auto* browse = new QPushButton("选择文件…");
    connect(browse, &QPushButton::clicked, this, [file] {
        const QString f = QFileDialog::getOpenFileName(nullptr, "选择文本文件", QString(),
                                                       "文本文件 (*.txt *.csv *.md *.log *.json *.srt *.lrc);;所有文件 (*.*)");
        if (!f.isEmpty()) file->setText(f);
    });
    rowDir->addWidget(browse);
    lay->addLayout(rowDir);

    auto detect = [file]() -> QString {
        QFile f(file->text());
        if (!f.open(QIODevice::ReadOnly)) return QString();
        const QByteArray head = f.read(3);
        if (head.startsWith("\xEF\xBB\xBF")) return "utf8bom";
        f.seek(0);
        // 严格 UTF-8 校验：失败即视为 GB18030
        const QByteArray all = f.readAll();
        QStringDecoder dec(QStringConverter::Utf8, QStringConverter::Flag::Stateless);
        const QString s = dec.decode(all);
        return dec.hasError() ? "gb18030" : "utf8";
    };

    auto* row = new QHBoxLayout;
    auto* state = new QLabel("");
    state->setObjectName("dim");
    auto* toUtf8 = new QPushButton("转成 UTF-8 另存…");
    connect(toUtf8, &QPushButton::clicked, this, [this, file, detect, state] {
        const QString enc = detect();
        if (enc.isEmpty()) { state->setText("文件读不到"); return; }
        if (enc != "gb18030") { state->setText(QString("文件已经是 UTF-8（%1），无需转换").arg(enc == "utf8bom" ? "带 BOM" : "无 BOM")); return; }
        const QString out = QFileDialog::getSaveFileName(this, "另存为 UTF-8",
            QFileInfo(file->text()).absolutePath() + "/" + QFileInfo(file->text()).completeBaseName() + ".utf8.txt");
        if (out.isEmpty()) return;
        QProcess p;
        const QString script = QString("$ErrorActionPreference='Stop';"
            "$src=[Text.Encoding]::GetEncoding(54936);"
            "$t=[IO.File]::ReadAllText('%1', $src);"
            "[IO.File]::WriteAllText('%2', $t, (New-Object Text.UTF8Encoding($false)));"
            "'OK'").arg(QString(file->text()).replace("'", "''"), QString(out).replace("'", "''"));
        p.start("powershell", {"-NoProfile", "-Command", script});
        p.waitForFinished(30000);
        const QString o = QString::fromUtf8(p.readAllStandardOutput()).trimmed();
        if (o == "OK") { state->setText("转换完成：" + out); toast("文件转码", "已转换并另存为 UTF-8"); }
        else state->setText("失败：" + QString::fromUtf8(p.readAllStandardError()).left(120));
    });
    auto* toGb = new QPushButton("转成 GB18030 另存…");
    connect(toGb, &QPushButton::clicked, this, [this, file, detect, state] {
        const QString enc = detect();
        if (enc.isEmpty()) { state->setText("文件读不到"); return; }
        if (enc == "gb18030") { state->setText("文件已经是 GB18030（或非 UTF-8），无需转换"); return; }
        const QString out = QFileDialog::getSaveFileName(this, "另存为 GB18030",
            QFileInfo(file->text()).absolutePath() + "/" + QFileInfo(file->text()).completeBaseName() + ".gb.txt");
        if (out.isEmpty()) return;
        QProcess p;
        const QString script = QString("$ErrorActionPreference='Stop';"
            "$t=[IO.File]::ReadAllText('%1', [Text.Encoding]::UTF8);"
            "[IO.File]::WriteAllText('%2', $t, [Text.Encoding]::GetEncoding(54936));"
            "'OK'").arg(QString(file->text()).replace("'", "''"), QString(out).replace("'", "''"));
        p.start("powershell", {"-NoProfile", "-Command", script});
        p.waitForFinished(30000);
        const QString o = QString::fromUtf8(p.readAllStandardOutput()).trimmed();
        if (o == "OK") { state->setText("转换完成：" + out); toast("文件转码", "已转换并另存为 GB18030"); }
        else state->setText("失败：" + QString::fromUtf8(p.readAllStandardError()).left(120));
    });
    row->addWidget(toUtf8);
    row->addWidget(toGb);
    row->addStretch();
    lay->addLayout(row);
    lay->addWidget(state);
    v->addWidget(card);
    v->addStretch();

    scroll->setWidget(body);
    auto* page = new QWidget;
    auto* wrap = new QVBoxLayout(page);
    wrap->setContentsMargins(0, 0, 0, 0);
    wrap->addWidget(scroll);
    return page;
}

// ---------- 定时关机/重启/锁屏/休眠（系统命令封装，可取消） ----------
QWidget* MainWindow::buildShutdownTab() {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* body = new QWidget;
    auto* v = new QVBoxLayout(body);
    v->setContentsMargins(0, 4, 12, 0);
    v->setSpacing(12);

    QVBoxLayout* lay;
    auto* card = toolCard("定时电源（关机 / 重启 / 锁屏 / 休眠）", lay);
    auto* tip = new QLabel("下载完、挂机跑完任务用。延迟期间可用系统自带 shutdown /a 或本页「取消」撤销。锁屏与休眠立即生效（不受延迟控制）。");
    tip->setObjectName("dim");
    tip->setWordWrap(true);
    lay->addWidget(tip);

    auto* row = new QHBoxLayout;
    row->addWidget(new QLabel("动作"));
    auto* act = new QComboBox;
    act->addItems({"关机", "重启", "锁屏", "休眠"});
    row->addWidget(act);
    row->addWidget(new QLabel("延迟"));
    auto* mins = new QSpinBox;
    mins->setRange(0, 720);
    mins->setValue(30);
    mins->setSuffix(" 分钟");
    row->addWidget(mins);
    auto* go = new QPushButton("执 行");
    row->addWidget(go);
    auto* cancel = new QPushButton("取消已计划");
    cancel->setObjectName("ghost");
    row->addWidget(cancel);
    row->addStretch();
    lay->addLayout(row);
    auto* state = new QLabel("");
    state->setObjectName("dim");
    lay->addWidget(state);
    v->addWidget(card);
    v->addStretch();

    connect(go, &QPushButton::clicked, this, [this, act, mins, state] {
        const QString a = act->currentText();
        if (a == "锁屏") {
            QProcess::startDetached("rundll32.exe", {"user32.dll,LockWorkStation"});
            state->setText("已锁屏");
            return;
        }
        if (a == "休眠") {
            if (QMessageBox::question(this, "定时电源", "立即休眠？") != QMessageBox::Yes) return;
            QProcess::startDetached("shutdown", {"/h"});
            state->setText("系统正在休眠");
            return;
        }
        const int sec = mins->value() * 60;
        if (QMessageBox::question(this, "定时电源",
                QString("%1：%2 后执行，确认？").arg(a, mins->value() == 0 ? "立即" : QString("%1 分钟").arg(mins->value())))
            != QMessageBox::Yes) return;
        QProcess p;
        p.start("shutdown", {a == "关机" ? "/s" : "/r", "/t", QString::number(sec)});
        p.waitForFinished(5000);
        if (p.exitCode() == 0)
            state->setText(QString("已计划：%1 于 %2 后执行（可用「取消已计划」撤销）").arg(a).arg(mins->value() == 0 ? "立即" : QString("%1 分钟").arg(mins->value())));
        else
            state->setText("执行失败：" + QString::fromLocal8Bit(p.readAllStandardError()).left(100));
    });
    connect(cancel, &QPushButton::clicked, this, [state] {
        QProcess p;
        p.start("shutdown", {"/a"});
        p.waitForFinished(5000);
        state->setText(p.exitCode() == 0 ? "已取消计划的关机/重启" : "没有待取消的计划");
    });

    scroll->setWidget(body);
    auto* page = new QWidget;
    auto* wrap = new QVBoxLayout(page);
    wrap->setContentsMargins(0, 0, 0, 0);
    wrap->addWidget(scroll);
    return page;
}

// ---------- 启动项查看（注册表 Run 键 · 只读，不修改） ----------
QWidget* MainWindow::buildStartupTab() {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* body = new QWidget;
    auto* v = new QVBoxLayout(body);
    v->setContentsMargins(0, 4, 12, 0);
    v->setSpacing(12);

    QVBoxLayout* lay;
    auto* card = toolCard("开机启动项（注册表 Run · 只读查看）", lay);
    auto* tip = new QLabel("列出当前用户与全机的注册表自启动项。想禁用某项：任务管理器 → 启动应用，这里只做查看不做修改，避免误伤系统项。");
    tip->setObjectName("dim");
    tip->setWordWrap(true);
    lay->addWidget(tip);

    auto* row = new QHBoxLayout;
    auto* refresh = new QPushButton("刷 新");
    auto* openReg = new QPushButton("打开注册表编辑器");
    openReg->setObjectName("ghost");
    row->addWidget(refresh);
    row->addWidget(openReg);
    row->addStretch();
    lay->addLayout(row);
    auto* list = new QListWidget;
    list->setMinimumHeight(240);
    lay->addWidget(list, 1);
    auto* state = new QLabel("");
    state->setObjectName("dim");
    lay->addWidget(state);
    v->addWidget(card);
    v->addStretch();

    auto query = [list, state](const QString& key, const QString& tag) {
        QProcess p;
        p.start("reg", {"query", key});
        p.waitForFinished(8000);
        const QStringList lines = QString::fromLocal8Bit(p.readAllStandardOutput()).split('\n');
        int n = 0;
        for (const QString& l : lines) {
            const QString t = l.trimmed();
            if (t.contains("REG_")) { // 数据行格式：名称    类型    值
                const QStringList parts = t.split(QRegularExpression("\\s{4,}"));
                if (parts.size() >= 3) {
                    list->addItem(QString("[%1] %2  →  %3").arg(tag, parts[0], parts[2]));
                    ++n;
                }
            }
        }
        return n;
    };
    connect(refresh, &QPushButton::clicked, this, [list, state, query] {
        list->clear();
        int n = query("HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", "当前用户");
        n += query("HKLM\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", "全机");
        n += query("HKLM\\Software\\Wow6432Node\\Microsoft\\Windows\\CurrentVersion\\Run", "全机32");
        state->setText(QString("共 %1 个自启动项（不含任务计划与启动文件夹）").arg(n));
    });
    connect(openReg, &QPushButton::clicked, this, [] {
        QProcess::startDetached("regedit");
    });

    scroll->setWidget(body);
    auto* page = new QWidget;
    auto* wrap = new QVBoxLayout(page);
    wrap->setContentsMargins(0, 0, 0, 0);
    wrap->addWidget(scroll);
    return page;
}

// ---------- 磁盘助手：目录占用分析（子目录排行 + 最大文件） ----------
QWidget* MainWindow::buildDiskPage() {
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(24, 20, 24, 20);
    v->setSpacing(12);

    auto* title = new QLabel("磁盘助手");
    title->setObjectName("heroTitle");
    v->addWidget(title);
    auto* sub = new QLabel("分析目录占用：哪个子文件夹最大、哪些文件最占空间（仅读盘，不修改任何文件）");
    sub->setObjectName("dim");
    v->addWidget(sub);

    auto* row = new QHBoxLayout;
    auto* dir = new QLineEdit;
    dir->setPlaceholderText("要分析的目录…");
    row->addWidget(dir, 1);
    auto* browse = new QPushButton("选择目录…");
    connect(browse, &QPushButton::clicked, this, [dir] {
        const QString d = QFileDialog::getExistingDirectory(nullptr, "选择要分析的目录");
        if (!d.isEmpty()) dir->setText(d);
    });
    row->addWidget(browse);
    auto* go = new QPushButton("分 析");
    row->addWidget(go);
    v->addLayout(row);

    m_diskState = new QLabel("");
    m_diskState->setObjectName("dim");
    v->addWidget(m_diskState);
    m_fileBar = new QProgressBar;
    m_fileBar->setTextVisible(false);
    m_fileBar->setFixedHeight(6);
    m_fileBar->hide();
    v->addWidget(m_fileBar);

    auto* cols = new QHBoxLayout;
    auto* leftBox = new QVBoxLayout;
    leftBox->addWidget(new QLabel("子目录占用（从大到小）"));
    m_diskDirs = new QListWidget;
    leftBox->addWidget(m_diskDirs, 1);
    cols->addLayout(leftBox, 1);
    auto* rightBox = new QVBoxLayout;
    rightBox->addWidget(new QLabel("最大文件 TOP 30"));
    m_diskFiles = new QListWidget;
    rightBox->addWidget(m_diskFiles, 1);
    cols->addLayout(rightBox, 1);
    v->addLayout(cols, 1);

    connect(go, &QPushButton::clicked, this, [this, dir, go] {
        const QDir d(dir->text());
        if (!d.exists()) { m_diskState->setText("目录不存在"); return; }
        go->setEnabled(false);
        m_fileBar->setRange(0, 0);
    m_fileBar->show();
    m_diskState->setText("扫描中…");
        QApplication::processEvents();

        QHash<QString, qint64> subSize;   // 一级子目录 → 累计大小
        QHash<QString, qint64> fileSizes; // 路径 → 大小
        qint64 total = 0, topFiles = 0;
        QDirIterator it(d.absolutePath(), QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QFileInfo fi(it.next());
            fileSizes.insert(fi.absoluteFilePath(), fi.size());
            total += fi.size();
            // 归属一级子目录
            const QString rel = d.relativeFilePath(fi.absoluteFilePath());
            const QStringList segs = rel.split('/');
            if (segs.size() >= 2) subSize[segs[0]] += fi.size();
            if (++topFiles % 800 == 0) {
                m_diskState->setText(QString("扫描中… %1 个文件 · 累计 %2 MB").arg(topFiles).arg(QString::number(total / 1048576.0, 'f', 1)));
                QApplication::processEvents();
            }
        }

        // 子目录排行
        m_diskDirs->clear();
        QVector<QPair<qint64, QString>> subs;
        for (auto sit = subSize.constBegin(); sit != subSize.constEnd(); ++sit)
            subs.append({sit.value(), sit.key()});
        std::sort(subs.begin(), subs.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        auto fmt = [](qint64 b) {
            if (b >= 1073741824LL) return QString::number(b / 1073741824.0, 'f', 2) + " GB";
            if (b >= 1048576LL) return QString::number(b / 1048576.0, 'f', 1) + " MB";
            return QString::number(b / 1024.0, 'f', 1) + " KB";
        };
        for (const auto& s : subs)
            m_diskDirs->addItem(QString("%1  ·  %2").arg(s.second, fmt(s.first)));
        if (subs.isEmpty()) m_diskDirs->addItem("(没有子目录)");

        // 最大文件 TOP 30
        m_diskFiles->clear();
        QVector<QPair<qint64, QString>> files;
        for (auto fit = fileSizes.constBegin(); fit != fileSizes.constEnd(); ++fit)
            files.append({fit.value(), fit.key()});
        std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        for (int i = 0; i < files.size() && i < 30; ++i) {
            const QString rel = d.relativeFilePath(files[i].second);
            m_diskFiles->addItem(QString("%1  ·  %2").arg(fmt(files[i].first), rel.size() > 52 ? "…" + rel.right(52) : rel));
        }
        m_fileBar->hide();
        go->setEnabled(true);
        m_diskState->setText(QString("完成：%1 个文件 · 总大小 %2 · 子目录 %3 个")
                                 .arg(topFiles).arg(fmt(total)).arg(subs.size()));
    });

    return page;
}

// ---------- 文件搜索：按名称递归搜目录，双击打开所在文件夹 ----------
QWidget* MainWindow::buildFileSearchTab() {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* body = new QWidget;
    auto* v = new QVBoxLayout(body);
    v->setContentsMargins(0, 4, 12, 0);
    v->setSpacing(12);

    QVBoxLayout* lay;
    auto* card = toolCard("文件搜索（按名称找文件 · 只读）", lay);
    auto* rowDir = new QHBoxLayout;
    auto* dir = new QLineEdit;
    dir->setPlaceholderText("搜索范围（目录）");
    rowDir->addWidget(dir, 1);
    auto* browse = new QPushButton("目录…");
    connect(browse, &QPushButton::clicked, this, [dir] {
        const QString d = QFileDialog::getExistingDirectory(nullptr, "选择搜索范围");
        if (!d.isEmpty()) dir->setText(d);
    });
    rowDir->addWidget(browse);
    lay->addLayout(rowDir);
    auto* row = new QHBoxLayout;
    auto* kw = new QLineEdit;
    kw->setPlaceholderText("文件名关键词（支持通配符如 *.pdf，回车搜索）");
    row->addWidget(kw, 1);
    auto* go = new QPushButton("搜 索");
    row->addWidget(go);
    lay->addLayout(row);
    m_diskState = new QLabel("");
    m_diskState->setObjectName("dim");
    lay->addWidget(m_diskState);
    m_fileBar = new QProgressBar;
    m_fileBar->setTextVisible(false);
    m_fileBar->setFixedHeight(6);
    m_fileBar->hide();
    lay->addWidget(m_fileBar);
    auto* list = new QListWidget;
    list->setMinimumHeight(280);
    list->setToolTip("双击打开文件所在文件夹");
    lay->addWidget(list, 1);
    v->addWidget(card);
    v->addStretch();

    connect(go, &QPushButton::clicked, this, [this, dir, kw, list, go] {
        const QDir d(dir->text());
        if (!d.exists() || kw->text().trimmed().isEmpty()) { m_diskState->setText("先选目录并输入关键词"); return; }
        list->clear();
        go->setEnabled(false);
        m_fileBar->setRange(0, 0);
        m_fileBar->show();
        m_diskState->setText("搜索中…");
        QApplication::processEvents();
        QString pattern = kw->text().trimmed();
        if (!pattern.contains('*') && !pattern.contains('?')) pattern = "*" + pattern + "*";
        int n = 0;
        QDirIterator it(d.absolutePath(), QStringList{pattern}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QFileInfo fi(it.next());
            auto* item = new QListWidgetItem(QString("%1  ·  %2 KB  ·  %3")
                .arg(fi.size() < 1024 ? QString::number(fi.size()) + " B" : QString::number(fi.size() / 1024.0, 'f', 0) + " KB")
                .arg(fi.lastModified().toString("yyyy-MM-dd"), d.relativeFilePath(fi.absoluteFilePath())));
            item->setData(Qt::UserRole, fi.absoluteFilePath());
            list->addItem(item);
            if (++n >= 500) { m_diskState->setText("结果过多，只显示前 500 条（请缩小范围）"); break; }
            if (n % 300 == 0) QApplication::processEvents();
        }
        m_fileBar->hide();
        go->setEnabled(true);
        if (n < 500) m_diskState->setText(QString("共找到 %1 个文件").arg(n));
    });
    connect(list, &QListWidget::itemDoubleClicked, this, [](QListWidgetItem* it) {
        const QString p = it->data(Qt::UserRole).toString();
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(p).absolutePath()));
    });

    scroll->setWidget(body);
    auto* page = new QWidget;
    auto* wrap = new QVBoxLayout(page);
    wrap->setContentsMargins(0, 0, 0, 0);
    wrap->addWidget(scroll);
    return page;
}

// ---------- 图片工具：信息 / 格式转换 / 压缩 / 缩放（纯 QImage 本地处理） ----------
QWidget* MainWindow::buildImgTab() {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* body = new QWidget;
    auto* v = new QVBoxLayout(body);
    v->setContentsMargins(0, 4, 12, 0);
    v->setSpacing(12);

    QVBoxLayout* lay;
    auto* card = toolCard("图片工具（格式转换 · 压缩 · 缩放 · 本地处理）", lay);
    auto* row = new QHBoxLayout;
    auto* file = new QLineEdit;
    file->setPlaceholderText("图片路径（png/jpg/webp/bmp）");
    row->addWidget(file, 1);
    auto* browse = new QPushButton("选择图片…");
    connect(browse, &QPushButton::clicked, this, [file] {
        const QString f = QFileDialog::getOpenFileName(nullptr, "选择图片", QString(),
            "图片 (*.png *.jpg *.jpeg *.webp *.bmp);;所有文件 (*.*)");
        if (!f.isEmpty()) file->setText(f);
    });
    row->addWidget(browse);
    lay->addLayout(row);

    auto* info = new QLabel("");
    info->setObjectName("dim");
    lay->addWidget(info);

    m_imgPreview = new QLabel;
    m_imgPreview->setAlignment(Qt::AlignCenter);
    m_imgPreview->setMinimumHeight(180);
    m_imgPreview->setObjectName("dim");
    lay->addWidget(m_imgPreview, 1);

    // 载入与预览刷新
    QImage img;
    auto loadImg = [this, file, info, img]() mutable -> bool {
        img = QImage(file->text().trimmed());
        if (img.isNull()) { info->setText("无法读取图片"); m_imgPreview->setText("(无预览)"); return false; }
        const QFileInfo fi(file->text());
        info->setText(QString("尺寸 %1 × %2  ·  文件 %3 KB  ·  格式 %4")
            .arg(img.width()).arg(img.height())
            .arg(QString::number(fi.size() / 1024.0, 'f', 1))
            .arg(fi.suffix().toUpper()));
        m_imgPreview->setPixmap(QPixmap::fromImage(img).scaled(
            m_imgPreview->width() > 40 ? m_imgPreview->width() - 20 : 400, 260,
            Qt::KeepAspectRatio, Qt::SmoothTransformation));
        return true;
    };
    connect(file, &QLineEdit::textChanged, this, [this, loadImg]() mutable { loadImg(); });

    auto* rowQ = new QHBoxLayout;
    rowQ->addWidget(new QLabel("质量"));
    auto* quality = new QSpinBox;
    quality->setRange(10, 100);
    quality->setValue(85);
    rowQ->addWidget(quality);
    rowQ->addWidget(new QLabel("缩放到宽"));
    auto* width = new QSpinBox;
    width->setRange(16, 20000);
    width->setValue(1280);
    width->setSuffix(" px");
    rowQ->addWidget(width);
    auto* scaleCk = new QCheckBox("执行缩放");
    rowQ->addWidget(scaleCk);
    rowQ->addStretch();
    lay->addLayout(rowQ);

    auto* rowCv = new QHBoxLayout;
    for (const QString& fmt : {"PNG", "JPG", "WEBP"}) {
        auto* b = new QPushButton("存为 " + fmt);
        b->setObjectName("ghost");
        const QString f = fmt;
        connect(b, &QPushButton::clicked, this, [this, file, img, quality, width, scaleCk, f, loadImg]() mutable {
            if (!img.isNull() || loadImg()) {
                QImage work = img;
                if (scaleCk->isChecked() && work.width() > 0)
                    work = work.scaledToWidth(width->value(), Qt::SmoothTransformation);
                const QString out = QFileDialog::getSaveFileName(this, "另存为 " + f,
                    QFileInfo(file->text()).absolutePath() + "/" + QFileInfo(file->text()).completeBaseName() + "." + f.toLower());
                if (out.isEmpty()) return;
                const bool ok = f == "JPG"
                    ? work.save(out, "JPG", quality->value())
                    : work.save(out, f.toUtf8().constData(), f == "WEBP" ? quality->value() : -1);
                QMessageBox::information(this, "图片工具", ok ? "已保存：" + out : "保存失败");
            }
        });
        rowCv->addWidget(b);
    }
    rowCv->addStretch();
    lay->addLayout(rowCv);
    v->addWidget(card);
    v->addStretch();

    scroll->setWidget(body);
    auto* page = new QWidget;
    auto* wrap = new QVBoxLayout(page);
    wrap->setContentsMargins(0, 0, 0, 0);
    wrap->addWidget(scroll);
    return page;
}

// ---------- 系统清理：用户 TEMP 目录扫描与 7 天前文件清理 ----------
QWidget* MainWindow::buildCleanupTab() {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* body = new QWidget;
    auto* v = new QVBoxLayout(body);
    v->setContentsMargins(0, 4, 12, 0);
    v->setSpacing(12);

    QVBoxLayout* lay;
    auto* card = toolCard("临时文件清理（只动用户 TEMP 目录 · 7 天前的旧文件）", lay);
    auto* tip = new QLabel("扫描当前用户的临时目录，统计占用并清理 7 天前的旧文件（正在使用的文件会自动跳过）。系统盘、下载目录等其他位置一概不碰。");
    tip->setObjectName("dim");
    tip->setWordWrap(true);
    lay->addWidget(tip);

    auto* row = new QHBoxLayout;
    auto* scan = new QPushButton("扫 描");
    row->addWidget(scan);
    auto* clean = new QPushButton("清理 7 天前的旧文件");
    clean->setObjectName("ghost");
    clean->setEnabled(false);
    row->addWidget(clean);
    row->addStretch();
    lay->addLayout(row);

    auto* state = new QLabel("");
    state->setObjectName("dim");
    state->setWordWrap(true);
    lay->addWidget(state);
    auto* list = new QListWidget;
    list->setMinimumHeight(220);
    lay->addWidget(list, 1);
    v->addWidget(card);
    v->addStretch();

    auto fmt = [](qint64 b) {
        if (b >= 1073741824LL) return QString::number(b / 1073741824.0, 'f', 2) + " GB";
        if (b >= 1048576LL) return QString::number(b / 1048576.0, 'f', 1) + " MB";
        return QString::number(b / 1024.0, 'f', 1) + " KB";
    };
    qint64* oldBytes = new qint64(0);
    qint64* oldCount = new qint64(0);

    connect(scan, &QPushButton::clicked, this, [this, list, state, clean, fmt, oldBytes, oldCount] {
        list->clear();
        clean->setEnabled(false);
        *oldBytes = 0; *oldCount = 0;
        const QString tmp = QDir::tempPath();
        state->setText("扫描 " + tmp + " …");
        QApplication::processEvents();
        qint64 total = 0, count = 0;
        const qint64 cutoff = QDateTime::currentSecsSinceEpoch() - 7 * 86400;
        QDirIterator it(tmp, QDir::Files, QDirIterator::Subdirectories);
        QHash<QString, qint64> top;
        while (it.hasNext()) {
            const QFileInfo fi(it.next());
            total += fi.size();
            ++count;
            top[fi.absoluteFilePath()] = fi.size();
            const qint64 mt = fi.lastModified().toSecsSinceEpoch();
            if (mt < cutoff) { *oldBytes += fi.size(); ++*oldCount; }
            if (count % 500 == 0) { state->setText(QString("扫描中… %1 个文件").arg(count)); QApplication::processEvents(); }
        }
        QVector<QPair<qint64, QString>> ranked;
        for (auto t = top.constBegin(); t != top.constEnd(); ++t) ranked.append({t.value(), t.key()});
        std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        for (int i = 0; i < ranked.size() && i < 20; ++i)
            list->addItem(QString("%1  ·  %2").arg(fmt(ranked[i].first), ranked[i].second));
        clean->setEnabled(*oldBytes > 0);
        state->setText(QString("临时目录共 %1 个文件 · 占用 %2 · 其中 7 天前的旧文件 %3 个（%4），可清理")
            .arg(count).arg(fmt(total)).arg(*oldCount).arg(fmt(*oldBytes)));
    });

    connect(clean, &QPushButton::clicked, this, [this, list, state, clean, fmt, oldBytes, oldCount, scan] {
        if (QMessageBox::question(this, "清理临时文件",
                QString("删除临时目录里 %1 天前的 %2 个文件（约 %3）？\n正在使用的文件会自动跳过。")
                    .arg(7).arg(*oldCount).arg(fmt(*oldBytes))) != QMessageBox::Yes) return;
        clean->setEnabled(false);
        const QString tmp = QDir::tempPath();
        const qint64 cutoff = QDateTime::currentSecsSinceEpoch() - 7 * 86400;
        int ok = 0, skip = 0;
        qint64 freed = 0;
        QDirIterator it(tmp, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QFileInfo fi(it.next());
            if (fi.lastModified().toSecsSinceEpoch() >= cutoff) continue;
            const qint64 sz = fi.size();
            if (QFile::remove(fi.absoluteFilePath())) { ++ok; freed += sz; }
            else ++skip;
            if ((ok + skip) % 200 == 0) { state->setText(QString("清理中… 已删 %1").arg(ok)); QApplication::processEvents(); }
        }
        *oldBytes = 0; *oldCount = 0;
        state->setText(QString("清理完成：删除 %1 个文件，释放 %2 · 跳过 %3 个（占用中）").arg(ok).arg(fmt(freed)).arg(skip));
        toast("系统清理", QString("释放了 %1 空间").arg(fmt(freed)));
        QTimer::singleShot(400, this, [scan] { scan->click(); }); // 自动重扫看最新占用
        list->clear();
    });

    scroll->setWidget(body);
    auto* page = new QWidget;
    auto* wrap = new QVBoxLayout(page);
    wrap->setContentsMargins(0, 0, 0, 0);
    wrap->addWidget(scroll);
    return page;
}

} // namespace tb
