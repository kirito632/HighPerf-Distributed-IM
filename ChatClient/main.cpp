#include "mainwindow.h"

#include <QApplication>
#include "LoginDialog.h"
#include "RegDialog.h"
#include"global.h"
#include"chatdialog.h"
#include "tcpmgr.h"
#include "usermgr.h"

// 原来是string的size，将string append到block里，block是bytearray，导致发中文的时候可能出问题

// 主函数：程序入口
// 
// 功能：
//   1. 初始化Qt应用程序
//   2. 注册元类型（用于信号/槽跨线程通信）
//   3. 加载配置文件
//   4. 显示登录对话框
//   5. 登录成功后显示主界面
// 
// 工作流程：
//   main() -> 加载配置 -> 显示登录框 -> 登录成功 -> 显示主窗口
int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    
    // 注册枚举类型，以便 Qt 可以在 queued connections 中传递它
    // 这是在多线程环境中使用这些类型所必需的
    qRegisterMetaType<ReqId>("ReqId");
    qRegisterMetaType<ErrorCodes>("ErrorCodes");
    qRegisterMetaType<ServerInfo>("ServerInfo");

    // ---- 先加载配置 ----
    // 读取GateServer的配置信息
    QString fileName = "config.ini";
    QString app_path = QCoreApplication::applicationDirPath();
    QString config_path = QDir::toNativeSeparators(app_path + QDir::separator() + fileName);
    QSettings settings(config_path, QSettings::IniFormat);
    
    // 从配置文件读取GateServer的地址和端口
    QString gate_host = settings.value("GateServer/host").toString();
    QString gate_port = settings.value("GateServer/port").toString();

    // // 🔍 调试输出
    // qDebug() << "配置文件路径:" << config_path;
    // qDebug() << "host:" << gate_host << "port:" << gate_port;

    // 构建GateServer的URL前缀（用于HTTP请求）
    gate_url_prefix = "http://" + gate_host + ":" + gate_port;

    qDebug() << "[CONFIG] config_path =" << config_path;
    qDebug() << "[CONFIG] gate_host =" << gate_host << " gate_port =" << gate_port;
    qDebug() << "[CONFIG] gate_url_prefix =" << gate_url_prefix;

    // ------------ 调试快捷：仅在 Debug 构建允许 ---------
// #ifdef QT_DEBUG
//     // Debug模式下跳过登录，直接进入主界面（用于快速开发测试）
//     qDebug() << "DEBUG build: forcing skip login for local debug.";
//     UserMgr::GetInstance()->SetUid(1);
//     UserMgr::GetInstance()->SetName("devuser");
//     UserMgr::GetInstance()->SetToken("dev-token-debug");

//     MainWindow w;
//     w.show();
    
//     // 可选：直接打开一个聊天对话框进行测试
//     // ChatDialog chat;
//     // chat.setCurrentContact("测试用户");
//     // chat.show();
    
//     return a.exec();
// #endif


    // ---- 正常流程：显示登录对话框 ----
    LoginDialog login;   // 登录对话框
    RegDialog reg;       // 注册对话框

    // 连接登录对话框和注册对话框（当用户点击注册按钮时打开注册对话框）
    QObject::connect(&login, &LoginDialog::registerRequested, [&]() {
        reg.exec();
    });

    // 显示登录对话框
    // 如果用户点击OK（登录成功），显示主界面
    if (login.exec() == QDialog::Accepted) {
        MainWindow w;
        w.show();
        return a.exec();
    }

    // 如果用户取消登录，直接退出程序
    return 0;
}
