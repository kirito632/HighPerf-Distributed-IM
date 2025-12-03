#include "logindialog.h"
#include "ui_logindialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include"httpmgr.h"
#include"tcpmgr.h"
#include"usermgr.h"
#include <QThread>

// 构造函数：初始化登录对话框
// 
// 功能：
//   1. 创建UI界面（邮箱输入框、密码输入框、登录/注册/忘记密码按钮）
//   2. 设置窗口标题、大小
//   3. 连接信号和槽
//   4. 初始化HTTP处理器
LoginDialog::LoginDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::LoginDialog)
{
    qDebug() << "[LoginDialog] this=" << this << " thread=" << QThread::currentThread();

    // 设置窗口属性
    setWindowTitle("登录");
    setFixedSize(600, 400);

    // 创建邮箱输入框
    QLabel *userLabel = new QLabel("邮箱:");
    userLabel->setFixedWidth(70);
    emailEdit = new QLineEdit;
    emailEdit->setPlaceholderText("请输入邮箱");
    emailEdit->setFixedHeight(36);

    // 创建密码输入框
    QLabel *passLabel = new QLabel("密码:");
    passLabel->setFixedWidth(70);
    passwordEdit = new QLineEdit;
    passwordEdit->setPlaceholderText("请输入密码");
    passwordEdit->setFixedHeight(36);
    passwordEdit->setEchoMode(QLineEdit::Password);  // 密码模式

    // 创建按钮
    loginBtn = new QPushButton("登录");
    registerBtn = new QPushButton("注册");
    forgotBtn = new QPushButton("忘记密码");

    // 设置样式
    QString lineEditStyle = "QLineEdit { border: 1px solid #aaa; border-radius: 5px; padding: 6px; }";
    emailEdit->setStyleSheet(lineEditStyle);
    passwordEdit->setStyleSheet(lineEditStyle);

    loginBtn->setFixedHeight(40);
    loginBtn->setStyleSheet("QPushButton { background-color: #0078d7; color: white; border-radius: 8px; padding: 6px; font-size: 32px;} QPushButton:hover { background-color: #005a9e; }");

    registerBtn->setStyleSheet("QPushButton { color: #0078d7; background: transparent; border: none; font-size: 26px;} QPushButton:hover { text-decoration: underline; }");
    forgotBtn->setStyleSheet("QPushButton { color: gray; background: transparent; border: none; font-size: 26px;} QPushButton:hover { text-decoration: underline; }");

    // 创建布局
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // 邮箱输入行
    QHBoxLayout *userLayout = new QHBoxLayout;
    userLayout->addWidget(userLabel);
    userLayout->addWidget(emailEdit);

    // 密码输入行
    QHBoxLayout *passLayout = new QHBoxLayout;
    passLayout->addWidget(passLabel);
    passLayout->addWidget(passwordEdit);

    // 注册和忘记密码按钮行
    QHBoxLayout *btnLayout = new QHBoxLayout;
    btnLayout->addWidget(registerBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(forgotBtn);

    // 添加到主布局
    mainLayout->addStretch();
    mainLayout->addLayout(userLayout);
    mainLayout->addLayout(passLayout);
    mainLayout->addSpacing(20);
    mainLayout->addWidget(loginBtn);
    mainLayout->addLayout(btnLayout);
    mainLayout->addStretch();

    // 连接按钮信号
    connect(loginBtn, &QPushButton::clicked, this, &LoginDialog::onLoginClicked);
    connect(registerBtn, &QPushButton::clicked, this, &LoginDialog::onRegisterClicked);
    connect(forgotBtn, &QPushButton::clicked, this, &LoginDialog::onForgotClicked);

    // 初始化HTTP处理器
    initHttpHandlers();
    
    // 连接登录回包信号
    connect(HttpMgr::GetInstance(), &HttpMgr::sig_login_mod_finish, this,
            &LoginDialog::slot_login_mod_finish);

    // 连接TCP连接请求的信号和槽函数
    // 当需要连接ChatServer时，发送信号给TcpMgr
    connect(this, &LoginDialog::sig_connect_tcp, TcpMgr::GetInstance(), &TcpMgr::slot_tcp_connect);
    
    // 连接TCP管理者发出的连接成功信号
    // 当TCP连接成功时，触发槽函数
    connect(TcpMgr::GetInstance(), &TcpMgr::sig_con_success, this, &LoginDialog::slot_tcp_con_finish);
    
    // 连接TCP管理者发出的登陆失败信号
    connect(TcpMgr::GetInstance(), &TcpMgr::sig_login_failed, this, &LoginDialog::slot_login_failed);

    // 连接TCP接收数据包的信号
    bool ok = connect(TcpMgr::GetInstance(), &TcpMgr::sig_recv_pkg,
            this, &LoginDialog::onTcpRecvPkg
            );
    qDebug() << "[LoginDialog] connect sig_recv_pkg -> onTcpRecvPkg returned" << ok;

    qDebug() << "[LoginDialog] this=" << this << " TcpMgr::GetInstance()=" << TcpMgr::GetInstance();
    // 添加调试lambda，监控所有收到的TCP数据包
    connect(TcpMgr::GetInstance(), &TcpMgr::sig_recv_pkg,
            this, [](ReqId id, const QString &body){
                qDebug() << "[lambda monitor] got sig_recv_pkg id=" << static_cast<int>(id)
                << " body_len=" << body.size() << " preview=" << body.left(80)
                << " thread=" << QThread::currentThread();
            }, Qt::QueuedConnection);

    // ui->setupUi(this);  // 如果使用.ui文件，取消注释
}

// 槽函数：处理登录按钮点击
// 
// 功能：
//   1. 验证用户输入（邮箱和密码）
//   2. 发送HTTP请求到GateServer进行登录
void LoginDialog::onLoginClicked() {
    loginBtn->setEnabled(false);
    qDebug()<<"login btn clicked";
    
    // 检查用户输入是否有效
    if(checkUserValid() == false){
        return;
    }
    if(checkPwdValid() == false){
        return ;
    }

    // 获取用户输入
    auto user = emailEdit->text().trimmed();
    auto pwd = passwordEdit->text().trimmed();
    
    // 发送HTTP请求到GateServer进行登录
    QJsonObject json_obj;
    json_obj["user"] = user;      // 用户名或邮箱
    json_obj["passwd"] = pwd;     // 密码
    
    // 发送HTTP POST请求
    // URL: http://gate_host:gate_port/user_login
    // ReqId: ID_LOGIN_USER
    // Module: LOGINMOD
    HttpMgr::GetInstance()->PostHttpReq(QUrl(gate_url_prefix+"/user_login"),
                                        json_obj, ReqId::ID_LOGIN_USER,Modules::LOGINMOD);
    qDebug() << "[Login] posting to" << gate_url_prefix + "/user_login" << "user=" << user;
}

// 槽函数：处理注册按钮点击
// 
// 功能：
//   发送信号请求打开注册对话框
void LoginDialog::onRegisterClicked() {
    emit registerRequested();
}

// 槽函数：处理忘记密码按钮点击
// 
// 功能：
//   打开忘记密码对话框
void LoginDialog::onForgotClicked()
{
    ForgetDialog dlg(this);   // 打开忘记密码对话框
    dlg.exec();               // 模态显示，等待用户操作完成
}

// 槽函数：处理登录模块HTTP响应
// 
// 参数：
//   - id: 请求ID
//   - res: 响应内容（JSON字符串）
//   - err: 错误码
// 
// 功能：
//   1. 检查网络错误
//   2. 解析JSON响应
//   3. 调用注册的处理器函数
void LoginDialog::slot_login_mod_finish(ReqId id, QString res, ErrorCodes err)
{
    // 检查网络错误
    if (err != ErrorCodes::SUCCESS) {
        QMessageBox::information(this, "错误", "网络请求错误");
        return;
    }

    // 解析JSON字符串
    QJsonParseError jerr;
    QJsonDocument jsonDoc = QJsonDocument::fromJson(res.toUtf8(), &jerr);
    if (jerr.error != QJsonParseError::NoError) {
        qDebug() << "Login: JSON parse error:" << jerr.errorString() << ", raw:" << res;
        QMessageBox::information(this, "错误", "json解析错误");
        return;
    }

    // 检查是否是JSON对象
    if (!jsonDoc.isObject()) {
        qDebug() << "Login: JSON is not an object, raw:" << res;
        QMessageBox::information(this, "错误", "返回数据格式不正确");
        return;
    }

    // 检查是否有对应的处理器
    if (!_handlers.contains(id)) {
        qDebug() << "Login: no handler for id" << id;
        return;
    }

    // 调用注册的处理器函数
    _handlers[id](jsonDoc.object());
}


// 初始化HTTP处理器
// 
// 作用：
//   注册登录响应的处理逻辑
// 
// 处理逻辑：
//   1. 从GateServer响应中提取错误码
//   2. 如果成功，获取ChatServer信息（host, port, token, uid）
//   3. 发送信号连接TCP服务器
void LoginDialog::initHttpHandlers()
{
    // 注册获取登录回包逻辑
    _handlers.insert(ReqId::ID_LOGIN_USER, [this](QJsonObject jsonObj){
        // 检查错误码
        int error = jsonObj["error"].toInt();
        if(error != ErrorCodes::SUCCESS){
            QMessageBox::warning(this, "错误", "参数错误");
            return;
        }
        
        // 获取用户信息
        auto user = jsonObj["user"].toString();

        // 从响应中提取ChatServer信息
        ServerInfo si;
        si.Uid = jsonObj["uid"].toInt();              // 用户ID
        si.Host = jsonObj["host"].toString();        // ChatServer主机
        si.Port = jsonObj["port"].toString();        // ChatServer端口
        si.Token = jsonObj["token"].toString();      // 认证令牌

        // 保存用户ID和令牌（用于后续TCP登录）
        _uid = si.Uid;
        _token = si.Token;
        
        // ⚠️ 关键修复：在HTTP登录成功时就设置UserMgr，确保主窗口打开时uid已有效
        // 这样即使TCP登录响应中没有uid字段，UserMgr也已经有了正确的uid
        UserMgr::GetInstance()->SetUid(si.Uid);
        if (jsonObj.contains("name")) {
            UserMgr::GetInstance()->SetName(jsonObj["name"].toString());
        }
        UserMgr::GetInstance()->SetToken(si.Token);
        
        qDebug() << "[LoginDialog] HTTP登录成功，已设置UserMgr - uid:" << si.Uid 
                 << " name:" << (jsonObj.contains("name") ? jsonObj["name"].toString() : "未提供");
        qDebug() << "user is " << user << " uid is " << si.Uid <<" host is "
                 << si.Host << " Port is " << si.Port << " Token is " << si.Token;
        
        // 发送信号连接TCP服务器（ChatServer）
        emit sig_connect_tcp(si);
    });
}

// 验证用户输入是否有效
// 
// 返回值：
//   有效返回true，否则返回false
// 
// 验证规则：
//   邮箱不能为空
bool LoginDialog::checkUserValid(){

    auto user = emailEdit->text().trimmed();
    if(user.isEmpty()){
        qDebug() << "User empty " ;
        return false;
    }

    return true;
}

// 验证密码是否有效
// 
// 返回值：
//   有效返回true，否则返回false
// 
// 验证规则：
//   密码长度必须在8-15之间
bool LoginDialog::checkPwdValid(){
    auto pwd = passwordEdit->text().trimmed();
    if(pwd.length() < 8 || pwd.length() > 15){
        qDebug() << "Pass length invalid";
        return false;
    }

    return true;
}

// 槽函数：处理TCP连接完成
// 
// 参数：
//   - bsuccess: 连接是否成功
// 
// 功能：
//   1. 如果连接成功，发送登录请求到ChatServer
//   2. 如果连接失败，显示错误消息
void LoginDialog::slot_tcp_con_finish(bool bsuccess)
{
    if(bsuccess){
        // 连接成功，构建登录请求
        QJsonObject jsonObj;
        jsonObj["uid"] = _uid;
        jsonObj["token"] = _token;

        QJsonDocument doc(jsonObj);
        QString jsonString = doc.toJson(QJsonDocument::Indented);

        // 发送TCP请求给ChatServer
        emit TcpMgr::GetInstance()->sig_send_data(ReqId::ID_CHAT_LOGIN, jsonString);
    }else{
        // 连接失败
        QMessageBox::warning(this, "错误", "网络错误");
    }
}

// 槽函数：处理登录失败
// 
// 参数：
//   - err: 错误码
// 
// 功能：
//   显示登录失败的错误消息
void LoginDialog::slot_login_failed(int err)
{
    QString result = QString("登录失败, err is %1")
                         .arg(err);
    QMessageBox::warning(this, "错误", "登录失败");
}

// 槽函数：处理TCP接收到的数据包
// 
// 参数：
//   - id: 消息ID
//   - body: 消息体内容
// 
// 功能：
//   1. 检查是否是登录响应消息
//   2. 解析响应内容
//   3. 如果成功，关闭登录对话框（accept）
//   4. 如果失败，显示错误消息
void LoginDialog::onTcpRecvPkg(ReqId id, const QString &body)
{
    qDebug() << "[LoginDialog] onTcpRecvPkg called, this=" << this << " thread=" << QThread::currentThread();
    qDebug() << "[LoginDialog] onTcpRecvPkg id=" << static_cast<int>(id) << " body=" << body;
    
    // 检查是否是登录响应消息
    if (id != ReqId::ID_CHAT_LOGIN_RSP) {
        // 不是 chat login response，忽略或其他处理
        qDebug() << "[LoginDialog] Ignoring non-chat-login response";
        return;
    }

    // 解析JSON响应
    QJsonParseError jerr;
    QJsonDocument doc = QJsonDocument::fromJson(body.toUtf8(), &jerr);
    if (jerr.error != QJsonParseError::NoError) {
        qDebug() << "onTcpRecvPkg json parse error:" << jerr.errorString();
        return;
    }
    if (!doc.isObject()) {
        qDebug() << "[LoginDialog] JSON is not an object, raw:" << body;
        return;
    }
    QJsonObject o = doc.object();

    // 获取错误码（支持 "error" 或 "err" 字段）
    int err = -1;
    if (o.contains("error")) err = o["error"].toInt();
    else if (o.contains("err")) err = o["err"].toInt();

    // 检查是否成功
    if (err == ErrorCodes::SUCCESS) {
        qDebug() << "Chat login success -> accept login dialog";
        // 使用 invokeMethod 保证线程安全地触发 UI accept()
        QMetaObject::invokeMethod(this, "accept", Qt::QueuedConnection);
    } else {
        // 登录失败，显示错误消息
        QString msg = o.value("msg").toString("聊天登录失败");
        qDebug() << "[LoginDialog] Chat login failed: " << msg;
        QMessageBox::warning(this, tr("错误"), msg);
    }
}

// 析构函数：清理资源
LoginDialog::~LoginDialog()
{
    delete ui;
}
