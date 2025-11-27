# 查询功能实现说明

## 功能概述

已实现与后端连接的查询功能，用户可以通过搜索框查找其他用户并发送好友申请。

## 实现内容

### 1. FriendManager 类（已存在）
- 负责处理所有好友相关的网络请求
- 支持搜索用户、获取好友列表、处理好友申请等功能
- 使用 QNetworkAccessManager 发送 HTTP POST 请求

### 2. AddFriendDialog 更新
- 添加了 `setFriendManager()` 方法，用于设置 FriendManager 实例
- 实现了 `onSearchResultsReceived()` 槽函数，处理从后端接收的搜索结果
- 实现了 `populateSearchResultsFromNetwork()` 方法，显示从网络获取的搜索结果
- 添加了错误处理机制

### 3. MainWindow 集成
- 在创建 AddFriendDialog 时设置 FriendManager 实例
- 从 UserMgr 获取当前用户ID，并设置到 FriendManager

### 4. 搜索流程
1. 用户点击"查找"按钮
2. AddFriendDialog 调用 FriendManager::searchFriends()
3. FriendManager 发送 HTTP 请求到后端服务器
4. 接收后端返回的搜索结果
5. 通过信号 `searchResultsReceived` 通知 AddFriendDialog
6. AddFriendDialog 显示搜索结果

### 5. 添加好友流程
1. 用户点击搜索结果中的"添加"按钮
2. AddFriendDialog 调用 FriendManager::sendFriendRequest()
3. FriendManager 发送好友申请到后端服务器
4. 通过信号 `friendRequestSent` 通知操作结果

## 后端接口说明

### 搜索用户
- **接口**: `/search_friends`
- **方法**: POST
- **请求参数**:
  ```json
  {
    "uid": 1,
    "keyword": "用户名或邮箱"
  }
  ```
- **响应格式**:
  ```json
  {
    "error": 0,
    "users": [
      {
        "uid": 2,
        "name": "用户名",
        "email": "user@example.com",
        "nick": "昵称",
        "icon": "头像URL",
        "sex": 0,
        "desc": "描述",
        "isFriend": false
      }
    ]
  }
  ```

### 发送好友申请
- **接口**: `/send_friend_request`
- **方法**: POST
- **请求参数**:
  ```json
  {
    "from_uid": 1,
    "to_uid": 2,
    "desc": "申请说明"
  }
  ```
- **响应格式**:
  ```json
  {
    "error": 0
  }
  ```

## 使用说明

### 1. 初始化
确保在 MainWindow 初始化时已经设置了正确的服务器地址和用户ID：
```cpp
m_friendManager->setServerUrl("http://localhost:8080");
m_friendManager->setCurrentUser(currentUid);
```

### 2. 搜索用户
1. 点击底部导航栏的"添加好友"按钮（👤+）
2. 在搜索框中输入用户名或邮箱
3. 点击"查找"按钮
4. 等待后端返回搜索结果

### 3. 添加好友
1. 在搜索结果中找到要添加的用户
2. 点击"添加"按钮
3. 等待后端处理好友申请

## 注意事项

1. **服务器地址**: 确保后端服务器的地址正确（默认为 `http://localhost:8080`）
2. **用户ID**: 确保从 UserMgr 获取的用户ID正确
3. **网络连接**: 确保网络连接正常，否则会显示错误提示
4. **错误处理**: 所有错误都会通过 `onErrorOccurred` 槽函数处理，并显示错误消息

## 后续优化建议

1. 添加搜索历史记录
2. 添加搜索结果的筛选和排序功能
3. 添加搜索结果的缓存机制
4. 优化搜索结果的显示样式
5. 添加更多搜索条件（如年龄、性别等）
