#include "regdialog.h"
#include "ui_regdialog.h"
#include"httpmgr.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>

// 构造函数：初始化注册对话框
// 
// 功能：
//   1. 创建UI界面（用户名、密码、确认密码、邮箱、验证码输入框）
//   2. 设置窗口标题、大小
//   3. 设置样式（圆角、颜色等）
//   4. 连接信号和槽
//   5. 添加实时输入验证功能
//   6. 初始化HTTP处理器
RegDialog::RegDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::RegDialog)
{
    ui->setupUi(this);

    // 设置窗口属性
    setWindowTitle("注册");
    setFixedSize(720, 540);

    // 通用样式
    auto styleLine = "QLineEdit { border: 1px solid #aaa; border-radius: 5px; padding: 6px; }";
    auto styleErr  = "color:#d93025; font-size:13px;"; // Google风格红字

    // 左侧标签统一宽度
    const int labelWidth = 100;
    const int errH = 18; // 错误行固定高度

    // 工具：创建一行(标签+编辑框+可选控件)+下面的错误行（固定高度，不抖动）
    // lambda函数，用于快速创建带错误提示的输入字段
    auto makeField = [&](const QString& text,
                         QLineEdit*& editRef,
                         QLabel*& errRef,
                         QWidget* extra = nullptr) -> QWidget*
    {
        QWidget* group = new QWidget(this);
        QVBoxLayout* v = new QVBoxLayout(group);
        v->setContentsMargins(0,0,0,0);
        v->setSpacing(4);

        QHBoxLayout* h = new QHBoxLayout();
        h->setContentsMargins(0,0,0,0);
        QLabel* lab = new QLabel(text, group);
        lab->setFixedWidth(labelWidth);

        editRef = new QLineEdit(group);
        editRef->setFixedHeight(40);
        editRef->setStyleSheet(styleLine);

        h->addWidget(lab);
        h->addWidget(editRef);
        if (extra) h->addWidget(extra);

        errRef = new QLabel(group);
        errRef->setStyleSheet(styleErr);
        errRef->setWordWrap(true);
        errRef->setMinimumHeight(errH);
        errRef->setMaximumHeight(errH);
        errRef->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        errRef->setText(""); // 空文本但保留占位高度

        v->addLayout(h);
        v->addWidget(errRef);
        return group;
    };

    // ================== 创建控件 ==================
    codeBtn    = new TimerBtn("获取验证码");  // 带倒计时功能的验证码按钮
    confirmBtn = new QPushButton("确认");
    cancelBtn  = new QPushButton("取消");

    codeBtn->setFixedHeight(40);
    confirmBtn->setFixedHeight(44);
    cancelBtn->setFixedHeight(44);

    // 设置按钮样式
    confirmBtn->setStyleSheet("QPushButton { background-color: #28a745; color: white; border-radius: 12px; font-size: 18px;} QPushButton:hover { background-color: #218838; }");
    cancelBtn->setStyleSheet ("QPushButton { background-color: #dc3545; color: white; border-radius: 12px; font-size: 18px;} QPushButton:hover { background-color: #c82333; }");
    codeBtn->setStyleSheet   ("QPushButton { background-color: #0078d7; color: white; border-radius: 8px;  font-size: 16px;} QPushButton:hover { background-color: #005a9e; }");

    // 眼睛按钮（密码显示/隐藏）
    QToolButton* passEyeBtn = new QToolButton(this);
    passEyeBtn->setCheckable(true);
    passEyeBtn->setIcon(QIcon(":/image/eye-off.png"));
    passEyeBtn->setCursor(Qt::PointingHandCursor);

    QToolButton* confirmEyeBtn = new QToolButton(this);
    confirmEyeBtn->setCheckable(true);
    confirmEyeBtn->setIcon(QIcon(":/image/eye-off.png"));
    confirmEyeBtn->setCursor(Qt::PointingHandCursor);

    // ================== 创建主布局 ==================
    QVBoxLayout* main = new QVBoxLayout(this);
    main->setContentsMargins(20,20,20,20);
    main->setSpacing(12);

    // 添加各个输入字段（包含错误提示）
    main->addWidget(makeField("用户名:", userEdit,   userErrorLabel));
    main->addWidget(makeField("密码:",   passEdit,   passErrorLabel, passEyeBtn));
    passEdit->setEchoMode(QLineEdit::Password);  // 密码模式
    main->addWidget(makeField("确认密码:", confirmEdit, confirmErrorLabel, confirmEyeBtn));
    confirmEdit->setEchoMode(QLineEdit::Password);  // 密码模式
    main->addWidget(makeField("邮箱:",   emailEdit,  emailErrorLabel));
    main->addWidget(makeField("验证码:", codeEdit,   codeErrorLabel, codeBtn));

    // 按钮行
    main->addSpacing(12);
    QHBoxLayout* btns = new QHBoxLayout();
    btns->addStretch();
    btns->addWidget(confirmBtn);
    btns->addWidget(cancelBtn);
    main->addLayout(btns);

    // ================== 连接信号和槽 ==================
    connect(confirmBtn, &QPushButton::clicked, this, &RegDialog::onConfirmClicked);
    connect(cancelBtn,  &QPushButton::clicked, this, &RegDialog::onCancelClicked);
    connect(codeBtn,    &QPushButton::clicked, this, &RegDialog::onGetCodeClicked);

    // 连接HTTP响应信号
    connect(HttpMgr::GetInstance(), &HttpMgr::sig_reg_mod_finish,
            this, &RegDialog::slot_reg_mod_finish);

    // 密码显示/隐藏功能
    connect(passEyeBtn, &QToolButton::toggled, this, [=](bool checked){
        passEdit->setEchoMode(checked ? QLineEdit::Normal : QLineEdit::Password);
        passEyeBtn->setIcon(QIcon(checked ? ":/image/eye-off.png" : ":/image/eye.png"));
    });
    connect(confirmEyeBtn, &QToolButton::toggled, this, [=](bool checked){
        confirmEdit->setEchoMode(checked ? QLineEdit::Normal : QLineEdit::Password);
        confirmEyeBtn->setIcon(QIcon(checked ? ":/image/eye-off.png" : ":/image/eye.png"));
    });

    // ================== 实时输入验证 ==================
    // 用户名验证
    auto validateUser = [this]{ userErrorLabel->setText(userEdit->text().isEmpty() ? "用户名不能为空" : ""); };
    
    // 邮箱验证（使用正则表达式）
    auto validateEmail = [this]{
        static QRegularExpression re(R"(^[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}$)");
        const bool ok = re.match(emailEdit->text()).hasMatch();
        emailErrorLabel->setText(ok ? "" : "邮箱格式不正确");
    };
    
    // 密码验证
    auto validatePass = [this]{
        const QString p = passEdit->text();
        QString e;
        if (p.isEmpty()) e = "密码不能为空";
        else if (p.size() < 8) e = "密码至少 8 位";
        else {
            bool num=false, alpha=false;
            for (QChar c: p){ num |= c.isDigit(); alpha |= c.isLetter(); }
            if (!num || !alpha) e = "密码需包含字母与数字";
        }
        passErrorLabel->setText(e);
    };
    
    // 确认密码验证
    auto validateConfirm = [this]{
        const QString e = confirmEdit->text().isEmpty() ? "确认密码不能为空"
                                                        : (confirmEdit->text() != passEdit->text() ? "两次密码不一致" : "");
        confirmErrorLabel->setText(e);
    };
    
    // 验证码验证
    auto validateCode = [this]{ codeErrorLabel->setText(codeEdit->text().isEmpty() ? "验证码不能为空" : ""); };

    // 连接输入变化信号到验证函数
    connect(userEdit,    &QLineEdit::textChanged, this, [=]{ validateUser(); });
    connect(emailEdit,   &QLineEdit::textChanged, this, [=]{ validateEmail(); });
    connect(passEdit,    &QLineEdit::textChanged, this, [=]{ validatePass(); validateConfirm(); });
    connect(confirmEdit, &QLineEdit::textChanged, this, [=]{ validateConfirm(); });
    connect(codeEdit,    &QLineEdit::textChanged, this, [=]{ validateCode(); });

    qDebug() << "RegDialog constructed.";
    initHttpHandlers();
}


