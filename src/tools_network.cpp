// 联网工具：HTTP 请求测试（翻译 / OCR 仍在主文件，后续可迁入）
// 由 mainwindow.cpp 模块化拆分：本文件实现 MainWindow 的联网类工具箱 tab。
#include "mainwindow.h"
#include "tools_common.h"
#include "localstore.h"
#include "petwindow.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <functional>
#include <QHostAddress>
#include <QHostInfo>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkInterface>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QSpinBox>
#include <QStandardPaths>
#include <QHeaderView>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace tb {

// ---------- HTTP 请求测试：方法/URL/头/体 → 状态码 + 耗时 + 响应预览 ----------
QWidget* MainWindow::buildHttpTab() {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* body = new QWidget;
    auto* v = new QVBoxLayout(body);
    v->setContentsMargins(0, 4, 12, 0);
    v->setSpacing(12);

    QVBoxLayout* lay;
    auto* card = toolCard("HTTP 请求测试（需要联网）", lay);
    auto* tip = new QLabel("调试接口用：方法 + URL + 自定义请求头（每行 Key: Value）+ 请求体。响应预览最多显示 10000 字符。");
    tip->setObjectName("dim");
    tip->setWordWrap(true);
    lay->addWidget(tip);

    auto* rowTop = new QHBoxLayout;
    auto* method = new QComboBox;
    method->addItems({"GET", "POST", "PUT", "DELETE"});
    rowTop->addWidget(method);
    auto* url = new QLineEdit;
    url->setPlaceholderText("https://httpbin.org/get");
    auto* recent = new QComboBox;
    recent->setToolTip("最近请求过的 URL");
    recent->setFixedWidth(34);
    for (const QJsonValue& val : ls::loadJson("http_history.json").value("urls").toArray()) {
        recent->addItem(val.toString());
        recent->setItemData(recent->count() - 1, val.toString(), Qt::ToolTipRole);
    }
    recent->setCurrentIndex(-1);
    connect(recent, &QComboBox::currentIndexChanged, url, [url, recent](int i) {
        if (i >= 0) url->setText(recent->itemText(i));
    });
    rowTop->addWidget(url, 1);
    rowTop->addWidget(recent);
    auto* send = new QPushButton("发 送");
    rowTop->addWidget(send);
    lay->addLayout(rowTop);

    auto* cols = new QHBoxLayout;
    auto* hdrBox = new QVBoxLayout;
    hdrBox->addWidget(new QLabel("请求头（每行 Key: Value）"));
    auto* headers = new QPlainTextEdit;
    headers->setPlaceholderText("Content-Type: application/json");
    headers->setFixedHeight(96);
    hdrBox->addWidget(headers);
    cols->addLayout(hdrBox, 1);
    auto* bodyBox = new QVBoxLayout;
    bodyBox->addWidget(new QLabel("请求体（POST/PUT）"));
    auto* bodyEd = new QPlainTextEdit;
    bodyEd->setPlaceholderText("{\"key\": \"value\"}");
    bodyEd->setFixedHeight(96);
    bodyBox->addWidget(bodyEd);
    cols->addLayout(bodyBox, 1);
    lay->addLayout(cols);

    auto* rowGo = new QHBoxLayout;
    auto* state = new QLabel("");
    state->setObjectName("dim");
    rowGo->addWidget(state, 1);
    auto* copy = new QPushButton("复制响应");
    copy->setObjectName("ghost");

    auto* resp = new QPlainTextEdit;
    resp->setReadOnly(true);
    resp->setFixedHeight(240);
    resp->setPlaceholderText("响应…");
    lay->addWidget(resp);
    rowGo->addWidget(copy);
    lay->addLayout(rowGo);

    connect(send, &QPushButton::clicked, this, [this, method, url, headers, bodyEd, resp, state, copy] {
        const QUrl u(url->text().trimmed());
        if (!u.isValid() || u.host().isEmpty()) { state->setText("URL 不合法"); return; }
        // 记入最近 URL（去重，最多 12 条）
        {
            QJsonObject h = ls::loadJson("http_history.json");
            QJsonArray arr = h.value("urls").toArray();
            const QString us = u.toString();
            QJsonArray keep;
            for (const QJsonValue& v : arr) if (v.toString() != us) keep.append(v);
            keep.prepend(us);
            while (keep.size() > 12) keep.removeLast();
            h.insert("urls", keep);
            ls::saveJson("http_history.json", h);
        }
        if (!m_net) m_net = new QNetworkAccessManager(this);
        QNetworkRequest req(u);
        req.setTransferTimeout(15000);
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        const QStringList lines = headers->toPlainText().split('\n', Qt::SkipEmptyParts);
        for (const QString& l : lines) {
            const int c = l.indexOf(':');
            if (c > 0) req.setRawHeader(l.left(c).trimmed().toUtf8(), l.mid(c + 1).trimmed().toUtf8());
        }
        if (req.header(QNetworkRequest::UserAgentHeader).isNull())
            req.setRawHeader("User-Agent", "ToolBox-HttpTest/1.0");
        state->setText("请求中…");
        const QByteArray payload = bodyEd->toPlainText().toUtf8();
        QElapsedTimer tm;
        tm.start();
        QNetworkReply* reply = nullptr;
        const QString m = method->currentText();
        if (m == "POST") {
            if (req.header(QNetworkRequest::ContentTypeHeader).isNull())
                req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
            reply = m_net->post(req, payload);
        } else if (m == "PUT") {
            reply = m_net->put(req, payload);
        } else if (m == "DELETE") {
            reply = m_net->deleteResource(req);
        } else {
            reply = m_net->get(req);
        }
        connect(reply, &QNetworkReply::finished, this, [reply, resp, state, tm, copy] {
            reply->deleteLater();
            const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QByteArray data = reply->readAll();
            QString body = QString::fromUtf8(data);
            if (body.size() > 10000) body = body.left(10000) + QString("\n…（截断，共 %1 字节）").arg(data.size());
            resp->setPlainText(QString("HTTP %1 %2\n耗时 %3 ms\n\n").arg(code)
                .arg(reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString())
                .arg(tm.elapsed()) + body);
            if (code == 0)
                state->setText("请求失败：" + reply->errorString());
            else
                state->setText(QString("HTTP %1 · %2 KB").arg(code).arg(QString::number(data.size() / 1024.0, 'f', 1)));
            disconnect(copy, &QPushButton::clicked, nullptr, nullptr);
            connect(copy, &QPushButton::clicked, resp, [resp] { QApplication::clipboard()->setText(resp->toPlainText()); });
        });
    });

    v->addWidget(card);
    v->addStretch();

    scroll->setWidget(body);
    auto* page = new QWidget;
    auto* wrap = new QVBoxLayout(page);
    wrap->setContentsMargins(0, 0, 0, 0);
    wrap->addWidget(scroll);
    return page;
}

