#include "chatbubble.h"
#include <QPainter>
#include <QPainterPath>
#include <QTextDocument>
#include <QTextOption>

// 构造函数：创建文字消息气泡
// 
// 参数：
//   - text: 消息文本内容
//   - isSender: 是否为发送者（true=右对齐蓝色，false=左对齐灰色）
//   - parent: 父控件
// 
// 功能：
//   创建并配置文字消息气泡，设置样式和布局
ChatBubble::ChatBubble(const QString &text, bool isSender, QWidget *parent)
    : QWidget(parent), _isSender(isSender), _isImage(false)
{
    setupTextBubble(text);
}

// 构造函数：创建图片消息气泡
// 
// 参数：
//   - pix: 图片对象
//   - isSender: 是否为发送者
//   - parent: 父控件
// 
// 功能：
//   创建并配置图片消息气泡，设置样式和布局
ChatBubble::ChatBubble(const QPixmap &pix, bool isSender, QWidget *parent)
    : QWidget(parent), _isSender(isSender), _isImage(true), _pix(pix)
{
    setupImageBubble(pix);
}

// setupTextBubble方法：设置文字消息气泡
// 
// 参数：
//   - text: 消息文本内容
// 
// 功能：
//   1. 设置控件属性（透明背景、大小策略）
//   2. 创建QTextBrowser控件用于显示文本
//   3. 配置文本换行和字体
//   4. 设置HTML内容
void ChatBubble::setupTextBubble(const QString &text)
{
    // 设置控件为透明背景（用于绘制圆角气泡）
    setAttribute(Qt::WA_TranslucentBackground);
    // 设置大小策略（优先大小，最小高度）
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);

    // 创建文本浏览器控件
    _textBrowser = new QTextBrowser(this);
    _textBrowser->setReadOnly(true);                           // 只读
    _textBrowser->setFrameStyle(QFrame::NoFrame);             // 无边框
    _textBrowser->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);   // 无垂直滚动条
    _textBrowser->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);  // 无水平滚动条
    _textBrowser->setOpenLinks(true);                         // 打开链接
    _textBrowser->setStyleSheet("background: transparent;");   // 透明背景

    // 设置文本换行（自动换行到任何地方）
    _textBrowser->setLineWrapMode(QTextEdit::WidgetWidth);
    _textBrowser->setWordWrapMode(QTextOption::WrapAnywhere);

    // 设置字体（12号字体）
    QFont f;
    f.setPointSize(12);
    _textBrowser->setFont(f);

    // 设置文档选项（任意地方换行）
    QTextDocument *doc = _textBrowser->document();
    QTextOption opt = doc->defaultTextOption();
    opt.setWrapMode(QTextOption::WrapAnywhere);
    doc->setDefaultTextOption(opt);

    // 设置文本内容
    _textBrowser->setText(text);
    // 更新几何布局
    updateGeometry();
}

// setupImageBubble方法：设置图片消息气泡
// 
// 参数：
//   - pix: 图片对象
// 
// 功能：
//   1. 设置控件属性（透明背景、大小策略）
//   2. 创建QLabel控件用于显示图片
//   3. 缩放图片以适应宽度限制（maxWidthForBubble=300）
void ChatBubble::setupImageBubble(const QPixmap &pix)
{
    // 设置控件为透明背景
    setAttribute(Qt::WA_TranslucentBackground);
    // 设置大小策略
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);

    // 创建图片标签
    _imageLabel = new QLabel(this);
    _imageLabel->setScaledContents(false);
    
    // 限制图片显示宽度（防止图片过大）
    int maxW = _maxWidthForBubble;  // 300像素
    QPixmap scaled = pix;
    if (pix.width() > maxW) {
        // 如果图片宽度超过限制，缩放图片
        scaled = pix.scaledToWidth(maxW, Qt::SmoothTransformation);
    }
    _imageLabel->setPixmap(scaled);
    _imageLabel->setFrameStyle(QFrame::NoFrame);
    // 更新几何布局
    updateGeometry();
}