// 验证所有输入字段
// 
// 返回值：
//   全部有效返回true，否则返回false
// 
// 验证规则：
//   - 用户名：不能为空
//   - 密码：至少8位，必须包含字母和数字
//   - 确认密码：必须与密码一致
//   - 邮箱：必须符合邮箱格式
//   - 验证码：不能为空
bool RegDialog::validateAll()
{
    // 逐项校验并设置错误文本（不 return，最终用 ok 汇总）
    bool ok = true;

    // 验证用户名
    if (userEdit->text().isEmpty()) { userErrorLabel->setText("用户名不能为空"); ok = false; }
    else userErrorLabel->setText("");

    // 验证邮箱
    {
        static QRegularExpression re(R"(^[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}$)");
        if (!re.match(emailEdit->text()).hasMatch()) { emailErrorLabel->setText("邮箱格式不正确"); ok = false; }
        else emailErrorLabel->setText("");
    }

    // 验证密码
    {
        const QString p = passEdit->text();
        QString e;
        if (p.isEmpty()) e = "密码不能为空";
        else if (p.size() < 8) e = "密码至少 8 位";
        else {
            bool num=false, alpha=false;
            for (QChar c: p){ num |= c.isDigit(); alpha |= c.isLetter(); }
            if (!num || !alpha) e = "密码需包含字母与数字";
        }
        passErrorLabel->setText(e);
        if (!e.isEmpty()) ok = false;
    }

    // 验证确认密码
    {
        QString e;
        if (confirmEdit->text().isEmpty()) e = "确认密码不能为空";
        else if (confirmEdit->text() != passEdit->text()) e = "两次密码不一致";
        confirmErrorLabel->setText(e);
        if (!e.isEmpty()) ok = false;
    }

    // 验证验证码
    if (codeEdit->text().isEmpty()) { codeErrorLabel->setText("验证码不能为空"); ok = false; }
    else codeErrorLabel->setText("");

    return ok;
}


