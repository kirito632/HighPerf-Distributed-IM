#include "chatdialog.h"
#include <QtWidgets>
#include <QPainter>
#include <QScrollBar>
#include <QTimer>
#include "localdb.h"
#include "usermgr.h"

// 构造函数：初始化聊天对话框
// 
// 参数：
//   - parent: 父窗口
// 
// 功能：
//   1. 初始化所有成员变量为nullptr
//   2. 调用setupUi()创建UI界面
ChatDialog::ChatDialog(QWidget *parent)
    : QDialog(parent)
    , m_mainLayout(nullptr)
    , m_headerWidget(nullptr)
    , m_titleLabel(nullptr)
    , m_closeButton(nullptr)
    , m_functionWidget(nullptr)
    , m_exportButton(nullptr)
    , m_fileHelperButton(nullptr)
    , m_openButton(nullptr)
    , m_copyButton(nullptr)
    , m_forwardButton(nullptr)
    , m_messageScrollArea(nullptr)
    , m_messageContainer(nullptr)
    , m_messageLayout(nullptr)
    , m_inputWidget(nullptr)
    , m_inputLayout(nullptr)
    , m_attachButton(nullptr)
    , m_textEdit(nullptr)
    , m_sendButton(nullptr)
    , m_bottomBar(nullptr)
    , m_bottomLayout(nullptr)
    , m_cutButton(nullptr)
    , m_folderButton(nullptr)
{
    setupUi();
}


// setupUi方法：初始化UI界面
// 
// 功能：
//   1. 设置窗口标题、大小、样式
//   2. 创建主布局（垂直布局）
//   3. 设置各个UI区域（头部、消息区域、输入区域、底部栏）
void ChatDialog::setupUi()
{
    // 设置窗口属性
    setWindowTitle("我的Android手机");
    setFixedSize(800, 600);
    setWindowFlags(Qt::Dialog | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    
    // 设置窗口样式
    setStyleSheet("QDialog { background-color: white; }");
    
    // 创建主布局
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);
    
    // 设置各个区域
    setupHeader();
    setupMessageArea();
    setupInputArea();
    setupBottomBar();
}

// setupHeader方法：设置头部区域
// 
// 功能：
//   1. 创建头部区域控件（蓝色渐变背景）
//   2. 创建标题标签（显示联系人名称）
//   3. 创建关闭按钮（×符号）
//   4. 连接关闭按钮信号到QDialog::close
void ChatDialog::setupHeader()
{
    // 创建头部区域
    m_headerWidget = new QWidget;
    m_headerWidget->setFixedHeight(50);
    m_headerWidget->setStyleSheet(
        "QWidget {"
        "    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
        "        stop:0 #4A90E2, stop:1 #357ABD);"
        "    border: none;"
        "}"
    );
    
    QHBoxLayout *headerLayout = new QHBoxLayout(m_headerWidget);
    headerLayout->setContentsMargins(15, 10, 15, 10);
    
    // 标题
    m_titleLabel = new QLabel("我的Android手机");
    m_titleLabel->setStyleSheet(
        "QLabel {"
        "    color: white;"
        "    font-size: 18px;"
        "    font-weight: bold;"
        "    background: transparent;"
        "}"
    );
    
    // 关闭按钮
    m_closeButton = new QPushButton("×");
    m_closeButton->setFixedSize(30, 30);
    m_closeButton->setStyleSheet(
        "QPushButton {"
        "    background: transparent;"
        "    border: none;"
        "    color: white;"
        "    font-size: 20px;"
        "    font-weight: bold;"
        "}"
        "QPushButton:hover {"
        "    background: rgba(255, 255, 255, 0.2);"
        "    border-radius: 15px;"
        "}"
    );
    
    headerLayout->addWidget(m_titleLabel);
    headerLayout->addStretch();
    headerLayout->addWidget(m_closeButton);
    
    m_mainLayout->addWidget(m_headerWidget);
    
    // 连接关闭按钮
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::close);
}