// ---------- 汇率换算（frankfurter.app · ECB 数据 · 免 key，1 小时缓存） ----------
QWidget* MainWindow::buildFxTab() {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* body = new QWidget;
    auto* v = new QVBoxLayout(body);
    v->setContentsMargins(0, 4, 12, 0);
    v->setSpacing(12);

    QVBoxLayout* lay;
    auto* card = toolCard("汇率换算（欧洲央行参考汇率 · 需要联网）", lay);
    auto* row = new QHBoxLayout;
    auto* amt = new QLineEdit;
    amt->setPlaceholderText("金额");
    amt->setFixedWidth(110);
    row->addWidget(amt);
    auto* from = new QComboBox;
    auto* to = new QComboBox;
    // 中文名 ↔ 代码（显示全中文，请求用代码）
    const QVector<QPair<QString, QString>> ccys = {
        {"人民币", "CNY"}, {"美元", "USD"}, {"欧元", "EUR"}, {"日元", "JPY"},
        {"英镑", "GBP"}, {"港币", "HKD"}, {"韩元", "KRW"}, {"新加坡元", "SGD"},
        {"澳元", "AUD"}, {"加元", "CAD"}, {"瑞士法郎", "CHF"}, {"新台币", "TWD"},
    };
    for (const auto& c : ccys) { from->addItem(c.first); to->addItem(c.first); }
    from->setCurrentIndex(0);
    to->setCurrentIndex(1);
    row->addWidget(from);
    row->addWidget(new QLabel("换成"));
    row->addWidget(to);
    auto* go = new QPushButton("换 算");
    row->addWidget(go);
    row->addStretch();
    lay->addLayout(row);
    auto* out = new QLabel("");
    out->setObjectName("postTitle");
    out->setWordWrap(true);
    lay->addWidget(out);
    auto* state = new QLabel("");
    state->setObjectName("dim");
    lay->addWidget(state);
    connect(go, &QPushButton::clicked, this, [this, amt, from, to, out, state, ccys] {
        bool ok; const double x = amt->text().toDouble(&ok);
        if (!ok || x <= 0) { state->setText("请输入合法金额"); return; }
        const QString f = ccys.value(from->currentIndex()).second;
        const QString t = ccys.value(to->currentIndex()).second;
        const QString fZh = from->currentText(), tZh = to->currentText();
        // 1 小时缓存
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        QJsonObject cache = ls::loadJson("fx.json");
        if (cache.value("ts").toDouble() > now - 3600 && cache.value("base").toString() == f
            && cache.value("rates").toObject().contains(t)) {
            const double r = cache.value("rates").toObject().value(t).toDouble();
            out->setText(QString("%1 %2 = %3 %4").arg(amt->text(), fZh, QString::number(x * r, 'f', 2), tZh));
            state->setText(QString("缓存汇率（获取于 %1），重新选择币种可刷新").arg(
                QDateTime::fromSecsSinceEpoch((qint64)cache.value("ts").toDouble()).toString("hh:mm")));
            return;
        }
        if (!m_net) m_net = new QNetworkAccessManager(this);
        state->setText("获取汇率中…");
        QUrl url(QString("https://api.frankfurter.app/latest?from=%1&to=%2").arg(f, t));
        QNetworkRequest req(url);
        req.setTransferTimeout(8000);
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy); // 域名 301 重定向
        auto* reply = m_net->get(req);
        connect(reply, &QNetworkReply::finished, this, [this, reply, x, f, t, amt, out, state, fZh, tZh] {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) { state->setText("获取失败：" + reply->errorString().left(80)); return; }
            const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
            const double r = o.value("rates").toObject().value(t).toDouble();
            if (r <= 0) { state->setText("返回数据异常"); return; }
            QJsonObject cache; cache.insert("ts", (double)QDateTime::currentSecsSinceEpoch());
            cache.insert("base", f); cache.insert("rates", o.value("rates"));
            ls::saveJson("fx.json", cache);
            out->setText(QString("%1 %2 = %3 %4").arg(amt->text(), fZh, QString::number(x * r, 'f', 2), tZh));
            state->setText("汇率日期 " + o.value("date").toString() + " · 欧洲央行参考价（缓存 1 小时）");
        });
    });
    v->addWidget(card);
    v->addStretch();
    scroll->setWidget(body);
    auto* page = new QWidget;
    auto* wrap = new QVBoxLayout(page);
    wrap->setContentsMargins(0, 0, 0, 0);
    wrap->addWidget(scroll);
    return page;
}

