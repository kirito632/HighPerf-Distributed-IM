#include "friendmanager.h"
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QDebug>

// 构造函数：初始化好友管理器
// 
// 参数：
//   - parent: 父对象
// 
// 功能：
//   创建QNetworkAccessManager用于发送HTTP请求
FriendManager::FriendManager(QObject *parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
    , m_currentUid(0)
{
    // 网络错误处理在具体的回复处理函数中已经处理
}

// 析构函数：清理资源
FriendManager::~FriendManager()
{
}

// 设置当前用户ID
// 
// 参数：
//   - uid: 当前用户的ID
void FriendManager::setCurrentUser(int uid)
{
    m_currentUid = uid;
}

// 设置服务器地址
// 
// 参数：
//   - url: GateServer的URL（例如：http://localhost:8080）
void FriendManager::setServerUrl(const QString &url)
{
    m_serverUrl = url;
}

// 搜索用户
// 
// 参数：
//   - keyword: 搜索关键词（用户名或邮箱）
// 
// 功能：
//   1. 构建搜索请求的JSON数据
//   2. 发送HTTP POST请求到GateServer的 /search_friends 端点
//   3. 收到响应后触发 onSearchReplyFinished 槽函数
void FriendManager::searchFriends(const QString &keyword)
{
    qDebug() << "[FriendManager] 开始搜索用户，关键词:" << keyword;
    
    // 防御性检查：如果能进入主窗口，理论上已经登录了，uid应该有效
    // 如果这里uid无效，说明登录流程有问题，记录警告但不阻止（避免阻塞功能）
    if (m_currentUid <= 0) {
        qDebug() << "[FriendManager] ⚠️ 警告: 用户ID无效 (" << m_currentUid << ")，这可能是一个bug";
        // 不直接返回，让请求发送出去，后端会返回错误，这样可以暴露问题
    }
    
    if (m_serverUrl.isEmpty()) {
        qDebug() << "[FriendManager] 错误: 服务器地址未设置";
        emit errorOccurred("服务器地址未配置");
        return;
    }
    
    QJsonObject data;
    data["uid"] = m_currentUid;       // 当前用户ID
    data["keyword"] = keyword;         // 搜索关键词
    
    qDebug() << "[FriendManager] 准备发送搜索请求，用户ID:" << m_currentUid << "关键词:" << keyword;
    
    sendPostRequest("/search_friends", data, SLOT(onSearchReplyFinished()));
}

// 获取好友申请列表
// 
// 功能：
//   1. 构建获取好友申请列表的请求JSON数据
//   2. 发送HTTP POST请求到GateServer的 /get_friend_requests 端点
//   3. 收到响应后触发 onFriendRequestsReplyFinished 槽函数
void FriendManager::getFriendRequests()
{
    QJsonObject data;
    data["uid"] = m_currentUid;  // 当前用户ID
    
    sendPostRequest("/get_friend_requests", data, SLOT(onFriendRequestsReplyFinished()));
}

// 获取我的好友列表
// 
// 功能：
//   1. 构建获取好友列表的请求JSON数据
//   2. 发送HTTP POST请求到GateServer的 /get_my_friends 端点
//   3. 收到响应后触发 onMyFriendsReplyFinished 槽函数
void FriendManager::getMyFriends()
{
    QJsonObject data;
    data["uid"] = m_currentUid;  // 当前用户ID
    
    sendPostRequest("/get_my_friends", data, SLOT(onMyFriendsReplyFinished()));
}

// 发送好友申请
// 
// 参数：
//   - toUid: 目标用户ID
//   - desc: 申请描述（可选）
// 
// 功能：
//   1. 构建发送好友申请的请求JSON数据
//   2. 发送HTTP POST请求到GateServer的 /send_friend_request 端点
//   3. 收到响应后触发 onSendRequestReplyFinished 槽函数
void FriendManager::sendFriendRequest(int toUid, const QString &desc)
{
    QJsonObject data;
    data["from_uid"] = m_currentUid;  // 发送者用户ID
    data["to_uid"] = toUid;            // 目标用户ID
    data["desc"] = desc;               // 申请描述
    
    sendPostRequest("/send_friend_request", data, SLOT(onSendRequestReplyFinished()));
}

// 回复好友申请
// 
// 参数：
//   - fromUid: 申请者用户ID
//   - agree: 是否同意（true=同意，false=拒绝）
// 
// 功能：
//   1. 构建回复好友申请的请求JSON数据
//   2. 发送HTTP POST请求到GateServer的 /reply_friend_request 端点
//   3. 收到响应后触发 onReplyRequestReplyFinished 槽函数
void FriendManager::replyFriendRequest(int fromUid, bool agree)
{
    QJsonObject data;
    data["from_uid"] = fromUid;       // 申请者用户ID
    data["to_uid"] = m_currentUid;    // 被申请者用户ID
    data["agree"] = agree;            // 是否同意
    
    sendPostRequest("/reply_friend_request", data, SLOT(onReplyRequestReplyFinished()));
}

// 槽函数：处理搜索用户响应
// 
// 功能：
//   1. 检查网络错误
//   2. 解析JSON响应
//   3. 如果成功，解析用户列表并发送 searchResultsReceived 信号
//   4. 如果失败，发送 errorOccurred 信号
void FriendManager::onSearchReplyFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) {
        qDebug() << "[FriendManager] 搜索响应错误: reply为空";
        return;
    }
    
    qDebug() << "[FriendManager] 搜索响应完成, 错误码:" << reply->error();
    qDebug() << "[FriendManager] HTTP状态码:" << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    
    if (reply->error() == QNetworkReply::NoError) {
        // 读取响应数据
        QByteArray data = reply->readAll();
        qDebug() << "[FriendManager] 收到响应数据:" << QString::fromUtf8(data);
        
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
        
        if (parseError.error != QJsonParseError::NoError) {
            qDebug() << "[FriendManager] JSON解析错误:" << parseError.errorString();
            emit errorOccurred("JSON解析失败: " + parseError.errorString());
            reply->deleteLater();
            return;
        }
        
        QJsonObject root = doc.object();
        qDebug() << "[FriendManager] 解析后的JSON根对象:" << root;
        
        // 检查错误码
        int errorCode = root["error"].toInt();
        qDebug() << "[FriendManager] 后端返回错误码:" << errorCode;
        
        if (errorCode == 0) {
            // 成功，解析用户列表
            QJsonArray usersArray = root["users"].toArray();
            qDebug() << "[FriendManager] 找到用户数量:" << usersArray.size();
            QList<FriendUser> users = parseUsersFromJson(usersArray);
            emit searchResultsReceived(users);
        } else {
            // 失败，发送错误信号
            QString errorMsg = root.contains("message") ? root["message"].toString() : QString("错误码: %1").arg(errorCode);
            qDebug() << "[FriendManager] 搜索失败，错误信息:" << errorMsg;
            emit errorOccurred("搜索失败: " + errorMsg);
        }
    } else {
        // 网络错误
        QString errorString = reply->errorString();
        qDebug() << "[FriendManager] 网络错误:" << errorString;
        qDebug() << "[FriendManager] 请求URL:" << reply->url().toString();
        emit errorOccurred("网络错误: " + errorString);
    }
    
    reply->deleteLater();
}

