// 密码库：主口令 + AES-256 加密整库存储（vault.json），解锁后内存操作
// 参考 KeePass 的思路：主口令不落盘，库文件只有密文，重启/锁定后需重新解锁
#include "mainwindow.h"
#include "tools_common.h"
#include "localstore.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <cstring>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QVBoxLayout>

namespace tb {

// 条目编辑对话框：返回 true 表示用户确认修改（新条目 id 为 0）
static bool vaultEntryDialog(QWidget* parent, const QString& title,
                             QString& name, QString& user, QString& pass,
                             QString& url, QString& note) {
    QDialog dlg(parent);
    dlg.setWindowTitle(title);
    dlg.setFixedWidth(380);
    auto* form = new QFormLayout(&dlg);
    auto* eName = new QLineEdit(name);
    auto* eUser = new QLineEdit(user);
    auto* ePass = new QLineEdit(pass);
    ePass->setEchoMode(QLineEdit::Password);
    auto* genRow = new QHBoxLayout;
    genRow->addStretch();
    auto* genBtn = new QPushButton("生成随机密码");
    genBtn->setObjectName("ghost");
    genRow->addWidget(genBtn);
    genRow->addStretch();
    genRow->addWidget(new QLabel("")); // 占位对齐
    QObject::connect(genBtn, &QPushButton::clicked, ePass, [ePass] {
        static const QString pool = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnpqrstuvwxyz23456789!@#$%^&*-_=+?";
        QString pw;
        for (int i = 0; i < 16; ++i) pw += pool[QRandomGenerator::global()->bounded(pool.size())];
        ePass->setText(pw);
        ePass->setEchoMode(QLineEdit::Normal); // 生成后明文展示一次
    });
    form->addRow(QString(), genRow);
    auto* eUrl = new QLineEdit(url);
    auto* eNote = new QLineEdit(note);
    form->addRow("名称*", eName);
    form->addRow("账号", eUser);
    form->addRow("密码", ePass);
    form->addRow("网址", eUrl);
    form->addRow("备注", eNote);
    auto* btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(btns);
    QObject::connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return false;
    if (eName->text().trimmed().isEmpty()) return false;
    name = eName->text().trimmed();
    user = eUser->text();
    pass = ePass->text();
    url = eUrl->text().trimmed();
    note = eNote->text().trimmed();
    return true;
}

QWidget* MainWindow::buildVaultPage() {
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(24, 20, 24, 20);
    v->setSpacing(12);

    auto* title = new QLabel("密码库");
    title->setObjectName("heroTitle");
    v->addWidget(title);
    auto* sub = new QLabel("账号密码统一保管 · 主口令 AES-256 加密整库 · 口令不落盘，忘记无法找回");
    sub->setObjectName("dim");
    v->addWidget(sub);

    // ---- 解锁 / 首次创建 ----
    auto* lockCard = new QFrame;
    lockCard->setObjectName("card");
    auto* lv = new QVBoxLayout(lockCard);
    lv->setContentsMargins(18, 14, 18, 14);
    lv->setSpacing(7);
    auto* lt = new QLabel("解锁密码库");
    lt->setObjectName("author");
    lv->addWidget(lt);
    m_vaultExists = ls::loadJson("vault.json").contains("data_b64");
    auto* rowP = new QHBoxLayout;
    rowP->addWidget(new QLabel("主口令"));
    m_vaultPass = new QLineEdit;
    m_vaultPass->setEchoMode(QLineEdit::Password);
    m_vaultPass->setPlaceholderText(m_vaultExists ? "输入主口令解锁" : "首次使用：设置主口令（8 位以上）");
    rowP->addWidget(m_vaultPass, 1);
    if (!m_vaultExists) {
        rowP->addWidget(new QLabel("再输一次"));
        m_vaultPass2 = new QLineEdit;
        m_vaultPass2->setEchoMode(QLineEdit::Password);
        rowP->addWidget(m_vaultPass2);
    }
    auto* unlockBtn = new QPushButton(m_vaultExists ? "解 锁" : "创建密码库");
    rowP->addWidget(unlockBtn);
    lv->addLayout(rowP);
    m_vaultState = new QLabel(m_vaultExists ? "已锁定" : "尚未创建密码库");
    m_vaultState->setObjectName("dim");
    lv->addWidget(m_vaultState);
    v->addWidget(lockCard);

    // ---- 解锁后的主界面（默认隐藏） ----
    m_vaultBody = new QWidget;
    auto* bv = new QVBoxLayout(m_vaultBody);
    bv->setContentsMargins(0, 0, 0, 0);
    bv->setSpacing(10);

    auto* rowS = new QHBoxLayout;
    m_vaultSearch = new QLineEdit;
    m_vaultSearch->setPlaceholderText("搜索名称 / 账号…");
    rowS->addWidget(m_vaultSearch, 1);
    auto* addBtn = new QPushButton("添加条目");
    rowS->addWidget(addBtn);
    auto* lockBtn = new QPushButton("锁 定");
    lockBtn->setObjectName("ghost");
    rowS->addWidget(lockBtn);
    bv->addLayout(rowS);

    m_vaultList = new QListWidget;
    m_vaultList->setMinimumHeight(300);
    bv->addWidget(m_vaultList, 1);
    auto* hint = new QLabel("点击条目：复制账号 · 双击：复制密码 · 行按钮：显示/编辑/删除");
    hint->setObjectName("dim");
    bv->addWidget(hint);
    v->addWidget(m_vaultBody, 1);
    m_vaultBody->setVisible(false);

    auto saveVault = [this] {
        QJsonObject lib;
        lib.insert("items", m_vaultItems);
        QString err;
        const QString enc = aesProcess("enc", m_vaultKey, QString::fromUtf8(QJsonDocument(lib).toJson()), &err);
        if (err.isEmpty()) {
            QJsonObject root;
            root.insert("data_b64", enc);
            root.insert("saved", QDateTime::currentDateTime().toString("MM-dd hh:mm"));
            ls::saveJson("vault.json", root);
            return true;
        }
        QMessageBox::warning(this, "密码库", "加密保存失败：" + err);
        return false;
    };
    auto renderVault = [this] {
        m_vaultList->clear();
        const QString q = m_vaultSearch->text().trimmed();
        for (const QJsonValue& val : m_vaultItems) {
            const QJsonObject o = val.toObject();
            if (!q.isEmpty() && !o.value("name").toString().contains(q)
                && !o.value("user").toString().contains(q)) continue;
            auto* it = new QListWidgetItem(QString("%1    账号：%2    %3")
                .arg(o.value("name").toString(), o.value("user").toString().isEmpty() ? "—" : o.value("user").toString(),
                     o.value("url").toString()));
            it->setData(Qt::UserRole, (qint64)o.value("id").toDouble());
            m_vaultList->addItem(it);
        }
        if (m_vaultList->count() == 0) m_vaultList->addItem(q.isEmpty() ? "还没有条目，点「添加条目」开始" : "无匹配条目");
    };
    auto findEntry = [this](qint64 id) -> QJsonObject {
        for (const QJsonValue& val : m_vaultItems) {
            QJsonObject o = val.toObject();
            if ((qint64)o.value("id").toDouble() == id) return o;
        }
        return QJsonObject();
    };

    // 解锁 / 创建
    connect(unlockBtn, &QPushButton::clicked, this, [this, saveVault, renderVault] {
        const QString pw = m_vaultPass->text();
        if (pw.length() < 8) { m_vaultState->setText("主口令至少 8 位"); return; }
        if (!m_vaultExists) {
            if (m_vaultPass2->text() != pw) { m_vaultState->setText("两次口令不一致"); return; }
            m_vaultKey = pw;
            m_vaultItems = QJsonArray();
            if (!saveVault()) return;
            m_vaultExists = true;
            m_vaultState->setText("密码库已创建");
        } else {
            QString err;
            const QString data = aesProcess("dec", pw, ls::loadJson("vault.json").value("data_b64").toString(), &err);
            if (!err.isEmpty()) { m_vaultState->setText("解锁失败：口令错误或库损坏"); return; }
            const QJsonDocument doc = QJsonDocument::fromJson(data.toUtf8());
            if (!doc.isObject()) { m_vaultState->setText("解锁失败：数据异常"); return; }
            m_vaultKey = pw;
            m_vaultItems = doc.object().value("items").toArray();
            m_vaultState->setText("已解锁 · " + QString::number(m_vaultItems.size()) + " 个条目");
        }
        m_vaultPass->clear();
        if (m_vaultPass2) m_vaultPass2->clear();
        m_vaultBody->setVisible(true);
        m_vaultUnlocked = true;
        if (!m_vaultAutoLock) {
            m_vaultAutoLock = new QTimer(this);
            m_vaultAutoLock->setSingleShot(true);
            m_vaultAutoLock->setInterval(10 * 60 * 1000); // 10 分钟无操作自动锁定
            connect(m_vaultAutoLock, &QTimer::timeout, this, [this, renderVault] {
                m_vaultItems = QJsonArray();
                m_vaultKey.clear();
                m_vaultUnlocked = false;
                m_vaultBody->setVisible(false);
                m_vaultState->setText("已自动锁定（10 分钟无操作）");
            });
        }
        m_vaultAutoLock->start();
        renderVault();
    });
    connect(m_vaultPass, &QLineEdit::returnPressed, unlockBtn, &QPushButton::click);

    // 搜索
    connect(m_vaultSearch, &QLineEdit::textChanged, this, [renderVault] { renderVault(); });

    // 添加
    connect(addBtn, &QPushButton::clicked, this, [this, saveVault, renderVault] {
        QString name, user, pass, url, note;
        if (!vaultEntryDialog(this, "添加条目", name, user, pass, url, note)) return;
        QJsonObject o;
        o.insert("id", (double)QDateTime::currentMSecsSinceEpoch());
        o.insert("name", name);
        o.insert("user", user);
        o.insert("pass", pass);
        o.insert("url", url);
        o.insert("note", note);
        m_vaultItems.prepend(o);
        saveVault();
        renderVault();
    });

    // 锁定
    connect(lockBtn, &QPushButton::clicked, this, [this, renderVault] {
        m_vaultItems = QJsonArray();
        m_vaultKey.clear();
        m_vaultUnlocked = false;
        m_vaultBody->setVisible(false);
        if (m_vaultAutoLock) m_vaultAutoLock->stop();
        m_vaultState->setText("已锁定");
    });

    // 条目操作：单击复制账号 / 双击复制密码 / 右键更多
    connect(m_vaultList, &QListWidget::itemClicked, this, [this, findEntry](QListWidgetItem* it) {
        const QJsonObject o = findEntry(it->data(Qt::UserRole).toLongLong());
        if (o.isEmpty()) return;
        QApplication::clipboard()->setText(o.value("user").toString());
        m_vaultState->setText(QString("已复制「%1」的账号").arg(o.value("name").toString()));
    });
    connect(m_vaultList, &QListWidget::itemDoubleClicked, this, [this, findEntry](QListWidgetItem* it) {
        const QJsonObject o = findEntry(it->data(Qt::UserRole).toLongLong());
        if (o.isEmpty()) return;
        QApplication::clipboard()->setText(o.value("pass").toString());
        m_vaultState->setText(QString("已复制「%1」的密码（小心粘贴板泄露）").arg(o.value("name").toString()));
    });
    m_vaultList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_vaultList, &QListWidget::customContextMenuRequested, this, [this, saveVault, renderVault, findEntry](const QPoint&) {
        auto* it = m_vaultList->currentItem();
        if (!it) return;
        const qint64 id = it->data(Qt::UserRole).toLongLong();
        QMenu menu(this);
        QAction* showA = menu.addAction("显示密码");
        QAction* urlA = menu.addAction("打开网址");
        QAction* editA = menu.addAction("编辑");
        QAction* delA = menu.addAction("删除");
        QAction* chosen = menu.exec(QCursor::pos());
        if (chosen == showA) {
            const QJsonObject o = findEntry(id);
            if (!o.isEmpty()) QMessageBox::information(this, o.value("name").toString(),
                "账号：" + o.value("user").toString() + "\n密码：" + o.value("pass").toString());
        } else if (chosen == urlA) {
            const QJsonObject o = findEntry(id);
            const QString u = o.value("url").toString();
            if (!u.isEmpty()) QDesktopServices::openUrl(QUrl(u.startsWith("http") ? u : "https://" + u));
        } else if (chosen == editA) {
            for (int i = 0; i < m_vaultItems.size(); ++i) {
                QJsonObject o = m_vaultItems[i].toObject();
                if ((qint64)o.value("id").toDouble() != id) continue;
                QString name = o.value("name").toString(), user = o.value("user").toString(),
                        pass = o.value("pass").toString(), url = o.value("url").toString(), note = o.value("note").toString();
                if (!vaultEntryDialog(this, "编辑条目", name, user, pass, url, note)) return;
                o.insert("name", name); o.insert("user", user); o.insert("pass", pass);
                o.insert("url", url); o.insert("note", note);
                m_vaultItems[i] = o;
                break;
            }
            saveVault();
            renderVault();
        } else if (chosen == delA) {
            if (QMessageBox::question(this, "删除条目", "确定删除这条？") != QMessageBox::Yes) return;
            QJsonArray keep;
            for (const QJsonValue& val : m_vaultItems)
                if ((qint64)val.toObject().value("id").toDouble() != id) keep.append(val);
            m_vaultItems = keep;
            saveVault();
            renderVault();
        }
    });

    return page;
}

} // namespace tb
