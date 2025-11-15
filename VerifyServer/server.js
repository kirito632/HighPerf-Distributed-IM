// VerifyServer gRPC服务器主文件
// 
// 作用：
//   实现VerifyServer的gRPC服务，提供验证码生成和发送功能
// 
// 主要功能：
//   GetVerifyCode: 根据邮箱地址生成和发送验证码
// 
// 依赖：
//   - grpc: gRPC库
//   - proto: protobuf定义
//   - const_module: 常量定义
//   - emailModule: 邮件发送模块
//   - redis_module: Redis操作模块

console.log("测试中文输出");
const grpc = require('@grpc/grpc-js')
const message_proto = require('./proto')
const const_module = require('./const')
const emailModule = require('./email');
const redis_module = require('./redis')

// 生成 n 位数字验证码（固定长度）
// 
// 参数：
//   @param {number} length 验证码长度（默认为6）
// 
// 返回值：
//   @returns {string} 生成的验证码
// 
// 实现逻辑：
//   循环length次，每次生成一个0-9的随机数字
function generateVerifyCode(length = 6) {
    let code = '';
    for (let i = 0; i < length; i++) {
        code += Math.floor(Math.random() * 10); // 每次生成一个 0~9
    }
    return code;
}

// 获取验证码（gRPC服务实现）
// 
// 功能：
//   1. 从Redis查询是否已有验证码
//   2. 如果没有，生成新的6位验证码
//   3. 将验证码存储到Redis（3分钟过期）
//   4. 发送验证码邮件
//   5. 返回结果
// 
// 参数：
//   @param {Object} call gRPC调用对象
//     - request: 请求对象（包含email字段）
//   @param {Function} callback 回调函数
//     - 参数1: 错误对象（null表示成功）
//     - 参数2: 响应对象（包含email、verifycode、error字段）
async function GetVerifyCode(call, callback) {
    console.log("email is ", call.request.email)
    try {
        // 从Redis查询是否已有验证码
        let verifyCode = await redis_module.GetRedis(const_module.code_prefix + call.request.email);
        console.log("query_res is ", verifyCode)

        // 如果没有验证码，生成新的
        if (verifyCode == null) {
            verifyCode = generateVerifyCode(6); // 生成 6 位数字验证码
            
            // 存储到Redis，设置3分钟过期时间
            let bres = await redis_module.SetRedisExpire(
                const_module.code_prefix + call.request.email,
                verifyCode,
                180 // 3分钟（180秒）
            )
            
            // 如果Redis操作失败，返回错误
            if (!bres) {
                callback(null, {
                    email: call.request.email,
                    error: const_module.Errors.RedisErr
                });
                return;
            }
        }

        console.log("verifyCode is ", verifyCode);

        // 发送邮件
        let mailOptions = {
            from: '2668348774@qq.com',
            to: call.request.email,
            subject: '验证码',
            html: `<p>您的验证码为：<b>${verifyCode}</b> ，请三分钟内完成注册</p>`,
            text: `您的验证码为：${verifyCode} ，请三分钟内完成注册`
        };

        let send_res = await emailModule.SendMail(mailOptions);
        console.log("send res is ", send_res)

        // 邮件发送失败，返回错误
        if (!send_res) {
            callback(null, {
                email: call.request.email,
                error: const_module.Errors.RedisErr
            });
            return;
        }

        //  成功返回，这里必须加
        callback(null, {
            email: call.request.email,
            verifycode: verifyCode,
            error: 0
        });

    } catch (error) {
        console.log("catch error is ", error)

        // 捕获异常，返回异常错误码
        callback(null, {
            email: call.request.email,
            error: const_module.Errors.Exception
        });
    }
}

// 主函数：启动gRPC服务器
// 
// 实现逻辑：
//   1. 创建gRPC服务器实例
//   2. 注册VerifyService服务，绑定GetVerifyCode方法
//   3. 绑定监听地址（127.0.0.1:50051）
//   4. 启动服务器
function main() {
    var server = new grpc.Server()
    
    // 添加服务和方法
    server.addService(message_proto.VerifyService.service, { GetVerifyCode: GetVerifyCode })
    
    // 绑定端口并启动
    server.bindAsync('127.0.0.1:50051', grpc.ServerCredentials.createInsecure(), () => {
        server.start()
        console.log('grpc server started')
    })
}

// 执行主函数
main()