// ---------- 网络诊断：网卡/内网 IP/公网 IP/ping ----------
QWidget* MainWindow::buildNetTab() {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* body = new QWidget;
    auto* v = new QVBoxLayout(body);
    v->setContentsMargins(0, 4, 12, 0);
    v->setSpacing(12);

    QVBoxLayout* lay;
    auto* card = toolCard("本机网络信息（网卡 · 公网 IP 归属地 · 域名解析 · Ping）", lay);
    auto* row = new QHBoxLayout;
    auto* info = new QLabel("");
    info->setObjectName("dim");
    info->setWordWrap(true);
    row->addWidget(info, 1);
    auto* refresh = new QPushButton("刷新");
    connect(refresh, &QPushButton::clicked, this, [info] {
        QString s;
        const auto ifs = QNetworkInterface::allInterfaces();
        for (const QNetworkInterface& ni : ifs) {
            if (!(ni.flags() & QNetworkInterface::IsUp) || !(ni.flags() & QNetworkInterface::IsRunning)
                || ni.flags() & QNetworkInterface::IsLoopBack) continue;
            for (const QNetworkAddressEntry& e : ni.addressEntries()) {
                if (e.ip().protocol() != QAbstractSocket::IPv4Protocol) continue;
                s += QString("%1  ·  %2\n").arg(ni.humanReadableName(), e.ip().toString());
            }
        }
        info->setText(s.isEmpty() ? "未检测到活动网卡" : s.trimmed() + "\n\n公网 IP 获取中…");
        QNetworkAccessManager* nm = new QNetworkAccessManager(info);
        // api.ipify.org 本网络 SSL 直连失败，改用国内源（返回 IPv4）
        QNetworkRequest req(QUrl("http://members.3322.org/dyndns/getip"));
        req.setTransferTimeout(6000);
        auto* reply = nm->get(req);
        QObject::connect(reply, &QNetworkReply::finished, info, [info, reply, s, nm] {
            reply->deleteLater();
            const QString pub = reply->error() == QNetworkReply::NoError
                ? QString::fromUtf8(reply->readAll()).trimmed() : QString();
            if (pub.isEmpty()) {
                info->setText(s.trimmed() + "\n\n公网 IP：获取失败");
                return;
            }
            info->setText(s.trimmed() + "\n\n公网 IP：" + pub + "\n归属地查询中…");
            // ip-api.com 中文归属地（免费免 key）
            QNetworkRequest req2(QUrl("http://ip-api.com/json/" + pub +
                                      "?lang=zh-CN&fields=country,regionName,city,isp"));
            req2.setTransferTimeout(6000);
            auto* reply2 = nm->get(req2);
            QObject::connect(reply2, &QNetworkReply::finished, info, [info, reply2, s, pub] {
                reply2->deleteLater();
                QString loc;
                if (reply2->error() == QNetworkReply::NoError) {
                    const QJsonObject o = QJsonDocument::fromJson(reply2->readAll()).object();
                    QStringList parts;
                    for (const QString& k : {"country", "regionName", "city"})
                        if (!o.value(k).toString().isEmpty()) parts << o.value(k).toString();
                    parts << o.value("isp").toString();
                    loc = parts.join(" · ");
                }
                info->setText(s.trimmed() + "\n\n公网 IP：" + pub +
                              (loc.isEmpty() ? "" : "\n归属地：" + loc));
            });
        });
    });
    row->addWidget(refresh);
    lay->addLayout(row);
    lay->addWidget(new QLabel("域名解析（域名 → IP 列表）"));
    auto* rowD = new QHBoxLayout;
    auto* dom = new QLineEdit;
    dom->setPlaceholderText("输入域名（如 www.baidu.com），回车解析");
    rowD->addWidget(dom, 1);
    auto* resolve = new QPushButton("解 析");
    rowD->addWidget(resolve);
    lay->addLayout(rowD);
    auto* dnsOut = new QLabel("");
    dnsOut->setObjectName("dim");
    dnsOut->setWordWrap(true);
    dnsOut->setTextInteractionFlags(Qt::TextSelectableByMouse);
    lay->addWidget(dnsOut);
    connect(resolve, &QPushButton::clicked, this, [dom, dnsOut] {
        const QString h = dom->text().trimmed();
        if (h.isEmpty()) return;
        dnsOut->setText("解析中…");
        QHostInfo::lookupHost(h, dnsOut, [h, dnsOut](const QHostInfo& r) {
            if (r.error() != QHostInfo::NoError) {
                dnsOut->setText(QString("解析失败：%1").arg(r.errorString()));
                return;
            }
            QStringList ips;
            for (const QHostAddress& a : r.addresses())
                if (a.protocol() == QAbstractSocket::IPv4Protocol) ips << a.toString();
            dnsOut->setText(QString("%1 → %2").arg(h, ips.isEmpty() ? QString("（无 IPv4 记录）") : ips.join("， ")));
        });
    });
    connect(dom, &QLineEdit::returnPressed, resolve, &QPushButton::click);
    lay->addWidget(new QLabel("Ping 测试"));
    auto* rowP = new QHBoxLayout;
    auto* host = new QLineEdit;
    host->setPlaceholderText("域名或 IP（回车执行，如 223.5.5.5）");
    rowP->addWidget(host, 1);
    auto* go = new QPushButton("Ping");
    rowP->addWidget(go);
    lay->addLayout(rowP);
    auto* out = new QPlainTextEdit;
    out->setReadOnly(true);
    out->setFixedHeight(190);
    out->setPlaceholderText("ping 输出…");
    lay->addWidget(out);
    connect(go, &QPushButton::clicked, this, [host, out, go] {
        const QString h = host->text().trimmed();
        if (h.isEmpty()) return;
        go->setEnabled(false);
        out->setPlainText("Pinging " + h + " …");
        QProcess* p = new QProcess(out);
        QObject::connect(p, &QProcess::finished, out, [out, p, go](int code) {
            out->setPlainText(QString::fromLocal8Bit(p->readAllStandardOutput()).trimmed()
                              + (code == 0 ? "" : "\n(退出码 " + QString::number(code) + ")"));
            p->deleteLater();
            go->setEnabled(true);
        });
        p->start("ping", {"-n", "4", h});
    });
    connect(host, &QLineEdit::returnPressed, go, &QPushButton::click);
    v->addWidget(card);
    v->addStretch();

    scroll->setWidget(body);
    auto* page = new QWidget;
    auto* wrap = new QVBoxLayout(page);
    wrap->setContentsMargins(0, 0, 0, 0);
    wrap->addWidget(scroll);
    return page;
}

// ---------- 下载器：URL → 文件，进度/速度/取消，完成托盘+桌宠通报 ----------
// ---------- 下载器辅助：刷新队列显示 ----------
void MainWindow::refreshQueueList() {
    if (!m_dlQueueList) return;
    m_dlQueueList->clear();
    for (const QString& q : m_dlQueue) m_dlQueueList->addItem(q);
    if (m_dlQueue.isEmpty()) m_dlQueueList->hide();
}