// 槽函数：处理好友申请列表响应
// 
// 功能：
//   1. 检查网络错误
//   2. 解析JSON响应
//   3. 如果成功，解析好友申请列表并发送 friendRequestsReceived 信号
//   4. 如果失败，发送 errorOccurred 信号
void FriendManager::onFriendRequestsReplyFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    
    if (reply->error() == QNetworkReply::NoError) {
        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        QJsonObject root = doc.object();
        
        if (root["error"].toInt() == 0) {
            QJsonArray requestsArray = root["requests"].toArray();
            QList<FriendRequest> requests = parseRequestsFromJson(requestsArray);
            emit friendRequestsReceived(requests);
        } else {
            emit errorOccurred("获取好友申请失败: " + root["error"].toString());
        }
    } else {
        emit errorOccurred("网络错误: " + reply->errorString());
    }
    
    reply->deleteLater();
}

// 槽函数：处理我的好友列表响应
// 
// 功能：
//   1. 检查网络错误
//   2. 解析JSON响应
//   3. 如果成功，解析好友列表并发送 myFriendsReceived 信号
//   4. 如果失败，发送 errorOccurred 信号
void FriendManager::onMyFriendsReplyFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    
    if (reply->error() == QNetworkReply::NoError) {
        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        QJsonObject root = doc.object();
        
        if (root["error"].toInt() == 0) {
            QJsonArray friendsArray = root["friends"].toArray();
            QList<FriendUser> friends = parseUsersFromJson(friendsArray);
            emit myFriendsReceived(friends);
        } else {
            emit errorOccurred("获取好友列表失败: " + root["error"].toString());
        }
    } else {
        emit errorOccurred("网络错误: " + reply->errorString());
    }
    
    reply->deleteLater();
}

// 槽函数：处理发送好友申请响应
// 
// 功能：
//   1. 检查网络错误
//   2. 解析JSON响应
//   3. 根据结果发送 friendRequestSent 信号
//   4. 如果有错误，发送 errorOccurred 信号
void FriendManager::onSendRequestReplyFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    
    if (reply->error() == QNetworkReply::NoError) {
        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        QJsonObject root = doc.object();
        
        bool success = (root["error"].toInt() == 0);
        emit friendRequestSent(success);
        
        if (!success) {
            emit errorOccurred("发送好友申请失败");
        }
    } else {
        emit errorOccurred("网络错误: " + reply->errorString());
        emit friendRequestSent(false);
    }
    
    reply->deleteLater();
}