// 槽函数：处理取消按钮点击
// 
// 功能：
//   关闭对话框并拒绝（返回到登录界面）
void RegDialog::onCancelClicked() {
    reject();
}

// 槽函数：处理获取验证码按钮点击
// 
// 功能：
//   1. 验证邮箱格式
//   2. 发送验证码请求到GateServer
//   3. GateServer会通过VerifyServer发送验证码到用户邮箱
void RegDialog::onGetCodeClicked() {
    // 获取邮箱输入
    auto email = emailEdit->text();
    // 验证邮箱格式
    QRegularExpression regex(R"((\w+)(\.|_)?(\w*)@(\w+)(\.(\w+))+)");
    bool match = regex.match(email).hasMatch();

    qDebug() << "请求 URL:" << gate_url_prefix + "/get_verifycode";

    if(match){
        // 禁用按钮，防止重复点击
        codeBtn->setEnabled(false);
        // 发送HTTP验证码请求
        QJsonObject json_obj;
        json_obj["email"] = email;

        // 发送HTTP POST请求到GateServer
        // URL: http://gate_host:gate_port/get_verifycode
        // ReqId: ID_GET_VERTIFY_CODE
        // Module: REGISTERMOD
        HttpMgr::GetInstance()->PostHttpReq(QUrl(gate_url_prefix + "/get_verifycode"),
                                            json_obj, ReqId::ID_GET_VERTIFY_CODE, Modules::REGISTERMOD);

    } else {
        // 邮箱格式不正确，显示错误消息
        QMessageBox::information(this,"错误", "邮箱地址不正确");
        codeBtn->reset();   // 邮箱不合法 → 立即恢复按钮
    }
}

