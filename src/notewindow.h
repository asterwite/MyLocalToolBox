#pragma once
// 桌面便签：无边框置顶小窗，颜色可换，内容/位置去抖自动保存（notes.json 由 MainWindow 持有）
#include <QWidget>
#include <QFrame>
#include <QPlainTextEdit>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QMouseEvent>
#include <QEvent>
#include <QPainter>

class NoteWindow : public QWidget {
    Q_OBJECT
public:
    qint64 m_id;
    QString m_color = "yellow";
    bool m_pinned = true;
    QPlainTextEdit* m_edit = nullptr;
    QFrame* m_header = nullptr;
    QTimer m_dirty;
    QPoint m_dragOff;

    static QColor bg(const QString& c) {
        if (c == "green") return QColor(0xD8, 0xF2, 0xC8);
        if (c == "blue") return QColor(0xC8, 0xE4, 0xF7);
        if (c == "pink") return QColor(0xFB, 0xD9, 0xE4);
        return QColor(0xFF, 0xF3, 0xB8); // yellow
    }
    static QColor head(const QString& c) {
        if (c == "green") return QColor(0xA8, 0xD9, 0x92);
        if (c == "blue") return QColor(0x92, 0xC4, 0xEA);
        if (c == "pink") return QColor(0xF2, 0xA8, 0xC4);
        return QColor(0xF5, 0xDF, 0x7E); // yellow
    }

    NoteWindow(qint64 id, const QString& text, const QString& color,
               const QPoint& pos, bool pinned) : m_id(id), m_color(color), m_pinned(pinned) {
        setWindowFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint);
        setFixedSize(268, 244);
        m_dirty.setSingleShot(true);
        m_dirty.setInterval(700);
        connect(&m_dirty, &QTimer::timeout, this, &NoteWindow::changed);

        auto* v = new QVBoxLayout(this);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(0);
        auto* header = new QFrame;
        m_header = header;
        header->setFixedHeight(28);
        header->setCursor(Qt::SizeAllCursor);
        header->installEventFilter(this); // 标题栏拖动
        auto* h = new QHBoxLayout(header);
        h->setContentsMargins(10, 0, 4, 0);
        h->setSpacing(3);
        auto* title = new QLabel("便签");
        title->setStyleSheet("color:#5b4a12;font-size:12px;background:transparent;");
        h->addWidget(title);
        h->addStretch();
        const QStringList colors = {"yellow", "green", "blue", "pink"};
        for (const QString& c : colors) {
            auto* dot = new QPushButton;
            dot->setFixedSize(13, 13);
            dot->setCursor(Qt::PointingHandCursor);
            dot->setStyleSheet(QString("QPushButton{background:%1;border:1px solid rgba(0,0,0,.25);border-radius:7px;}"
                                       "QPushButton:hover{border:2px solid rgba(0,0,0,.5);}").arg(bg(c).name()));
            const QString cc = c;
            connect(dot, &QPushButton::clicked, this, [this, cc] { setColor(cc); });
            h->addWidget(dot);
        }
        auto* pin = new QPushButton("置");
        pin->setFixedSize(20, 20);
        pin->setCursor(Qt::PointingHandCursor);
        pin->setToolTip("切换置顶");
        connect(pin, &QPushButton::clicked, this, [this, pin] {
            m_pinned = !m_pinned;
            pin->setStyleSheet(m_pinned ? "font-size:11px;border:none;background:rgba(0,0,0,.18);border-radius:4px;color:#3d3208;"
                                        : "font-size:11px;border:none;background:transparent;color:#5b4a12;");
            setWindowFlag(Qt::WindowStaysOnTopHint, m_pinned);
            show(); // setWindowFlag 会隐藏窗口，重显
            emit changed();
        });
        pin->setStyleSheet(m_pinned ? "font-size:11px;border:none;background:rgba(0,0,0,.18);border-radius:4px;color:#3d3208;"
                                    : "font-size:11px;border:none;background:transparent;color:#5b4a12;");
        h->addWidget(pin);
        auto* close = new QPushButton("×");
        close->setFixedSize(20, 20);
        close->setCursor(Qt::PointingHandCursor);
        close->setToolTip("收起（内容已保存）");
        close->setStyleSheet("font-size:14px;border:none;background:transparent;color:#5b4a12;");
        connect(close, &QPushButton::clicked, this, [this] { emit changed(); hide(); });
        h->addWidget(close);
        v->addWidget(header);

        m_edit = new QPlainTextEdit;
        m_edit->setFrameShape(QFrame::NoFrame);
        m_edit->setPlainText(text);
        connect(m_edit, &QPlainTextEdit::textChanged, this, [this] { m_dirty.start(); });
        v->addWidget(m_edit, 1);

        applyColor();
        if (!pos.isNull()) move(pos);
    }

    void applyColor() {
        setStyleSheet(QString("NoteWindow{background:%1;} QPlainTextEdit{background:%1;color:#3a3a3a;font-size:13px;selection-background-color:rgba(0,0,0,.18);}")
                          .arg(bg(m_color).name()));
    }

    void setColor(const QString& c) {
        m_color = c;
        applyColor();
        m_dirty.start();
        update();
    }

    QString text() const { return m_edit ? m_edit->toPlainText() : QString(); }

signals:
    void changed(); // 内容/颜色/位置/置顶变化 → MainWindow 去重保存

protected:
    bool eventFilter(QObject* obj, QEvent* e) override {
        if (obj != m_header) return QWidget::eventFilter(obj, e);
        if (e->type() == QEvent::MouseButtonPress) {
            QMouseEvent* me = static_cast<QMouseEvent*>(e);
            m_dragOff = me->globalPosition().toPoint() - frameGeometry().topLeft();
            return true;
        }
        if (e->type() == QEvent::MouseMove && !m_dragOff.isNull()) {
            QMouseEvent* me = static_cast<QMouseEvent*>(e);
            move(me->globalPosition().toPoint() - m_dragOff);
            m_dirty.start();
            return true;
        }
        if (e->type() == QEvent::MouseButtonRelease) {
            m_dragOff = QPoint();
            return true;
        }
        return QWidget::eventFilter(obj, e);
    }

    void mouseReleaseEvent(QMouseEvent* e) override {
        m_dragOff = QPoint(); // 拖出窗外松手兜底
        QWidget::mouseReleaseEvent(e);
    }

    void paintEvent(QPaintEvent*) override {
        // header 底色
        QPainter p(this);
        p.fillRect(QRect(0, 0, width(), 28), head(m_color));
    }

    void closeEvent(QCloseEvent* e) override {
        emit changed();
        hide();
        e->ignore(); // 关闭=收起，数据由 MainWindow 管理
    }
};
