// 智能联网：翻译 / OCR
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
#include <QCheckBox>
#include <QUuid>
#include <QVBoxLayout>
#include "shotoverlay.h"
#include "petwindow.h"
#include <algorithm>

namespace tb {

QWidget* MainWindow::buildTransTab() {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* body = new QWidget;
    auto* v = new QVBoxLayout(body);
    v->setContentsMargins(0, 4, 12, 0);
    v->setSpacing(12);

    QVBoxLayout* lay;
    auto* card = toolCard("翻译（需要联网 · Google / MyMemory 双源自动兜底）", lay);
    auto* tip = new QLabel("输入文本，选择目标语言。自动检测源语言；网络不可达时状态栏提示。");
    tip->setObjectName("dim");
    tip->setWordWrap(true);
    lay->addWidget(tip);

    m_transIn = new QPlainTextEdit;
    m_transIn->setPlaceholderText("输入要翻译的文本…");
    m_transIn->setFixedHeight(110);
    lay->addWidget(m_transIn);

    auto* row = new QHBoxLayout;
    row->addWidget(new QLabel("目标语言"));
    m_transLang = new QComboBox;
    m_transLang->addItems({"简体中文", "English", "日本語", "한국어"});
    m_transLang->setCurrentIndex(1);
    row->addWidget(m_transLang);
    auto* go = new QPushButton("翻 译");
    connect(go, &QPushButton::clicked, this, &MainWindow::translateGo);
    row->addWidget(go);
    row->addStretch();
    lay->addLayout(row);

    m_transState = new QLabel("");
    m_transState->setObjectName("dim");
    lay->addWidget(m_transState);

    m_transOut = new QPlainTextEdit;
    m_transOut->setReadOnly(true);
    m_transOut->setFixedHeight(140);
    m_transOut->setPlaceholderText("翻译结果…");
    lay->addWidget(m_transOut);

    auto* row2 = new QHBoxLayout;
    auto* copy = new QPushButton("复制结果");
    copy->setObjectName("ghost");
    connect(copy, &QPushButton::clicked, this, [this] {
        const QString t = m_transOut->toPlainText();
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

void MainWindow::translateGo() {
    const QString text = m_transIn->toPlainText().trimmed().left(4500);
    if (text.isEmpty()) { m_transState->setText("先输入要翻译的文本"); return; }
    const QStringList tls = {"zh-CN", "en", "ja", "ko"};
    const QString tl = tls.value(m_transLang->currentIndex(), "en");
    if (!m_net) m_net = new QNetworkAccessManager(this);
    m_transState->setText("翻译中…");

    // 主源：Google gtx（自动检测源语言）
    QUrl url("https://translate.googleapis.com/translate_a/single");
    QUrlQuery q;
    q.addQueryItem("client", "gtx");
    q.addQueryItem("sl", "auto");
    q.addQueryItem("tl", tl);
    q.addQueryItem("dt", "t");
    q.addQueryItem("q", text);
    url.setQuery(q);
    QNetworkRequest req(url);
    req.setTransferTimeout(4000); // gtx 在部分网络不可达，快速失败回落 MyMemory
    auto* reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, text, tl] {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            const QJsonArray arr = QJsonDocument::fromJson(reply->readAll()).array();
            if (!arr.isEmpty()) {
                QString out;
                for (const QJsonValue& s : arr.at(0).toArray()) out += s.toArray().at(0).toString();
                if (!out.isEmpty()) {
                    m_transOut->setPlainText(out);
                    m_transState->setText("已翻译（Google）");
                    return;
                }
            }
        }
        // 兜底：MyMemory（不支持自动检测，按 ASCII 猜源语言；限 480 字符）
        bool ascii = true;
        for (const QChar& c : text) if (c.unicode() > 127) { ascii = false; break; }
        const QString pair = ascii ? ("en|" + tl) : ("zh-CN|" + tl);
        QUrl u2("https://api.mymemory.translated.net/get");
        QUrlQuery q2;
        q2.addQueryItem("q", text.left(480));
        q2.addQueryItem("langpair", pair);
        u2.setQuery(q2);
        QNetworkRequest r2(u2);
        r2.setTransferTimeout(10000);
        auto* rep2 = m_net->get(r2);
        connect(rep2, &QNetworkReply::finished, this, [this, rep2] {
            rep2->deleteLater();
            if (rep2->error() == QNetworkReply::NoError) {
                const QJsonObject o = QJsonDocument::fromJson(rep2->readAll()).object();
                const QString t = o.value("responseData").toObject().value("translatedText").toString();
                if (!t.isEmpty()) {
                    m_transOut->setPlainText(t);
                    m_transState->setText("已翻译（MyMemory，仅前 480 字符）");
                    return;
                }
            }
            m_transState->setText("翻译失败：网络不可达或服务不可用，请检查网络");
        });
    });
}

QWidget* MainWindow::buildOcrTab() {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* body = new QWidget;
    auto* v = new QVBoxLayout(body);
    v->setContentsMargins(0, 4, 12, 0);
    v->setSpacing(12);

    QVBoxLayout* lay;
    auto* card = toolCard("文字识别 OCR（Windows 内置引擎 · 离线）", lay);
    auto* tip = new QLabel("基于 Windows.Media.Ocr，支持中英文（取决于系统已安装的语言包）。可识别图片文件、剪贴板图片，或直接截图识别。");
    tip->setObjectName("dim");
    tip->setWordWrap(true);
    lay->addWidget(tip);

    auto* row = new QHBoxLayout;
    auto* pick = new QPushButton("选择图片…");
    connect(pick, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, "选择图片", QString(), "图片 (*.png *.jpg *.jpeg *.bmp *.webp)");
        if (!f.isEmpty()) runOcr(f);
    });
    auto* clip = new QPushButton("识别剪贴板图片");
    clip->setObjectName("ghost");
    connect(clip, &QPushButton::clicked, this, [this] {
        const QImage img = QApplication::clipboard()->image();
        if (img.isNull()) { m_ocrState->setText("剪贴板里没有图片"); return; }
        const QString tmp = ls::dataDir() + "/ocr_tmp.png";
        if (img.save(tmp, "PNG")) runOcr(tmp);
    });
    auto* shot = new QPushButton("截图并识别");
    shot->setObjectName("ghost");
    connect(shot, &QPushButton::clicked, this, [this] {
        if (m_overlay) m_overlay->deleteLater();
        m_overlay = new ShotOverlay();
        QPointer<ShotOverlay> guard = m_overlay;
        connect(m_overlay, &ShotOverlay::finished, this, [this, guard](const QPixmap& pix) {
            m_shotPixmap = pix;
            QApplication::clipboard()->setPixmap(pix);
            if (m_overlay) m_overlay->deleteLater();
            const QString tmp = ls::dataDir() + "/ocr_tmp.png";
            if (pix.save(tmp, "PNG")) runOcr(tmp);
        });
        connect(m_overlay, &ShotOverlay::cancelled, m_overlay, &QObject::deleteLater);
        m_overlay->start();
    });
    row->addWidget(pick);
    row->addWidget(clip);
    row->addWidget(shot);
    row->addStretch();
    lay->addLayout(row);

    m_ocrBar = new QProgressBar;
    m_ocrBar->setTextVisible(false);
    m_ocrBar->setFixedHeight(6);
    m_ocrBar->hide();
    lay->addWidget(m_ocrBar);
    m_ocrState = new QLabel("");
    m_ocrState->setObjectName("dim");
    lay->addWidget(m_ocrState);

    m_ocrOut = new QPlainTextEdit;
    m_ocrOut->setReadOnly(true);
    m_ocrOut->setFixedHeight(200);
    m_ocrOut->setPlaceholderText("识别结果…（可复制）");
    lay->addWidget(m_ocrOut);

    auto* row2 = new QHBoxLayout;
    auto* copy = new QPushButton("复制识别结果");
    copy->setObjectName("ghost");
    connect(copy, &QPushButton::clicked, this, [this] {
        const QString t = m_ocrOut->toPlainText();
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

void MainWindow::runOcr(const QString& imgPath) {
    const QString ps1 = QCoreApplication::applicationDirPath() + "/tools/ocr.ps1";
    if (!QFile::exists(ps1)) { m_ocrState->setText("缺少 tools\\ocr.ps1（部署不完整）"); return; }
    if (!m_ocrProc) {
        m_ocrProc = new QProcess(this);
        connect(m_ocrProc, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
            const QString out = QString::fromUtf8(m_ocrProc->readAllStandardOutput()).trimmed();
            const QString err = QString::fromUtf8(m_ocrProc->readAllStandardError()).trimmed();
            m_ocrBar->hide();
            if (code == 0 && !out.isEmpty()) {
                m_ocrOut->setPlainText(out);
                m_ocrState->setText("识别完成");
                toast("文字识别", "OCR 识别完成，结果已就绪");
            } else {
                m_ocrState->setText(err.isEmpty() ? "识别失败：可能缺少 OCR 语言包（设置→时间和语言→语言）"
                                                  : "识别失败：" + err.left(140));
            }
        });
    }
    if (m_ocrProc->state() != QProcess::NotRunning) { m_ocrState->setText("上一次识别还在进行…"); return; }
    m_ocrBar->setRange(0, 0); // 忙碌态
    m_ocrBar->show();
    m_ocrState->setText("识别中…");
    m_ocrProc->start("powershell", {"-NoProfile", "-ExecutionPolicy", "Bypass", "-File", ps1, "-img", imgPath});
}

// ==================== 工具箱扩展：脱敏 / 加解密 / 开发工具箱 ====================

} // namespace tb