// sizeHint方法：获取建议的控件大小
// 
// 返回值：
//   控件建议的大小（根据内容自动计算）
// 
// 功能：
//   1. 对于图片消息：根据图片实际大小计算
//   2. 对于文字消息：根据文档高度计算，考虑最大宽度限制
//   3. 返回包含margins的完整大小
QSize ChatBubble::sizeHint() const
{
    const int margin = 12;  // 边距
    if (_isImage && _imageLabel) {
        // 图片消息：根据图片大小计算
        QSize img = _imageLabel->pixmap() ? _imageLabel->pixmap()->size() : QSize(100, 100);
        return QSize(img.width() + margin * 2, img.height() + margin * 2);
    }
    if (_textBrowser) {
        // 文字消息：计算文档高度
        QTextDocument *doc = _textBrowser->document();
        int w = _maxWidthForBubble;  // 默认最大宽度300
        // 如果父控件存在，使用父控件宽度的50%和300中的较小值
        if (parentWidget()) {
            int pw = parentWidget()->width();
            w = qMin(_maxWidthForBubble, qMax(150, pw * 50 / 100));
        }
        // 设置文档宽度
        doc->setTextWidth(w);
        QSizeF docSize = doc->size();
        return QSize(static_cast<int>(docSize.width()) + margin * 2,
                     static_cast<int>(docSize.height()) + margin * 2);
    }
    return QSize(100, 50); // 默认大小
}

// paintEvent方法：绘制消息气泡
// 
// 功能：
//   1. 绘制圆角矩形背景
//   2. 根据isSender设置不同的颜色（发送者=蓝色，接收者=灰色）
//   3. 绘制尾巴（三角形指向左侧或右侧）
void ChatBubble::paintEvent(QPaintEvent *ev)
{
    Q_UNUSED(ev);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);  // 启用抗锯齿

    QRect r = rect();
    const int margin = 12;  // 边距
    QRect bubbleRect = r.adjusted(margin, margin, -margin, -margin);

    const int tailW = 10;  // 尾巴宽度
    QPainterPath path;
    
    // 设置气泡颜色
    // 发送者：蓝色；接收者：灰色
    QColor bgColor = _isSender ? QColor("#4A90E2") : QColor("#f1f1f1");
    QColor borderColor = _isSender ? QColor("#357ABD") : QColor(220, 220, 220);

    if (_isSender) {
        // 发送者气泡靠右，尾巴在右边
        QRect body = bubbleRect.adjusted(0, 0, -tailW, 0);
        path.addRoundedRect(body, 12, 12);
        
        // 绘制尾巴（指向右侧的三角形）
        QPointF p1(body.right(), body.center().y() - 8);
        QPointF p2(body.right() + tailW, body.center().y());
        QPointF p3(body.right(), body.center().y() + 8);
        path.moveTo(p1);
        path.lineTo(p2);
        path.lineTo(p3);
        path.closeSubpath();
    } else {
        // 接收者气泡靠左，尾巴在左边
        QRect body = bubbleRect.adjusted(tailW, 0, 0, 0);
        path.addRoundedRect(body, 12, 12);
        
        // 绘制尾巴（指向左侧的三角形）
        QPointF p1(body.left(), body.center().y() - 8);
        QPointF p2(body.left() - tailW, body.center().y());
        QPointF p3(body.left(), body.center().y() + 8);
        path.moveTo(p1);
        path.lineTo(p2);
        path.lineTo(p3);
        path.closeSubpath();
    }

    // 填充气泡
    p.fillPath(path, bgColor);
    
    // 绘制边框
    p.setPen(QPen(borderColor, 1));
    p.drawPath(path);
}

// resizeEvent方法：处理控件大小改变事件
// 
// 功能：
//   1. 当控件大小改变时，调整内部的文本浏览器或图片标签的大小
//   2. 对于文字消息：更新文档宽度以适应新的尺寸
void ChatBubble::resizeEvent(QResizeEvent *ev)
{
    QWidget::resizeEvent(ev);
    const int margin = 12;
    
    if (_isImage && _imageLabel) {
        // 图片消息：调整图片标签的大小和位置
        QRect r = rect().adjusted(margin, margin, -margin, -margin);
        _imageLabel->setGeometry(r);
    } else if (_textBrowser) {
        // 文字消息：调整文本浏览器的大小和位置
        QRect r = rect().adjusted(margin, margin, -margin, -margin);
        _textBrowser->setGeometry(r);
        
        // 更新文档宽度以适应新的尺寸
        QTextDocument *doc = _textBrowser->document();
        doc->setTextWidth(r.width());
    }
}
