#pragma once
// Qt 客户端主题：主题色预设切换，背景色由主题色自动派生（同色系深色），
// 支持背景图片 + 浓度。存 QSettings("ToolBox","ToolBoxQt")，与 imgui 端设置独立。
#include <QColor>
#include <QString>
#include <QSettings>
#include <algorithm>

namespace theme {

inline QString& accentName() {
    static QString s = "蓝";
    return s;
}

inline QColor& accent() {
    static QColor c = QColor(0x4e, 0x9a, 0xff);
    return c;
}

inline QString& bgImagePath() {
    static QString s;
    return s;
}

inline double& bgOpacity() {
    static double v = 1.0;
    return v;
}

inline bool& bgAnimated() {
    static bool v = true;
    return v;
}

inline int& uiFontPx() {
    static int v = 13;
    return v;
}

inline const QColor* presets() {
    static const QColor p[] = {
        QColor(0x4e, 0x9a, 0xff), QColor(0x3d, 0xc9, 0x7e), QColor(0xff, 0x6f, 0xa0),
        QColor(0xff, 0x9a, 0x45), QColor(0xb0, 0x7d, 0x51), QColor(0x9b, 0x7b, 0xff),
        QColor(0x35, 0xc8, 0xd8), QColor(0xff, 0x5d, 0x5d),
    };
    return p;
}
inline const QString* presetNames() {
    static const QString n[] = {"蓝", "绿", "粉", "橙", "棕", "紫", "青", "红"};
    return n;
}
inline int presetCount() { return 8; }

inline void load() {
    QSettings st("ToolBox", "ToolBoxQt");
    int idx = st.value("theme/accent", 0).toInt();
    if (idx < 0 || idx >= presetCount()) idx = 0;
    accent() = presets()[idx];
    accentName() = presetNames()[idx];
    bgImagePath() = st.value("theme/bgImage", "").toString();
    bgOpacity() = st.value("theme/bgOpacity", 1.0).toDouble();
    bgAnimated() = st.value("theme/bgAnimated", true).toBool();
    uiFontPx() = st.value("theme/uiFontPx", 13).toInt();
    if (uiFontPx() < 11 || uiFontPx() > 18) uiFontPx() = 13;
}

inline void save() {
    QSettings st("ToolBox", "ToolBoxQt");
    int idx = 0;
    for (int i = 0; i < presetCount(); ++i)
        if (presets()[i] == accent()) { idx = i; break; }
    st.setValue("theme/accent", idx);
    st.setValue("theme/bgImage", bgImagePath());
    st.setValue("theme/bgAnimated", bgAnimated());
    st.setValue("theme/uiFontPx", uiFontPx());
    st.setValue("theme/bgOpacity", bgOpacity());
}

inline QColor derivedBg() {
    QColor a = accent();
    float h = 0, s = 0, v = 0;
    a.getHsvF(&h, &s, &v);
    return QColor::fromHsvF(a.hsvHueF(), std::min(0.30, std::max(0.10, a.hsvSaturationF() * 0.45)),
                            0.072);
}

// 浅色 QSS：白底玻璃 + 粉彩极光背景，主题色只用于强调
inline QString qss() {
    QString a = accent().name(QColor::HexRgb);
    QString a2 = accent().darker(135).name(QColor::HexRgb);
    QString t = R"(
* { background: transparent; color: #242838; font-family: "Microsoft YaHei"; font-size: %3px; selection-background-color: %1; }
QMainWindow, QWidget#root { background: transparent; }
QWidget#side { background: qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 rgba(255,255,255,.72), stop:1 rgba(255,255,255,.38)); border-right: 1px solid rgba(20,26,48,.08); }
QLabel#brand { font-size: 15px; font-weight: bold; }
QLabel#dim { color: #7c8294; font-size: 12px; }
QPushButton#navbtn { background: transparent; border: 0; padding: 0; border-radius: 11px; }
QFrame#card { background: rgba(255,255,255,.66); border: 1px solid rgba(20,26,48,.08); border-radius: 16px; }
QFrame#card:hover { background: rgba(255,255,255,.85); border-color: rgba(20,26,48,.12); }
QFrame#hero { background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 %1, stop:1 %2); border: 0; border-radius: 16px; }
QFrame#usercard { background: rgba(255,255,255,.60); border: 1px solid rgba(20,26,48,.10); border-radius: 12px; }
QFrame#usercard:hover { background: rgba(255,255,255,.85); border-color: rgba(20,26,48,.16); }
QFrame#hline { background: rgba(20,26,48,.08); border: 0; max-height: 1px; }
QFrame#bubbleMine { background: %1; border-radius: 14px; }
QFrame#bubbleOther { background: rgba(20,26,48,.06); border-radius: 14px; }
QLabel#heroTitle { font-size: 24px; font-weight: bold; color: #ffffff; }
QLabel#heroSub { color: rgba(255,255,255,.80); font-size: 12px; }
QLabel#author { color: %1; font-weight: bold; }
QLabel#postTitle { font-size: 15px; font-weight: bold; }
QLabel#postBody { color: #4a5064; }
QLabel#price { font-size: 22px; font-weight: bold; color: %1; }
QLabel#statNum { font-size: 22px; font-weight: bold; color: %1; }
QLabel#pill { background: %1; color: #fff; font-size: 11px; font-weight: bold; border-radius: 8px; padding: 2px 9px; }
QLabel#pillGhost { background: rgba(20,26,48,.07); color: #565c70; font-size: 11px; border-radius: 8px; padding: 2px 9px; }
QPushButton { background: %1; color: #fff; border: 0; border-radius: 10px; padding: 8px 18px; font-weight: 500; }
QPushButton:hover { background: %2; }
QPushButton:disabled { background: rgba(20,26,48,.08); color: #a2a8b8; }
QPushButton#ghost { background: rgba(20,26,48,.05); color: #242838; }
QPushButton#ghost:hover { background: rgba(20,26,48,.10); }
QPushButton#flat { background: transparent; color: #7c8294; padding: 2px 8px; font-weight: normal; }
QPushButton#flat:hover { color: %1; background: rgba(20,26,48,.04); }
QLineEdit, QTextEdit, QPlainTextEdit, QDateEdit, QTimeEdit, QDateTimeEdit, QSpinBox, QDoubleSpinBox { background: rgba(255,255,255,.80); border: 1px solid rgba(20,26,48,.10); border-radius: 10px; padding: 8px 12px; color: #242838; selection-background-color: %1; }
QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus, QDateEdit:focus, QTimeEdit:focus, QDateTimeEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus { border: 1px solid %1; background: #ffffff; }
QSpinBox::up-button, QSpinBox::down-button, QDateEdit::up-button, QDateEdit::down-button, QTimeEdit::up-button, QTimeEdit::down-button, QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { background: rgba(20,26,48,.05); border: 0; width: 18px; margin: 3px; border-radius: 4px; }
QSpinBox::up-button:hover, QSpinBox::down-button:hover, QDateEdit::up-button:hover, QDateEdit::down-button:hover, QTimeEdit::up-button:hover, QTimeEdit::down-button:hover, QDoubleSpinBox::up-button:hover, QDoubleSpinBox::down-button:hover { background: rgba(20,26,48,.12); }
QComboBox { background: rgba(255,255,255,.80); border: 1px solid rgba(20,26,48,.10); border-radius: 10px; padding: 8px 12px; color: #242838; }
QComboBox:focus { border: 1px solid %1; }
QComboBox::drop-down { border: 0; width: 26px; }
QComboBox QAbstractItemView { background: #ffffff; color: #242838; border: 1px solid rgba(20,26,48,.12); border-radius: 8px; selection-background-color: %1; selection-color: #ffffff; padding: 4px; }
QCalendarWidget QWidget { alternate-background-color: #eef1f9; }
QCalendarWidget QAbstractItemView { background: #ffffff; color: #242838; selection-background-color: %1; selection-color: #ffffff; }
QCalendarWidget QToolButton { background: transparent; color: #242838; border-radius: 6px; padding: 5px 10px; font-weight: bold; }
QCalendarWidget QToolButton:hover { background: rgba(20,26,48,.06); }
QCalendarWidget #qt_calendar_navigationbar { background: rgba(20,26,48,.04); }
QLineEdit:disabled, QTextEdit:disabled { color: #a2a8b8; }
QListWidget { background: rgba(255,255,255,.55); border: 1px solid rgba(20,26,48,.08); border-radius: 12px; }
QListWidget::item { padding: 9px 11px; border-radius: 8px; }
QListWidget::item:hover { background: rgba(20,26,48,.045); }
QListWidget::item:selected { background: rgba(20,26,48,.08); color: %1; }
QScrollArea { border: 0; background: transparent; }
QScrollBar:vertical { background: transparent; width: 8px; margin: 2px; }
QScrollBar::handle:vertical { background: rgba(20,26,48,.16); border-radius: 4px; min-height: 30px; }
QScrollBar::handle:vertical:hover { background: rgba(20,26,48,.28); }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar:horizontal { background: transparent; height: 8px; margin: 2px; }
QScrollBar::handle:horizontal { background: rgba(20,26,48,.16); border-radius: 4px; }
QSpinBox, QDoubleSpinBox { padding: 6px 10px; }
QSpinBox QLineEdit, QDoubleSpinBox QLineEdit { background: transparent; border: 0; padding: 0; margin: 0; }
QComboBox::down-arrow { image: url(:/res/arrow_down.png); width: 12px; height: 8px; }
QSpinBox::up-arrow, QDoubleSpinBox::up-arrow { image: url(:/res/arrow_up.png); width: 12px; height: 8px; }
QSpinBox::down-arrow, QDoubleSpinBox::down-arrow { image: url(:/res/arrow_down.png); width: 12px; height: 8px; }
QProgressBar { background: rgba(20,26,48,.08); border: 0; border-radius: 5px; max-height: 10px; text-align: center; color: transparent; }
QProgressBar::chunk { background: %1; border-radius: 5px; }
QPushButton#tabbtn { background: transparent; color: #7c8294; border-radius: 10px; padding: 7px 16px 7px 14px; font-weight: 500; border-left: 3px solid transparent; }
QPushButton#tabbtn:checked { background: rgba(20,26,48,.06); color: %1; border-left: 3px solid %1; font-weight: bold; }
QPushButton#tabbtn:hover { color: #242838; background: rgba(20,26,48,.035); }
QTreeWidget { background: rgba(255,255,255,.55); border: 1px solid rgba(20,26,48,.08); border-radius: 12px; }
QTreeWidget::item { padding: 6px; border-radius: 6px; }
QTreeWidget::item:hover { background: rgba(20,26,48,.045); }
QTreeWidget::item:selected { background: rgba(20,26,48,.08); color: %1; }
QHeaderView::section { background: transparent; color: #7c8294; border: 0; padding: 6px 8px; }
QDialog { background: #ffffff; }
QLabel#aboutVer { font-size: 22px; font-weight: bold; color: %1; }
QToolTip { background: #ffffff; color: #242838; border: 1px solid rgba(20,26,48,.14); border-radius: 6px; padding: 5px 9px; }
QMenu { background: #ffffff; color: #242838; border: 1px solid rgba(20,26,48,.12); border-radius: 10px; padding: 6px; }
QMenu::item { background: transparent; padding: 7px 26px 7px 14px; border-radius: 6px; }
QMenu::item:selected { background: rgba(20,26,48,.07); }
QMenu::item:disabled { color: #a2a8b8; }
QMenu::separator { height: 1px; background: rgba(20,26,48,.08); margin: 5px 8px; }
QMenu::icon { margin-left: 6px; }
QCheckBox { background: transparent; color: #242838; spacing: 7px; }
QCheckBox::indicator { width: 16px; height: 16px; border: 1px solid rgba(20,26,48,.25); border-radius: 4px; background: #ffffff; }
QCheckBox::indicator:checked { background: %1; border-color: %1; }
)";
    return t.arg(a, a2, QString::number(uiFontPx()));
}

} // namespace theme
