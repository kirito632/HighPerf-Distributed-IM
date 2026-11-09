// VerifyServer 配置加载模块
//
// 配置来源（后者覆盖前者）：
//   1. VerifyServer/config.json（本地文件，已被 .gitignore 忽略）
//   2. 环境变量：SMTP_USER / SMTP_PASS / REDIS_HOST / REDIS_PORT /
//      REDIS_PASS / INTERNAL_TOKEN
//
// 新环境请复制 config.example.json 为 config.json 再填写，或使用
// VERIFY_CONFIG 指向仓库外的配置文件（推荐，避免再次提交凭据）。
// 提交任何文件前确认其中没有真实的邮箱授权码、数据库口令。

const fs = require('fs');
const path = require('path');

const configPath = process.env.VERIFY_CONFIG ||
    path.join(__dirname, 'config.json');

let config = {};
if (fs.existsSync(configPath)) {
    config = JSON.parse(fs.readFileSync(configPath, 'utf8'));
}

const email = config.email || {};
const mysql = config.mysql || {};
const redis = config.redis || {};

// 邮箱配置：环境变量优先，避免把授权码写进仓库。
let email_user = process.env.SMTP_USER || email.user || "";
let email_pass = process.env.SMTP_PASS || email.pass || "";

// MySQL 配置
let mysql_host = process.env.MYSQL_HOST || mysql.host || "127.0.0.1";
let mysql_port = process.env.MYSQL_PORT || mysql.port || 3306;

// Redis 配置
let redis_host = process.env.REDIS_HOST || redis.host || "127.0.0.1";
let redis_port = process.env.REDIS_PORT || redis.port || 6379;
let redis_passwd = process.env.REDIS_PASS || redis.passwd || "";

// 验证码 Redis 键前缀（与 const.js 保持一致）
let code_prefix = "code_";

// 内部 gRPC 服务令牌；生产环境优先使用 INTERNAL_TOKEN 环境变量
let internal_token = process.env.INTERNAL_TOKEN ||
    (config.internal && config.internal.token) || "";

module.exports = {
    email_pass,
    email_user,
    mysql_host,
    mysql_port,
    redis_host,
    redis_port,
    redis_passwd,
    code_prefix,
    internal_token
}
