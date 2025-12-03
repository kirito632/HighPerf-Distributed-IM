#include "messagelistview.h"
#include <QtWidgets>

// 构造函数：初始化消息列表视图
// 
// 功能：
//   1. 创建QScrollArea和消息容器
//   2. 设置布局（垂直布局）
//   3. 连接滚动条信号（实现自动滚动逻辑）
// 
// 自动滚动逻辑：
//   - rangeChanged信号：当新消息添加时，如果_autoScroll=true，滚动到底部
//   - valueChanged信号：如果用户手动滚动，更新_autoScroll状态
MessageListView::MessageListView(QWidget *parent)
    : QWidget(parent),
    _scrollArea(new QScrollArea(this)),
    _container(new QWidget),
    _layout(new QVBoxLayout),
    _autoScroll(true)
{
    // 设置布局对齐方式和间距
    _layout->setAlignment(Qt::AlignTop);
    _layout->setSpacing(6);
    _layout->setContentsMargins(0,0,0,0);
    _container->setLayout(_layout);

    // 设置滚动区域属性
    _scrollArea->setWidgetResizable(true);  // 允许内容自适应大小
    _scrollArea->setWidget(_container);     // 设置滚动内容

    // 创建主布局
    auto *mainLay = new QVBoxLayout(this);
    mainLay->setContentsMargins(0,0,0,0);
    mainLay->addWidget(_scrollArea);
    setLayout(mainLay);

    // 自动滚动逻辑：rangeChanged + valueChanged
    QScrollBar *vbar = _scrollArea->verticalScrollBar();
    // 当滚动范围改变时（新消息添加），如果自动滚动开启，滚动到底部
    connect(vbar, &QScrollBar::rangeChanged, this, [this, vbar](int /*min*/, int max){
        if (_autoScroll) {
            QTimer::singleShot(0, this, [vbar, max](){ vbar->setValue(max); });
        }
    });
    // 当用户手动滚动时，更新自动滚动状态
    connect(vbar, &QScrollBar::valueChanged, this, [this, vbar](int value){
        _autoScroll = (value == vbar->maximum());
    });
}

// 获取垂直滚动条
// 
// 返回值：
//   垂直滚动条指针
QScrollBar* MessageListView::verticalScrollBar() const {
    return _scrollArea->verticalScrollBar();
}

