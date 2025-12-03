#ifndef SIDEBARBUTTON_H
#define SIDEBARBUTTON_H

#pragma once
#include <QPushButton>

// SidebarButton类：侧边栏按钮（自定义按钮）
// 
// 作用：
//   1. 提供带通知红点的侧边栏按钮
//   2. 支持可选中状态（用于QButtonGroup互斥选择）
//   3. 可以设置key用于识别按钮
// 
// 设计特点：
//   - 继承QPushButton
//   - 选中时有背景高亮
//   - 可以显示/隐藏红色通知点（右上角）
//   - 设置最大宽度80px，最小高度56px
class SidebarButton : public QPushButton
{
    Q_OBJECT
public:
    // 构造函数：初始化侧边栏按钮
    // 
    // 参数：
    //   - text: 按钮文本
    //   - parent: 父控件
    // 
    // 功能：
    //   1. 设置为可选中（用于QButtonGroup互斥选择）
    //   2. 设置样式（透明背景、选中高亮）
    //   3. 设置光标为手型
    //   4. 设置最小高度56px，最大宽度80px
    explicit SidebarButton(const QString &text, QWidget *parent = nullptr);

    // 控制红点显示（通知）
    // 参数：
    //   - on: 是否显示红点
    // 
    // 功能：
    //   设置或清除通知红点，触发重绘
    void setNotification(bool on);
    
    // 获取是否有通知
    // 
    // 返回值：
    //   有通知返回true，否则返回false
    bool hasNotification() const { return _hasNotification; }

    // 设置按钮key（用于识别按钮）
    // 参数：
    //   - k: key值
    void setKey(const QString &k) { _key = k; }
    
    // 获取按钮key
    // 
    // 返回值：
    //   按钮的key值
    QString key() const { return _key; }

protected:
    // 绘制事件：绘制按钮和红点
    // 
    // 功能：
    //   1. 调用QPushButton的paintEvent绘制按钮
    //   2. 如果有通知，在右上角绘制红色圆点
    void paintEvent(QPaintEvent *ev) override;

private:
    // 是否有通知（显示红点）
    bool _hasNotification = false;
    
    // 按钮的key（用于识别按钮）
    QString _key;
};

#endif // SIDEBARBUTTON_H
