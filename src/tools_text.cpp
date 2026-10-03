// 文本与开发：脱敏 / 加解密 / 开发工具箱 / 文本对比
// 由 mainwindow.cpp 模块化拆分（2026-10-03）：本文件实现 MainWindow 对应成员。
#include "mainwindow.h"
#include "tools_common.h"
#include "localstore.h"

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
#include <QTreeWidget>
#include <QCheckBox>
#include <QUuid>
#include <QVBoxLayout>
#include <algorithm>

namespace tb {

static QString maskAround(const QString& in, const QRegularExpression& re,
                          const std::function<QString(const QRegularExpressionMatch&)>& fn) {
    QString out;
    qsizetype last = 0;
    auto it = re.globalMatch(in);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out += in.mid(last, m.capturedStart() - last);
        out += fn(m);
        last = m.capturedEnd();
    }
    out += in.mid(last);
    return out;
}

QWidget* MainWindow::buildMaskTab() {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* body = new QWidget;
    auto* v = new QVBoxLayout(body);
    v->setContentsMargins(0, 4, 12, 0);
    v->setSpacing(12);

    QVBoxLayout* lay;
    auto* card = toolCard("文本脱敏（本地正则处理，不上传）", lay);
    auto* tip = new QLabel("自动识别并打码：身份证（前4后2）、银行卡（前4后4）、手机号（前3后4）、邮箱（首字***）、IPv4（首段）。注意先脱敏再分享截图/日志。");
    tip->setObjectName("dim");
    tip->setWordWrap(true);
    lay->addWidget(tip);

    auto* rowCk = new QHBoxLayout;
    auto* ckId = new QCheckBox("身份证");
    auto* ckBank = new QCheckBox("银行卡");
    auto* ckPhone = new QCheckBox("手机号");
    auto* ckMail = new QCheckBox("邮箱");
    auto* ckIp = new QCheckBox("IPv4");
    for (QCheckBox* c : {ckId, ckBank, ckPhone, ckMail, ckIp}) { c->setChecked(true); rowCk->addWidget(c); }
    rowCk->addStretch();
    lay->addLayout(rowCk);

    m_maskIn = new QPlainTextEdit;
    m_maskIn->setPlaceholderText("粘贴含敏感信息的文本…（日志、配置、聊天记录等）");
    m_maskIn->setFixedHeight(130);
    lay->addWidget(m_maskIn);

    auto* row = new QHBoxLayout;
    auto* go = new QPushButton("一键脱敏");
    connect(go, &QPushButton::clicked, this, [this, ckId, ckBank, ckPhone, ckMail, ckIp] {
        QString s = m_maskIn->toPlainText();
        if (ckId->isChecked())
            s = maskAround(s, QRegularExpression("(?<!\\d)\\d{17}[\\dXx](?!\\d)"),
                           [](const QRegularExpressionMatch& m) { return m.captured(0).left(4) + "**********" + m.captured(0).right(2); });
        if (ckBank->isChecked())
            s = maskAround(s, QRegularExpression("(?<!\\d)\\d{13,19}(?!\\d)"),
                           [](const QRegularExpressionMatch& m) { return m.captured(0).left(4) + QString(m.captured(0).size() - 8, '*') + m.captured(0).right(4); });
        if (ckPhone->isChecked())
            s = maskAround(s, QRegularExpression("(?<!\\d)1[3-9]\\d{9}(?!\\d)"),
                           [](const QRegularExpressionMatch& m) { return m.captured(0).left(3) + "****" + m.captured(0).right(4); });
        if (ckMail->isChecked())
            s = maskAround(s, QRegularExpression("[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\\.[A-Za-z]{2,}"),
                           [](const QRegularExpressionMatch& m) { return m.captured(0).left(1) + "***" + m.captured(0).mid(m.captured(0).indexOf('@')); });
        if (ckIp->isChecked())
            s = maskAround(s, QRegularExpression("\\b\\d{1,3}(\\.\\d{1,3}){3}\\b"),
                           [](const QRegularExpressionMatch& m) { return m.captured(0).section('.', 0, 0) + ".*.*.*"; });
        m_maskOut->setPlainText(s);
    });
    row->addWidget(go);
    row->addStretch();
    lay->addLayout(row);

    m_maskOut = new QPlainTextEdit;
    m_maskOut->setReadOnly(true);
    m_maskOut->setFixedHeight(130);
    m_maskOut->setPlaceholderText("脱敏结果…");
    lay->addWidget(m_maskOut);

    auto* row2 = new QHBoxLayout;
    auto* copy = new QPushButton("复制结果");
    copy->setObjectName("ghost");
    connect(copy, &QPushButton::clicked, this, [this] {
        const QString t = m_maskOut->toPlainText();
        if (!t.isEmpty()) QApplication::clipboard()->setText(t);
    });
    row2->addWidget(copy);
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

// AES-256-CBC（PBKDF2 口令派生）：密文 = Base64(盐16B + IV16B + 密文)。
// 通过环境变量传参避免命令行泄露；PS 脚本内嵌（Win10+ 自带 .NET，无第三方依赖）。
QString MainWindow::aesProcess(const QString& mode, const QString& pass, const QString& data, QString* err) {
    static const char* kScript =
        "$ErrorActionPreference='Stop';"
        "[Console]::OutputEncoding=[Text.Encoding]::UTF8;"
        "try{"
        "$pass=$env:TBPASS;"
        "if($env:TBMODE -eq 'enc'){"
        "$raw=[Text.Encoding]::UTF8.GetBytes([Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($env:TBDATA)));"
        "$salt=New-Object byte[] 16;$iv=New-Object byte[] 16;"
        "$rng=[Security.Cryptography.RandomNumberGenerator]::Create();"
        "$rng.GetBytes($salt);$rng.GetBytes($iv);"
        "$kdf=New-Object Security.Cryptography.Rfc2898DeriveBytes($pass,$salt,100000);"
        "$aes=[Security.Cryptography.Aes]::Create();$aes.Key=$kdf.GetBytes(32);$aes.IV=$iv;"
        "$enc=$aes.CreateEncryptor();"
        "$ct=$enc.TransformFinalBlock($raw,0,$raw.Length);"
        "$all=New-Object byte[] (32+$ct.Length);"
        "[Array]::Copy($salt,0,$all,0,16);[Array]::Copy($iv,0,$all,16,16);[Array]::Copy($ct,0,$all,32,$ct.Length);"
        "[Convert]::ToBase64String($all)"
        "}else{"
        "$all=[Convert]::FromBase64String($env:TBDATA);"
        "if($all.Length -lt 48){throw 'data too short'}"
        "$salt=[byte[]]$all[0..15];$iv=[byte[]]$all[16..31];"
        "$ct=New-Object byte[] ($all.Length-32);[Array]::Copy($all,32,$ct,0,$ct.Length);"
        "$kdf=New-Object Security.Cryptography.Rfc2898DeriveBytes($pass,$salt,100000);"
        "$aes=[Security.Cryptography.Aes]::Create();$aes.Key=$kdf.GetBytes(32);$aes.IV=$iv;"
        "$dec=$aes.CreateDecryptor();"
        "$pt=$dec.TransformFinalBlock($ct,0,$ct.Length);"
        "[Text.Encoding]::UTF8.GetString($pt)"
        "}}"
        "catch{Write-Output (\"__ERR__\" + $_.Exception.Message)}";

    QProcess p;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("TBMODE", mode);
    env.insert("TBPASS", pass);
    env.insert("TBDATA", QString::fromLatin1(data.toUtf8().toBase64()));
    p.setProcessEnvironment(env);
    p.start("powershell", {"-NoProfile", "-Command", QString::fromUtf8(kScript)});
    if (!p.waitForStarted(5000)) { if (err) *err = "PowerShell 启动失败"; return QString(); }
    if (!p.waitForFinished(20000)) { p.kill(); if (err) *err = "处理超时"; return QString(); }
    const QString out = QString::fromUtf8(p.readAllStandardOutput()).trimmed();
    if (out.startsWith("__ERR__")) { if (err) *err = out.mid(7); return QString(); }
    if (out.isEmpty()) { if (err) *err = "无输出（口令错误或密文损坏）"; return QString(); }
    return out;
}

QWidget* MainWindow::buildCryptoTab() {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* body = new QWidget;
    auto* v = new QVBoxLayout(body);
    v->setContentsMargins(0, 4, 12, 0);
    v->setSpacing(12);

    {
        QVBoxLayout* lay;
        auto* card = toolCard("AES-256 加密 / 解密（口令派生 · 同口令跨机器可解）", lay);
        auto* tip = new QLabel("输入口令 + 文本：加密得到 Base64 密文（盐+IV+密文，每次随机），解密粘贴同口令的密文。适合保护笔记、配置里的敏感内容。");
        tip->setObjectName("dim");
        tip->setWordWrap(true);
        lay->addWidget(tip);

        auto* rowP = new QHBoxLayout;
        rowP->addWidget(new QLabel("口令"));
        m_cryptoPass = new QLineEdit;
        m_cryptoPass->setEchoMode(QLineEdit::Password);
        rowP->addWidget(m_cryptoPass, 1);
        auto* show = new QCheckBox("显示");
        connect(show, &QCheckBox::toggled, this, [this](bool on) {
            m_cryptoPass->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password);
        });
        rowP->addWidget(show);
        lay->addLayout(rowP);

        m_cryptoText = new QPlainTextEdit;
        m_cryptoText->setPlaceholderText("明文或 Base64 密文（加密后替换为密文，解密后替换为明文）");
        m_cryptoText->setFixedHeight(150);
        lay->addWidget(m_cryptoText);

        auto* row = new QHBoxLayout;
        auto* enc = new QPushButton("加密 → 密文");
        connect(enc, &QPushButton::clicked, this, [this] {
            const QString plain = m_cryptoText->toPlainText();
            const QString pass = m_cryptoPass->text();
            if (plain.isEmpty() || pass.isEmpty()) { m_cryptoState->setText("口令和文本都不能为空"); return; }
            m_cryptoState->setText("处理中…");
            QApplication::processEvents();
            QString err;
            const QString out = aesProcess("enc", pass, plain, &err);
            if (err.isEmpty()) { m_cryptoText->setPlainText(out); m_cryptoState->setText("已加密（Base64 密文已放入上方框，可复制）"); }
            else m_cryptoState->setText("加密失败：" + err);
        });
        auto* dec = new QPushButton("解密 → 明文");
        connect(dec, &QPushButton::clicked, this, [this] {
            const QString data = m_cryptoText->toPlainText().trimmed();
            const QString pass = m_cryptoPass->text();
            if (data.isEmpty() || pass.isEmpty()) { m_cryptoState->setText("口令和密文都不能为空"); return; }
            m_cryptoState->setText("处理中…");
            QApplication::processEvents();
            QString err;
            const QString out = aesProcess("dec", pass, data, &err);
            if (err.isEmpty()) { m_cryptoText->setPlainText(out); m_cryptoState->setText("已解密"); }
            else m_cryptoState->setText("解密失败：" + err + "（口令不对或密文损坏）");
        });
        row->addWidget(enc);
        row->addWidget(dec);
        row->addStretch();
        lay->addLayout(row);

        m_cryptoState = new QLabel("");
        m_cryptoState->setObjectName("dim");
        lay->addWidget(m_cryptoState);
        v->addWidget(card);
    }

    QVBoxLayout* lay;
    auto* card = toolCard("哈希摘要（本地计算）", lay);
    auto* in = new QPlainTextEdit;
    in->setPlaceholderText("输入要计算摘要的文本…");
    in->setFixedHeight(80);
    lay->addWidget(in);
    auto* row = new QHBoxLayout;
    auto* out = new QLineEdit;
    out->setReadOnly(true);
    for (auto algo : {QCryptographicHash::Sha256, QCryptographicHash::Sha1, QCryptographicHash::Md5}) {
        const QString name = algo == QCryptographicHash::Sha256 ? "SHA-256" : algo == QCryptographicHash::Sha1 ? "SHA-1" : "MD5";
        auto* b = new QPushButton(name);
        connect(b, &QPushButton::clicked, this, [in, out, algo] {
            out->setText(QString::fromLatin1(QCryptographicHash::hash(in->toPlainText().toUtf8(), algo).toHex()));
        });
        row->addWidget(b);
    }
    auto* cp = new QPushButton("复制");
    cp->setObjectName("ghost");
    connect(cp, &QPushButton::clicked, out, [out] { QApplication::clipboard()->setText(out->text()); });
    row->addWidget(cp);
    row->addStretch();
    lay->addLayout(row);
    lay->addWidget(out);
    v->addWidget(card);

    {
        QVBoxLayout* lf;
        auto* fCard = toolCard("文件校验（大文件分块读取，比对下载完整性用）", lf);
        auto* rowF = new QHBoxLayout;
        auto* fpath = new QLineEdit;
        fpath->setPlaceholderText("文件路径");
        rowF->addWidget(fpath, 1);
        auto* browse = new QPushButton("选择文件…");
        browse->setObjectName("ghost");
        rowF->addWidget(browse);
        auto* calc = new QPushButton("计算");
        rowF->addWidget(calc);
        lf->addLayout(rowF);
        auto* result = new QPlainTextEdit;
        result->setReadOnly(true);
        result->setFixedHeight(74);
        result->setPlaceholderText("MD5 / SHA-1 / SHA-256 结果…");
        lf->addWidget(result);
        auto* rowR = new QHBoxLayout;
        auto* copyB = new QPushButton("复制全部");
        copyB->setObjectName("ghost");
        rowR->addWidget(copyB);
        rowR->addStretch();
        lf->addLayout(rowR);
        v->addWidget(fCard);

        connect(browse, &QPushButton::clicked, this, [fpath] {
            const QString f = QFileDialog::getOpenFileName(nullptr, "选择文件");
            if (!f.isEmpty()) fpath->setText(f);
        });
        connect(calc, &QPushButton::clicked, this, [this, fpath, result] {
            QFile f(fpath->text().trimmed());
            if (!f.open(QIODevice::ReadOnly)) { result->setPlainText("无法读取文件"); return; }
            result->setPlainText("计算中…");
            QApplication::processEvents();
            QCryptographicHash hMd5(QCryptographicHash::Md5);
            QCryptographicHash hSha1(QCryptographicHash::Sha1);
            QCryptographicHash hSha256(QCryptographicHash::Sha256);
            qint64 total = f.size(), done = 0;
            while (!f.atEnd()) {
                const QByteArray chunk = f.read(1 << 20);
                hMd5.addData(chunk); hSha1.addData(chunk); hSha256.addData(chunk);
                done += chunk.size();
            }
            const QString r = QString("MD5     %1\nSHA-1   %2\nSHA-256 %3")
                .arg(QString::fromLatin1(hMd5.result().toHex()),
                     QString::fromLatin1(hSha1.result().toHex()),
                     QString::fromLatin1(hSha256.result().toHex()));
            result->setPlainText(r);
            toast("文件校验", "算好啦 · " + QString::number(total / 1048576.0, 'f', 1) + " MB");
        });
        connect(copyB, &QPushButton::clicked, this, [result] {
            QApplication::clipboard()->setText(result->toPlainText());
        });
    }

    {
        QVBoxLayout* lay3;
        auto* pwCard = toolCard("密码生成器（本地随机）", lay3);
        auto* rowG = new QHBoxLayout;
        rowG->addWidget(new QLabel("长度"));
        auto* len = new QSpinBox;
        len->setRange(8, 64);
        len->setValue(16);
        rowG->addWidget(len);
        auto* ckU = new QCheckBox("大写");
        auto* ckL = new QCheckBox("小写");
        auto* ckD = new QCheckBox("数字");
        auto* ckS = new QCheckBox("符号");
        ckU->setChecked(true); ckL->setChecked(true); ckD->setChecked(true); ckS->setChecked(true);
        rowG->addWidget(ckU); rowG->addWidget(ckL); rowG->addWidget(ckD); rowG->addWidget(ckS);
        auto* gen = new QPushButton("生成");
        rowG->addWidget(gen);
        rowG->addStretch();
        lay3->addLayout(rowG);
        auto* outG = new QLineEdit;
        outG->setReadOnly(true);
        lay3->addWidget(outG);
        auto* rowC = new QHBoxLayout;
        auto* cp = new QPushButton("复制");
        cp->setObjectName("ghost");
        connect(cp, &QPushButton::clicked, outG, [outG] { QApplication::clipboard()->setText(outG->text()); });
        rowC->addWidget(cp);
        rowC->addStretch();
        lay3->addLayout(rowC);
        connect(gen, &QPushButton::clicked, this, [len, ckU, ckL, ckD, ckS, outG] {
            QString pool;
            if (ckU->isChecked()) pool += "ABCDEFGHJKLMNPQRSTUVWXYZ";
            if (ckL->isChecked()) pool += "abcdefghijkmnpqrstuvwxyz";
            if (ckD->isChecked()) pool += "23456789";
            if (ckS->isChecked()) pool += "!@#$%^&*-_=+?";
            if (pool.isEmpty()) { outG->setText("至少勾选一类字符"); return; }
            QString pw;
            for (int i = 0; i < len->value(); ++i)
                pw += pool[QRandomGenerator::global()->bounded(pool.size())];
            outG->setText(pw);
        });
        v->addWidget(pwCard);
    }
    v->addStretch();

    scroll->setWidget(body);
    auto* page = new QWidget;
    auto* wrap = new QVBoxLayout(page);
    wrap->setContentsMargins(0, 0, 0, 0);
    wrap->addWidget(scroll);
    return page;
}

