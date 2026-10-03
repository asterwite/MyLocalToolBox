// 网址安全检查：本地启发式风险评分（离线）+ 可选在线库核查（VirusTotal / URLhaus，用户自填免费 key）+ 本地黑名单
#include "mainwindow.h"
#include "tools_common.h"
#include "localstore.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QHostAddress>
#include <QHostInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QUrl>
#include <QVBoxLayout>

namespace tb {

namespace {
// 知名域名白名单（命中直接判可信）
const char* kTrustedHosts[] = {
    "baidu.com", "qq.com", "weixin.qq.com", "gtimg.com", "alipay.com", "taobao.com",
    "tmall.com", "jd.com", "163.com", "126.com", "bilibili.com", "zhihu.com",
    "douyin.com", "baiducontent.com", "microsoft.com", "windows.com", "office.com",
    "apple.com", "icloud.com", "google.com", "github.com", "steamcontent.com",
    "steampowered.com", "edu.cn", "gov.cn", "qcloud.com", "aliyun.com",
};
// 常见短链
const char* kShorteners[] = {
    "bit.ly", "t.cn", "t.co", "url.cn", "dwz.cn", "tinyurl.com", "is.gd",
    "cutt.ly", "suo.im", "mrw.so", "uuu9.com", "urlc.cn",
};
// 高风险 TLD
const char* kRiskyTlds[] = {
    "tk", "ml", "ga", "cf", "gq", "xyz", "top", "buzz", "click", "link", "rest", "cyou", "cam",
};
// 仿冒敏感词（出现在主机名里）
const char* kPhishWords[] = {
    "login", "signin", "verify", "secure", "account", "update", "wallet",
    "bank", "pay-", "-pay", "casts", "center-id", "id-confirm",
};

struct UrlRisk {
    int score = 0;
    QStringList reasons;
};

UrlRisk scoreUrl(const QUrl& u) {
    UrlRisk r;
    const QString host = u.host().toLower();
    const QString full = u.toString().toLower();
    if (host.isEmpty()) { r.reasons << "URL 不完整"; return r; }

    // 白名单
    for (const char* t : kTrustedHosts)
        if (host == t || host.endsWith("." + QString(t))) {
            r.score = 0; r.reasons << "知名域名（" + QString(t) + "）"; return r;
        }

    if (u.scheme() == "http") { r.score += 5; r.reasons << "使用明文 HTTP（无加密）+5"; }
    { // IP 直连（正规服务几乎不用裸 IP）
        QHostAddress ip; QString h = host;
        if (h.startsWith('[')) h = h.mid(1, h.size() - 2); // IPv6 字面量
        if (ip.setAddress(h) && !ip.isLoopback() && !ip.isPrivateUse() && !ip.isLinkLocal())
            { r.score += 20; r.reasons << "裸 IP 直连地址 +20"; }
    }
    if (u.userName().isEmpty() == false || full.contains('@')) { r.score += 15; r.reasons << "URL 中含 @ 用户信息伪装 +15"; }
    if (host.startsWith("xn--") || host.contains(".xn--")) { r.score += 20; r.reasons << "Punycode 国际化域名（可能同形字仿冒）+20"; }

    const QString tld = host.section('.', -1);
    for (const char* t : kRiskyTlds)
        if (tld == t) { r.score += 15; r.reasons << QString("高风险顶级域名 .%1 +15").arg(t); break; }

    for (const char* s : kShorteners)
        if (host == s || host.endsWith("." + QString(s))) { r.score += 10; r.reasons << "短链服务（真实目标被隐藏）+10"; break; }

    const QStringList parts = host.split('.');
    if (parts.size() >= 5) { r.score += 10; r.reasons << QString("子域名过多（%1 级）+10").arg(parts.size()); }

    for (const char* w : kPhishWords)
        if (host.contains(w)) { r.score += 15; r.reasons << QString("主机名含仿冒敏感词「%1」+15").arg(w); break; }

    const int dashes = host.count('-');
    if (dashes >= 3) { r.score += 10; r.reasons << QString("主机名连字符过多（%1 个）+10").arg(dashes); }

    const int port = u.port(-1);
    if (port != -1 && port != 80 && port != 443) { r.score += 10; r.reasons << QString("非标准端口 %1 +10").arg(port); }

    if (full.size() > 120) { r.score += 5; r.reasons << "URL 过长 +5"; }

    const QString path = u.path().toLower();
    if (path.endsWith(".exe") || path.endsWith(".scr") || path.endsWith(".apk") || path.endsWith(".msi")) {
        r.score += 25; r.reasons << "直接下载可执行文件 +25";
    } else if (path.contains(".exe.") || path.contains(".pdf.") || path.contains(".jpg.")) {
        r.score += 15; r.reasons << "双重扩展名伪装 +15";
    }
    return r;
}
} // namespace

QWidget* MainWindow::buildUrlSafetyTab() {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* body = new QWidget;
    auto* v = new QVBoxLayout(body);
    v->setContentsMargins(0, 4, 12, 0);
    v->setSpacing(12);

    QVBoxLayout* lay;
    auto* card = toolCard("网址安全检查（本地启发式评分 · 可选在线库核查）", lay);
    auto* tip = new QLabel("输入网址立即评分：IP 直连、仿冒词、短链、punycode 伪装、高风险域名等。"
                           "可选：填入 VirusTotal / URLhaus 的免费 API key 联网核查恶意库（不填则只用本地规则）。"
                           "结果仅供参考，涉及转账务必再用官方渠道核实。");
    tip->setObjectName("dim");
    tip->setWordWrap(true);
    lay->addWidget(tip);

    auto* row = new QHBoxLayout;
    auto* url = new QLineEdit;
    url->setPlaceholderText("粘贴或输入网址（回车检查）");
    row->addWidget(url, 1);
    auto* paste = new QPushButton("粘贴");
    paste->setObjectName("ghost");
    connect(paste, &QPushButton::clicked, url, [url] {
        const QString t = QApplication::clipboard()->text().trimmed();
        if (!t.isEmpty()) url->setText(t);
    });
    row->addWidget(paste);
    auto* go = new QPushButton("检 查");
    row->addWidget(go);
    lay->addLayout(row);

    auto* verdict = new QLabel("");
    verdict->setAlignment(Qt::AlignCenter);
    QFont vf = verdict->font();
    vf.setPixelSize(22);
    vf.setBold(true);
    verdict->setFont(vf);
    lay->addWidget(verdict);

    auto* reasons = new QLabel("");
    reasons->setObjectName("dim");
    reasons->setWordWrap(true);
    reasons->setTextInteractionFlags(Qt::TextSelectableByMouse);
    lay->addWidget(reasons, 1);

    // 本地黑名单（mal_urls.json：{"urls":["片段",...]}，命中片段即高危）
    auto inBlacklist = [](const QString& full) {
        for (const QJsonValue& v : ls::loadJson("mal_urls.json").value("urls").toArray())
            if (full.contains(v.toString(), Qt::CaseInsensitive)) return true;
        return false;
    };

    // 在线核查（可选 key）
    auto* onlineRow = new QHBoxLayout;
    onlineRow->addWidget(new QLabel("在线库"));
    auto* provider = new QComboBox;
    provider->addItems({"不使用", "VirusTotal", "URLhaus"});
    onlineRow->addWidget(provider);
    auto* keyEd = new QLineEdit;
    keyEd->setPlaceholderText("API key（可选，存本机）");
    keyEd->setEchoMode(QLineEdit::Password);
    keyEd->setFixedWidth(190);
    QSettings st("ToolBox", "ToolBoxQt");
    keyEd->setText(st.value("security/vtKey").toString());
    connect(keyEd, &QLineEdit::textChanged, this, [keyEd] {
        QSettings("ToolBox", "ToolBoxQt").setValue("security/vtKey", keyEd->text().trimmed());
    });
    onlineRow->addWidget(keyEd, 1);
    lay->addLayout(onlineRow);

    auto* onlineOut = new QLabel("");
    onlineOut->setObjectName("dim");
    onlineOut->setWordWrap(true);
    lay->addWidget(onlineOut);

    v->addWidget(card);
    v->addStretch();

    connect(go, &QPushButton::clicked, this, [=, this] {
        QString raw = url->text().trimmed();
        if (raw.isEmpty()) { verdict->setText("先输入网址"); return; }
        if (!raw.startsWith("http")) raw = "http://" + raw;
        const QUrl u(raw);
        if (!u.isValid() || u.host().isEmpty()) { verdict->setText("URL 不合法"); return; }

        UrlRisk r = scoreUrl(u);
        const bool black = inBlacklist(u.toString());
        if (black) { r.score += 60; r.reasons.prepend("命中本地黑名单 +60"); }

        QString vcolor, vtext;
        if (r.score >= 60)      { vcolor = "#c0392b"; vtext = "高风险"; }
        else if (r.score >= 30) { vcolor = "#c7781a"; vtext = "中风险"; }
        else                    { vcolor = "#189d63"; vtext = r.score == 0 && r.reasons.size() == 1 ? "可信" : "低风险"; }
        verdict->setText(QString("<span style='color:%1;'>%2 · 风险分 %3</span>")
                             .arg(vcolor, vtext, QString::number(r.score)));
        reasons->setText(r.reasons.isEmpty() ? "未触发任何规则" : r.reasons.join("\n"));

        // 在线核查
        const QString prov = provider->currentText();
        const QString key = keyEd->text().trimmed();
        if (prov == "不使用" || key.isEmpty()) {
            onlineOut->setText("在线核查未启用（选择服务并填入免费 key）");
            return;
        }
        onlineOut->setText("在线核查中…");
        if (!m_net) m_net = new QNetworkAccessManager(this);
        if (prov == "URLhaus") {
            QNetworkRequest req(QUrl("https://urlhaus-api.abuse.ch/v1/url/"));
            req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
            req.setTransferTimeout(10000);
            auto* reply = m_net->post(req, QByteArray("url=") + QUrl::toPercentEncoding(u.toString())
                                      + (key.isEmpty() ? QByteArray() : QByteArray("&auth-key=") + QUrl::toPercentEncoding(key)));
            connect(reply, &QNetworkReply::finished, this, [onlineOut, reply] {
                reply->deleteLater();
                const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
                const QString qs = o.value("query_status").toString();
                if (qs == "ok")
                    onlineOut->setText(QString("URLhaus：该网址在恶意库中（状态 %1，威胁 %2）——切勿访问！")
                        .arg(o.value("url_status").toString(), o.value("threat").toString()));
                else if (qs == "no_results")
                    onlineOut->setText("URLhaus：恶意库中无记录（不代表安全）");
                else
                    onlineOut->setText("URLhaus 查询失败：" + qs);
            });
        } else { // VirusTotal
            QNetworkRequest req(QUrl("https://www.virustotal.com/api/v3/urls"));
            req.setRawHeader("x-apikey", key.toUtf8());
            req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
            req.setTransferTimeout(12000);
            auto* reply = m_net->post(req, QByteArray("url=") + QUrl::toPercentEncoding(u.toString()));
            connect(reply, &QNetworkReply::finished, this, [onlineOut, reply] {
                reply->deleteLater();
                if (reply->error() != QNetworkReply::NoError) {
                    onlineOut->setText(QString("VirusTotal 查询失败（%1）：key 是否有效/免费额度是否用完（每分钟 4 次）")
                                           .arg(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()));
                    return;
                }
                const QString id = QJsonDocument::fromJson(reply->readAll()).object()
                                       .value("data").toObject().value("id").toString();
                if (id.isEmpty()) { onlineOut->setText("VirusTotal 返回异常"); return; }
                // 拿到分析 id 后需再查一次报告
                QNetworkRequest req2(QUrl("https://www.virustotal.com/api/v3/urls/" + id));
                req2.setRawHeader("x-apikey", QSettings("ToolBox", "ToolBoxQt").value("security/vtKey").toString().toUtf8());
                req2.setTransferTimeout(12000);
                QNetworkAccessManager* nm = new QNetworkAccessManager(onlineOut);
                auto* rep2 = nm->get(req2);
                QObject::connect(rep2, &QNetworkReply::finished, onlineOut, [onlineOut, rep2] {
                    rep2->deleteLater();
                    const QJsonObject attrs = QJsonDocument::fromJson(rep2->readAll()).object()
                        .value("data").toObject().value("attributes").toObject();
                    const QJsonObject stats = attrs.value("last_analysis_stats").toObject();
                    const int mal = stats.value("malicious").toInt();
                    const int sus = stats.value("suspicious").toInt();
                    const int und = stats.value("undetected").toInt();
                    const int harm = stats.value("harmless").toInt();
                    onlineOut->setText(QString("VirusTotal：%1 家引擎检出恶意（共 %2 家 · 无害 %3 · 可疑 %4）")
                        .arg(mal).arg(mal + sus + und + harm).arg(harm).arg(sus));
                });
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
