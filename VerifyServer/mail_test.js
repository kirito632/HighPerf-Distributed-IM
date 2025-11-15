/*const nodemailer = require("nodemailer");

async function main() {
    let transporter = nodemailer.createTransport({
        host: "smtp.qq.com",
        port: 465,
        secure: true,
        auth: {
            user: "2668348774@qq.com",      // 你的邮箱
            pass: "cnqyagbmejxedjjj"        // 你的授权码
        },
        logger: true,
        debug: true
    });

    try {
        let info = await transporter.sendMail({
            from: '"测试用户" <2668348774@qq.com>',
            to: "2668348774@qq.com",
            subject: "测试邮件",
            text: "这是一个测试"
        });
        console.log("邮件已发送: %s", info.messageId);
    } catch (err) {
        console.error("发送失败:", err);
    }
}

main();*/