// 槽函数：处理回复好友申请响应
// 
// 功能：
//   1. 检查网络错误
//   2. 解析JSON响应
//   3. 根据结果发送 friendRequestReplied 信号
//   4. 如果有错误，发送 errorOccurred 信号
void FriendManager::onReplyRequestReplyFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    
    if (reply->error() == QNetworkReply::NoError) {
        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        QJsonObject root = doc.object();
        
        bool success = (root["error"].toInt() == 0);
        emit friendRequestReplied(success);
        
        if (!success) {
            emit errorOccurred("回复好友申请失败");
        }
    } else {
        emit errorOccurred("网络错误: " + reply->errorString());
        emit friendRequestReplied(false);
    }
    
    reply->deleteLater();
}


// 从JSON数组中解析用户列表
// 
// 参数：
//   - usersArray: JSON数组
// 返回值：
//   解析后的用户列表
// 
// 功能：
//   遍历JSON数组，解析每个用户对象并添加到列表中
QList<FriendUser> FriendManager::parseUsersFromJson(const QJsonArray &usersArray)
{
    QList<FriendUser> users;
    for (const QJsonValue &value : usersArray) {
        if (value.isObject()) {
            FriendUser user = parseUserFromJson(value.toObject());
            users.append(user);
        }
    }
    return users;
}

// 从JSON数组中解析好友申请列表
// 
// 参数：
//   - requestsArray: JSON数组
// 返回值：
//   解析后的好友申请列表
// 
// 功能：
//   遍历JSON数组，解析每个好友申请对象并添加到列表中
QList<FriendRequest> FriendManager::parseRequestsFromJson(const QJsonArray &requestsArray)
{
    QList<FriendRequest> requests;
    for (const QJsonValue &value : requestsArray) {
        if (value.isObject()) {
            FriendRequest request = parseRequestFromJson(value.toObject());
            requests.append(request);
        }
    }
    return requests;
}

// 从JSON对象解析单个用户
// 
// 参数：
//   - userObj: JSON对象
// 返回值：
//   解析后的用户信息
// 
// 功能：
//   从JSON对象中提取用户的所有字段并填充到FriendUser结构体
FriendUser FriendManager::parseUserFromJson(const QJsonObject &userObj)
{
    FriendUser user;
    user.uid = userObj["uid"].toInt();
    user.name = userObj["name"].toString();
    user.email = userObj["email"].toString();
    user.nick = userObj["nick"].toString();
    user.icon = userObj["icon"].toString();
    user.sex = userObj["sex"].toInt();
    user.desc = userObj["desc"].toString();
    user.isFriend = userObj["isFriend"].toBool();
    return user;
}

// 从JSON对象解析单个好友申请
// 
// 参数：
//   - requestObj: JSON对象
// 返回值：
//   解析后的好友申请信息
// 
// 功能：
//   从JSON对象中提取好友申请的所有字段并填充到FriendRequest结构体
FriendRequest FriendManager::parseRequestFromJson(const QJsonObject &requestObj)
{
    FriendRequest request;
    request.uid = requestObj["uid"].toInt();
    request.name = requestObj["name"].toString();
    request.desc = requestObj["desc"].toString();
    request.icon = requestObj["icon"].toString();
    request.nick = requestObj["nick"].toString();
    request.sex = requestObj["sex"].toInt();
    request.status = requestObj["status"].toInt();
    return request;
}

// 发送HTTP POST请求
// 
// 参数：
//   - endpoint: API端点（如 /search_friends）
//   - data: 请求数据（JSON对象）
//   - finishedSlot: 完成后的槽函数（使用SLOT宏）
// 
// 功能：
//   1. 构建完整的URL（m_serverUrl + endpoint）
//   2. 创建QNetworkRequest并设置Content-Type头
//   3. 将JSON数据转换为字节数组
//   4. 发送POST请求
//   5. 连接finished信号到指定的槽函数
// 
// 实现细节：
//   - 使用QNetworkAccessManager发送HTTP请求
//   - 响应处理在对应的槽函数中进行
void FriendManager::sendPostRequest(const QString &endpoint, const QJsonObject &data, 
                                  const char *finishedSlot)
{
    // 构建完整的URL
    QUrl url(m_serverUrl + endpoint);
    QNetworkRequest request(url);
    
    // 设置请求头
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    
    // 将JSON对象转换为字节数组
    QJsonDocument doc(data);
    QByteArray jsonData = doc.toJson();
    
    qDebug() << "[FriendManager] 发送POST请求";
    qDebug() << "[FriendManager] URL:" << url.toString();
    qDebug() << "[FriendManager] 请求数据:" << QString::fromUtf8(jsonData);
    qDebug() << "[FriendManager] 当前用户ID:" << m_currentUid;
    
    // 发送POST请求
    QNetworkReply *reply = m_networkManager->post(request, jsonData);
    
    // 连接完成信号到指定的槽函数
    connect(reply, SIGNAL(finished()), this, finishedSlot);
    
    // 连接错误信号以便调试
    connect(reply, QOverload<QNetworkReply::NetworkError>::of(&QNetworkReply::error),
            [this, endpoint](QNetworkReply::NetworkError error) {
                qDebug() << "[FriendManager] 请求" << endpoint << "发生网络错误:" << error;
            });
}