QWidget* MainWindow::buildDevTab() {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* body = new QWidget;
    auto* grid = new QGridLayout(body);
    grid->setContentsMargins(0, 4, 12, 0);
    grid->setSpacing(12);
    for (int i = 0; i < 2; ++i) grid->setColumnStretch(i, 1);

    // JSON 格式化
    {
        QVBoxLayout* lay;
        auto* card = toolCard("JSON 格式化 / 压缩", lay);
        auto* in = new QPlainTextEdit;
        in->setPlaceholderText("粘贴 JSON…");
        in->setFixedHeight(84);
        lay->addWidget(in);
        auto* row = new QHBoxLayout;
        auto* out = new QLabel("");
        out->setObjectName("dim");
        auto* fmt = new QPushButton("格式化");
        connect(fmt, &QPushButton::clicked, this, [in, out] {
            QJsonParseError e;
            const QJsonDocument d = QJsonDocument::fromJson(in->toPlainText().toUtf8(), &e);
            if (d.isNull()) { out->setText("解析失败：" + e.errorString() + QString(" (偏移 %1)").arg(e.offset)); return; }
            in->setPlainText(QString::fromUtf8(d.toJson(QJsonDocument::Indented)));
            out->setText("已格式化");
        });
        auto* min = new QPushButton("压缩");
        connect(min, &QPushButton::clicked, this, [in, out] {
            QJsonParseError e;
            const QJsonDocument d = QJsonDocument::fromJson(in->toPlainText().toUtf8(), &e);
            if (d.isNull()) { out->setText("解析失败：" + e.errorString() + QString(" (偏移 %1)").arg(e.offset)); return; }
            in->setPlainText(QString::fromUtf8(d.toJson(QJsonDocument::Compact)));
            out->setText("已压缩");
        });
        auto* cp = new QPushButton("复制");
        cp->setObjectName("ghost");
        connect(cp, &QPushButton::clicked, in, [in] { QApplication::clipboard()->setText(in->toPlainText()); });
        row->addWidget(fmt); row->addWidget(min); row->addWidget(cp); row->addWidget(out, 1);
        lay->addLayout(row);
        grid->addWidget(card, 0, 0);
    }
    // 时间戳
    {
        QVBoxLayout* lay;
        auto* card = toolCard("时间戳 ↔ 日期", lay);
        auto* row = new QHBoxLayout;
        auto* in = new QLineEdit;
        in->setPlaceholderText("秒或毫秒时间戳");
        row->addWidget(in, 1);
        auto* now = new QPushButton("当前");
        connect(now, &QPushButton::clicked, in, [in] { in->setText(QString::number(QDateTime::currentMSecsSinceEpoch())); });
        row->addWidget(now);
        lay->addLayout(row);
        auto* row2 = new QHBoxLayout;
        auto* out = new QLabel("");
        out->setObjectName("dim");
        auto* b1 = new QPushButton("→ 日期");
        connect(b1, &QPushButton::clicked, this, [in, out] {
            bool ok; const qint64 v = in->text().toLongLong(&ok);
            if (!ok || v <= 0) { out->setText("请输入合法时间戳"); return; }
            const qint64 ms = v > 9999999999LL ? v : v * 1000;
            out->setText(QDateTime::fromMSecsSinceEpoch(ms).toString("yyyy-MM-dd hh:mm:ss (ddd)"));
        });
        auto* b2 = new QPushButton("← 日期");
        connect(b2, &QPushButton::clicked, this, [in, out] {
            const QDateTime dt = QDateTime::fromString(in->text(), "yyyy-MM-dd hh:mm:ss");
            if (!dt.isValid()) { out->setText("格式：yyyy-MM-dd hh:mm:ss"); return; }
            out->setText(QString("毫秒 %1 / 秒 %2").arg(dt.toMSecsSinceEpoch()).arg(dt.toMSecsSinceEpoch() / 1000));
        });
        row2->addWidget(b1); row2->addWidget(b2); row2->addWidget(out, 1);
        lay->addLayout(row2);
        grid->addWidget(card, 0, 1);
    }
    // Base64 / URL 编解码
    {
        QVBoxLayout* lay;
        auto* card = toolCard("Base64 / URL 编解码", lay);
        auto* in = new QPlainTextEdit;
        in->setPlaceholderText("输入文本或 Base64/URL 编码串…");
        in->setFixedHeight(70);
        lay->addWidget(in);
        auto* row = new QHBoxLayout;
        auto mk = [in](bool enc, bool b64) -> QPushButton* {
            auto* b = new QPushButton(enc ? (b64 ? "Base64编码" : "URL编码") : (b64 ? "Base64解码" : "URL解码"));
            QObject::connect(b, &QPushButton::clicked, in, [in, enc, b64] {
                const QString t = in->toPlainText();
                if (enc) {
                    const QByteArray raw = t.toUtf8();
                    in->setPlainText(QString::fromLatin1(b64 ? raw.toBase64() : QUrl::toPercentEncoding(t)));
                } else {
                    const QString dec = b64 ? QString::fromUtf8(QByteArray::fromBase64(t.toLatin1()))
                                            : QUrl::fromPercentEncoding(t.toUtf8());
                    in->setPlainText(dec);
                }
            });
            return b;
        };
        row->addWidget(mk(true, true)); row->addWidget(mk(false, true));
        row->addWidget(mk(true, false)); row->addWidget(mk(false, false));
        row->addStretch();
        lay->addLayout(row);
        grid->addWidget(card, 1, 0);
    }
    // 进制转换
    {
        QVBoxLayout* lay;
        auto* card = toolCard("进制转换", lay);
        auto* row = new QHBoxLayout;
        auto* in = new QLineEdit;
        in->setPlaceholderText("十进制");
        row->addWidget(in, 1);
        auto* out = new QLabel("");
        out->setObjectName("dim");
        out->setWordWrap(true);
        auto* go = new QPushButton("转换");
        connect(go, &QPushButton::clicked, this, [in, out] {
            bool ok; const qlonglong v = in->text().toLongLong(&ok);
            if (!ok) { out->setText("请输入合法整数"); return; }
            out->setText(QString("HEX %1  ·  OCT %2  ·  BIN %3")
                             .arg(QString::number(v, 16).toUpper(), QString::number(v, 8), QString::number(v, 2)));
        });
        row->addWidget(go);
        row->addWidget(out, 1);
        lay->addLayout(row);
        auto* row2 = new QHBoxLayout;
        auto* hexIn = new QLineEdit;
        hexIn->setPlaceholderText("十六进制 → 回车转十进制");
        connect(hexIn, &QLineEdit::returnPressed, this, [hexIn] {
            bool ok; const qlonglong v = hexIn->text().toLongLong(&ok, 16);
            hexIn->setText(ok ? QString::number(v) : hexIn->text());
        });
        row2->addWidget(hexIn, 1);
        lay->addLayout(row2);
        grid->addWidget(card, 1, 1);
    }
    // UUID
    {
        QVBoxLayout* lay;
        auto* card = toolCard("UUID 生成", lay);
        auto* row = new QHBoxLayout;
        auto* out = new QLineEdit;
        out->setReadOnly(true);
        row->addWidget(out, 1);
        auto* go = new QPushButton("生成");
        connect(go, &QPushButton::clicked, out, [out] { out->setText(QUuid::createUuid().toString(QUuid::WithoutBraces)); });
        auto* cp = new QPushButton("复制");
        cp->setObjectName("ghost");
        connect(cp, &QPushButton::clicked, out, [out] { QApplication::clipboard()->setText(out->text()); });
        row->addWidget(go);
        row->addWidget(cp);
        lay->addLayout(row);
        grid->addWidget(card, 2, 0);
    }
    // 正则测试
    {
        QVBoxLayout* lay;
        auto* card = toolCard("正则测试", lay);
        auto* row = new QHBoxLayout;
        auto* pat = new QLineEdit;
        pat->setPlaceholderText("正则表达式");
        row->addWidget(pat, 1);
        auto* ck = new QCheckBox("忽略大小写");
        row->addWidget(ck);
        lay->addLayout(row);
        auto* text = new QPlainTextEdit;
        text->setPlaceholderText("被匹配的文本…");
        text->setFixedHeight(70);
        lay->addWidget(text);
        auto* out = new QLabel("");
        out->setObjectName("dim");
        out->setWordWrap(true);
        auto* go = new QPushButton("匹配");
        connect(go, &QPushButton::clicked, this, [pat, text, ck, out] {
            QRegularExpression::PatternOptions op = QRegularExpression::NoPatternOption;
            if (ck->isChecked()) op |= QRegularExpression::CaseInsensitiveOption;
            QRegularExpression re(pat->text(), op);
            if (!re.isValid()) { out->setText("正则错误：" + re.errorString()); return; }
            auto it = re.globalMatch(text->toPlainText());
            QStringList hits;
            int n = 0;
            while (it.hasNext() && n < 20) {
                const QRegularExpressionMatch m = it.next();
                hits << QString("[%1..%2] %3").arg(m.capturedStart()).arg(m.capturedEnd()).arg(m.captured(0));
                ++n;
            }
            out->setText(it.hasNext() ? "结果过多，仅显示前 20 条" : QString());
            out->setText((hits.isEmpty() ? "无匹配" : QString("共 %1+ 条：\n").arg(n) + hits.join("\n")));
        });
        auto* row2 = new QHBoxLayout;
        row2->addWidget(go);
        row2->addStretch();
        lay->addLayout(row2);
        lay->addWidget(out);
        grid->addWidget(card, 2, 1);
    }
    // 文本统计
    {
        QVBoxLayout* layS;
        auto* cardS = toolCard("文本统计", layS);
        auto* inS = new QPlainTextEdit;
        inS->setPlaceholderText("粘贴文本，实时统计…");
        inS->setFixedHeight(80);
        layS->addWidget(inS);
        auto* outS = new QLabel("");
        outS->setObjectName("dim");
        outS->setWordWrap(true);
        layS->addWidget(outS);
        connect(inS, &QPlainTextEdit::textChanged, this, [inS, outS] {
            const QString t = inS->toPlainText();
            int han = 0;
            for (const QChar& ch : t) if (ch.unicode() >= 0x4E00 && ch.unicode() <= 0x9FFF) ++han;
            outS->setText(QString("字符 %1 · 汉字 %2 · 行数 %3 · UTF-8 字节 %4")
                .arg(t.length()).arg(han).arg(t.isEmpty() ? 0 : t.count('\n') + 1)
                .arg(QString::number(t.toUtf8().size())));
        });
        grid->addWidget(cardS, 3, 0, 1, 2);
    }

    scroll->setWidget(body);
    auto* page = new QWidget;
    auto* wrap = new QVBoxLayout(page);
    wrap->setContentsMargins(0, 0, 0, 0);
    wrap->addWidget(scroll);
    return page;
}

