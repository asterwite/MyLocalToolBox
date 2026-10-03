#pragma once
// 工具箱各 tab 共用的小构件 helper（模块化拆分后由各 tools_*.cpp 引用）
#include <QFrame>
#include <QLabel>
#include <QVBoxLayout>

namespace tb {

// 统一卡片外壳：标题 + 内容布局
inline QFrame* toolCard(const QString& title, QVBoxLayout*& body) {
    QFrame* card = new QFrame;
    card->setObjectName("card");
    body = new QVBoxLayout(card);
    body->setContentsMargins(18, 14, 18, 14);
    body->setSpacing(7);
    auto* head = new QHBoxLayout;
    head->setSpacing(6);
    auto* dot = new QLabel;
    dot->setFixedSize(8, 8);
    dot->setStyleSheet("background:#4e9aff;border-radius:4px;");
    dot->setStyleSheet("background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #6fb3ff,stop:1 #3d7fe0);border-radius:4px;");
    head->addWidget(dot);
    QLabel* t = new QLabel(title);
    t->setObjectName("author");
    head->addWidget(t);
    head->addStretch();
    body->addLayout(head);
    return card;
}

} // namespace tb