// setupMessageArea方法：设置消息显示区域
// 
// 功能：
//   1. 创建功能按钮区域（导出手机相册、打开文件助手、操作按钮）
//   2. 创建可滚动的消息显示区域（QScrollArea）
//   3. 创建消息容器（QVBoxLayout，包含stretch在底部）
//   4. 设置滚动条样式
//   5. 连接按钮信号
//   6. 添加示例消息
void ChatDialog::setupMessageArea()
{
    // 创建功能按钮区域
    m_functionWidget = new QWidget;
    m_functionWidget->setFixedHeight(60);
    m_functionWidget->setStyleSheet("QWidget { background: white; }");
    
    QHBoxLayout *functionLayout = new QHBoxLayout(m_functionWidget);
    functionLayout->setContentsMargins(15, 10, 15, 10);
    functionLayout->setSpacing(15);
    
    // 导出手机相册按钮
    m_exportButton = new QPushButton("📷 导出手机相册");
    m_exportButton->setStyleSheet(
        "QPushButton {"
        "    background: #f0f0f0;"
        "    border: 1px solid #ddd;"
        "    border-radius: 5px;"
        "    padding: 8px 15px;"
        "    font-size: 14px;"
        "    color: #333;"
        "}"
        "QPushButton:hover {"
        "    background: #e0e0e0;"
        "}"
    );
    
    // 打开文件助手按钮
    m_fileHelperButton = new QPushButton("📁 打开文件助手");
    m_fileHelperButton->setStyleSheet(
        "QPushButton {"
        "    background: #f0f0f0;"
        "    border: 1px solid #ddd;"
        "    border-radius: 5px;"
        "    padding: 8px 15px;"
        "    font-size: 14px;"
        "    color: #333;"
        "}"
        "QPushButton:hover {"
        "    background: #e0e0e0;"
        "}"
    );
    
    functionLayout->addWidget(m_exportButton);
    functionLayout->addWidget(m_fileHelperButton);
    functionLayout->addStretch();
    
    // 右侧操作按钮
    m_openButton = new QPushButton("打开");
    m_copyButton = new QPushButton("复制");
    m_forwardButton = new QPushButton("转发");
    
    QString buttonStyle = 
        "QPushButton {"
        "    background: transparent;"
        "    border: none;"
        "    color: #4A90E2;"
        "    font-size: 14px;"
        "    padding: 5px 10px;"
        "}"
        "QPushButton:hover {"
        "    background: #f0f8ff;"
        "    border-radius: 3px;"
        "}";
    
    m_openButton->setStyleSheet(buttonStyle);
    m_copyButton->setStyleSheet(buttonStyle);
    m_forwardButton->setStyleSheet(buttonStyle);
    
    functionLayout->addWidget(m_openButton);
    functionLayout->addWidget(m_copyButton);
    functionLayout->addWidget(m_forwardButton);
    
    m_mainLayout->addWidget(m_functionWidget);
    
    // 创建消息显示区域
    m_messageScrollArea = new QScrollArea;
    m_messageScrollArea->setWidgetResizable(true);
    m_messageScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_messageScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_messageScrollArea->setStyleSheet(
        "QScrollArea {"
        "    background: white;"
        "    border: none;"
        "}"
        "QScrollBar:vertical {"
        "    background: #f0f0f0;"
        "    width: 8px;"
        "    border-radius: 4px;"
        "}"
        "QScrollBar::handle:vertical {"
        "    background: #c0c0c0;"
        "    border-radius: 4px;"
        "    min-height: 20px;"
        "}"
        "QScrollBar::handle:vertical:hover {"
        "    background: #a0a0a0;"
        "}"
    );
    
    // 创建消息容器
    m_messageContainer = new QWidget;
    m_messageLayout = new QVBoxLayout(m_messageContainer);
    m_messageLayout->setContentsMargins(20, 20, 20, 20);
    m_messageLayout->setSpacing(15);
    m_messageLayout->addStretch();
    
    m_messageScrollArea->setWidget(m_messageContainer);
    m_mainLayout->addWidget(m_messageScrollArea);
    
    // 连接按钮信号
    connect(m_exportButton, &QPushButton::clicked, this, &ChatDialog::onExportClicked);
    connect(m_fileHelperButton, &QPushButton::clicked, this, &ChatDialog::onFileHelperClicked);
}