// ==================== 工具箱扩展：番茄钟 / 取色 / 单位换算 / 快捷启动 / 文本对比 ====================

QWidget* MainWindow::buildDiffTab() {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* body = new QWidget;
    auto* v = new QVBoxLayout(body);
    v->setContentsMargins(0, 4, 12, 0);
    v->setSpacing(12);

    QVBoxLayout* lay;
    auto* card = toolCard("文本对比（行级差异 · 本地计算）", lay);
    auto* tip = new QLabel("左侧=原文（删除标红），右侧=新文本（新增标绿）。对比限制 400 行以内。");
    tip->setObjectName("dim");
    tip->setWordWrap(true);
    lay->addWidget(tip);

    auto* cols = new QHBoxLayout;
    m_diffA = new QPlainTextEdit;
    m_diffA->setPlaceholderText("原文…");
    m_diffB = new QPlainTextEdit;
    m_diffB->setPlaceholderText("新文本…");
    cols->addWidget(m_diffA, 1);
    cols->addWidget(m_diffB, 1);
    lay->addLayout(cols);

    auto* row = new QHBoxLayout;
    auto* go = new QPushButton("对 比");
    row->addWidget(go);
    row->addStretch();
    lay->addLayout(row);

    m_diffOut = new QTextBrowser;
    m_diffOut->setMinimumHeight(220);
    lay->addWidget(m_diffOut, 1);
    v->addWidget(card);

    connect(go, &QPushButton::clicked, this, [this] {
        const QStringList A = m_diffA->toPlainText().split('\n');
        const QStringList B = m_diffB->toPlainText().split('\n');
        if (A.size() > 400 || B.size() > 400) { m_diffOut->setPlainText("文本超过 400 行，请精简后再对比"); return; }
        const int n = A.size(), m = B.size();
        // LCS DP
        QVector<int> dp((n + 1) * (m + 1), 0);
        for (int i = n - 1; i >= 0; --i)
            for (int j = m - 1; j >= 0; --j)
                dp[i * (m + 1) + j] = A[i] == B[j] ? dp[(i + 1) * (m + 1) + j + 1] + 1
                                                   : qMax(dp[(i + 1) * (m + 1) + j], dp[i * (m + 1) + j + 1]);
        QString html = "<style>td{padding:1px 8px;font-family:Consolas,monospace;}</style>";
        int i = 0, j = 0;
        while (i < n && j < m) {
            if (A[i] == B[j]) {
                html += QString("<tr><td></td><td>%1</td></tr>").arg(A[i].toHtmlEscaped());
                ++i; ++j;
            } else if (dp[(i + 1) * (m + 1) + j] >= dp[i * (m + 1) + j + 1]) {
                html += QString("<tr><td style='color:#c00'>-</td><td style='background:#ffdddd'>%1</td></tr>").arg(A[i].toHtmlEscaped());
                ++i;
            } else {
                html += QString("<tr><td style='color:#080'>+</td><td style='background:#ddffdd'>%1</td></tr>").arg(B[j].toHtmlEscaped());
                ++j;
            }
        }
        for (; i < n; ++i) html += QString("<tr><td style='color:#c00'>-</td><td style='background:#ffdddd'>%1</td></tr>").arg(A[i].toHtmlEscaped());
        for (; j < m; ++j) html += QString("<tr><td style='color:#080'>+</td><td style='background:#ddffdd'>%1</td></tr>").arg(B[j].toHtmlEscaped());
        m_diffOut->setHtml("<table>" + html + "</table>");
    });

    scroll->setWidget(body);
    auto* page = new QWidget;
    auto* wrap = new QVBoxLayout(page);
    wrap->setContentsMargins(0, 0, 0, 0);
    wrap->addWidget(scroll);
    return page;
}

