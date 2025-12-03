#ifndef FRIENDMANAGER_H
#define FRIENDMANAGER_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>

// 结构体：好友用户信息
// 
// 作用：
//   存储用户的基本信息，用于好友列表、搜索结果等
struct FriendUser {
    int uid;          // 用户ID
    QString name;     // 用户名
    QString email;    // 邮箱
    QString nick;     // 昵称
    QString icon;     // 头像URL
    int sex;          // 性别（0:未知, 1:男, 2:女）
    QString desc;     // 个人描述
    bool isFriend;    // 是否已是好友
};

// 结构体：好友申请信息
// 
// 作用：
//   存储好友申请的详细信息，用于好友申请列表
struct FriendRequest {
    int uid;          // 申请者用户ID
    QString name;     // 申请者用户名
    QString desc;     // 申请描述
    QString icon;     // 申请者头像
    QString nick;     // 申请者昵称
    int sex;          // 申请者性别
    int status;       // 申请状态（0:待处理, 1:已同意, 2:已拒绝）
};

// FriendManager类：好友管理器
// 
// 作用：
//   1. 管理所有好友相关的网络请求
//   2. 与GateServer进行HTTP通信
//   3. 处理搜索、申请、回复等操作
// 
// 设计模式：
//   QObject - 使用信号/槽机制进行异步通信
// 
// 主要功能：
//   - 搜索用户：根据关键词搜索用户
//   - 发送好友申请：向指定用户发送好友申请
//   - 获取好友申请列表：获取收到的好友申请
//   - 回复好友申请：同意或拒绝好友申请
//   - 获取我的好友列表：获取当前用户的好友列表
class FriendManager : public QObject
{
    Q_OBJECT

public:
    // 构造函数：初始化好友管理器
    explicit FriendManager(QObject *parent = nullptr);
    
    // 析构函数：清理资源
    ~FriendManager();

    // ========== 配置方法 ==========
    
    // 设置当前用户ID
    // 参数：
    //   - uid: 当前用户的ID
    void setCurrentUser(int uid);
    
    // 设置服务器地址
    // 参数：
    //   - url: GateServer的URL（例如：http://localhost:8080）
    void setServerUrl(const QString &url);

    // ========== 好友操作 ==========
    
    // 搜索好友
    // 参数：
    //   - keyword: 搜索关键词（用户名或邮箱）
    // 
    // 功能：
    //   发送HTTP请求到GateServer搜索用户
    //   收到响应后发送 searchResultsReceived 信号
    void searchFriends(const QString &keyword);
    
    // 获取好友申请列表
    // 
    // 功能：
    //   发送HTTP请求获取当前用户收到的好友申请
    //   收到响应后发送 friendRequestsReceived 信号
    void getFriendRequests();
    
    // 获取我的好友列表
    // 
    // 功能：
    //   发送HTTP请求获取当前用户的好友列表
    //   收到响应后发送 myFriendsReceived 信号
    void getMyFriends();
    
    // 发送好友申请
    // 参数：
    //   - toUid: 目标用户ID
    //   - desc: 申请描述（可选）
    // 
    // 功能：
    //   发送HTTP请求向指定用户发送好友申请
    //   收到响应后发送 friendRequestSent 信号
    void sendFriendRequest(int toUid, const QString &desc = "");
    
    // 回复好友申请
    // 参数：
    //   - fromUid: 申请者用户ID
    //   - agree: 是否同意（true=同意, false=拒绝）
    // 
    // 功能：
    //   发送HTTP请求同意或拒绝好友申请
    //   收到响应后发送 friendRequestReplied 信号
    void replyFriendRequest(int fromUid, bool agree);

signals:
    // 信号：搜索用户结果已收到
    // 参数：
    //   - users: 搜索到的用户列表
    void searchResultsReceived(const QList<FriendUser> &users);
    
    // 信号：好友申请列表已收到
    // 参数：
    //   - requests: 好友申请列表
    void friendRequestsReceived(const QList<FriendRequest> &requests);
    
    // 信号：我的好友列表已收到
    // 参数：
    //   - friends: 好友列表
    void myFriendsReceived(const QList<FriendUser> &friends);
    
    // 信号：好友申请已发送
    // 参数：
    //   - success: 是否成功
    void friendRequestSent(bool success);
    
    // 信号：好友申请已回复
    // 参数：
    //   - success: 是否成功
    void friendRequestReplied(bool success);
    
    // 信号：发生错误
    // 参数：
    //   - error: 错误信息
    void errorOccurred(const QString &error);

private slots:
    // 槽函数：处理搜索用户响应
    void onSearchReplyFinished();
    
    // 槽函数：处理好友申请列表响应
    void onFriendRequestsReplyFinished();
    
    // 槽函数：处理我的好友列表响应
    void onMyFriendsReplyFinished();
    
    // 槽函数：处理发送好友申请响应
    void onSendRequestReplyFinished();
    
    // 槽函数：处理回复好友申请响应
    void onReplyRequestReplyFinished();

private:
    // 网络管理器（用于发送HTTP请求）
    QNetworkAccessManager *m_networkManager;
    
    // GateServer的URL
    QString m_serverUrl;
    
    // 当前用户ID
    int m_currentUid;
    
    // ========== 辅助方法 ==========
    
    // 从JSON数组中解析用户列表
    // 参数：
    //   - usersArray: JSON数组
    // 返回值：
    //   解析后的用户列表
    QList<FriendUser> parseUsersFromJson(const QJsonArray &usersArray);
    
    // 从JSON数组中解析好友申请列表
    // 参数：
    //   - requestsArray: JSON数组
    // 返回值：
    //   解析后的好友申请列表
    QList<FriendRequest> parseRequestsFromJson(const QJsonArray &requestsArray);
    
    // 从JSON对象解析单个用户
    // 参数：
    //   - userObj: JSON对象
    // 返回值：
    //   解析后的用户信息
    FriendUser parseUserFromJson(const QJsonObject &userObj);
    
    // 从JSON对象解析单个好友申请
    // 参数：
    //   - requestObj: JSON对象
    // 返回值：
    //   解析后的好友申请信息
    FriendRequest parseRequestFromJson(const QJsonObject &requestObj);
    
    // 发送HTTP POST请求
    // 参数：
    //   - endpoint: API端点（如 /search_friends）
    //   - data: 请求数据（JSON对象）
    //   - finishedSlot: 完成后的槽函数（使用SLOT宏）
    // 
    // 功能：
    //   1. 构建完整的URL
    //   2. 设置请求头
    //   3. 发送POST请求
    //   4. 连接完成信号到指定的槽函数
    void sendPostRequest(const QString &endpoint, const QJsonObject &data, 
                        const char *finishedSlot);
};

#endif // FRIENDMANAGER_H
