#ifndef MESSAGELISTVIEW_H
#define MESSAGELISTVIEW_H

#pragma once
#include <QWidget>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QScrollBar>

#include "chatbubble.h" // 你的 ChatBubble 类
#include "global.h"     // 如果需要 ReqId/Contact 定义可包含

// MessageListView类：消息列表视图
// 
// 作用：
//   1. 提供可滚动的消息列表容器
//   2. 自动滚动到底部显示最新消息
//   3. 支持添加文字/图片消息
//   4. 支持清空消息和历史消息插入
// 
// 设计特点：
//   - 使用QScrollArea实现可滚动
//   - 使用QVBoxLayout垂直排列消息
//   - 自动跟踪滚动位置（如果用户手动滚动，停止自动滚动）
//   - 支持批量插入历史消息
class MessageListView : public QWidget
{
    Q_OBJECT
public:
    // 构造函数：初始化消息列表视图
    explicit MessageListView(QWidget *parent = nullptr);

    // 添加文本消息（外部调用）
    // 参数：
    //   - text: 消息文本
    //   - isSender: 是否为发送者
    // 
    // 功能：
    //   创建ChatBubble并添加到列表，自动滚动到底部
    void addMessage(const QString &text, bool isSender);
    
    // 添加图片消息（外部调用）
    // 参数：
    //   - pix: 图片对象
    //   - isSender: 是否为发送者
    // 
    // 功能：
    //   创建ChatBubble并添加到列表，自动滚动到底部
    void addImageMessage(const QPixmap &pix, bool isSender);

    // 清空所有消息
    // 
    // 功能：
    //   删除列表中的所有消息气泡
    void clearMessages();
    
    // 在顶部插入历史消息
    // 参数：
    //   - history: 历史消息列表（QPair<文本, isSender>）
    // 
    // 功能：
    //   在消息列表顶部插入历史消息，保持滚动位置不变
    void insertHistoryAtTop(const QList<QPair<QString,bool>> &history);

    // 控制自动滚动行为
    // 参数：
    //   - on: 是否自动滚动到底部
    // 
    // 功能：
    //   设置是否在添加新消息时自动滚动到底部
    //   如果用户手动向上滚动查看历史消息，则停止自动滚动
    void setAutoScrollToBottom(bool on) { _autoScroll = on; }
    
    // 获取自动滚动状态
    bool autoScroll() const { return _autoScroll; }

    // 如果需要，暴露滚动条
    // 
    // 返回值：
    //   垂直滚动条指针
    // 
    // 功能：
    //   返回内部使用的垂直滚动条，允许外部控制滚动
    QScrollBar* verticalScrollBar() const;

private:
    // 创建消息行容器
    // 参数：
    //   - bubble: 消息气泡控件
    //   - isSender: 是否为发送者
    //   - avatar: 头像图片
    // 
    // 返回值：
    //   消息行控件（包含头像和气泡）
    // 
    // 功能：
    //   根据isSender设置布局（发送者=头像在右，接收者=头像在左）
    QWidget* makeRowForBubble(ChatBubble *bubble, bool isSender, const QPixmap &avatar);

    // 消息滚动区域（可滚动容器）
    QScrollArea *_scrollArea;
    
    // 消息容器（包含所有消息气泡）
    QWidget *_container;
    
    // 消息布局（垂直布局）
    QVBoxLayout *_layout;
    
    // 自动滚动标志（true=自动滚动到底部）
    bool _autoScroll;
};


#endif // MESSAGELISTVIEW_H