// setupInputArea方法：设置输入区域
// 
// 功能：
//   1. 创建输入区域控件
//   2. 创建附件按钮（📎图标）
//   3. 创建文本输入框（支持多行输入）
//   4. 创建发送按钮
//   5. 连接信号（附件按钮、发送按钮、文本变化）
void ChatDialog::setupInputArea()
{
    // 创建输入区域
    m_inputWidget = new QWidget;
    m_inputWidget->setFixedHeight(100);
    m_inputWidget->setStyleSheet("QWidget { background: white; }");
    
    m_inputLayout = new QHBoxLayout(m_inputWidget);
    m_inputLayout->setContentsMargins(15, 10, 15, 10);
    m_inputLayout->setSpacing(10);
    
    // 附件按钮
    m_attachButton = new QPushButton("📎");
    m_attachButton->setFixedSize(40, 40);
    m_attachButton->setStyleSheet(
        "QPushButton {"
        "    background: #f0f0f0;"
        "    border: 1px solid #ddd;"
        "    border-radius: 20px;"
        "    font-size: 18px;"
        "}"
        "QPushButton:hover {"
        "    background: #e0e0e0;"
        "}"
    );
    
    // 文本输入框
    m_textEdit = new QTextEdit;
    m_textEdit->setFixedHeight(80);
    m_textEdit->setStyleSheet(
        "QTextEdit {"
        "    border: 1px solid #ddd;"
        "    border-radius: 8px;"
        "    padding: 10px;"
        "    font-size: 14px;"
        "    background: white;"
        "}"
    );
    m_textEdit->setPlaceholderText("输入消息...");
    
    // 发送按钮
    m_sendButton = new QPushButton("发送(S)");
    m_sendButton->setFixedSize(80, 40);
    m_sendButton->setStyleSheet(
        "QPushButton {"
        "    background: #4A90E2;"
        "    color: white;"
        "    border: none;"
        "    border-radius: 6px;"
        "    font-size: 14px;"
        "    font-weight: bold;"
        "}"
        "QPushButton:hover {"
        "    background: #357ABD;"
        "}"
        "QPushButton:pressed {"
        "    background: #2968A3;"
        "}"
    );
    
    m_inputLayout->addWidget(m_attachButton);
    m_inputLayout->addWidget(m_textEdit);
    m_inputLayout->addWidget(m_sendButton);
    
    m_mainLayout->addWidget(m_inputWidget);
    
    // 连接信号
    connect(m_attachButton, &QPushButton::clicked, this, &ChatDialog::onAttachClicked);
    connect(m_sendButton, &QPushButton::clicked, this, &ChatDialog::onSendClicked);
    connect(m_textEdit, &QTextEdit::textChanged, this, [this]() {
        // 检查文字长度，如果超过限制则自动转为文件发送
        QString text = m_textEdit->toPlainText();
        if (text.length() > 1000) { // 假设1000字符为限制
            // 这里可以添加自动转为文件发送的逻辑
        }
    });
}

