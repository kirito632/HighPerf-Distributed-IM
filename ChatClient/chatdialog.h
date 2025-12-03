#ifndef CHATDIALOG_H
#define CHATDIALOG_H

#include <QDialog>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTextEdit>
#include <QScrollArea>
#include <QPushButton>
#include <QLineEdit>
#include "chatbubble.h"

// ChatDialog类：聊天对话框
// 
// 作用：
//   1. 提供与指定联系人的聊天界面
//   2. 显示和发送文字消息、图片消息
//   3. 管理聊天记录（滚动到底部）
//   4. 提供附件、导出、文件助手等功能按钮（待实现）
// 
// 界面组成：
//   - 头部区域：显示联系人名称、关闭按钮
//   - 功能按钮区域：导出手机相册、打开文件助手、操作按钮
//   - 消息显示区域：可滚动的消息列表（使用ChatBubble显示）
//   - 输入区域：附件按钮、文本输入框、发送按钮
//   - 底部操作栏：剪切、文件夹等操作按钮
class ChatDialog : public QDialog
{
    Q_OBJECT
public:
    // 构造函数：初始化聊天对话框
    explicit ChatDialog(QWidget *parent = nullptr);
    
    // 析构函数
    ~ChatDialog() override = default;

    // ========== 公共接口 ==========
    
    // 设置当前聊天对象
    // 参数：
    //   - contactName: 联系人名称
    //   - contactUid: 联系人UID
    // 
    // 功能：
    //   更新对话框标题和头部标签为指定的联系人名称
    void setCurrentContact(const QString &contactName, int contactUid);
    
    // 获取当前联系人UID
    int getCurrentContactUid() const { return m_currentContactUid; }
    
    // 添加文字消息
    // 参数：
    //   - text: 消息内容
    //   - isSender: 是否为发送者（true=右对齐，false=左对齐）
    // 
    // 功能：
    //   创建ChatBubble并添加到消息列表，自动滚动到底部
    void addMessage(const QString &text, bool isSender);
    
    // 添加图片消息
    // 参数：
    //   - pix: 图片对象
    //   - isSender: 是否为发送者
    // 
    // 功能：
    //   创建ChatBubble并添加到消息列表，自动滚动到底部
    void addImageMessage(const QPixmap &pix, bool isSender);
    
    // 清空所有消息
    // 
    // 功能：
    //   删除消息列表中的所有消息气泡
    void clearMessages();

signals:
    // 信号：消息已发送
    // 参数：
    //   - toUser: 目标用户
    //   - text: 消息内容
    void messageSent(const QString &toUser, const QString &text);

private slots:
    // 槽函数：处理发送按钮点击
    // 
    // 功能：
    //   1. 获取输入框内容
    //   2. 添加到消息列表
    //   3. 发送messageSent信号
    //   4. 清空输入框
    void onSendClicked();
    
    // 槽函数：处理附件按钮点击
    void onAttachClicked();
    
    // 槽函数：处理导出按钮点击
    void onExportClicked();
    
    // 槽函数：处理文件助手按钮点击
    void onFileHelperClicked();

private:
    // ========== UI初始化方法 ==========
    
    // 设置UI界面
    // 
    // 功能：
    //   初始化所有UI组件（头部、消息区域、输入区域、底部栏）
    void setupUi();
    
    // 设置头部区域
    // 
    // 功能：
    //   创建标题和关闭按钮，使用蓝色渐变背景
    void setupHeader();
    
    // 设置消息显示区域
    // 
    // 功能：
    //   创建功能按钮区域、可滚动的消息容器、添加示例消息
    void setupMessageArea();
    
    // 设置输入区域
    // 
    // 功能：
    //   创建附件按钮、文本输入框、发送按钮
    void setupInputArea();
    
    // 设置底部操作栏
    // 
    // 功能：
    //   创建剪切、文件夹等操作按钮
    void setupBottomBar();
    
    // ========== 辅助方法 ==========
    
    // 滚动到底部
    // 
    // 功能：
    //   将消息列表滚动到最底部（显示最新消息）
    void scrollToBottom();
    
    // 添加示例消息
    // 
    // 功能：
    //   初始化时添加一些示例消息用于演示
    void addSampleMessages();
    
    // 生成头像
    // 
    // 参数：
    //   - text: 头像文字（通常是用户名的前两个字符）
    //   - bg: 背景颜色
    //   - size: 头像大小
    // 
    // 返回值：
    //   生成的圆形头像QPixmap
    QPixmap makeAvatar(const QString &text, const QColor &bg, int size = 40);

private:
    // ========== 布局组件 ==========
    
    // 主布局（垂直布局）
    QVBoxLayout *m_mainLayout;
    
    // ========== 头部区域组件 ==========
    
    // 头部区域控件
    QWidget *m_headerWidget;
    // 标题标签
    QLabel *m_titleLabel;
    // 关闭按钮
    QPushButton *m_closeButton;
    
    // ========== 功能按钮区域组件 ==========
    
    // 功能按钮区域控件
    QWidget *m_functionWidget;
    // 导出手机相册按钮
    QPushButton *m_exportButton;
    // 打开文件助手按钮
    QPushButton *m_fileHelperButton;
    // 打开按钮
    QPushButton *m_openButton;
    // 复制按钮
    QPushButton *m_copyButton;
    // 转发按钮
    QPushButton *m_forwardButton;
    
    // ========== 消息显示区域组件 ==========
    
    // 消息滚动区域
    QScrollArea *m_messageScrollArea;
    // 消息容器（包含所有消息气泡）
    QWidget *m_messageContainer;
    // 消息布局（垂直布局，包含stretch在底部）
    QVBoxLayout *m_messageLayout;
    
    // ========== 输入区域组件 ==========
    
    // 输入区域控件
    QWidget *m_inputWidget;
    // 输入布局
    QHBoxLayout *m_inputLayout;
    // 附件按钮
    QPushButton *m_attachButton;
    // 文本输入框
    QTextEdit *m_textEdit;
    // 发送按钮
    QPushButton *m_sendButton;
    
    // ========== 底部操作栏组件 ==========
    
    // 底部操作栏控件
    QWidget *m_bottomBar;
    // 底部布局
    QHBoxLayout *m_bottomLayout;
    // 剪切按钮
    QPushButton *m_cutButton;
    // 文件夹按钮
    QPushButton *m_folderButton;
    
    // ========== 状态数据 ==========
    
    // 当前聊天对象名称
    QString m_currentContact;
    // 当前联系人UID
    int m_currentContactUid;
};

#endif // CHATDIALOG_H