// ---------- 下载器：多线程分块加速（Range），不支持的源自动回落单线程 ----------
QWidget* MainWindow::buildDownloadTab() {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* body = new QWidget;
    auto* v = new QVBoxLayout(body);
    v->setContentsMargins(0, 4, 12, 0);
    v->setSpacing(12);

    QVBoxLayout* lay;
    auto* card = toolCard("下载器（多线程分块加速 · 服务器不支持时自动单线程）", lay);
    auto* row = new QHBoxLayout;
    auto* url = new QLineEdit;
    url->setPlaceholderText("文件直链 URL（自动取文件名）");
    row->addWidget(url, 1);
    auto* paste = new QPushButton("粘 贴");
    paste->setObjectName("ghost");
    connect(paste, &QPushButton::clicked, url, [url] {
        const QString t = QApplication::clipboard()->text().trimmed();
        if (!t.isEmpty()) url->setText(t);
    });
    row->addWidget(paste);
    lay->addLayout(row);
    auto* rowTo = new QHBoxLayout;
    rowTo->addWidget(new QLabel("保存到"));
    auto* to = new QLineEdit(ls::loadJson("dl_prefs.json").value("dir").toString(
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)));
    rowTo->addWidget(to, 1);
    auto* browse = new QPushButton("…");
    browse->setFixedWidth(34);
    connect(browse, &QPushButton::clicked, this, [to] {
        const QString d = QFileDialog::getExistingDirectory(nullptr, "选择保存目录", to->text());
        if (!d.isEmpty()) to->setText(d);
    });
    rowTo->addWidget(browse);
    rowTo->addWidget(new QLabel("线程"));
    auto* threads = new QSpinBox;
    threads->setRange(1, 16);
    threads->setValue(8);
    threads->setSuffix(" 线程");
    rowTo->addWidget(threads);
    lay->addLayout(rowTo);
    auto* queueRow = new QHBoxLayout;
    m_dlQueueInput = new QLineEdit;
    m_dlQueueInput->setPlaceholderText("加入队列：再贴一个直链（最多排 3 个，串行下载）");
    queueRow->addWidget(m_dlQueueInput, 1);
    auto* enq = new QPushButton("入 队");
    queueRow->addWidget(enq);
    m_dlQueueList = new QListWidget;
    m_dlQueueList->setMaximumHeight(72);
    m_dlQueueList->setToolTip("待下载队列 · 双击移除");
    m_dlQueueList->hide();
    queueRow->addWidget(m_dlQueueList, 1);
    lay->addLayout(queueRow);
    auto* rowGo = new QHBoxLayout;
    auto* start = new QPushButton("开始下载");
    rowGo->addWidget(start);
    auto* cancel = new QPushButton("取消");
    cancel->setObjectName("ghost");
    cancel->setEnabled(false);
    rowGo->addWidget(cancel);
    rowGo->addStretch();
    lay->addLayout(rowGo);
    m_dlBar = new QProgressBar;
    m_dlBar->setRange(0, 100);
    m_dlBar->setTextVisible(false);
    lay->addWidget(m_dlBar);
    m_dlState = new QLabel("");
    m_dlState->setObjectName("dim");
    lay->addWidget(m_dlState);
    v->addWidget(card);
    v->addStretch();

    // 终止全部块请求并清理
    auto abortAll = [this] {
        for (QNetworkReply* r : m_dlReplies) if (r) r->abort();
        m_dlReplies.clear();
        if (m_dlFile) { m_dlFile->close(); delete m_dlFile; m_dlFile = nullptr; }
    };

    connect(start, &QPushButton::clicked, this, [this, url, to, threads, start, cancel] {
        const QUrl u(url->text().trimmed());
        if (!u.isValid() || u.host().isEmpty()) { m_dlState->setText("URL 不合法"); return; }
        QString name = u.fileName();
        if (name.isEmpty()) name = "download_" + QDateTime::currentDateTime().toString("hhmmss");
        // 同名不覆盖：自动追加序号
        QString path = to->text().trimmed() + "/" + name;
        if (QFileInfo::exists(path)) {
            const QString base = QFileInfo(path).completeBaseName();
            const QString ext = QFileInfo(path).suffix();
            for (int i = 1; QFileInfo::exists(path); ++i)
                path = to->text().trimmed() + "/" + base + "(" + QString::number(i) + ")" + (ext.isEmpty() ? "" : "." + ext);
            name = QFileInfo(path).fileName();
        }

        if (!m_net) m_net = new QNetworkAccessManager(this);
        start->setEnabled(false);
        cancel->setEnabled(true);
        m_dlBar->setValue(0);
        m_dlState->setText("探测服务器分块支持…");
        m_dlClock.start();
        m_dlLast = 0;
        m_dlLastPath = path;
        m_dlOpenDir->setEnabled(false);
        // 断点续传：检测 .tbdl 半成品与 sidecar
        m_dlResume = false;
        m_dlBlockStart.clear();
        m_dlBlockGot.clear();
        const QString part = path + ".tbdl";
        const QString side = path + ".tbdl.json";
        if (QFile::exists(part) && QFile::exists(side)) {
            const QJsonObject sd = QJsonDocument::fromJson(QFile(side).readAll()).object();
            if (sd.value("url").toString() == u.toString() && sd.contains("blocks")) {
                m_dlResume = true;
                m_dlSavedPart = part;
                m_dlSavedSide = side;
                m_dlTotal = (qint64)sd.value("total").toDouble();
                const QJsonArray st = sd.value("start").toArray();
                const QJsonArray gt = sd.value("got").toArray();
                for (int i = 0; i < st.size(); ++i) {
                    m_dlBlockStart.append((qint64)st[i].toDouble());
                    m_dlBlockGot.append((qint64)gt[i].toDouble());
                }
                m_dlBlocks = m_dlBlockStart.size();
            }
        }

        // 第一步：HEAD 拿大小 + Accept-Ranges
        QNetworkRequest hreq(u);
        hreq.setTransferTimeout(8000);
        hreq.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        hreq.setRawHeader("User-Agent", "ToolBox-Downloader/1.0");
        auto* head = m_net->head(hreq);
        connect(head, &QNetworkReply::finished, this, [this, head, u, name, threads, start, cancel, path, url] {
            head->deleteLater();
            qint64 total = head->header(QNetworkRequest::ContentLengthHeader).toLongLong();
            const bool ranges = QString::fromLatin1(head->rawHeader("Accept-Ranges")).contains("bytes");
            const QUrl finalUrl = head->url(); // Qt 跟随重定向后 url() 即最终地址

            int n;
            if (m_dlResume) {
                n = m_dlBlocks;
                total = m_dlTotal; // 续传沿用原布局
            } else {
                n = (ranges && total > 1) ? qBound(1, threads->value(), 16) : 1;
                if (n > 1 && total / n < 512 * 1024) n = 1;
                m_dlTotal = total;
                m_dlBlocks = n;
                m_dlBlockStart.assign(n, 0);
                m_dlBlockGot.assign(n, 0);
            }
            m_dlDone = 0;
            m_dlReplies.clear();
            m_dlReplies.resize(n);

            m_dlFile = new QFile(m_dlResume ? m_dlSavedPart : path);
            if (!m_dlFile->open(QIODevice::WriteOnly | QIODevice::Unbuffered)) {
                m_dlState->setText("无法创建文件：" + path);
                delete m_dlFile; m_dlFile = nullptr;
                start->setEnabled(true); cancel->setEnabled(false);
                return;
            }
            if (total > 0 && !m_dlResume) m_dlFile->resize(total); // 预分配，分块随机写
            m_dlState->setText(QString("开始下载：%1 · %2 · %3").arg(name)
                .arg(total > 0 ? QString::number(total / 1048576.0, 'f', 1) + " MB" : "未知大小")
                .arg(m_dlResume ? QString("断点续传 · 已完成 %1%").arg(int(100 * [&]{ qint64 g=0; for (qint64 x : m_dlBlockGot) g+=x; return g; }() / qMax<qint64>(1, m_dlTotal))) : n == 1 ? "单线程（服务器不支持分块）" : QString("%1 线程分块").arg(n)));

            // 分块并发请求
            const qint64 chunk = n == 1 ? total : (total + n - 1) / n;
            for (int i = 0; i < n; ++i) {
                qint64 s = qint64(i) * chunk;
                if (m_dlResume) s = m_dlBlockStart[i] + m_dlBlockGot[i]; // 从保存的断点继续
                else m_dlBlockStart[i] = s;
                const qint64 e = (i == n - 1) ? (total > 0 ? total - 1 : -1) : (s + chunk - 1);
                QNetworkRequest req(finalUrl);
                req.setTransferTimeout(0);
                req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
                req.setRawHeader("User-Agent", "ToolBox-Downloader/1.0");
                if (m_dlResume && s > e) { // 该块已完成
                    m_dlReplies[i] = nullptr;
                    ++m_dlDone;
                    continue;
                }
                if (n > 1 && total > 0) req.setRawHeader("Range", QString("bytes=%1-%2").arg(s).arg(e).toUtf8());
                auto* reply = m_net->get(req);
                m_dlReplies[i] = reply;
                connect(reply, &QNetworkReply::readyRead, this, [this, i] {
                    if (!m_dlFile || i >= m_dlReplies.size() || !m_dlReplies[i]) return;
                    const QByteArray data = m_dlReplies[i]->readAll();
                    if (data.isEmpty()) return;
                    m_dlFile->seek(m_dlBlockStart[i] + m_dlBlockGot[i]);
                    m_dlFile->write(data);
                    m_dlBlockGot[i] += data.size();
                });
                connect(reply, &QNetworkReply::finished, this, [this, i, path, name, url, start, cancel] {
                    if (i >= m_dlReplies.size() || !m_dlReplies[i]) return;
                    const QString err = m_dlReplies[i]->errorString();
                    const bool okReply = m_dlReplies[i]->error() == QNetworkReply::NoError;
                    m_dlReplies[i]->deleteLater();
                    m_dlReplies[i] = nullptr;
                    if (!okReply && m_dlFile) { // 任一块失败 → 整体失败
                        for (QNetworkReply* r : m_dlReplies) if (r) r->abort();
                        m_dlReplies.clear();
                        if (m_dlFile) { m_dlFile->close(); delete m_dlFile; m_dlFile = nullptr; }
                        QFile::remove(path);
                        start->setEnabled(true); cancel->setEnabled(false);
                        m_dlState->setText("下载失败：" + err.left(90));
                        return;
                    }
                    if (++m_dlDone < m_dlBlocks) return;
                    // 全部块完成
                    if (m_dlFile) { m_dlFile->close(); delete m_dlFile; m_dlFile = nullptr; }
                    if (m_dlResume) QFile::remove(m_dlSavedSide);
                    QFile::remove(path + ".tbdl.json");
                    if (m_dlResume) QFile::rename(m_dlSavedPart, path);
                    m_dlLastPath = path;
                    m_dlOpenDir->setEnabled(true);
                    const double secs = m_dlClock.elapsed() / 1000.0;
                    const double mb = m_dlTotal / 1048576.0;
                    m_dlBar->setValue(100);
                    m_dlState->setText(QString("下载完成：%1 · %2 MB · 平均 %3 MB/s · %4 线程")
                        .arg(path).arg(QString::number(mb, 'f', 1))
                        .arg(QString::number(mb / qMax(secs, 0.1), 'f', 2)).arg(m_dlBlocks));
                    pet::playRemote("原地敲击桌面互动", "下载完成啦！去看看吧～");
                    // 队列：自动开始下一个
                    if (m_dlQueue.count() > 0) {
                        const QString next = m_dlQueue.takeFirst();
                        refreshQueueList();
                        url->setText(next);
                        QTimer::singleShot(300, this, [start] { start->click(); });
                    }
                    toast("下载完成", name + "  ·  " + QString::number(mb, 'f', 1) + " MB", 6000, true);
                    // 点「查看」打开所在文件夹（连接一次）
                    disconnect(m_dlOpenConn);
                    m_dlOpenConn = connect(toastHost(), &ToastHost::hostClicked, this, [path] {
                        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
                    });
                });
            }
        });

        // 聚合进度（定时采样各块已下载总和）
        QTimer* meter = new QTimer(this);
        connect(meter, &QTimer::timeout, this, [this, meter, url, path] {
            if (m_dlReplies.isEmpty() || !m_dlFile) { meter->deleteLater(); return; }
            qint64 got = 0;
            for (qint64 g : m_dlBlockGot) got += g;
            const double secs = m_dlClock.elapsed() / 1000.0;
            if (m_dlTotal > 0) m_dlBar->setValue(int(got * 100 / m_dlTotal));
            m_dlState->setText(QString("已下载 %1 / %2 · %3 KB/s · %4 线程")
                .arg(QString::number(got / 1048576.0, 'f', 1))
                .arg(m_dlTotal > 0 ? QString::number(m_dlTotal / 1048576.0, 'f', 1) + " MB" : "未知")
                .arg(QString::number(got > m_dlLast ? (got - m_dlLast) / 1024.0 : 0, 'f', 0))
                .arg(m_dlBlocks));
            m_dlLast = got;
            // 每 2 秒保存断点（节流：偶数次触发）
            if (++m_dlMeterTick % 4 == 0 && m_dlFile) {
                QJsonObject sd;
                sd.insert("url", url->text().trimmed());
                sd.insert("total", (double)m_dlTotal);
                QJsonArray st, gt;
                for (int i = 0; i < m_dlBlocks; ++i) { st.append((double)m_dlBlockStart[i]); gt.append((double)m_dlBlockGot[i]); }
                sd.insert("start", st); sd.insert("got", gt);
                QFile sf(path + ".tbdl.json");
                if (sf.open(QIODevice::WriteOnly)) { sf.write(QJsonDocument(sd).toJson()); sf.close(); }
            }
        });
        meter->start(500);
    });
    connect(cancel, &QPushButton::clicked, this, [this, start, cancel] {
        for (QNetworkReply* r : m_dlReplies) if (r) r->abort();
        m_dlReplies.clear();
        if (m_dlFile) { m_dlFile->close(); delete m_dlFile; m_dlFile = nullptr; }
        start->setEnabled(true);
        cancel->setEnabled(false);
        m_dlState->setText("已取消（半成品保留，同链接再下即断点续传）");
    });
    // 下载队列：最多 3 个，串行
    connect(enq, &QPushButton::clicked, this, [this] {
        const QString q = m_dlQueueInput->text().trimmed();
        if (q.isEmpty() || m_dlQueue.size() >= 3) return;
        m_dlQueue.enqueue(q);
        m_dlQueueInput->clear();
        m_dlQueueList->show();
        refreshQueueList();
    });
    connect(m_dlQueueList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) {
        m_dlQueue.removeOne(it->text());
        refreshQueueList();
    });

    scroll->setWidget(body);
    auto* page = new QWidget;
    auto* wrap = new QVBoxLayout(page);
    wrap->setContentsMargins(0, 0, 0, 0);
    wrap->addWidget(scroll);
    return page;
}

