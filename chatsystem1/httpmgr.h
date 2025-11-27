#ifndef HTTPMGR_H
#define HTTPMGR_H
#include<QString>
#include<QUrl>
#include<QObject>
#include<QtNetwork/QNetworkAccessManager>
#include<QJsonObject>
#include<QJsonDocument>
#include"QObjectSingleton.h"
#include"global.h"

// HttpMgr类：HTTP管理器（单例模式）
// 
// 作用：
//   管理所有与GateServer的HTTP通信，包括：
//   1. 获取验证码
//   2. 用户注册
//   3. 找回密码
//   4. 用户登录
// 
// 设计模式：
//   QObjectSingleton - 使用CRTP实现的单例模式
// 
// 工作原理：
//   1. 使用QNetworkAccessManager发送HTTP请求
//   2. 使用信号/槽机制返回结果
//   3. 根据Modules类型分发到不同的信号
class HttpMgr : public QObject, public QObjectSingleton<HttpMgr>
{
    Q_OBJECT
    // 让 QObjectSingleton 能访问 HttpMgr 的私有构造函数
    friend class QObjectSingleton<HttpMgr>;

public:
    // 析构函数
    ~HttpMgr();
    
    // 发送HTTP POST请求
    // 参数：
    //   - url: 请求URL
    //   - json: JSON请求体
    //   - req_id: 请求ID（用于标识请求类型）
    //   - mod: 模块类型（用于信号分发）
    void PostHttpReq(QUrl url, QJsonObject json, ReqId reg_id, Modules mod);

private:
    // 私有构造函数：单例模式
    HttpMgr();
    
    // QNetworkAccessManager对象（用于发送HTTP请求）
    QNetworkAccessManager _manager;

private slots:
    // 槽函数：处理HTTP请求完成
    // 参数：
    //   - id: 请求ID
    //   - res: 响应内容
    //   - err: 错误码
    //   - mod: 模块类型
    // 
    // 作用：
    //   根据模块类型（mod）将结果分发到对应的信号
    void slot_http_finish(ReqId id, QString res, ErrorCodes err, Modules mod);

signals:
    // 信号：HTTP请求完成（通用）
    // 参数：
    //   - id: 请求ID
    //   - res: 响应内容
    //   - err: 错误码
    //   - mod: 模块类型
    void sig_http_finish(ReqId id, QString res, ErrorCodes err, Modules mod);
    
    // 信号：注册模块完成
    void sig_reg_mod_finish(ReqId id, QString res, ErrorCodes err);
    
    // 信号：忘记密码模块完成
    void sig_forget_mod_finish(ReqId id, QString res, ErrorCodes err);
    
    // 信号：登录模块完成
    void sig_login_mod_finish(ReqId id, QString res, ErrorCodes err);
};

#endif // HTTPMGR_H
