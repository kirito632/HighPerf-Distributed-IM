#ifndef CHATBUBBLE_H
#define CHATBUBBLE_H
#pragma once
#include <QWidget>
#include <QLabel>
#include <QTextBrowser>
#include <QPixmap>

// ChatBubble类：聊天气泡控件
// 
// 作用：
//   1. 显示文字消息或图片消息
//   2. 根据isSender参数设置样式（发送者=右对齐蓝色，接收者=左对齐灰色）
//   3. 自动调整大小以适应内容
// 
// 设计特点：
//   - 文字消息：使用QTextBrowser（支持自动换行、富文本）
//   - 图片消息：使用QLabel显示图片
//   - 样式：根据isSender设置不同的颜色和圆角
class ChatBubble : public QWidget {
    // Q_OBJECT
public:
    // 构造函数：创建文字消息气泡
    // 
    // 参数：
    //   - text: 消息文本内容
    //   - isSender: 是否为发送者（true=右对齐蓝色，false=左对齐灰色）
    //   - parent: 父控件
    // 
    // 功能：
    //   创建并配置文字消息气泡，设置样式和布局
    explicit ChatBubble(const QString &text, bool isSender, QWidget *parent = nullptr);

    // 构造函数：创建图片消息气泡
    // 
    // 参数：
    //   - pix: 图片对象
    //   - isSender: 是否为发送者
    //   - parent: 父控件
    // 
    // 功能：
    //   创建并配置图片消息气泡，设置样式和布局
    explicit ChatBubble(const QPixmap &pix, bool isSender, QWidget *parent = nullptr);

    // 获取建议的控件大小
    // 
    // 返回值：
    //   控件建议的大小（根据内容自动计算）
    QSize sizeHint() const override;

protected:
    // 绘制事件：绘制消息气泡
    // 
    // 功能：
    //   绘制圆角矩形背景和边框
    void paintEvent(QPaintEvent *ev) override;
    
    // 大小改变事件：调整子控件大小
    // 
    // 功能：
    //   当控件大小改变时，调整内部的文本浏览器或图片标签的大小
    void resizeEvent(QResizeEvent *ev) override;

private:
    // 设置文字消息气泡
    // 
    // 参数：
    //   - text: 消息文本内容
    // 
    // 功能：
    //   1. 创建QTextBrowser控件
    //   2. 设置样式（白色背景、圆角、内边距）
    //   3. 设置HTML内容（支持换行）
    //   4. 根据内容调整大小
    void setupTextBubble(const QString &text);
    
    // 设置图片消息气泡
    // 
    // 参数：
    //   - pix: 图片对象
    // 
    // 功能：
    //   1. 创建QLabel控件
    //   2. 设置样式
    //   3. 设置图片（缩放以适应）
    void setupImageBubble(const QPixmap &pix);

private:
    // 是否为发送者（true=右对齐蓝色，false=左对齐灰色）
    bool _isSender = false;
    
    // 是否为图片消息（true=图片，false=文字）
    bool _isImage = false;

    // 文本显示控件（使用QTextBrowser支持任意断行）
    QTextBrowser *_textBrowser = nullptr;

    // 图片显示控件
    QLabel *_imageLabel = nullptr;
    
    // 缓存的图片数据
    QPixmap _pix;
    
    // 气泡最大宽度（限制消息气泡的最大宽度，防止过宽）
    int _maxWidthForBubble = 300; // 减小最大宽度以适应对话框
};

#endif // CHATBUBBLE_H
