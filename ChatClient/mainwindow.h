#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTabWidget>
#include <QButtonGroup>
#include "sidebarbutton.h"
#include "chatdialog.h"
#include "friendmanager.h"
#include "usermgr.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

// 结构体：联系人信息
// 
// 作用：
//   存储联系人的基本信息，用于消息列表和好友列表
struct Contact {
    int uid;                // 联系人用户ID
    QString name;           // 联系人名称
    QString snippet;        // 消息摘要（最后一条消息预览）
    QString time;           // 最后消息时间
    QString avatarUrl;      // 头像URL
    int unread = 0;         // 未读消息数
    bool isMuted = false;   // 是否静音
};

// MainWindow类：主窗口
// 
// 作用：
//   1. 提供聊天系统的主界面
//   2. 显示消息列表、联系人列表
//   3. 管理聊天对话框的打开
//   4. 处理好友申请和好友列表
// 
// 界面组成：
//   - 头部区域：用户信息、搜索框
//   - 导航标签：消息、联系人切换
//   - 消息列表：显示与各个联系人的最后一条消息
//   - 联系人界面：显示好友申请和我的好友
//   - 底部导航栏：各种功能按钮
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    // 构造函数：初始化主窗口
    MainWindow(QWidget *parent = nullptr);
    
    // 析构函数：清理资源
    ~MainWindow();

private slots:
    // 槽函数：处理联系人选择
    // 参数：
    //   - row: 选中的行号
    // 
    // 功能：
    //   打开与选定联系人的聊天对话框
    void onContactSelected(int row);
    
    // 槽函数：处理搜索框内容变化
    // 参数：
    //   - text: 搜索关键词
    // 
    // 功能：
    //   过滤消息列表，只显示匹配的联系人
    void onSearchChanged(const QString &text);
    
    // 槽函数：处理标签页切换
    // 参数：
    //   - index: 切换到的标签页索引（0=消息，1=联系人）
    // 
    // 功能：
    //   切换显示消息界面或联系人界面
    void onTabChanged(int index);
    
    // 槽函数：处理侧边栏按钮点击
    // 参数：
    //   - button: 被点击的按钮
    // 
    // 功能：
    //   处理侧边栏导航按钮的点击事件
    void onSidebarButtonClicked(QAbstractButton *button);

private:
    // ========== UI初始化方法 ==========
    
    // 设置UI界面
    // 
    // 功能：
    //   初始化所有UI组件，包括头部、导航、消息列表、联系人等
    void setupUi();
    
    // 设置头部区域
    // 
    // 功能：
    //   创建头部UI（用户头像、搜索框等）
    void setupHeader();
    
    // 设置导航标签
    // 
    // 功能：
    //   创建"消息"和"联系人"标签页
    void setupNavigation();
    
    // 设置消息列表
    // 
    // 功能：
    //   创建消息列表并填充示例数据
    void setupMessageList();
    
    // 设置联系人界面
    // 
    // 功能：
    //   创建联系人界面，包括新朋友和我的好友列表
    void setupContactsWidget();
    
    // 设置底部导航栏
    // 
    // 功能：
    //   创建底部功能按钮栏
    void setupBottomBar();
    
    // ========== 数据填充方法 ==========
    
    // 填充联系人列表
    // 
    // 功能：
    //   向消息列表添加示例联系人数据
    void populateContacts();
    
    // 填充新朋友列表
    // 
    // 功能：
    //   向新朋友列表添加好友申请数据
    void populateNewFriends();
    
    // 填充我的好友列表
    // 
    // 功能：
    //   向我的好友列表添加好友数据
    void populateMyFriends();
    
    // ========== 辅助方法 ==========
    
    // 生成头像
    // 
    // 参数：
    //   - text: 头像文字（通常是用户名的前两个字符）
    //   - bg: 背景颜色
    //   - size: 头像大小（默认56）
    // 
    // 返回值：
    //   生成的圆形头像QPixmap
    // 
    // 功能：
    //   创建一个圆形的、带有文字的头像图片
    QPixmap makeAvatar(const QString &text, const QColor &bg, int size = 56);
    
    // 打开聊天对话框
    // 
    // 参数：
    //   - contactName: 联系人名称
    // 
    // 功能：
    //   创建或显示与指定联系人的聊天对话框
    void openChatDialog(const QString &contactName, int contactUid);
    
    // 处理添加好友按钮点击
    // 
    // 功能：
    //   打开添加好友对话框
    void onAddFriendClicked();
    
    // 处理接受好友申请
    // 
    // 参数：
    //   - uid: 好友ID
    //   - username: 好友用户名
    // 
    // 功能：
    //   接受好友申请，将其从"新朋友"移到"我的好友"
    void onAcceptFriendRequest(int uid, const QString &username);
    
    // 切换新朋友列表显示
    // 
    // 参数：
    //   - contactsList: 联系人列表控件
    //   - friendRequests: 好友申请数据
    // 
    // 功能：
    //   展开或折叠新朋友列表
    void toggleNewFriendsList(QListWidget *contactsList, const QVector<Contact> &friendRequests);
    
    // 切换我的好友列表显示
    // 
    // 参数：
    //   - contactsList: 联系人列表控件
    //   - myFriends: 我的好友数据
    // 
    // 功能：
    //   展开或折叠我的好友列表
    void toggleMyFriendsList(QListWidget *contactsList, const QVector<Contact> &myFriends);

