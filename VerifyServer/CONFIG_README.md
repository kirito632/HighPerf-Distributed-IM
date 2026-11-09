# VerifyServer 配置说明

`VerifyServer/config.json` **不再提交到仓库**（已加入 `.gitignore`），因为它保存 SMTP 授权码、MySQL/Redis 口令。仓库里只保留脱敏的 `config.example.json`。

## 初始化

```bash
cp VerifyServer/config.example.json VerifyServer/config.json
# 编辑 config.json，填入真实的 SMTP 授权码和数据库口令
```

也可以把配置放在仓库外，用 `VERIFY_CONFIG` 指定：

```bash
VERIFY_CONFIG=/etc/im/verify.json node server.js
```

## 环境变量覆盖

环境变量优先于 `config.json`，适合 Docker/K8s：

| 变量 | 含义 |
|---|---|
| `SMTP_USER` / `SMTP_PASS` | 发件邮箱与 SMTP 授权码 |
| `MYSQL_HOST` / `MYSQL_PORT` | MySQL 地址与端口 |
| `REDIS_HOST` / `REDIS_PORT` / `REDIS_PASS` | Redis 地址、端口、口令 |
| `INTERNAL_TOKEN` | Gate/Chat/Status/Verify 内部 gRPC 令牌 |

## 测试环境示例

`config.example.json` 使用 `docker-compose.test.yml` 的端口：

```json
{
  "email":  { "user": "your-account@example.com", "pass": "smtp-auth-code" },
  "internal": { "token": "dev-internal-token" },
  "mysql":  { "host": "127.0.0.1", "port": 13306, "user": "chatuser", "passwd": "test", "database": "chat_system_test" },
  "redis":  { "host": "127.0.0.1", "port": 16380, "passwd": "test" }
}
```

如果不需要真实发信，可以只使用测试账号并关闭邮件发送路径；集成测试不依赖 SMTP。

## 安全要求

1. **不要提交真实凭据**：提交前执行 `git status`，确认 `VerifyServer/config.json` 不在变更列表。
2. **轮换历史凭据**：该文件曾在 git 历史中出现过，旧的 SMTP 授权码和数据库口令应视为已泄露，需要在服务商侧重新生成。
3. 生产环境使用 `VERIFY_CONFIG` 指向仓库外文件，并通过环境变量注入密钥；不要依赖 Docker 镜像内的明文配置。
4. 若需要彻底清理历史，可在确认所有克隆已迁移后执行 `git filter-repo` 重写历史；该操作会改变提交哈希，需团队统一执行。
