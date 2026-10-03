// 本地数据存储：exe 目录下 ToolBox/ 文件夹（便携便携，不写 C 盘/注册表）
// 文件：profile.json（积分/签到）· pet.json（云养状态，读时按流逝时间衰减）· pet_ui.json（桌宠窗口设置）
#pragma once
#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>

namespace ls {

inline QString dataDir() {
    QDir d(QCoreApplication::applicationDirPath() + "/ToolBox");
    if (!d.exists()) QDir().mkpath(d.absolutePath());
    return d.absolutePath();
}

inline QJsonObject loadJson(const QString& name) {
    QFile f(dataDir() + "/" + name);
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        f.close();
        return doc.object();
    }
    return {};
}

inline void saveJson(const QString& name, const QJsonObject& obj) {
    QFile f(dataDir() + "/" + name);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
}

// ---- 积分 / 签到（profile.json）----
inline QJsonObject profile() {
    QJsonObject p = loadJson("profile.json");
    if (!p.contains("points")) p.insert("points", 0);
    if (!p.contains("lastCheckin")) p.insert("lastCheckin", QString());
    if (!p.contains("streak")) p.insert("streak", 0);
    return p;
}
inline void saveProfile(const QJsonObject& p) { saveJson("profile.json", p); }

inline double clampStat(double v) { return std::max(0.0, std::min(100.0, std::round(v))); }

// ---- 云养宠物状态（pet.json）；读取即按流逝时间衰减并写回 ----
// 衰减速率（每小时）：饥饿 -4、口渴 -6、心情 -3、清醒精力 -3；睡觉精力 +8、消耗 ×0.4、心情 ×0.5
inline QJsonObject petState() {
    QJsonObject st = loadJson("pet.json");
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 updated = qint64(st.value("updated").toDouble(double(now)));
    const double hours = std::max(0.0, double(now - updated) / 3600000.0);
    const bool sleeping = st.value("sleeping").toBool();
    if (!st.contains("hunger")) st.insert("hunger", 70);
    if (!st.contains("thirst")) st.insert("thirst", 70);
    if (!st.contains("energy")) st.insert("energy", 80);
    if (!st.contains("mood")) st.insert("mood", 70);
    if (!st.contains("sleeping")) st.insert("sleeping", sleeping);
    if (hours > 0) {
        const double f = sleeping ? 0.4 : 1.0;
        st.insert("hunger", clampStat(st.value("hunger").toDouble(70) - 4 * hours * f));
        st.insert("thirst", clampStat(st.value("thirst").toDouble(70) - 6 * hours * f));
        st.insert("mood", clampStat(st.value("mood").toDouble(70) - 3 * hours * (sleeping ? 0.5 : 1.0)));
        st.insert("energy", clampStat(st.value("energy").toDouble(80) + (sleeping ? 8 : -3) * hours));
    }
    st.insert("updated", double(now));
    if (!st.contains("name")) st.insert("name", "谭少小楠娘");
    if (!st.value("bag").isObject()) st.insert("bag", QJsonObject());
    saveJson("pet.json", st); // 写回衰减检查点，否则下次读取 updated 缺失、衰减永不累积
    return st;
}
inline void savePetState(const QJsonObject& st) { saveJson("pet.json", st); }

// ---- 记账本（ledger.json，records 数组）----
inline QJsonArray ledgerRecords() {
    return loadJson("ledger.json").value("records").toArray();
}
inline void saveLedgerRecords(const QJsonArray& a) {
    QJsonObject o;
    o.insert("records", a);
    saveJson("ledger.json", o);
}

// ---- 自定义桌宠模型（custom_pet/：model.json + <clip>/NNN.webp）----
inline QString customPetDir() {
    QDir d(dataDir() + "/custom_pet");
    if (!d.exists()) QDir().mkpath(d.absolutePath());
    return d.absolutePath();
}
inline QJsonObject customModel() { return loadJson("custom_pet/model.json"); }
inline void saveCustomModel(const QJsonObject& o) { saveJson("custom_pet/model.json", o); }

// ---- 桌宠窗口设置（pet_ui.json，替代原注册表 QSettings）----
inline QJsonObject petUi() { return loadJson("pet_ui.json"); }
inline void savePetUi(const QJsonObject& o) { saveJson("pet_ui.json", o); }

} // namespace ls
