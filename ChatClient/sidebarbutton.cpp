#include "sidebarbutton.h"
#include <QPainter>

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
SidebarButton::SidebarButton(const QString &text, QWidget *parent)
    : QPushButton(text, parent)
{
    setCheckable(true); // 可被选中（用于 QButtonGroup exclusive）
    // 美化：文字在下，图标/占位在上（如果需要图标，可用 setIcon）：
    setStyleSheet(R"(
        SidebarButton {
            border: none;
            background: transparent;
            padding: 8px;
            font-size: 14px;
        }
        SidebarButton:checked {
            background: rgba(0,0,0,0.04);
            border-radius: 8px;
        }
    )");
    setCursor(Qt::PointingHandCursor);
    setMinimumHeight(56);
    setMaximumWidth(80);
}

// setNotification方法：控制红点显示
// 
// 参数：
//   - on: 是否显示红点
// 
// 功能：
//   设置或清除通知红点标志，触发重绘
void SidebarButton::setNotification(bool on)
{
    if (_hasNotification == on) return;
    _hasNotification = on;
    update(); // 重绘以显示/隐藏红点
}

// paintEvent方法：绘制按钮和红点
// 
// 功能：
//   1. 调用QPushButton的paintEvent绘制按钮
//   2. 如果有通知，在右上角绘制红色圆点（直径12px）
void SidebarButton::paintEvent(QPaintEvent *ev)
{
    // 先让 QPushButton 自己画背景/文本
    QPushButton::paintEvent(ev);

    if (!_hasNotification) return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // 红点大小与位置（右上角）
    const int dotDiameter = 12;  // 红点直径
    const int margin = 6;        // 边距
    int x = width() - dotDiameter - margin;
    int y = margin;

    QRect dotRect(x, y, dotDiameter, dotDiameter);
    p.setBrush(QColor("#F44336")); // 红色圆点
    p.setPen(Qt::NoPen);
    p.drawEllipse(dotRect);
}
