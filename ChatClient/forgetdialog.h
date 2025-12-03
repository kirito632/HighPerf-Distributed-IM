#ifndef FORGETDIALOG_H
#define FORGETDIALOG_H

#include <QDialog>
#include<QPushButton>
#include<QLabel>
#include<QLineEdit>
#include<QMap>
#include"timerbtn.h"
#include"global.h"

namespace Ui {
class ForgetDialog;
}

// ForgetDialog类：忘记密码对话框
// 
// 作用：
//   1. 提供重置密码界面
//   2. 处理密码重置逻辑
//   3. 获取验证码
//   4. 提交新密码到GateServer
// 
// 工作流程：
//   1. 用户输入邮箱
//   2. 点击"获取验证码"，发送验证码到邮箱
//   3. 输入验证码和新密码
//   4. 点击确认，发送重置密码请求到GateServer
//   5. GateServer验证成功，密码重置完成
//   6. 自动关闭对话框并返回到登录界面
// 
// 验证规则：
//   - 密码：至少8位，必须包含字母和数字
//   - 确认密码：必须与密码一致
//   - 邮箱：必须符合邮箱格式
//   - 验证码：不能为空
class ForgetDialog : public QDialog
{
    Q_OBJECT

public:
    // 构造函数：初始化忘记密码对话框
    explicit ForgetDialog(QWidget *parent = nullptr);
    
    // 析构函数：清理资源
    ~ForgetDialog();

private slots:
    // 槽函数：处理确认按钮点击
    // 
    // 功能：
    //   1. 验证所有输入
    //   2. 发送重置密码请求到GateServer
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

    // 槽函数：处理忘记密码模块HTTP响应
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
    void slot_forget_mod_finish(ReqId id, QString res, ErrorCodes err);

private:
    // 初始化HTTP处理器
    // 
    // 作用：
    //   注册验证码和重置密码响应的处理逻辑
    void initHttpHandlers();

    // UI对象指针
    Ui::ForgetDialog *ui;
    
    // HTTP响应处理器映射表
    // key: ReqId（请求ID）
    // value: 处理函数（接收JSON对象）
    QMap<ReqId, std::function<void(const QJsonObject&)>> _handlers;
    
    // UI控件
    QLineEdit *passEdit;            // 新密码输入框
    QLineEdit *confirmEdit;         // 确认密码输入框
    QLineEdit *emailEdit;           // 邮箱输入框
    QLineEdit *codeEdit;            // 验证码输入框
    QPushButton *confirmBtn;        // 确认按钮
    QPushButton *cancelBtn;         // 取消按钮
    TimerBtn *codeBtn;              // 获取验证码按钮（带倒计时功能）
    
    // 错误提示标签
    QLabel *passErrorLabel;         // 密码错误提示
    QLabel *confirmErrorLabel;      // 确认密码错误提示
    QLabel *emailErrorLabel;        // 邮箱错误提示
    QLabel *codeErrorLabel;         // 验证码错误提示
};

#endif // FORGETDIALOG_H
