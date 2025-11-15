// VerifyServer配置加载模块
// 
// 作用：
//   从config.json文件读取配置信息，并提供给其他模块使用
// 
//  重要配置说明：
//   当前配置指向远程服务器 81.68.86.146
//   
//   如果需要改成本机配置，请修改 config.json 文件：
//   - mysql.host: "81.68.86.146" → "127.0.0.1" 或 "localhost"
//   - mysql.user: "root" → "chatuser" (根据实际MySQL用户)
//   - mysql.passwd: "123456" (根据实际MySQL密码)
//   - redis.host: "81.68.86.146" → "127.0.0.1" 或 "localhost"
//   - redis.port: 6380 (保持不变，如果本机使用标准端口可改为6379)
//   - redis.passwd: 123456 (如果本机Redis无密码，改为 "")
// 
// 注意：
//   1. 如果改成本机配置，需要确保本机已安装并运行Redis和MySQL
//   2. 确保本机MySQL中有 chat_system 数据库
//   3. 确保本机Redis已启动并配置了相同的密码
//   4. 如果其他服务（GateServer、StatusServer、ChatServer）还在使用远程服务器，
//      需要同时修改它们的config.ini文件，保持配置一致

const fs = require('fs');

// 读取并解析config.json配置文件
let config = JSON.parse(fs.readFileSync('config.json', 'utf8'));

// 提取邮箱配置
let email_user = config.email.user;    // 发件人邮箱地址
let email_pass = config.email.pass;    // 授权码或密码

// 提取MySQL配置
let mysql_host = config.mysql.host;    // MySQL主机地址（当前：远程服务器）
let mysql_port = config.mysql.port;    // MySQL端口号

// 提取Redis配置
let redis_host = config.redis.host;    // Redis主机地址（当前：远程服务器）
let redis_port = config.redis.port;    // Redis端口号
let redis_passwd = config.redis.passwd; // Redis密码

// 验证码Redis键前缀（与const.js保持一致）
let code_prefix = "code_";

// 导出所有配置项
module.exports = { email_pass, email_user, mysql_host, mysql_port, redis_host, redis_port, redis_passwd, code_prefix }
