# VerifyServer 配置文件说明

## config.json 配置说明

### 当前配置（远程服务器）

当前配置文件指向远程服务器 `81.68.86.146`：
- MySQL: `81.68.86.146:3306`
- Redis: `81.68.86.146:6380`

### 切换到本机配置

如果需要切换到本机（本地）配置，需要修改 `config.json` 文件：

#### 修改前（远程服务器）
```json
{
  "mysql": {
    "host": "81.68.86.146",
    "user": "root",
    "passwd": "password"
  },
  "redis": {
    "host": "81.68.86.146",
    "port": 6380,
    "passwd": 123456
  }
}
```

#### 修改后（本机）
```json
{
  "mysql": {
    "host": "127.0.0.1",
    "user": "chatuser",
    "passwd": "123456"
  },
  "redis": {
    "host": "127.0.0.1",
    "port": 6380,  // 或 6379（标准端口）
    "passwd": "123456"  // 如果无密码，改为 ""
  }
}
```

### 修改注意事项

1. **MySQL配置**：
   - `host`: 改成本机 IP（127.0.0.1 或 localhost）
   - `user`: 根据实际情况修改（如：chatuser）
   - `passwd`: 根据实际情况修改（如：123456）
   - 确保本机MySQL中有 `chat_system` 数据库

2. **Redis配置**：
   - `host`: 改成本机 IP（127.0.0.1 或 localhost）
   - `port`: 本机Redis的端口（通常是6379，当前使用6380）
   - `passwd`: 本机Redis的密码（如果无密码，改为空字符串 `""`）

3. **其他服务同步**：
   如果改成本机配置，需要同时修改以下文件：
   - `GateServer\GateServer\config.ini`
   - `StatusServer\StatusServer\config.ini`
   - `ChatServer\ChatServer\config.ini`
   
   确保这些服务都指向同一个Redis和MySQL服务器。

4. **验证配置**：
   - 启动Redis：确保本机Redis已启动
   - 启动MySQL：确保本机MySQL已启动
   - 测试连接：使用命令行工具测试连接
     ```bash
     # 测试Redis
     redis-cli -h 127.0.0.1 -p 6380 -a 123456
     
     # 测试MySQL
     mysql -h 127.0.0.1 -P 3306 -u chatuser -p
     ```

## 当前配置状态

✅ **当前配置（远程服务器）可正常工作**
- Redis连接：`81.68.86.146:6380`
- MySQL连接：`81.68.86.146:3306`
- 已验证可以发送验证码并注册账号

## 建议

- 如果是在远程服务器上部署生产环境，保持当前配置
- 如果需要在本机开发/测试，可以改成本机配置，更方便调试

