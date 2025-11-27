# 好友系统前后端整合说明

## 概述
本项目实现了完整的聊天系统好友功能，包括好友查询、申请、同意/拒绝等核心功能。

## 系统架构
- **前端**: Qt C++ 桌面应用
- **后端**: C++ 微服务架构
  - **GateServer**: HTTP网关服务器，处理HTTP API请求
  - **ChatServer**: 聊天服务器，处理好友相关业务逻辑
  - **数据库**: MySQL，存储用户和好友数据

## 已实现功能

### 1. 后端功能
- ✅ 扩展了消息协议，添加好友相关消息定义
- ✅ 扩展了数据库DAO，实现好友查询、申请、同意等功能
- ✅ 在GateServer中添加了HTTP API接口：
  - `/search_friends` - 搜索好友
  - `/get_friend_requests` - 获取好友申请列表
  - `/get_my_friends` - 获取我的好友列表
  - `/send_friend_request` - 发送好友申请
  - `/reply_friend_request` - 回复好友申请

### 2. 前端功能
- ✅ 创建了FriendManager类，处理HTTP API调用
- ✅ 集成了真实的后端API调用
- ✅ 实现了好友搜索、申请、同意等功能
- ✅ 更新了主窗口，支持真实数据加载

## 数据库设置

### 1. 执行数据库脚本
```sql
-- 执行 database_schema.sql 中的SQL语句
-- 这将创建必要的表结构
```

### 2. 确保用户表有必要的字段
```sql
ALTER TABLE user ADD COLUMN IF NOT EXISTS nick VARCHAR(50) DEFAULT '';
ALTER TABLE user ADD COLUMN IF NOT EXISTS icon VARCHAR(255) DEFAULT '';
ALTER TABLE user ADD COLUMN IF NOT EXISTS sex TINYINT DEFAULT 0;
ALTER TABLE user ADD COLUMN IF NOT EXISTS desc TEXT;
```

## 运行步骤

### 1. 启动后端服务
```bash
# 启动GateServer (HTTP网关)
cd D:\cppworks\全栈项目实战\GateServer
# 编译并运行GateServer

# 启动ChatServer (聊天服务器)
cd D:\cppworks\全栈项目实战\ChatServer
# 编译并运行ChatServer
```

### 2. 启动前端应用
```bash
cd D:\Qt\chatsystem1
qmake
make
# 运行生成的Qt应用
```

## API接口说明

### 搜索好友
- **URL**: `POST /search_friends`
- **请求体**:
```json
{
    "uid": 1,
    "keyword": "搜索关键词"
}
```
- **响应**:
```json
{
    "error": 0,
    "users": [
        {
            "uid": 2,
            "name": "用户名",
            "email": "邮箱",
            "nick": "昵称",
            "icon": "头像",
            "sex": 1,
            "desc": "描述",
            "isFriend": false
        }
    ]
}
```

### 获取好友申请列表
- **URL**: `POST /get_friend_requests`
- **请求体**:
```json
{
    "uid": 1
}
```

### 获取我的好友列表
- **URL**: `POST /get_my_friends`
- **请求体**:
```json
{
    "uid": 1
}
```

### 发送好友申请
- **URL**: `POST /send_friend_request`
- **请求体**:
```json
{
    "from_uid": 1,
    "to_uid": 2,
    "desc": "申请描述"
}
```

### 回复好友申请
- **URL**: `POST /reply_friend_request`
- **请求体**:
```json
{
    "from_uid": 2,
    "to_uid": 1,
    "agree": true
}
```

## 测试建议

### 1. 数据库测试
- 确保数据库连接正常
- 验证表结构创建成功
- 插入一些测试用户数据

### 2. 后端API测试
- 使用Postman或curl测试各个API接口
- 验证JSON格式正确
- 检查错误处理

### 3. 前端集成测试
- 启动Qt应用
- 测试好友搜索功能
- 测试好友申请流程
- 验证数据同步

## 注意事项

1. **服务器地址配置**: 在`mainwindow.cpp`中修改服务器地址
   ```cpp
   m_friendManager->setServerUrl("http://localhost:8080");
   ```

2. **用户ID设置**: 在`mainwindow.cpp`中设置当前用户ID
   ```cpp
   m_friendManager->setCurrentUser(1); // 设置实际用户ID
   ```

3. **数据库连接**: 确保后端服务能正常连接数据库

4. **网络配置**: 确保前端能访问后端HTTP服务

## 故障排除

### 常见问题
1. **数据库连接失败**: 检查数据库配置和连接字符串
2. **API调用失败**: 检查服务器地址和端口
3. **JSON解析错误**: 检查API响应格式
4. **编译错误**: 确保所有依赖库正确安装

### 调试建议
- 查看后端日志输出
- 使用Qt的qDebug()输出调试信息
- 检查网络请求和响应
- 验证数据库查询结果

## 扩展功能

可以考虑添加的功能：
- 好友分组管理
- 好友在线状态显示
- 好友动态/状态更新
- 群组功能
- 消息推送通知

## 技术支持

如有问题，请检查：
1. 数据库连接和表结构
2. 后端服务运行状态
3. 前端网络请求日志
4. 系统错误日志
