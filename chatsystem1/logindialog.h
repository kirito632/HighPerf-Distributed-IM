#ifndef LOGINDIALOG_H
#define LOGINDIALOG_H
#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include"global.h"
#include"forgetdialog.h"


namespace Ui {
class LoginDialog;
}

// LoginDialog类：登录对话框
// 
// 作用：
//   1. 提供用户登录界面
//   2. 处理用户登录逻辑
//   3. 与GateServer通信获取ChatServer信息
//   4. 与ChatServer建立TCP连接并完成登录
// 
// 工作流程：
//   1. 用户输入邮箱和密码
//   2. 发送HTTP请求到GateServer进行登录验证
//   3. GateServer返回ChatServer信息（host, port, token）
//   4. 使用这些信息连接到ChatServer
//   5. 向ChatServer发送登录请求
//   6. 收到ChatServer的登录响应后关闭对话框
class LoginDialog : public QDialog
{
    Q_OBJECT

public:
    // 构造函数：初始化登录对话框
    explicit LoginDialog(QWidget *parent = nullptr);
    
    // 析构函数：清理资源
    ~LoginDialog();

signals:
    // 信号：请求注册对话框
    // 当用户点击注册按钮时触发
    void registerRequested();
    
    // 信号：登录成功
    // 参数：邮箱地址
    // 当成功完成所有登录步骤后触发
    void loginSuccess(QString email);
    
    // 信号：连接TCP服务器
    // 参数：ServerInfo - 服务器信息（包含host, port, token, uid）
    void sig_connect_tcp(ServerInfo si);

private slots:
    // 槽函数：处理登录按钮点击
    void onLoginClicked();
    
    // 槽函数：处理注册按钮点击
    void onRegisterClicked();
    
    // 槽函数：处理忘记密码按钮点击
    void onForgotClicked();
    
    // 槽函数：处理登录模块HTTP响应
    // 参数：
    //   - id: 请求ID
    //   - res: 响应内容
    //   - err: 错误码
    void slot_login_mod_finish(ReqId id, QString res, ErrorCodes err);
    
    // 槽函数：处理TCP连接完成
    // 参数：
    //   - bsuccess: 连接是否成功
    void slot_tcp_con_finish(bool bsuccess);
    
    // 槽函数：处理登录失败
    // 参数：
    //   - err: 错误码
    void slot_login_failed(int err);

    // 槽函数：处理TCP接收到的数据包
    // 参数：
    //   - id: 消息ID
    //   - body: 消息体内容
    void onTcpRecvPkg(ReqId id, const QString &body);

private:
    // 初始化HTTP处理器
    // 作用：注册登录响应的处理逻辑
    void initHttpHandlers();
    
    // UI对象指针
    Ui::LoginDialog *ui;
    
    // 验证用户输入是否有效
    bool checkUserValid();
    
    // 验证密码是否有效（长度8-15）
    bool checkPwdValid();

    // UI控件
    QLineEdit *emailEdit;      // 邮箱输入框
    QLineEdit *passwordEdit;    // 密码输入框
    QPushButton *loginBtn;      // 登录按钮
    QPushButton *registerBtn;   // 注册按钮
    QPushButton *forgotBtn;     // 忘记密码按钮
    
    // HTTP响应处理器映射表
    // key: ReqId（请求ID）
    // value: 处理函数（接收JSON对象）
    QMap<ReqId, std::function<void(const QJsonObject&)>> _handlers;
    
    // 从GateServer获取到的用户ID
    int _uid;
    
    // 从GateServer获取到的认证令牌
    QString _token;
};

#endif // LOGINDIALOG_H
