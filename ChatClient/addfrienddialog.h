#ifndef ADDFRIENDDIALOG_H
#define ADDFRIENDDIALOG_H

#include <QDialog>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QListWidget>
#include <QListWidgetItem>
#include <QScrollArea>
#include <QTabWidget>
#include "global.h"
#include "friendmanager.h"

// 用户信息结构体
// 
// 作用：
//   存储用户的基本信息，用于搜索结果显示
struct UserInfo {
    int uid;              // 用户ID
    QString username;     // 用户名
    QString email;        // 邮箱
    QString avatarUrl;    // 头像URL
    bool isFriend;        // 是否已经是好友
    bool hasRequest;      // 是否已经发送过好友申请
};

// AddFriendDialog类：添加好友对话框
// 
// 作用：
//   1. 提供搜索用户界面
//   2. 搜索并显示匹配的用户列表
//   3. 支持发送好友申请
//   4. 显示搜索结果（与网络数据集成）
// 
// 工作流程：
//   1. 用户输入搜索关键词（用户名或邮箱）
//   2. 点击"查找"按钮
//   3. 通过FriendManager发送搜索请求到GateServer
//   4. 显示搜索结果列表
//   5. 点击"添加"按钮发送好友申请
class AddFriendDialog : public QDialog
{
    Q_OBJECT

public:
    // 构造函数：初始化添加好友对话框
    explicit AddFriendDialog(QWidget *parent = nullptr);
    
    // 析构函数
    ~AddFriendDialog() override = default;
    
    // 设置好友管理器
    // 参数：
    //   - manager: FriendManager指针，用于网络请求
    // 
    // 功能：
    //   1. 保存FriendManager引用
    //   2. 连接FriendManager的信号到本地槽函数
    void setFriendManager(FriendManager *manager);

signals:
    // 信号：好友申请已发送
    // 参数：
    //   - uid: 目标用户ID
    //   - username: 目标用户名
    void friendRequestSent(int uid, const QString &username);
    
    // 信号：好友申请被接受
    // 参数：
    //   - uid: 用户ID
    //   - username: 用户名
    void friendRequestAccepted(int uid, const QString &username);
    
    // 信号：好友申请被拒绝
    // 参数：
    //   - uid: 用户ID
    //   - username: 用户名
    void friendRequestRejected(int uid, const QString &username);

private slots:
    // 槽函数：处理查找按钮点击
    // 
    // 功能：
    //   1. 获取搜索关键词
    //   2. 如果有FriendManager，使用网络搜索
    //   3. 如果没有FriendManager，使用模拟数据搜索
    void onSearchClicked();
    
    // 槽函数：处理搜索文本变化
    // 
    // 功能：
    //   保存当前搜索文本
    void onSearchTextChanged();
    
    // 槽函数：处理添加好友按钮点击
    // 
    // 功能：
    //   显示好友申请已发送的消息
    void onAddFriendClicked();
    
    // 槽函数：处理搜索结果接收
    // 参数：
    //   - users: 搜索结果用户列表
    // 
    // 功能：
    //   将网络搜索结果填充到UI
    void onSearchResultsReceived(const QList<FriendUser> &users);
    
    // 槽函数：处理好友申请发送结果
    // 参数：
    //   - success: 是否成功
    // 
    // 功能：
    //   显示发送成功或失败的消息
    void onFriendRequestSent(bool success);
    
    // 槽函数：处理错误
    // 参数：
    //   - error: 错误信息
    // 
    // 功能：
    //   显示错误消息
    void onErrorOccurred(const QString &error);

private:
    // ========== UI初始化方法 ==========
    
    // 设置UI界面
    // 
    // 功能：
    //   初始化所有UI组件（搜索区域、标签页、搜索结果列表）
    void setupUi();
    
    // 设置搜索区域
    // 
    // 功能：
    //   创建搜索输入框和查找按钮
    void setupSearchArea();
    
    // 设置标签页
    // 
    // 功能：
    //   创建标签页控件（目前只有一个"搜索结果"标签）
    void setupTabWidget();
    
    // 设置搜索结果列表
    // 
    // 功能：
    //   创建搜索结果列表控件并设置样式
    void setupSearchResults();
    
    // ========== 辅助方法 ==========
    
    // 初始化模拟数据
    // 
    // 功能：
    //   创建一些模拟用户数据用于测试（如果没有网络连接）
    void initializeMockData();
    
    // 填充搜索结果（使用模拟数据）
    // 参数：
    //   - users: 用户列表
    // 
    // 功能：
    //   将用户列表数据填充到搜索结果列表中显示
    void populateSearchResults(const QVector<UserInfo> &users);
    
    // 填充搜索结果（使用网络数据）
    // 参数：
    //   - users: 来自网络的用户列表
    // 
    // 功能：
    //   将FriendUser列表转换为UI显示格式并填充到列表
    void populateSearchResultsFromNetwork(const QList<FriendUser> &users);
    
    // 生成头像
    // 参数：
    //   - text: 头像文字（通常是用户名的前两个字符）
    //   - bg: 背景颜色
    //   - size: 头像大小
    // 返回值：
    //   生成的圆形头像QPixmap
    QPixmap makeAvatar(const QString &text, const QColor &bg, int size = 40);
    
    // 清空搜索结果
    // 
    // 功能：
    //   删除搜索结果列表中的所有项
    void clearSearchResults();

private:
    // ========== 布局组件 ==========
    
    // 主布局（垂直布局）
    QVBoxLayout *m_mainLayout;
    
    // ========== 搜索区域组件 ==========
    
    // 搜索区域控件
    QWidget *m_searchWidget;
    // 搜索布局
    QHBoxLayout *m_searchLayout;
    // 搜索输入框
    QLineEdit *m_searchEdit;
    // 查找按钮
    QPushButton *m_searchBtn;
    
    // ========== 标签页组件 ==========
    
    // 标签页控件（搜索结果显示）
    QTabWidget *m_tabWidget;
    
    // ========== 搜索结果组件 ==========
    
    // 搜索结果列表控件
    QListWidget *m_searchResults;
    
    // ========== 状态数据 ==========
    
    // 当前搜索的关键词
    QString m_currentSearchText;
    
    // 模拟用户数据（用于测试）
    QVector<UserInfo> m_allUsers;
    
    // 好友管理器（用于发送网络请求）
    FriendManager *m_friendManager;
};

#endif // ADDFRIENDDIALOG_H