// ---------- 代码片段库：存取本地 snippets.json，一键复制 ----------
QWidget* MainWindow::buildSnippetTab() {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* body = new QWidget;
    auto* v = new QVBoxLayout(body);
    v->setContentsMargins(0, 4, 12, 0);
    v->setSpacing(12);

    QVBoxLayout* lay;
    auto* card = toolCard("代码片段库（自动保存 · 一键复制）", lay);
    auto* row = new QHBoxLayout;
    m_snipName = new QLineEdit;
    m_snipName->setPlaceholderText("片段名称（如：Python 快速排序）");
    row->addWidget(m_snipName, 1);
    auto* save = new QPushButton("保存片段");
    row->addWidget(save);
    lay->addLayout(row);

    m_snipList = new QListWidget;
    m_snipList->setMinimumHeight(140);
    m_snipList->setToolTip("点击载入 · 双击复制");
    lay->addWidget(m_snipList, 1);

    m_snipEdit = new QPlainTextEdit;
    m_snipEdit->setPlaceholderText("片段内容…");
    m_snipEdit->setFixedHeight(170);
    lay->addWidget(m_snipEdit, 1);

    auto* row2 = new QHBoxLayout;
    auto* copy = new QPushButton("复制内容");
    copy->setObjectName("ghost");
    auto* del = new QPushButton("删除选中");
    del->setObjectName("ghost");
    row2->addWidget(copy);
    row2->addWidget(del);
    row2->addStretch();
    lay->addLayout(row2);
    v->addWidget(card);
    v->addStretch();

    auto render = [this] {
        m_snipList->clear();
        const QJsonArray items = ls::loadJson("snippets.json").value("items").toArray();
        for (const QJsonValue& val : items) {
            const QJsonObject o = val.toObject();
            auto* it = new QListWidgetItem(QString("%1  ·  %2 行  ·  %3").arg(
                o.value("name").toString(),
                QString::number(o.value("content").toString().count('\n') + 1),
                o.value("updated").toString()));
            it->setData(Qt::UserRole, (qint64)o.value("id").toDouble());
            m_snipList->addItem(it);
        }
        if (items.isEmpty()) m_snipList->addItem("还没有片段，填名称和内容后点「保存片段」");
    };
    connect(save, &QPushButton::clicked, this, [this, render] {
        const QString name = m_snipName->text().trimmed();
        const QString content = m_snipEdit->toPlainText();
        if (name.isEmpty() || content.isEmpty()) return;
        QJsonObject root = ls::loadJson("snippets.json");
        QJsonArray arr = root.value("items").toArray();
        // 同名覆盖
        for (int i = 0; i < arr.size(); ++i) {
            if (arr[i].toObject().value("name").toString() == name) { arr.removeAt(i); break; }
        }
        QJsonObject o;
        o.insert("id", (double)QDateTime::currentMSecsSinceEpoch());
        o.insert("name", name);
        o.insert("content", content);
        o.insert("updated", QDateTime::currentDateTime().toString("MM-dd hh:mm"));
        arr.prepend(o);
        root.insert("items", arr);
        ls::saveJson("snippets.json", root);
        m_snipName->clear();
        render();
        toast("代码片段", "已保存「" + name + "」");
    });
    connect(m_snipList, &QListWidget::itemClicked, this, [this] {
        auto* it = m_snipList->currentItem();
        if (!it) return;
        const qint64 id = it->data(Qt::UserRole).toLongLong();
        for (const QJsonValue& val : ls::loadJson("snippets.json").value("items").toArray()) {
            const QJsonObject o = val.toObject();
            if ((qint64)o.value("id").toDouble() == id) {
                m_snipName->setText(o.value("name").toString());
                m_snipEdit->setPlainText(o.value("content").toString());
                break;
            }
        }
    });
    connect(m_snipList, &QListWidget::itemDoubleClicked, this, [this] {
        QApplication::clipboard()->setText(m_snipEdit->toPlainText());
        toast("代码片段", "内容已复制到剪贴板");
    });
    connect(copy, &QPushButton::clicked, this, [this] {
        const QString t = m_snipEdit->toPlainText();
        if (!t.isEmpty()) QApplication::clipboard()->setText(t);
    });
    connect(del, &QPushButton::clicked, this, [this, render] {
        auto* it = m_snipList->currentItem();
        if (!it) return;
        const qint64 id = it->data(Qt::UserRole).toLongLong();
        QJsonObject root = ls::loadJson("snippets.json");
        QJsonArray arr = root.value("items").toArray();
        QJsonArray keep;
        for (const QJsonValue& val : arr)
            if ((qint64)val.toObject().value("id").toDouble() != id) keep.append(val);
        root.insert("items", keep);
        ls::saveJson("snippets.json", root);
        render();
    });

    render();
    scroll->setWidget(body);
    auto* page = new QWidget;
    auto* wrap = new QVBoxLayout(page);
    wrap->setContentsMargins(0, 0, 0, 0);
    wrap->addWidget(scroll);
    return page;
}

} // namespace tb