// makeRowForBubble方法：创建消息行容器
// 
// 参数：
//   - bubble: 消息气泡控件
//   - isSender: 是否为发送者
//   - avatar: 头像图片
// 
// 返回值：
//   消息行控件
// 
// 功能：
//   根据isSender设置布局（发送者=右对齐+头像在右，接收者=左对齐+头像在左）
QWidget* MessageListView::makeRowForBubble(ChatBubble *bubble, bool isSender, const QPixmap &avatar) {
    QWidget *row = new QWidget;
    QHBoxLayout *h = new QHBoxLayout(row);
    h->setContentsMargins(12,6,12,6);
    h->setSpacing(8);

    // 创建头像标签
    QLabel *lblAv = new QLabel;
    lblAv->setPixmap(avatar.scaled(40,40, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    lblAv->setFixedSize(40,40);

    if (isSender) {
        // 发送者：气泡在右，头像在右
        h->addStretch();
        h->addWidget(bubble);
        h->addWidget(lblAv);
    } else {
        // 接收者：头像在左，气泡在左
        h->addWidget(lblAv);
        h->addWidget(bubble);
        h->addStretch();
    }
    row->setLayout(h);
    row->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    return row;
}

// addMessage方法：添加文本消息
// 
// 参数：
//   - text: 消息文本
//   - isSender: 是否为发送者
// 
// 功能：
//   1. 创建ChatBubble文本消息气泡
//   2. 设置气泡最大宽度（限制为viewport宽度的60%，最小200px）
//   3. 生成头像占位图
//   4. 创建消息行容器并添加到布局
//   5. 更新布局并滚动到底部
void MessageListView::addMessage(const QString &text, bool isSender) {
    ChatBubble *bubble = new ChatBubble(text, isSender);
    // 限制气泡宽度为 viewport 的一部分（保证换行）
    if (_scrollArea && _scrollArea->viewport()) {
        int avail = _scrollArea->viewport()->width();
        int maxW = qMax(200, static_cast<int>(avail * 0.60));
        bubble->setMaximumWidth(maxW);
    }
    // 头像暂用文字首字符生成占位（上层可替换）
    QPixmap avatar(40,40);
    avatar.fill(Qt::transparent);
    QPainter p(&avatar);
    p.setBrush(QColor("#90CAF9"));
    p.setPen(Qt::NoPen);
    p.drawEllipse(0,0,40,40);
    p.setPen(Qt::white);
    QFont f = p.font(); f.setBold(true); f.setPointSize(12); p.setFont(f);
    p.drawText(avatar.rect(), Qt::AlignCenter, QString(isSender ? "我" : "?"));
    p.end();

    QWidget *row = makeRowForBubble(bubble, isSender, avatar);
    _layout->addWidget(row);

    // 请求布局更新并在事件循环后滚到底（rangeChanged 也会处理）
    _container->updateGeometry();
    _container->adjustSize();
    QTimer::singleShot(0, this, [this]() {
        if (_autoScroll) _scrollArea->verticalScrollBar()->setValue(_scrollArea->verticalScrollBar()->maximum());
    });
}

// addImageMessage方法：添加图片消息
// 
// 参数：
//   - pix: 图片对象
//   - isSender: 是否为发送者
// 
// 功能：
//   1. 创建ChatBubble图片消息气泡
//   2. 设置气泡最大宽度（限制为viewport宽度的60%，最小200px）
//   3. 生成头像占位图
//   4. 创建消息行容器并添加到布局
//   5. 更新布局并滚动到底部
void MessageListView::addImageMessage(const QPixmap &pix, bool isSender) {
    // 创建 ChatBubble（图片类型的构造应该在 ChatBubble 构造函数里处理图片缩放/显示）
    ChatBubble *bubble = new ChatBubble(pix, isSender);

    // ---- 确保气泡按当前可用宽度换行 / 缩放 ----
    // 取 viewport 可用宽度，根据经验我们把消息最大宽度限制为可用宽度的 60%（最小 200）
    if (_scrollArea && _scrollArea->viewport()) {
        int avail = _scrollArea->viewport()->width();
        int maxW = qMax(200, static_cast<int>(avail * 0.60));
        // 这里使用 QWidget::setMaximumWidth 来限制 bubble 的宽度，促使其内部尺寸/绘制按该宽度布局
        bubble->setMaximumWidth(maxW);
        // 强制让 widget 重新计算大小（sizeHint 会基于内容返回合适高度）
        bubble->adjustSize();
    }

    // ---- 生成头像占位（与文本消息一致的样式） ----
    // 头像用于在消息行两侧显示（左侧或右侧），这里只做一个简单圆形带字母的占位
    QPixmap avatar(40,40);
    avatar.fill(Qt::transparent);
    QPainter p(&avatar);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(QColor("#90CAF9"));
    p.setPen(Qt::NoPen);
    p.drawEllipse(0,0,40,40);
    // 在头像上绘制白色字母（"我" 或 "?"）
    p.setPen(Qt::white);
    QFont f = p.font();
    f.setBold(true);
    f.setPointSize(12);
    p.setFont(f);
    p.drawText(avatar.rect(), Qt::AlignCenter, QString(isSender ? "我" : "?"));
    p.end(); // 结束绘制

    // ---- 用行容器包装气泡和头像（根据 isSender 决定左右） ----
    QWidget *row = makeRowForBubble(bubble, isSender, avatar);

    // 把行添加到垂直布局中
    _layout->addWidget(row);

    // ---- 强制更新布局并在事件循环后滚到底 ----
    // updateGeometry/adjustSize 鼓励布局系统立即计算新大小（有助于 rangeChanged 触发）
    if (_container) {
        _container->updateGeometry();
        _container->adjustSize();
    }

    // 延迟执行滚动操作，保证在 Qt 事件循环处理完布局后再滚动（避免跳动）
    QTimer::singleShot(0, this, [this]() {
        if (_autoScroll && _scrollArea) {
            _scrollArea->verticalScrollBar()->setValue(_scrollArea->verticalScrollBar()->maximum());
        }
    });
}


// clearMessages方法：清空所有消息
// 
// 功能：
//   删除消息布局中的所有控件（ChatBubble和消息行容器）
void MessageListView::clearMessages() {
    QLayoutItem *child;
    while ((child = _layout->takeAt(0)) != nullptr) {
        if (child->widget()) child->widget()->deleteLater();
        delete child;
    }
}

// insertHistoryAtTop方法：在顶部插入历史消息
// 
// 参数：
//   - history: 历史消息列表（QPair<文本, isSender>，按时间从旧到新排序）
// 
// 功能：
//   1. 保存当前滚动位置
//   2. 在顶部插入历史消息
//   3. 调整滚动位置，保持用户看到的内容不变
void MessageListView::insertHistoryAtTop(const QList<QPair<QString,bool>> &history) {
    if (history.isEmpty()) return;
    QScrollBar *v = _scrollArea->verticalScrollBar();
    int oldValue = v->value();
    int oldMax = v->maximum();

    // 插入从旧到新（历史[0] 最旧）
    for (int i = 0; i < history.size(); ++i) {
        auto p = history.at(i);
        ChatBubble *bubble = new ChatBubble(p.first, p.second);
        if (_scrollArea && _scrollArea->viewport()) {
            int avail = _scrollArea->viewport()->width();
            bubble->setMaximumWidth(qMax(200, static_cast<int>(avail * 0.60)));
        }
        // no avatar for simplicity; you can provide proper avatar here
        QPixmap avatar(40,40); avatar.fill(Qt::transparent);
        QPainter painter(&avatar);
        painter.setBrush(QColor("#90CAF9"));
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(0,0,40,40);
        painter.end();

        QWidget *row = makeRowForBubble(bubble, p.second, avatar);
        _layout->insertWidget(i, row);
    }

    // 调整滚动抵消新增
    QTimer::singleShot(0, this, [this, oldValue, oldMax]() {
        QScrollBar *v2 = _scrollArea->verticalScrollBar();
        int newMax = v2->maximum();
        int delta = newMax - oldMax;
        v2->setValue(oldValue + delta);
    });
}
