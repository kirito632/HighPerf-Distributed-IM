#include "addfrienddialog.h"
#include "friendmanager.h"
#include <QPainter>
#include <QMessageBox>
#include <QApplication>
#include <algorithm>

// 构造函数：初始化添加好友对话框
// 
// 参数：
//   - parent: 父窗口
// 
// 功能：
//   1. 调用setupUi()创建UI界面
//   2. 调用initializeMockData()初始化模拟数据
AddFriendDialog::AddFriendDialog(QWidget *parent)
    : QDialog(parent)
    , m_mainLayout(nullptr)
    , m_searchWidget(nullptr)
    , m_searchLayout(nullptr)
    , m_searchEdit(nullptr)
    , m_searchBtn(nullptr)
    , m_tabWidget(nullptr)
    , m_searchResults(nullptr)
    , m_friendManager(nullptr)
{
    setupUi();
    
    // 初始化模拟数据
    initializeMockData();
}

// setFriendManager方法：设置好友管理器
// 
// 参数：
//   - manager: FriendManager指针，用于网络请求
// 
// 功能：
//   1. 保存FriendManager引用
//   2. 连接FriendManager的信号（搜索结果、好友申请结果、错误）到本地槽函数
void AddFriendDialog::setFriendManager(FriendManager *manager)
{
    m_friendManager = manager;
    
    if (m_friendManager) {
        // 连接信号
        connect(m_friendManager, &FriendManager::searchResultsReceived,
                this, &AddFriendDialog::onSearchResultsReceived);
        connect(m_friendManager, &FriendManager::friendRequestSent,
                this, &AddFriendDialog::onFriendRequestSent);
        connect(m_friendManager, &FriendManager::errorOccurred,
                this, &AddFriendDialog::onErrorOccurred);
    }
}

// setupUi方法：初始化UI界面
// 
// 功能：
//   1. 设置窗口标题、大小、样式
//   2. 创建主布局
//   3. 设置搜索区域和标签页
void AddFriendDialog::setupUi()
{
    // 设置窗口属性
    setWindowTitle("查找");
    setFixedSize(600, 500);
    setWindowFlags(Qt::Dialog | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    
    // 设置窗口样式
    setStyleSheet("QDialog { background-color: white; }");
    
    // 创建主布局
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);
    
    // 设置各个区域
    setupSearchArea();
    setupTabWidget();
}

