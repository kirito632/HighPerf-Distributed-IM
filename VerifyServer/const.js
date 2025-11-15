// VerifyServer常量定义模块

// 验证码Redis键前缀
let code_prefix = "code_";

// 错误码定义
// 
// 说明：
//   Success(0): 成功
//   RedisErr(1): Redis操作错误
//   Exception(2): 异常错误
const Errors = {
    Success: 0,      // 成功
    RedisErr: 1,     // Redis操作错误
    Exception: 2,    // 异常错误
};

module.exports = { code_prefix, Errors }
