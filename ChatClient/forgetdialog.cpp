#include "forgetdialog.h"
#include "ui_forgetdialog.h"
#include"httpmgr.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include<QToolButton>

// 构造函数：初始化忘记密码对话框
// 
// 功能：
//   1. 创建UI界面（邮箱、验证码、新密码、确认密码输入框）
//   2. 设置窗口标题、大小
//   3. 设置样式（圆角、颜色等）
//   4. 连接信号和槽
//   5. 添加实时输入验证功能
//   6. 初始化HTTP处理器
ForgetDialog::ForgetDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::ForgetDialog)
{
    ui->setupUi(this);

    // 设置窗口属性
    setWindowTitle("重置密码");
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
    confirmBtn->setStyleSheet("QPushButton { background-color: #28a745; color: white; border-radius: 18px; font-size: 34px;} QPushButton:hover { background-color: #218838; }");
    cancelBtn->setStyleSheet ("QPushButton { background-color: #dc3545; color: white; border-radius: 18px; font-size: 34px;} QPushButton:hover { background-color: #c82333; }");
    codeBtn->setStyleSheet   ("QPushButton { background-color: #0078d7; color: white; border-radius: 14px;  font-size: 32px;} QPushButton:hover { background-color: #005a9e; }");

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
    connect(confirmBtn, &QPushButton::clicked, this, &ForgetDialog::onConfirmClicked);
    connect(cancelBtn,  &QPushButton::clicked, this, &ForgetDialog::onCancelClicked);
    connect(codeBtn,    &QPushButton::clicked, this, &ForgetDialog::onGetCodeClicked, Qt::UniqueConnection);

    // 连接HTTP响应信号
    connect(HttpMgr::GetInstance(), &HttpMgr::sig_forget_mod_finish,
            this, &ForgetDialog::slot_forget_mod_finish, Qt::UniqueConnection);

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
    connect(emailEdit,   &QLineEdit::textChanged, this, [=]{ validateEmail(); });
    connect(passEdit,    &QLineEdit::textChanged, this, [=]{ validatePass(); validateConfirm(); });
    connect(confirmEdit, &QLineEdit::textChanged, this, [=]{ validateConfirm(); });
    connect(codeEdit,    &QLineEdit::textChanged, this, [=]{ validateCode(); });

    qDebug() << "RegDialog constructed.";
    initHttpHandlers();
}

// 初始化HTTP处理器
// 
// 作用：
//   注册验证码和重置密码响应的处理逻辑
// 
// 处理内容：
//   1. ID_GET_VERTIFY_CODE：验证码请求响应
//      - 检查错误码
//      - 成功：显示提示，启动倒计时
//      - 失败：显示错误，恢复按钮
//   2. ID_FORGET_PASSWORD：重置密码请求响应
//      - 检查错误码
//      - 成功：显示成功消息，关闭对话框
//      - 失败：显示错误消息
void ForgetDialog::initHttpHandlers()
{
    // 注册验证码请求响应处理器
    _handlers.insert(ReqId::ID_GET_VERTIFY_CODE, [this](const QJsonObject& jsonObj) {
        int error = jsonObj["error"].toInt();
        if (error != ErrorCodes::SUCCESS) {
            // 验证码发送失败
            QMessageBox::warning(this, "错误", "获取验证码失败，请稍后再试");
            codeBtn->reset();   // 服务端失败 → 恢复按钮
            codeBtn->setEnabled(true);
            return;
        }

        // 验证码发送成功
        QMessageBox::information(this, "提示", "验证码已发送到邮箱，请注意查收");
        codeBtn->start();       // 服务端成功 → 开始倒计时
        codeBtn->setEnabled(false); // 由定时器或 start() 恢复
        qDebug() << "[ForgetDialog] email:" << jsonObj["email"].toString();
    });

    // 重置密码请求响应处理器
    _handlers.insert(ReqId::ID_FORGET_PASSWORD, [this](const QJsonObject& jsonObj) {
        int error = jsonObj["error"].toInt();
        if (error != ErrorCodes::SUCCESS) {
            // 重置密码失败
            QMessageBox::warning(this, "重置失败", "验证码错误或过期");
            return;
        }

        // 重置密码成功
        QMessageBox::information(this, "提示", "密码重置成功，请重新登录");
        this->accept(); // 关闭对话框，返回登录界面
    });
}