// setupSearchArea方法：设置搜索区域
// 
// 功能：
//   1. 创建搜索区域控件
//   2. 创建搜索输入框和查找按钮
//   3. 设置样式（圆角、悬停效果）
//   4. 连接按钮信号到onSearchClicked
void AddFriendDialog::setupSearchArea()
{
    // 创建搜索区域
    m_searchWidget = new QWidget;
    m_searchWidget->setFixedHeight(80);
    m_searchWidget->setStyleSheet("QWidget { background: white; }");
    
    m_searchLayout = new QHBoxLayout(m_searchWidget);
    m_searchLayout->setContentsMargins(20, 15, 20, 15);
    m_searchLayout->setSpacing(15);
    
    // 搜索框
    m_searchEdit = new QLineEdit;
    m_searchEdit->setPlaceholderText("搜索邮箱号或用户名");
    m_searchEdit->setFixedHeight(40);
    m_searchEdit->setStyleSheet(
        "QLineEdit {"
        "    border: 1px solid #ddd;"
        "    border-radius: 6px;"
        "    padding: 0 15px;"
        "    font-size: 14px;"
        "    background: white;"
        "}"
        "QLineEdit:focus {"
        "    border: 2px solid #4A90E2;"
        "}"
    );
    
    // 查找按钮
    m_searchBtn = new QPushButton("查找");
    m_searchBtn->setFixedSize(80, 40);
    m_searchBtn->setStyleSheet(
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
    
    m_searchLayout->addWidget(m_searchEdit);
    m_searchLayout->addWidget(m_searchBtn);
    
    m_mainLayout->addWidget(m_searchWidget);
    
    // 连接信号
    connect(m_searchBtn, &QPushButton::clicked, this, &AddFriendDialog::onSearchClicked);
    connect(m_searchEdit, &QLineEdit::textChanged, this, &AddFriendDialog::onSearchTextChanged);
}

// setupTabWidget方法：设置标签页
// 
// 功能：
//   1. 创建标签页控件
//   2. 设置标签页样式
//   3. 添加"搜索结果"标签页
void AddFriendDialog::setupTabWidget()
{
    // 创建标签页
    m_tabWidget = new QTabWidget;
    m_tabWidget->setStyleSheet(
        "QTabWidget::pane {"
        "    border: 1px solid #ddd;"
        "    background: white;"
        "}"
        "QTabBar::tab {"
        "    background: #f5f5f5;"
        "    border: 1px solid #ddd;"
        "    padding: 10px 20px;"
        "    font-size: 14px;"
        "    color: #666;"
        "}"
        "QTabBar::tab:selected {"
        "    background: white;"
        "    color: #4A90E2;"
        "    border-bottom: 2px solid #4A90E2;"
        "}"
    );
    
    // 只保留搜索结果标签页
    setupSearchResults();
    m_tabWidget->addTab(m_searchResults, "搜索结果");
    
    m_mainLayout->addWidget(m_tabWidget);
}

// setupSearchResults方法：设置搜索结果列表
// 
// 功能：
//   1. 创建搜索结果列表控件
//   2. 设置列表样式（白色背景、悬停效果）
void AddFriendDialog::setupSearchResults()
{
    m_searchResults = new QListWidget;
    m_searchResults->setStyleSheet(
        "QListWidget {"
        "    background: white;"
        "    border: none;"
        "    outline: none;"
        "}"
        "QListWidget::item {"
        "    border: none;"
        "    border-bottom: 1px solid #f0f0f0;"
        "    padding: 15px;"
        "    min-height: 80px;"
        "}"
        "QListWidget::item:hover {"
        "    background: #f8f9fa;"
        "}"
    );
}


// initializeMockData方法：初始化模拟数据
// 
// 功能：
//   创建一些模拟用户数据用于测试（当没有网络连接时使用）
void AddFriendDialog::initializeMockData()
{
    // 初始化模拟用户数据
    m_allUsers = {
        {301, "用户A", "userA@example.com", "", false, false},
        {302, "用户B", "userB@example.com", "", false, false},
        {303, "用户C", "userC@example.com", "", false, false},
        {304, "用户D", "userD@example.com", "", false, false},
        {305, "用户E", "userE@example.com", "", false, false}
    };
}

// onSearchClicked方法：处理查找按钮点击
// 
// 功能：
//   1. 获取搜索关键词并去除空格
//   2. 验证关键词是否为空
//   3. 如果有FriendManager，使用网络搜索
//   4. 如果没有FriendManager，使用模拟数据搜索
void AddFriendDialog::onSearchClicked()
{
    QString searchText = m_searchEdit->text().trimmed();
    if (searchText.isEmpty()) {
        QMessageBox::information(this, "提示", "请输入搜索关键词");
        return;
    }
    
    // 如果有好友管理器，使用真实的后端查询
    if (m_friendManager) {
        m_friendManager->searchFriends(searchText);
    } else {
        // 否则使用模拟数据
        QVector<UserInfo> searchResults;
        for (const UserInfo &user : m_allUsers) {
            if (user.username.contains(searchText, Qt::CaseInsensitive) ||
                user.email.contains(searchText, Qt::CaseInsensitive)) {
                searchResults.append(user);
            }
        }
        
        populateSearchResults(searchResults);
    }
    
    m_tabWidget->setCurrentIndex(0); // 切换到搜索结果标签页
}

// onSearchTextChanged方法：处理搜索文本变化
// 
// 功能：
//   保存当前搜索文本（用于后续处理）
void AddFriendDialog::onSearchTextChanged()
{
    // 搜索文本改变时的处理
    m_currentSearchText = m_searchEdit->text();
}

// onAddFriendClicked方法：处理添加好友按钮点击
// 
// 功能：
//   显示好友申请已发送的消息（此方法在lambda中被调用）
void AddFriendDialog::onAddFriendClicked()
{
    // 处理添加好友按钮点击
    QMessageBox::information(this, "添加好友", "好友申请已发送");
}

// onSearchResultsReceived方法：处理搜索结果接收（网络数据）
// 
// 参数：
//   - users: 搜索结果用户列表（来自FriendManager信号）
// 
// 功能：
//   调用populateSearchResultsFromNetwork将网络搜索结果填充到UI
void AddFriendDialog::onSearchResultsReceived(const QList<FriendUser> &users)
{
    qDebug() << "[AddFriendDialog] 收到搜索结果，用户数量:" << users.size();
    if (users.isEmpty()) {
        QMessageBox::information(this, "搜索结果", "未找到匹配的用户，请尝试其他关键词");
    }
    populateSearchResultsFromNetwork(users);
}

// onFriendRequestSent方法：处理好友申请发送结果
// 
// 参数：
//   - success: 是否成功
// 
// 功能：
//   根据success参数显示成功或失败的提示消息
void AddFriendDialog::onFriendRequestSent(bool success)
{
    if (success) {
        QMessageBox::information(this, "成功", "好友申请已发送");
    } else {
        QMessageBox::warning(this, "失败", "发送好友申请失败");
    }
}

// onErrorOccurred方法：处理错误
// 
// 参数：
//   - error: 错误信息
// 
// 功能：
//   显示错误消息框
void AddFriendDialog::onErrorOccurred(const QString &error)
{
    QMessageBox::warning(this, "错误", error);
}

// populateSearchResultsFromNetwork方法：填充搜索结果（使用网络数据）
// 
// 参数：
//   - users: 来自网络的用户列表（FriendUser类型）
// 
// 功能：
//   1. 清空现有搜索结果
//   2. 为每个用户创建列表项（包含头像、用户信息、添加按钮）
//   3. 根据isFriend状态设置按钮状态（已添加=禁用，未添加=可点击）
//   4. 连接添加按钮信号到发送好友申请的逻辑
void AddFriendDialog::populateSearchResultsFromNetwork(const QList<FriendUser> &users)
{
    clearSearchResults();
    
    for (const FriendUser &user : users) {
        QListWidgetItem *item = new QListWidgetItem;
        item->setData(Qt::UserRole, user.uid);
        item->setData(Qt::UserRole + 1, user.name);
        
        // 创建搜索结果项控件
        QWidget *itemWidget = new QWidget;
        QHBoxLayout *itemLayout = new QHBoxLayout(itemWidget);
        itemLayout->setContentsMargins(15, 10, 15, 10);
        itemLayout->setSpacing(15);
        
        // 头像
        QLabel *avatar = new QLabel;
        avatar->setFixedSize(56, 56);
        QPixmap avatarPix = makeAvatar(user.name.left(2), QColor("#90CAF9"), 56);
        avatar->setPixmap(avatarPix);
        avatar->setStyleSheet("border-radius: 28px;");
        
        // 用户信息
        QWidget *textWidget = new QWidget;
        QVBoxLayout *textLayout = new QVBoxLayout(textWidget);
        textLayout->setContentsMargins(0, 0, 0, 0);
        textLayout->setSpacing(5);
        
        QLabel *nameLabel = new QLabel(user.name);
        nameLabel->setStyleSheet(
            "QLabel {"
            "    font-size: 16px;"
            "    font-weight: bold;"
            "    color: #333;"
            "}"
        );
        
        QString emailText = user.email.isEmpty() ? "暂无邮箱" : user.email;
        QLabel *emailLabel = new QLabel(emailText);
        emailLabel->setStyleSheet(
            "QLabel {"
            "    font-size: 14px;"
            "    color: #666;"
            "}"
        );
        
        textLayout->addWidget(nameLabel);
        textLayout->addWidget(emailLabel);
        
        // 添加按钮
        QPushButton *actionButton = new QPushButton(user.isFriend ? "已添加" : "添加");
        actionButton->setFixedSize(80, 35);
        
        if (user.isFriend) {
            actionButton->setEnabled(false);
            actionButton->setStyleSheet(
                "QPushButton {"
                "    background: #ccc;"
                "    color: #666;"
                "    border: none;"
                "    border-radius: 6px;"
                "    font-size: 14px;"
                "}"
            );
        } else {
            actionButton->setStyleSheet(
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
            
            // 连接添加按钮信号
            connect(actionButton, &QPushButton::clicked, this, [this, user]() {
                if (m_friendManager) {
                    m_friendManager->sendFriendRequest(user.uid, "");
                }
                emit friendRequestSent(user.uid, user.name);
            });
        }
        
        itemLayout->addWidget(avatar);
        itemLayout->addWidget(textWidget);
        itemLayout->addStretch();
        itemLayout->addWidget(actionButton);
        
        item->setSizeHint(itemWidget->sizeHint());
        m_searchResults->addItem(item);
        m_searchResults->setItemWidget(item, itemWidget);
    }
}



// populateSearchResults方法：填充搜索结果（使用模拟数据）
// 
// 参数：
//   - users: 用户列表（UserInfo类型）
// 
// 功能：
//   1. 清空现有搜索结果
//   2. 为每个用户创建列表项（包含头像、用户信息、添加按钮）
//   3. 根据isFriend状态设置按钮状态
//   4. 连接添加按钮信号到发送好友申请的逻辑
void AddFriendDialog::populateSearchResults(const QVector<UserInfo> &users)
{
    clearSearchResults();
    
    for (const UserInfo &user : users) {
        QListWidgetItem *item = new QListWidgetItem;
        item->setData(Qt::UserRole, user.uid);
        item->setData(Qt::UserRole + 1, user.username);
        
        // 创建搜索结果项控件
        QWidget *itemWidget = new QWidget;
        QHBoxLayout *itemLayout = new QHBoxLayout(itemWidget);
        itemLayout->setContentsMargins(15, 10, 15, 10);
        itemLayout->setSpacing(15);
        
        // 头像
        QLabel *avatar = new QLabel;
        avatar->setFixedSize(56, 56);
        QPixmap avatarPix = makeAvatar(user.username.left(2), QColor("#90CAF9"), 56);
        avatar->setPixmap(avatarPix);
        avatar->setStyleSheet("border-radius: 28px;");
        
        // 用户信息
        QWidget *textWidget = new QWidget;
        QVBoxLayout *textLayout = new QVBoxLayout(textWidget);
        textLayout->setContentsMargins(0, 0, 0, 0);
        textLayout->setSpacing(5);
        
        QLabel *nameLabel = new QLabel(user.username);
        nameLabel->setStyleSheet(
            "QLabel {"
            "    font-size: 16px;"
            "    font-weight: bold;"
            "    color: #333;"
            "}"
        );
        
        QLabel *emailLabel = new QLabel(user.email);
        emailLabel->setStyleSheet(
            "QLabel {"
            "    font-size: 14px;"
            "    color: #666;"
            "}"
        );
        
        textLayout->addWidget(nameLabel);
        textLayout->addWidget(emailLabel);
        
        // 添加按钮
        QPushButton *actionButton = new QPushButton(user.isFriend ? "已添加" : "添加");
        actionButton->setFixedSize(80, 35);
        
        if (user.isFriend) {
            actionButton->setEnabled(false);
            actionButton->setStyleSheet(
                "QPushButton {"
                "    background: #ccc;"
                "    color: #666;"
                "    border: none;"
                "    border-radius: 6px;"
                "    font-size: 14px;"
                "}"
            );
        } else {
            actionButton->setStyleSheet(
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
            
            // 连接添加按钮信号
            connect(actionButton, &QPushButton::clicked, this, [this, user]() {
                emit friendRequestSent(user.uid, user.username);
                QMessageBox::information(this, "添加好友", "好友申请已发送");
            });
        }
        
        itemLayout->addWidget(avatar);
        itemLayout->addWidget(textWidget);
        itemLayout->addStretch();
        itemLayout->addWidget(actionButton);
        
        item->setSizeHint(itemWidget->sizeHint());
        m_searchResults->addItem(item);
        m_searchResults->setItemWidget(item, itemWidget);
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
QPixmap AddFriendDialog::makeAvatar(const QString &text, const QColor &bg, int size)
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

// clearSearchResults方法：清空搜索结果
// 
// 功能：
//   删除搜索结果列表中的所有项
void AddFriendDialog::clearSearchResults()
{
    m_searchResults->clear();
}

