#ifndef REGDIALOG_H
#define REGDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include<QToolButton>
#include"global.h"
#include"timerbtn.h"


namespace Ui {
class RegDialog;
}

// RegDialog类：注册对话框
// 
// 作用：
//   1. 提供用户注册界面
//   2. 处理用户注册逻辑
//   3. 获取邮箱验证码
//   4. 提交注册信息到GateServer
// 
// 工作流程：
//   1. 用户填写用户名、密码、确认密码、邮箱
//   2. 用户点击"获取验证码"，发送验证码到邮箱
//   3. 用户输入验证码
//   4. 用户点击确认，发送注册请求到GateServer
//   5. GateServer验证成功，返回注册成功
//   6. 自动关闭对话框并返回到登录界面
// 
// 验证规则：
//   - 用户名：不能为空
//   - 密码：至少8位，必须包含字母和数字
//   - 确认密码：必须与密码一致
//   - 邮箱：必须符合邮箱格式
//   - 验证码：不能为空
class RegDialog : public QDialog
{
    Q_OBJECT

public:
    // 构造函数：初始化注册对话框
    explicit RegDialog(QWidget *parent = nullptr);
    
    // 析构函数：清理资源
    ~RegDialog();
    
    // 验证所有输入字段
    // 
    // 返回值：
    //   全部有效返回true，否则返回false
    // 
    // 功能：
    //   检查用户名、邮箱、密码、确认密码、验证码是否全部有效
    bool validateAll();

private slots:
    // 槽函数：处理确认按钮点击
    // 
    // 功能：
    //   1. 验证所有输入
    //   2. 发送注册请求到GateServer
    void onConfirmClicked();
    
    // 槽函数：处理取消按钮点击
    // 
    // 功能：
    //   关闭对话框并拒绝（返回登录界面）
    void onCancelClicked();
    
    // 槽函数：处理获取验证码按钮点击
    // 
    // 功能：
    //   1. 验证邮箱格式
    //   2. 发送验证码请求到GateServer
    void onGetCodeClicked();

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
    void slot_reg_mod_finish(ReqId id, QString res, ErrorCodes err);

private:
    // 初始化HTTP处理器
    // 
    // 作用：
    //   注册验证码和注册响应的处理逻辑
    void initHttpHandlers();

    // HTTP响应处理器映射表
    // key: ReqId（请求ID）
    // value: 处理函数（接收JSON对象）
    QMap<ReqId, std::function<void(const QJsonObject&)>> _handlers;

    // UI对象指针
    Ui::RegDialog *ui;

    // UI控件
    QLineEdit *userEdit;           // 用户名输入框
    QLineEdit *passEdit;           // 密码输入框
    QLineEdit *confirmEdit;        // 确认密码输入框
    QLineEdit *emailEdit;          // 邮箱输入框
    QLineEdit *codeEdit;           // 验证码输入框
    QPushButton *confirmBtn;       // 确认按钮
    QPushButton *cancelBtn;        // 取消按钮
    TimerBtn *codeBtn;             // 获取验证码按钮（带倒计时功能）
    
    // 错误提示标签
    QLabel *userErrorLabel;        // 用户名错误提示
    QLabel *passErrorLabel;        // 密码错误提示
    QLabel *confirmErrorLabel;      // 确认密码错误提示
    QLabel *emailErrorLabel;       // 邮箱错误提示
    QLabel *codeErrorLabel;        // 验证码错误提示
};

#endif // REGDIALOG_H
