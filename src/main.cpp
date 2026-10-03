// ToolBox Qt 客户端入口：启动过场动画 -> 主窗口淡入
#include "mainwindow.h"
#include "theme.h"
#include "bootsplash.h"
#include "petwindow.h"

#include <QApplication>
#include <QEventLoop>
#include <QIcon>
#include <QTimer>
#include <QElapsedTimer>
#include <algorithm>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    // 界面语言环境：日历/日期控件全部显示中文
    QLocale::setDefault(QLocale(QLocale::Chinese, QLocale::China));
    app.setApplicationName("我的工具箱");
    app.setOrganizationName("ToolBox");

    theme::load(); // 读取上次保存的主题色 / 背景设置
    app.setWindowIcon(QIcon(":/res/char_badge256.png"));

    // 启动过场：看板娘淡入，点击可跳过（参照 dsh-boot-animation 的过场形式）
    app.setQuitOnLastWindowClosed(false); // 过场关闭时主窗口尚未显示，避免触发"关最后窗口即退出"
    boot::BootSplash splash;
    QEventLoop splashLoop;
    splash.onFinished = [&splashLoop] { splashLoop.quit(); };
    splash.show();

    // 过场播放期间完成主窗口构建（联网预加载在后台异步进行）
    tb::MainWindow win;
    win.setWindowTitle("我的工具箱");
    win.resize(1180, 760);

    splashLoop.exec(); // 等过场结束

    // 主窗口淡入
    win.setWindowOpacity(0.0);
    win.show();
    QEventLoop fadeLoop;
    QElapsedTimer ft;
    ft.start();
    auto* ftimer = new QTimer(&win);
    QObject::connect(ftimer, &QTimer::timeout, [&] {
        double k = std::min(1.0, ft.elapsed() / 600.0);
        win.setWindowOpacity(k);
        if (k >= 1.0) fadeLoop.quit();
    });
    ftimer->start(16);
    fadeLoop.exec();

    // 桌面宠物：随主程序启动（关掉后可在「云养宠物」页重新召唤，下次启动显示与否可在菜单里关）
    QSettings pst("ToolBox", "ToolBoxQt");
    pet::PetWindow pet;
    pet.onShowMain = [&win] {
        win.showNormal();
        win.raise();
        win.activateWindow();
    };
    if (pst.value("pet/enabled", true).toBool()) pet.show();

    // 关闭主窗口 = 隐藏到托盘；真正退出走托盘菜单「退出」
    app.setQuitOnLastWindowClosed(false);
    return app.exec();
}