// setupBottomBar方法：设置底部操作栏
// 
// 功能：
//   1. 创建底部操作栏控件
//   2. 创建剪切按钮（✂图标）
//   3. 创建文件夹按钮（📁图标）
void ChatDialog::setupBottomBar()
{
    // 创建底部操作栏
    m_bottomBar = new QWidget;
    m_bottomBar->setFixedHeight(50);
    m_bottomBar->setStyleSheet("QWidget { background: white; border-top: 1px solid #e0e0e0; }");
    
    m_bottomLayout = new QHBoxLayout(m_bottomBar);
    m_bottomLayout->setContentsMargins(15, 8, 15, 8);
    m_bottomLayout->setSpacing(20);
    
    // 剪切按钮
    m_cutButton = new QPushButton("✂");
    m_cutButton->setFixedSize(40, 40);
    m_cutButton->setStyleSheet(
        "QPushButton {"
        "    background: transparent;"
        "    border: none;"
        "    font-size: 18px;"
        "    color: #666;"
        "}"
        "QPushButton:hover {"
        "    background: #f0f0f0;"
        "    border-radius: 20px;"
        "}"
    );
    
    // 文件夹按钮
    m_folderButton = new QPushButton("📁");
    m_folderButton->setFixedSize(40, 40);
    m_folderButton->setStyleSheet(
        "QPushButton {"
        "    background: transparent;"
        "    border: none;"
        "    font-size: 18px;"
        "    color: #666;"
        "}"
        "QPushButton:hover {"
        "    background: #f0f0f0;"
        "    border-radius: 20px;"
        "}"
    );
    
    m_bottomLayout->addWidget(m_cutButton);
    m_bottomLayout->addWidget(m_folderButton);
    m_bottomLayout->addStretch();
    
    m_mainLayout->addWidget(m_bottomBar);
}

// setCurrentContact方法：设置当前聊天对象
// 
// 参数：
//   - contactName: 联系人名称
// 
// 功能：
//   1. 保存当前联系人名称
//   2. 更新标题标签文本
//   3. 更新窗口标题
void ChatDialog::setCurrentContact(const QString &contactName, int contactUid)
{
    m_currentContact = contactName;
    m_currentContactUid = contactUid;
    if (m_titleLabel) {
        m_titleLabel->setText(contactName);
    }
    setWindowTitle(contactName);

    // 加载历史记录
    clearMessages();

    // 获取最近 50 条
    auto msgs = LocalDb::GetInstance()->GetHistory(contactUid, 0, 50);
    int myUid = UserMgr::GetInstance()->GetUid();

    for (const auto& msg : msgs) {
        bool isSender = (msg.from_uid == myUid);
        // 这里简单处理：只显示文本消息
        // 如果你以后支持图片 msg.type == 1，可以在这里 switch case
        addMessage(msg.content, isSender);
    }
}

// addMessage方法：添加文字消息
// 
// 参数：
//   - text: 消息内容
//   - isSender: 是否为发送者（true=右对齐，false=左对齐）
// 
// 功能：
//   1. 创建ChatBubble消息气泡
//   2. 创建消息行容器，根据isSender设置对齐方式
//   3. 插入到消息布局中（在stretch之前）
//   4. 延迟滚动到底部（使用QTimer）
void ChatDialog::addMessage(const QString &text, bool isSender)
{
    // 创建消息气泡
    ChatBubble *bubble = new ChatBubble(text, isSender);
    
    // 创建消息行容器
    QWidget *messageRow = new QWidget;
    QHBoxLayout *rowLayout = new QHBoxLayout(messageRow);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(10);
    
    if (isSender) {
        // 发送者消息靠右
        rowLayout->addStretch();
        rowLayout->addWidget(bubble);
    } else {
        // 接收者消息靠左
        rowLayout->addWidget(bubble);
        rowLayout->addStretch();
    }
    
    // 插入到消息布局中（在stretch之前）
    int insertIndex = m_messageLayout->count() - 1;
    m_messageLayout->insertWidget(insertIndex, messageRow);
    
    // 滚动到底部
    QTimer::singleShot(0, this, &ChatDialog::scrollToBottom);
}

// addImageMessage方法：添加图片消息
// 
// 参数：
//   - pix: 图片对象
//   - isSender: 是否为发送者
// 
// 功能：
//   1. 创建ChatBubble图片消息气泡
//   2. 创建消息行容器，根据isSender设置对齐方式
//   3. 插入到消息布局中（在stretch之前）
//   4. 延迟滚动到底部（使用QTimer）
void ChatDialog::addImageMessage(const QPixmap &pix, bool isSender)
{
    // 创建图片消息气泡
    ChatBubble *bubble = new ChatBubble(pix, isSender);
    
    // 创建消息行容器
    QWidget *messageRow = new QWidget;
    QHBoxLayout *rowLayout = new QHBoxLayout(messageRow);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(10);
    
    if (isSender) {
        rowLayout->addStretch();
        rowLayout->addWidget(bubble);
    } else {
        rowLayout->addWidget(bubble);
        rowLayout->addStretch();
    }
    
    // 插入到消息布局中
    int insertIndex = m_messageLayout->count() - 1;
    m_messageLayout->insertWidget(insertIndex, messageRow);
    
    // 滚动到底部
    QTimer::singleShot(0, this, &ChatDialog::scrollToBottom);
}