// 槽函数：处理忘记密码模块HTTP响应
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
void ForgetDialog::slot_forget_mod_finish(ReqId id, QString res, ErrorCodes err)
{
    qDebug() << "slot_forget_mod_finish called, id=" << id << "res=" << res;

    // 检查网络错误
    if (err != ErrorCodes::SUCCESS) {
        QMessageBox::warning(this, "错误", "网络请求错误");
        return;
    }

    // 解析JSON字符串
    QJsonDocument doc = QJsonDocument::fromJson(res.toUtf8());
    if (doc.isNull() || !doc.isObject()) {
        QMessageBox::warning(this, "错误", "返回数据解析失败");
        return;
    }

    // 调用对应的处理器
    if (_handlers.contains(id)) {
        _handlers[id](doc.object());
    }
}

// 槽函数：处理确认按钮点击
// 
// 功能：
//   1. 验证所有输入（邮箱、验证码、密码、确认密码）
//   2. 验证两次密码是否一致
//   3. 发送重置密码请求到GateServer
void ForgetDialog::onConfirmClicked()
{
    // 获取输入内容
    QString email = emailEdit->text().trimmed();
    QString code = codeEdit->text().trimmed();
    QString pass = passEdit->text().trimmed();
    QString confirm = confirmEdit->text().trimmed();

    // 验证必填字段
    if (email.isEmpty() || code.isEmpty() || pass.isEmpty() || confirm.isEmpty()) {
        QMessageBox::warning(this, "提示", "请完整填写信息");
        return;
    }
    
    // 验证两次密码是否一致
    if (pass != confirm) {
        QMessageBox::warning(this, "提示", "两次密码输入不一致");
        return;
    }

    // 构建请求数据
    QJsonObject obj;
    obj["email"] = email;
    obj["verifycode"] = code;
    obj["passwd"] = pass;

    // 构造完整 URL 并打印调试信息
    QString fullUrl = gate_url_prefix + "/reset_password";
    QUrl url = QUrl::fromUserInput(fullUrl); // 确保 QUrl 有效
    qDebug() << "reset url =" << fullUrl;
    qDebug() << "url.isValid()=" << url.isValid();

    // 发送HTTP POST请求到GateServer
    // URL: http://gate_host:gate_port/reset_password
    // ReqId: ID_FORGET_PASSWORD
    // Module: FORGETMOD
    HttpMgr::GetInstance()->PostHttpReq(
        url,                          // 使用局部变量 url，保证不会为空
        obj,
        ReqId::ID_FORGET_PASSWORD,
        Modules::FORGETMOD
        );
}

// 槽函数：处理获取验证码按钮点击
// 
// 功能：
//   1. 验证邮箱格式
//   2. 禁用按钮防止重复点击
//   3. 发送验证码请求到GateServer
//   4. GateServer会通过VerifyServer发送验证码到用户邮箱
void ForgetDialog::onGetCodeClicked() {
    qDebug() << "[ForgetDialog] onGetCodeClicked, this=" << this;

    // 获取邮箱输入
    auto email = emailEdit->text();
    // 验证邮箱格式
    QRegularExpression regex(R"((\w+)(\.|_)?(\w*)@(\w+)(\.(\w+))+)");
    bool match = regex.match(email).hasMatch();

    qDebug() << "[ForgetDialog] 请求 URL:" << gate_url_prefix + "/get_verifycode";
    qDebug() << "gate_url_prefix=" << gate_url_prefix;

    // 立刻禁用按钮防止重复点击
    codeBtn->setEnabled(false);

    if (match) {
        // 发送HTTP验证码请求
        QJsonObject json_obj;
        json_obj["email"] = email;

        // 发送HTTP POST请求到GateServer
        // URL: http://gate_host:gate_port/get_verifycode
        // ReqId: ID_GET_VERTIFY_CODE
        // Module: FORGETMOD
        HttpMgr::GetInstance()->PostHttpReq(
            QUrl(gate_url_prefix + "/get_verifycode"),
            json_obj,
            ReqId::ID_GET_VERTIFY_CODE,
            Modules::FORGETMOD
            );

    } else {
        // 邮箱格式不正确，显示错误消息并恢复按钮
        QMessageBox::information(this, "错误", "邮箱地址不正确");
        codeBtn->reset();   // 邮箱不合法 → 立即恢复按钮
    }
}

// 槽函数：处理取消按钮点击
// 
// 功能：
//   关闭对话框并拒绝（返回到登录界面）
void ForgetDialog::onCancelClicked()
{
    this->reject(); // 关闭窗口并返回
}

// 析构函数：清理资源
ForgetDialog::~ForgetDialog()
{
    delete ui;
}
