// VerifyServer邮件发送模块
// 
// 作用：
//   封装邮件发送功能，使用nodemailer库发送验证码邮件
// 
// 依赖：
//   - nodemailer: Node.js邮件发送库
//   - config: 配置模块（提供邮箱账号和密码）

const nodemailer = require('nodemailer');
const config_module = require("./config")

/**
 * 创建邮件传输配置（使用QQ邮箱SMTP服务）
 * 
 * 配置说明：
 *   - host: QQ邮箱SMTP服务器地址
 *   - port: 加密端口（465）
 *   - secure: true表示使用SSL/TLS加密
 *   - auth: 邮箱认证信息（账号和授权码）
 */
let transport = nodemailer.createTransport({
    host: 'smtp.qq.com',              // QQ邮箱SMTP服务器
    port: 465,                         // SSL端口
    secure: true,                      // 使用SSL/TLS加密
    auth: {
        user: config_module.email_user, // 发件人邮箱地址
        pass: config_module.email_pass // 授权码（或应用专用密码）
    },

    /*logger: true,
    debug: true*/  // 调试选项（已注释）
});

/**
 * 发送邮件的函数
 * 
 * 参数：
 *   @param {Object} mailOptions_ 邮件选项对象
 *     - from: 发件人地址
 *     - to: 收件人地址
 *     - subject: 邮件主题
 *     - text: 纯文本内容
 *     - html: HTML内容
 * 
 * 返回值：
 *   @returns {Promise} Promise对象，成功时resolve(info)，失败时reject(error)
 * 
 * 实现逻辑：
 *   1. 使用Promise包装异步操作
 *   2. 调用transport.sendMail发送邮件
 *   3. 处理成功和失败的情况
 */
function SendMail(mailOptions_) {
    return new Promise(function (resolve, reject) {
        transport.sendMail(mailOptions_, function (error, info) {
            if (error) {
                // 发送失败，打印错误并reject
                console.log(error);
                reject(error);
            } else {
                // 发送成功，打印响应并resolve
                console.log('邮件已成功发送：' + info.response);
                resolve(info.response)
            }
        });
    })

}

// 导出SendMail函数
module.exports.SendMail = SendMail