// clearMessages方法：清空所有消息
// 
// 功能：
//   1. 删除消息布局中的所有控件（ChatBubble）
//   2. 保留stretch（用于底部对齐）
void ChatDialog::clearMessages()
{
    // 清空所有消息（保留stretch）
    QLayoutItem *item;
    while ((item = m_messageLayout->takeAt(0)) != nullptr) {
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }
    m_messageLayout->addStretch();
}

// scrollToBottom方法：滚动到底部
// 
// 功能：
//   将消息列表滚动到最底部，显示最新消息
void ChatDialog::scrollToBottom()
{
    if (m_messageScrollArea) {
        QScrollBar *scrollBar = m_messageScrollArea->verticalScrollBar();
        if (scrollBar) {
            scrollBar->setValue(scrollBar->maximum());
        }
    }
}

// makeAvatar方法：生成头像
// 
// 参数：
//   - text: 头像文字（通常是用户名的前两个字符）
//   - bg: 背景颜色
//   - size: 头像大小
// 
// 返回值：
//   生成的圆形头像QPixmap
// 
// 功能：
//   创建一个圆形的、带有文字的彩色头像图片
QPixmap ChatDialog::makeAvatar(const QString &text, const QColor &bg, int size)
{
    QPixmap pix(size, size);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(bg);
    p.setPen(Qt::NoPen);
    p.drawEllipse(0, 0, size, size);
    p.setPen(Qt::white);
    QFont f = p.font();
    f.setBold(true);
    f.setPointSize(size / 3);
    p.setFont(f);
    QRect r(0, 0, size, size);
    p.drawText(r, Qt::AlignCenter, text.left(2).toUpper());
    return pix;
}

// onSendClicked方法：处理发送按钮点击
// 
// 功能：
//   1. 获取文本输入框内容并去除空格
//   2. 如果内容为空则返回
//   3. 添加消息到消息列表（isSender=true，右对齐）
//   4. 发送messageSent信号（可用于网络传输）
//   5. 清空文本输入框
void ChatDialog::onSendClicked()
{
    QString text = m_textEdit->toPlainText().trimmed();
    if (text.isEmpty()) return;
    
    // 添加发送的消息
    addMessage(text, true);
    
    // 发送信号
    if (!m_currentContact.isEmpty()) {
        emit messageSent(m_currentContact, text);
    }
    
    // 清空输入框
    m_textEdit->clear();
}

// onAttachClicked方法：处理附件按钮点击
// 
// 功能：
//   提示用户附件功能待实现（预留功能）
void ChatDialog::onAttachClicked()
{
    // 处理附件按钮点击
    QMessageBox::information(this, "附件", "附件功能待实现");
}

// onExportClicked方法：处理导出按钮点击
// 
// 功能：
//   提示用户导出手机相册功能待实现（预留功能）
void ChatDialog::onExportClicked()
{
    // 处理导出按钮点击
    QMessageBox::information(this, "导出", "导出手机相册功能待实现");
}

// onFileHelperClicked方法：处理文件助手按钮点击
// 
// 功能：
//   提示用户文件助手功能待实现（预留功能）
void ChatDialog::onFileHelperClicked()
{
    // 处理文件助手按钮点击
    QMessageBox::information(this, "文件助手", "打开文件助手功能待实现");
}

// addSampleMessages方法：添加示例消息
// 
// 功能：
//   在初始化时添加一些示例消息用于演示效果
//   包括短消息、长消息、发送者和接收者消息
void ChatDialog::addSampleMessages()
{
    // 已移除示例消息，实际消息由真实收发逻辑填充
    return;
}

