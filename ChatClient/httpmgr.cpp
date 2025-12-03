#include "httpmgr.h"

// 析构函数：清理资源
HttpMgr::~HttpMgr()
{
}

// 构造函数：初始化HttpMgr
// 
// 实现逻辑：
//   1. 连接信号和槽，用于HTTP响应分发
HttpMgr::HttpMgr() {
    // 将通用完成信号连接到处理槽函数
    connect(this, &HttpMgr::sig_http_finish, this, &HttpMgr::slot_http_finish);
}

// 发送HTTP POST请求
// 
// 参数：
//   - url: 请求URL（例如：http://127.0.0.1:8080/get_verifycode）
//   - json: JSON请求体
//   - req_id: 请求ID（用于识别请求类型）
//   - mod: 模块类型（用于信号分发）
// 
// 实现逻辑：
//   1. 将JSON对象转换为字节数组
//   2. 创建HTTP请求对象
//   3. 设置请求头（Content-Type, Content-Length）
//   4. 发送POST请求
//   5. 异步等待响应
//   6. 响应到达后，emit sig_http_finish信号
void HttpMgr::PostHttpReq(QUrl url, QJsonObject json, ReqId req_id, Modules mod)
{
    qDebug() << "PostHttpReq called, url=" << url;

    // 将JSON对象转换为字节数组
    QByteArray data = QJsonDocument(json).toJson();
    
    // 创建HTTP请求对象
    QNetworkRequest request(url);

    // 设置HTTP请求头
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setHeader(QNetworkRequest::ContentLengthHeader, QByteArray::number(data.length()));

    // 发送POST请求
    QNetworkReply* reply = _manager.post(request, data);
    qDebug() << "[HttpMgr] PostHttpReq url=" << url.toString();
    
    // 连接响应完成信号
    QObject::connect(reply, &QNetworkReply::finished, [this, reply, req_id, mod](){
        qDebug() << "Reply finished, ptr=" << reply; // 调试用
        
        // 处理错误情况
        if(reply->error() != QNetworkReply::NoError){
            qDebug() << "Http error:" << reply->errorString();
            // 发送信号通知完成（带错误码）
            emit sig_http_finish(req_id, "", ErrorCodes::ERR_NETWORK, mod);
            reply->deleteLater();
            return; // <- 关键：遇错后直接返回，避免继续走成功分支
        }

        // 无错误，读取响应内容
        QString res = reply->readAll();
        qDebug() << "Http response:" << res;
        
        // 发送信号通知完成（成功）
        emit sig_http_finish(req_id, res, ErrorCodes::SUCCESS, mod);
        reply->deleteLater();
        return;
    });
}

// 槽函数：处理HTTP请求完成
// 
// 参数：
//   - id: 请求ID
//   - res: 响应内容
//   - err: 错误码
//   - mod: 模块类型
// 
// 作用：
//   根据模块类型（mod）将结果分发到对应的专用信号
//   便于不同模块独立处理自己的HTTP响应
void HttpMgr::slot_http_finish(ReqId id, QString res, ErrorCodes err, Modules mod)
{
    qDebug() << "slot_http_finish called, mod=" << mod << "id=" << id;

    // 根据模块类型分发到不同的信号
    if(mod == Modules::REGISTERMOD){
        // 发送信号通知注册模块http的响应结束了
        emit sig_reg_mod_finish(id, res, err);
    }

    if(mod == Modules::FORGETMOD){
        // 发送信号通知忘记密码模块响应结束了
        emit sig_forget_mod_finish(id, res, err);
    }

    if(mod == Modules::LOGINMOD){
        // 发送信号通知登录模块响应结束了
        emit sig_login_mod_finish(id, res, err);
    }
}