// wttr.in 英文天气描述 → 中文 + emoji
namespace {
struct WxDesc { const char* en; const char* zh; const char* icon; };
inline QString wxZh(const QString& en, QString* iconOut = nullptr) {
    static const WxDesc table[] = {
        {"Sunny", "晴", "☀️"}, {"Clear", "晴", "🌙"},
        {"Partly cloudy", "多云转晴", "🌤️"}, {"Partly cloudy night", "多云转晴", "☁️"},
        {"Cloudy", "多云", "☁️"}, {"Overcast", "阴", "☁️"},
        {"Mist", "薄雾", "🌫️"}, {"Fog", "雾", "🌫️"}, {"Freezing fog", "冻雾", "🌫️"},
        {"Light rain", "小雨", "🌦️"}, {"Moderate rain", "中雨", "🌧️"}, {"Heavy rain", "大雨", "🌧️"},
        {"Light rain shower", "阵雨", "🌦️"}, {"Patchy rain possible", "可能有零星降雨", "🌦️"},
        {"Patchy rain nearby", "附近有零星降雨", "🌦️"}, {"Light drizzle", "毛毛雨", "🌦️"},
        {"Patchy light drizzle", "零星毛毛雨", "🌦️"}, {"Heavy rain shower", "强阵雨", "🌧️"},
        {"Light snow", "小雪", "🌨️"}, {"Moderate snow", "中雪", "🌨️"}, {"Heavy snow", "大雪", "❄️"},
        {"Light snow showers", "阵雪", "🌨️"}, {"Patchy snow possible", "可能有零星降雪", "🌨️"},
        {"Blizzard", "暴风雪", "❄️"}, {"Light sleet", "雨夹雪", "🌨️"}, {"Heavy sleet", "强雨夹雪", "🌨️"},
        {"Thundery outbreaks possible", "可能有雷雨", "⛈️"}, {"Thunder", "雷", "⛈️"},
        {"Thunderstorm", "雷暴", "⛈️"}, {"Light showers of ice pellets", "小冰雹", "🌨️"},
        {"Hail", "冰雹", "🌨️"}, {"Sunny interval", "间歇晴", "🌤️"},
    };
    for (const auto& w : table) {
        if (en == w.en) { if (iconOut) *iconOut = w.icon; return w.zh; }
    }
    if (en.contains("rain") || en.contains("Drizzle")) { if (iconOut) *iconOut = "🌧️"; return "降雨"; }
    if (en.contains("snow") || en.contains("sleet")) { if (iconOut) *iconOut = "❄️"; return "降雪"; }
    if (en.contains("thunder")) { if (iconOut) *iconOut = "⛈️"; return "雷雨"; }
    if (en.contains("cloud")) { if (iconOut) *iconOut = "☁️"; return "多云"; }
    if (en.contains("fog") || en.contains("Mist")) { if (iconOut) *iconOut = "🌫️"; return "雾"; }
    if (iconOut) *iconOut = "🌡️";
    return en;
}
} // namespace

