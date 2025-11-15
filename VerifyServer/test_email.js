const nodemailer = require("nodemailer");

// QQ 邮箱 SMTP
let transporter = nodemailer.createTransport({
    host: "smtp.qq.com",
    port: 465,
    secure: true, // SSL
    auth: {
        user: "2668348774@qq.com",       // 你的QQ邮箱
        pass: "cnqyagbmejxedjjj"                // QQ邮箱生成的授权码
    },
    logger: true, // 打印日志
    debug: true   // 调试模式
});

async function test() {
    try {
        let info = await transporter.sendMail({
            from: '"测试用户" <2668348774@qq.com>', // 必须和 auth.user 保持一致
            to: "2668348774@qq.com", // 你要收的邮箱
            subject: "测试邮件",
            text: "这是一个测试邮件",
        });
        console.log("邮件已发送: %s", info.messageId);
    } catch (err) {
        console.error("发送失败:", err);
    }
}
test();