private:
    // UI对象指针（由.ui文件生成）
    Ui::MainWindow *ui;
    
    // ========== 布局组件 ==========
    
    // 主中央部件
    QWidget *m_centralWidget;
    // 主布局（垂直布局）
    QVBoxLayout *m_mainLayout;
    
    // ========== 头部区域组件 ==========
    
    // 头部区域控件
    QWidget *m_headerWidget;
    // 用户头像标签
    QLabel *m_userAvatar;
    // 搜索输入框
    QLineEdit *m_searchEdit;
    
    // ========== 导航组件 ==========
    
    // 导航标签页控件（消息/联系人）
    QTabWidget *m_navTabs;
    
    // ========== 消息列表组件 ==========
    
    // 消息列表控件
    QListWidget *m_messageList;
    
    // ========== 联系人界面组件 ==========
    
    // 联系人界面控件
    QWidget *m_contactsWidget;
    // 联系人界面布局
    QVBoxLayout *m_contactsLayout;
    // 新朋友列表
    QListWidget *m_newFriendsList;
    // 我的好友列表
    QListWidget *m_myFriendsList;
    
    // ========== 侧边栏组件 ==========
    
    // 侧边栏控件
    QWidget *m_sidebar;
    // 侧边栏布局
    QVBoxLayout *m_sidebarLayout;
    // 消息按钮
    SidebarButton *m_btnMessages;
    // 联系人按钮
    SidebarButton *m_btnContacts;
    // 空间按钮
    SidebarButton *m_btnSpace;
    // 按钮组（用于互斥选择）
    QButtonGroup *m_navGroup;
    
    // ========== 底部导航栏组件 ==========
    
    // 底部导航栏控件
    QWidget *m_bottomBar;
    // 底部导航栏布局
    QHBoxLayout *m_bottomLayout;
    
    // ========== 功能组件 ==========
    
    // 聊天对话框
    ChatDialog *m_chatDialog;
    
    // 好友管理器（用于与GateServer通信）
    FriendManager *m_friendManager;
    
    // ========== 状态数据 ==========
    
    // 当前选中的联系人名称
    QString m_currentContact;

    // 统一联系人列表（包含“新朋友”和“我的好友”两段）
    QListWidget *m_contactsList;
    // 两个分组头在列表中的行号，便于在其下方插入/刷新数据
    int m_newFriendsHeaderRow = -1;
    int m_myFriendsHeaderRow = -1;

    // 将真实数据渲染到“新朋友”与“我的好友”分组
    void renderFriendRequests(const QList<FriendRequest> &requests);
    void renderMyFriends(const QList<FriendUser> &friends);
};

#endif // MAINWINDOW_H