QWidget* MainWindow::buildWeatherTab() {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* body = new QWidget;
    auto* v = new QVBoxLayout(body);
    v->setContentsMargins(0, 4, 12, 0);
    v->setSpacing(12);

    QVBoxLayout* lay;
    auto* card = toolCard("天气（免 key · 支持中文城市名）", lay);
    auto* row = new QHBoxLayout;
    auto* city = new QLineEdit;
    city->setPlaceholderText("输入城市，如：北京 / 上海 / Guangzhou，回车查询");
    row->addWidget(city, 1);
    auto* go = new QPushButton("查天气");
    row->addWidget(go);
    lay->addLayout(row);
    auto* out = new QLabel("");
    out->setTextFormat(Qt::RichText);
    out->setWordWrap(true);
    out->setTextInteractionFlags(Qt::TextSelectableByMouse);
    out->setStyleSheet("QLabel{font-size:13px;line-height:150%;}");
    lay->addWidget(out, 1);

    std::function<void(const QString&)> query = [this, city, out](const QString& c) {
        if (c.isEmpty()) return;
        if (!m_net) m_net = new QNetworkAccessManager(this);
        out->setText("<span style='color:#7c8294;'>查询中…</span>");
        QUrl url("https://wttr.in/" + QUrl::toPercentEncoding(c) + "?format=j1");
        QNetworkRequest req(url);
        req.setTransferTimeout(12000);
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        auto* reply = m_net->get(req);
        connect(reply, &QNetworkReply::finished, this, [reply, out] {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) { out->setText("查询失败：" + reply->errorString().left(90)); return; }
            const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
            const QJsonArray cur = root.value("current_condition").toArray();
            const QJsonArray days = root.value("weather").toArray();
            if (cur.isEmpty()) { out->setText("未找到该城市，试试拼音（如 Beijing）"); return; }
            const QJsonObject cc = cur[0].toObject();
            QString icon;
            const QString desc = wxZh(cc.value("weatherDesc").toArray()[0].toObject().value("value").toString(), &icon);
            QString s = QString(
                "<div style='margin-bottom:6px;'>"
                "<span style='font-size:34px;font-weight:bold;color:#242838;'>%1°C</span>"
                "  <span style='font-size:15px;color:#7c8294;'>体感 %2°C</span>"
                "  <span style='font-size:30px;'>%3</span></div>"
                "<div style='color:#3c4256;font-size:14px;margin-bottom:10px;'>%4 &nbsp;·&nbsp; 湿度 %5%% &nbsp;·&nbsp; 风速 %6 公里/小时 &nbsp;·&nbsp; 气压 %7 百帕</div>"
                "<div style='color:#7c8294;font-size:12px;margin-bottom:4px;'>未来三天</div>"
                "<table cellspacing='0' cellpadding='3' style='color:#242838;font-size:13px;'>"
                "<tr><td style='color:#7c8294;'>日期</td><td></td><td style='color:#7c8294;'>气温</td><td style='color:#7c8294;'>白天</td></tr>")
                .arg(cc.value("temp_C").toString(), cc.value("FeelsLikeC").toString(), icon,
                     desc, cc.value("humidity").toString(), cc.value("windspeedKmph").toString(),
                     cc.value("pressure").toString());
            for (const QJsonValue& d : days) {
                const QJsonObject w = d.toObject();
                QString di;
                const QString dd = wxZh(w.value("hourly").toArray()[4].toObject()
                                            .value("weatherDesc").toArray()[0].toObject().value("value").toString(), &di);
                const QDate dt = QDate::fromString(w.value("date").toString(), "yyyy-MM-dd");
                const QString dayName = dt == QDate::currentDate() ? "今天"
                    : dt == QDate::currentDate().addDays(1) ? "明天" : dt.toString("M月d日");
                s += QString("<tr><td>%1</td><td style='font-size:16px;'>%2</td>"
                             "<td>%3 ~ %4°C</td><td style='color:#4a5064;'>%5</td></tr>")
                    .arg(dayName, di, w.value("mintempC").toString(), w.value("maxtempC").toString(), dd);
            }
            s += "</table>";
            out->setText(s);
        });
    };
    connect(go, &QPushButton::clicked, this, [query, city] { query(city->text().trimmed()); });
    connect(city, &QLineEdit::returnPressed, go, &QPushButton::click);

    // 常用城市 chips（weather_cities.json 持久化，可收藏）
    auto* favRow = new QHBoxLayout;
    auto* favLab = new QLabel("常用");
    favLab->setObjectName("dim");
    favRow->addWidget(favLab);
    auto* star = new QPushButton("★ 收藏当前");
    star->setObjectName("flat");
    favRow->addWidget(star);
    favRow->addStretch();
    lay->addLayout(favRow);
    auto refreshFavs = [&, city](const std::function<void(const QString&)>& q) {
        while (favRow->count() > 3) {
            QLayoutItem* it = favRow->takeAt(favRow->count() - 1);
            if (QWidget* w = it->widget()) w->deleteLater();
            delete it;
        }
        QJsonObject fc = ls::loadJson("weather_cities.json");
        QJsonArray arr = fc.value("cities").toArray();
        if (arr.isEmpty()) {
            arr = {"北京", "上海", "广州", "杭州"};
            fc.insert("cities", arr);
            ls::saveJson("weather_cities.json", fc);
        }
        for (const QJsonValue& val : arr) {
            auto* chip = new QPushButton(val.toString());
            chip->setObjectName("flat");
            const QString cname = val.toString();
            connect(chip, &QPushButton::clicked, this, [q, chip, city, cname] {
                city->setText(cname);
                q(cname);
            });
            favRow->addWidget(chip);
        }
    };
    connect(star, &QPushButton::clicked, this, [&, city, refreshFavs, query] {
        const QString c = city->text().trimmed();
        if (c.isEmpty()) return;
        QJsonObject fc = ls::loadJson("weather_cities.json");
        QJsonArray arr = fc.value("cities").toArray();
        for (const QJsonValue& val : arr) if (val.toString() == c) return;
        arr.append(c);
        while (arr.size() > 8) arr.removeFirst();
        fc.insert("cities", arr);
        ls::saveJson("weather_cities.json", fc);
        refreshFavs(query);
        toast("天气", "已收藏「" + c + "」");
    });
    refreshFavs(query);

    v->addWidget(card);
    v->addStretch();
    scroll->setWidget(body);
    auto* page = new QWidget;
    auto* wrap = new QVBoxLayout(page);
    wrap->setContentsMargins(0, 0, 0, 0);
    wrap->addWidget(scroll);
    // 启动不发请求：天气 tab 首次显示时才自动查询默认城市
    {
        const QJsonArray arr = ls::loadJson("weather_cities.json").value("cities").toArray();
        if (!arr.isEmpty()) {
            page->installEventFilter(this);
            m_weatherPage = page;
            m_weatherDefault = arr[0].toString();
            m_weatherQuery = query;
        }
    }
    return page;
}