// 槽函数：处理注册模块HTTP响应
// 
// 参数：
//   - id: 请求ID
//   - res: 响应内容
//   - err: 错误码
// 
// 功能：
//   1. 检查网络错误
//   2. 解析JSON响应
//   3. 调用注册的处理器函数
void RegDialog::slot_reg_mod_finish(ReqId id, QString res, ErrorCodes err)
{
    qDebug() << "slot_reg_mod_finish called, id=" << id << "res=" << res;

    // 检查网络错误
    if(err != ErrorCodes::SUCCESS){
        QMessageBox::information(this,"错误", "网络请求错误");
        return;
    }

    // 解析JSON字符串
    QJsonDocument jsonDoc = QJsonDocument::fromJson(res.toUtf8());
    if(jsonDoc.isNull() || !jsonDoc.isObject()){
        QMessageBox::information(this,"错误", "json解析失败");
        return;
    }

    // 交给对应 handler
    _handlers[id](jsonDoc.object());
}

// 初始化HTTP处理器
// 
// 作用：
//   注册验证码和注册响应的处理逻辑
// 
// 处理内容：
//   1. ID_GET_VERTIFY_CODE：验证码请求响应
//      - 检查错误码
//      - 成功：显示提示，启动倒计时
//      - 失败：显示错误，恢复按钮
//   2. ID_REG_USER：注册用户请求响应
//      - 检查错误码
//      - 成功：显示成功消息，关闭对话框
//      - 失败：显示错误消息
void RegDialog::initHttpHandlers()
{
    // 注册验证码请求响应处理器
    _handlers.insert(ReqId::ID_GET_VERTIFY_CODE, [this](const QJsonObject& jsonObj){
        int error = jsonObj["error"].toInt();
        if(error != ErrorCodes::SUCCESS){
            // 验证码发送失败
            QMessageBox::information(this,"错误", "参数错误");
            codeBtn->reset();   // 服务端失败 → 恢复按钮
            return;
        }

        // 验证码发送成功
        auto email = jsonObj["email"].toString();
        QMessageBox::information(this,"提示", "验证码已发送到邮箱，注意查收");
        codeBtn->start();       // 服务端成功 → 开始倒计时
        qDebug()<<"user uuid is "<<jsonObj["uuid"].toString();
        qDebug()<<"email is "<<email;
    });

    // 注册用户请求响应处理器
    _handlers.insert(ReqId::ID_REG_USER, [this](QJsonObject jsonObj){
        int error = jsonObj["error"].toInt();
        if (error != ErrorCodes::SUCCESS) {
            // 注册失败
            QMessageBox::warning(this, tr("注册失败"), tr("参数错误"));
            return;
        }
        
        // 注册成功
        auto email = jsonObj["email"].toString();
        QMessageBox::information(this, tr("注册成功"), tr("用户注册成功，邮箱：") + email);
        qDebug() << "email is " << email;

        this->accept();  //  自动关闭注册对话框，返回到登录界面
    });
}

// 析构函数：清理资源
RegDialog::~RegDialog()
{
    delete ui;
}

// 槽函数：处理确认按钮点击
// 
// 功能：
//   1. 验证所有输入
//   2. 发送注册请求到GateServer
void RegDialog::onConfirmClicked()
{
    // 验证所有输入字段
    if (!validateAll()) return; // 同时显示所有错误

    // 全部通过，发起注册请求
    QJsonObject json_obj;
    json_obj["user"]      = userEdit->text();        // 用户名
    json_obj["email"]     = emailEdit->text();       // 邮箱
    json_obj["passwd"]    = passEdit->text();        // 密码
    json_obj["confirm"]   = confirmEdit->text();     // 确认密码
    json_obj["verifycode"]= codeEdit->text();       // 验证码

    QJsonDocument doc(json_obj);
    qDebug() << "[Qt] 注册请求 URL:" << gate_url_prefix + "/user_register";
    qDebug() << "[Qt] 注册请求体:" << doc.toJson(QJsonDocument::Compact);

    // 发送HTTP POST请求到GateServer
    // URL: http://gate_host:gate_port/user_register
    // ReqId: ID_REG_USER
    // Module: REGISTERMOD
    HttpMgr::GetInstance()->PostHttpReq(
        QUrl(gate_url_prefix + "/user_register"),
        json_obj,
        ReqId::ID_REG_USER,
        Modules::REGISTERMOD
        );
}
