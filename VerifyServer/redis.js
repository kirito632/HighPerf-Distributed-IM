// VerifyServer Redis操作模块
// 
// 作用：
//   封装Redis操作，提供验证码的存储和查询功能
// 
// 依赖：
//   - ioredis: Redis客户端库
//   - config: 配置模块

const config_module = require('./config')
const Redis = require("ioredis")

// 创建Redis客户端实例
// 
// 配置说明：
//   - host: Redis主机地址
//   - port: Redis端口号
//   - password: Redis密码
const RedisCli = new Redis({
    host: config_module.redis_host,       // Redis服务器主机地址
    port: config_module.redis_port,        // Redis服务器端口号
    password: config_module.redis_passwd, // Redis密码
});

/**
 * 连接错误处理
 * 
 * 当Redis连接发生错误时，打印错误信息并退出连接
 */
RedisCli.on("error", function (err) {
    console.log("RedisCli connect error");
    RedisCli.quit();
});

/**
 * 根据key获取value
 * 
 * 参数：
 *   @param {string} key Redis键名
 * 
 * 返回值：
 *   @returns {Promise<string|null>} 键值，不存在返回null
 * 
 * 实现逻辑：
 *   1. 调用RedisCli.get()查询key
 *   2. 如果结果为null，返回null
 *   3. 否则返回查询结果
 */
async function GetRedis(key) {
    try {
        const result = await RedisCli.get(key)
        if (result === null) {
            console.log('result:', '<' + result + '>', 'This key cannot be find...')
            return null
        }
        console.log('Result:', '<' + result + '>', 'Get key success!...');
        return result
    } catch (error) {
        console.log('GetRedis error is', error);
        return null
    }
}

/**
 * 根据key查询redis中是否存在key
 * 
 * 参数：
 *   @param {string} key Redis键名
 * 
 * 返回值：
 *   @returns {Promise<number|null>} 存在返回1，不存在返回0，错误返回null
 * 
 * 实现逻辑：
 *   1. 调用RedisCli.exists()查询key是否存在
 *   2. 返回结果（0表示不存在，1表示存在）
 */
async function QueryRedis(key) {
    try {
        const result = await RedisCli.exists(key)
        //  判断该值是否为空，如果为空返回null
        if (result === 0) {
            console.log('result:<', '<' + result + '>', 'This key is null...');
            return null
        }
        console.log('Result:', '<' + result + '>', 'With this value!...');
        return result
    } catch (error) {
        console.log('QueryRedis error is', error);
        return null
    }
}

/**
* 设置key和value，并设置过期时间
* 
* 参数：
*   @param {string} key Redis键名
*   @param {string} value Redis键值
*   @param {number} exptime 过期时间（秒为单位）
* 
* 返回值：
*   @returns {Promise<boolean>} 成功返回true，失败返回false
* 
* 实现逻辑：
*   1. 调用RedisCli.set()设置键值对
*   2. 调用RedisCli.expire()设置过期时间
*   3. 返回操作结果
*/
async function SetRedisExpire(key, value, exptime) {
    try {
        // 设置键值对
        await RedisCli.set(key, value)
        // 设置过期时间（秒为单位）
        await RedisCli.expire(key, exptime);
        return true;
    } catch (error) {
        console.log('SetRedisExpire error is', error);
        return false;
    }
}

/**
 * 退出连接
 * 
 * 作用：
 *   关闭Redis连接
 */
function Quit() {
    RedisCli.quit();
}

// 导出所有函数
module.exports = { GetRedis, QueryRedis, Quit, SetRedisExpire }