// ---------- 网速测试：Cloudflare 大文件下载采样，实时速度 + 最终 Mbps ----------
QWidget* MainWindow::buildSpeedTab() {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* body = new QWidget;
    auto* v = new QVBoxLayout(body);
    v->setContentsMargins(0, 4, 12, 0);
    v->setSpacing(12);

    QVBoxLayout* lay;
    auto* card = toolCard("网速测试（Cloudflare 测速节点 · 下载带宽）", lay);
    auto* tip = new QLabel("下载 50MB 测试文件采样，约 10 秒出结果。占用带宽，测速时别开下载任务。");
    tip->setObjectName("dim");
    tip->setWordWrap(true);
    lay->addWidget(tip);

    auto* big = new QLabel("--");
    big->setAlignment(Qt::AlignCenter);
    QFont bf = big->font();
    bf.setPixelSize(42);
    bf.setBold(true);
    big->setFont(bf);
    lay->addWidget(big);
    auto* live = new QLabel("");
    live->setObjectName("dim");
    live->setAlignment(Qt::AlignCenter);
    lay->addWidget(live);
    auto* bar = new QProgressBar;
    bar->setRange(0, 100);
    bar->setTextVisible(false);
    lay->addWidget(bar);

    auto* row = new QHBoxLayout;
    row->addStretch();
    auto* go = new QPushButton("开始测速");
    row->addWidget(go);
    row->addStretch();
    lay->addLayout(row);
    v->addWidget(card);
    v->addStretch();

    connect(go, &QPushButton::clicked, this, [this, big, live, bar, go] {
        if (!m_net) m_net = new QNetworkAccessManager(this);
        go->setEnabled(false);
        big->setText("测试中…");
        live->setText("连接测速节点…");
        bar->setValue(0);
        QElapsedTimer* clock = new QElapsedTimer;
        qint64* got = new qint64(0);
        clock->start();
        QNetworkRequest req(QUrl("https://speed.cloudflare.com/__down?bytes=50000000"));
        req.setTransferTimeout(0);
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        auto* reply = m_net->get(req);
        connect(reply, &QNetworkReply::downloadProgress, this, [big, live, bar, clock, got](qint64 g, qint64 total) {
            *got = g;
            const double secs = clock->elapsed() / 1000.0;
            if (secs > 0.2) {
                const double mbps = g * 8.0 / 1e6 / secs;
                big->setText(QString::number(mbps, 'f', 1) + " Mbps");
                live->setText(QString("已下载 %1 MB / %2 · 已用时 %3 秒")
                    .arg(QString::number(g / 1048576.0, 'f', 1))
                    .arg(total > 0 ? QString::number(total / 1048576.0, 'f', 0) + " MB" : "未知")
                    .arg(QString::number(secs, 'f', 1)));
            }
            if (total > 0) bar->setValue(int(g * 100 / total));
        });
        // 10 秒截断采样
        QTimer::singleShot(10000, reply, [reply] { if (reply->isRunning()) reply->abort(); });
        connect(reply, &QNetworkReply::finished, this, [this, reply, big, live, bar, go, clock, got] {
            reply->deleteLater();
            const double secs = clock->elapsed() / 1000.0;
            const qint64 total = *got;
            delete clock; delete got;
            go->setEnabled(true);
            bar->setValue(100);
            if (total < 100000) {
                big->setText("--");
                live->setText("测速失败：" + reply->errorString().left(80));
                return;
            }
            const double mbps = total * 8.0 / 1e6 / secs;
            big->setText(QString::number(mbps, 'f', 1) + " Mbps");
            live->setText(QString("测速完成 · 10 秒共下载 %1 MB · 约等于 %2 MB/s 的下载速度")
                .arg(QString::number(total / 1048576.0, 'f', 1))
                .arg(QString::number(mbps / 8.0, 'f', 2)));
            pet::playRemote("原地敲击桌面互动", QString("测速完成：%1 Mbps！").arg(QString::number(mbps, 'f', 0)));
            toast("网速测试", QString("下载带宽 %1 Mbps（%2 MB/s）").arg(QString::number(mbps, 'f', 1), QString::number(mbps / 8.0, 'f', 2)));
        });
    });

    scroll->setWidget(body);
    auto* page = new QWidget;
    auto* wrap = new QVBoxLayout(page);
    wrap->setContentsMargins(0, 0, 0, 0);
    wrap->addWidget(scroll);
    return page;
}

// ---------- 端口查看：netstat 解析（监听/连接 · PID 进程名） ----------
QWidget* MainWindow::buildPortTab() {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* body = new QWidget;
    auto* v = new QVBoxLayout(body);
    v->setContentsMargins(0, 4, 12, 0);
    v->setSpacing(12);

    QVBoxLayout* lay;
    auto* card = toolCard("端口查看（谁占着端口 · 只读）", lay);
    auto* row = new QHBoxLayout;
    auto* ckListen = new QCheckBox("只看监听中");
    ckListen->setChecked(true);
    auto* refresh = new QPushButton("刷 新");
    row->addWidget(ckListen);
    row->addStretch();
    row->addWidget(refresh);
    lay->addLayout(row);
    auto* state = new QLabel("");
    state->setObjectName("dim");
    lay->addWidget(state);
    auto* tree = new QTreeWidget;
    tree->setColumnCount(4);
    tree->setHeaderLabels({"协议", "本地地址:端口", "状态", "PID / 进程"});
    tree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    tree->setMinimumHeight(300);
    lay->addWidget(tree, 1);
    v->addWidget(card);
    v->addStretch();

    connect(refresh, &QPushButton::clicked, this, [this, tree, state, ckListen] {
        tree->clear();
        state->setText("读取中…");
        QApplication::processEvents();
        // PID → 进程名
        QHash<QString, QString> proc;
        QProcess tl;
        tl.start("tasklist", {"/fo", "csv", "/nh"});
        tl.waitForFinished(8000);
        const QString tlOut = QString::fromLocal8Bit(tl.readAllStandardOutput());
        QRegularExpression csvre("\"([^\"]+)\"(?:,\"([^\"]+)\")?");
        auto itc = csvre.globalMatch(tlOut);
        // csv 行格式："name","pid",... 每行前两个字段
        for (const QString& line : tlOut.split('\n')) {
            QStringList cols = line.split(',');
            if (cols.size() >= 2)
                proc[cols[1].remove('"').trimmed()] = cols[0].remove('"').trimmed();
        }
        QProcess ns;
        ns.start("netstat", {"-ano"});
        ns.waitForFinished(10000);
        int n = 0;
        for (const QString& line : QString::fromLocal8Bit(ns.readAllStandardOutput()).split('\n')) {
            const QStringList c = line.trimmed().split(QRegularExpression("\\s+"));
            if (c.size() < 5 || (c[0] != "TCP" && c[0] != "UDP")) continue;
            const QString proto = c[0], local = c[1], st2 = c.size() > 3 ? c[3] : QString(), pid = c.last();
            if (ckListen->isChecked() && proto == "TCP" && st2 != "LISTENING") continue;
            if (ckListen->isChecked() && proto == "UDP") {
                // UDP 无状态列，保留
            }
            auto* it = new QTreeWidgetItem({proto, local,
                proto == "UDP" ? "-" : st2,
                pid + "  " + proc.value(pid, "?")});
            tree->addTopLevelItem(it);
            ++n;
        }
        state->setText(QString("共 %1 条记录（进程名来自 tasklist，需权限的系统进程显示 ?）").arg(n));
    });

    scroll->setWidget(body);
    auto* page = new QWidget;
    auto* wrap = new QVBoxLayout(page);
    wrap->setContentsMargins(0, 0, 0, 0);
    wrap->addWidget(scroll);
    return page;
}

// ---------- 批量测活：URL 列表并发 HEAD，看状态码与耗时 ----------
QWidget* MainWindow::buildCheckTab() {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* body = new QWidget;
    auto* v = new QVBoxLayout(body);
    v->setContentsMargins(0, 4, 12, 0);
    v->setSpacing(12);

    QVBoxLayout* lay;
    auto* card = toolCard("批量网址测活（每行一个 URL · 并发检测）", lay);
    auto* urls = new QPlainTextEdit;
    urls->setPlaceholderText("https://www.baidu.com\nhttps://github.com\n…");
    urls->setFixedHeight(110);
    lay->addWidget(urls);
    auto* row = new QHBoxLayout;
    auto* go = new QPushButton("开始检测");
    row->addWidget(go);
    auto* state = new QLabel("");
    state->setObjectName("dim");
    row->addWidget(state, 1);
    lay->addLayout(row);
    auto* list = new QListWidget;
    list->setMinimumHeight(240);
    lay->addWidget(list, 1);
    v->addWidget(card);
    v->addStretch();

    connect(go, &QPushButton::clicked, this, [this, urls, list, state, go] {
        QStringList pending;
        for (const QString& l : urls->toPlainText().split('\n', Qt::SkipEmptyParts)) {
            QString u = l.trimmed();
            if (u.isEmpty()) continue;
            if (!u.startsWith("http")) u = "https://" + u;
            pending << u;
        }
        if (pending.isEmpty()) { state->setText("先填 URL 列表"); return; }
        if (pending.size() > 30) pending = pending.mid(0, 30);
        list->clear();
        go->setEnabled(false);
        state->setText(QString("检测 %1 个站点…").arg(pending.size()));
        if (!m_net) m_net = new QNetworkAccessManager(this);
        int* done = new int(0);
        const int total = pending.size();
        for (const QString& u : pending) {
            auto* it = new QListWidgetItem(u + "   检测中…");
            list->addItem(it);
            QNetworkRequest req{QUrl(u)};
            req.setTransferTimeout(6000);
            req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
            QElapsedTimer* tm = new QElapsedTimer;
            tm->start();
            auto* reply = m_net->head(req);
            connect(reply, &QNetworkReply::finished, this, [it, reply, tm, done, total, state, go] {
                reply->deleteLater();
                const qint64 ms = tm->elapsed();
                delete tm;
                const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                if (code > 0)
                    it->setText(QString("%1   ✓ %2   %3 ms").arg(it->text().section("   ", 0, 0)).arg(code).arg(ms));
                else
                    it->setText(QString("%1   ✗ %2").arg(it->text().section("   ", 0, 0), reply->errorString().left(40)));
                it->setForeground(code >= 200 && code < 400 ? QColor("#189d63") : QColor("#ff6f6f"));
                if (++*done >= total) {
                    delete done;
                    go->setEnabled(true);
                    state->setText("检测完成");
                }
            });
        }
    });

    scroll->setWidget(body);
    auto* page = new QWidget;
    auto* wrap = new QVBoxLayout(page);
    wrap->setContentsMargins(0, 0, 0, 0);
    wrap->addWidget(scroll);
    return page;
}

} // namespace tb
