#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "chatdialog.h"
#include "addfrienddialog.h"
#include "friendmanager.h"
#include "usermgr.h"
#include "localdb.h"
#include "tcpmgr.h"
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QApplication>
#include <QDesktopWidget>
#include <algorithm>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>

// 构造函数：初始化主窗口
// 
// 参数：
//   - parent: 父窗口（通常为nullptr）
// 
// 功能：
//   1. 初始化所有成员变量为nullptr
//   2. 调用setupUi()初始化UI
//   3. 调用populateContacts()填充示例联系人数据
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_centralWidget(nullptr)
    , m_mainLayout(nullptr)
    , m_headerWidget(nullptr)
    , m_userAvatar(nullptr)
    , m_searchEdit(nullptr)
    , m_navTabs(nullptr)
    , m_messageList(nullptr)
    , m_contactsWidget(nullptr)
    , m_contactsLayout(nullptr)
    , m_newFriendsList(nullptr)
    , m_myFriendsList(nullptr)
    , m_sidebar(nullptr)
    , m_sidebarLayout(nullptr)
    , m_btnMessages(nullptr)
    , m_btnContacts(nullptr)
    , m_btnSpace(nullptr)
    , m_navGroup(nullptr)
    , m_bottomBar(nullptr)
    , m_bottomLayout(nullptr)
    , m_chatDialog(nullptr)
    , m_friendManager(nullptr)
{
    setupUi();
    populateContacts();
}

// 将真实数据渲染到“新朋友”分组
void MainWindow::renderFriendRequests(const QList<FriendRequest> &requests)
{
    if (!m_contactsList) return;

    // 清理现有的“新朋友”与“我的好友”之外的所有项，稍后 myFriends 再渲染
    for (int i = m_contactsList->count() - 1; i >= 0; --i) {
        QListWidgetItem *it = m_contactsList->item(i);
        QString type = it->data(Qt::UserRole).toString();
        if (type != "new_friends_header" && type != "my_friends_header" && type != "my_friend") {
            delete m_contactsList->takeItem(i);
        }
    }

    int insertRow = (m_newFriendsHeaderRow >= 0) ? (m_newFriendsHeaderRow + 1) : 0;

    for (const FriendRequest &req : requests) {
        QListWidgetItem *item = new QListWidgetItem;
        item->setData(Qt::UserRole, "friend_request");
        item->setData(Qt::UserRole + 1, req.uid);
        item->setData(Qt::UserRole + 2, req.nick.isEmpty() ? req.name : req.nick);

        QWidget *itemWidget = new QWidget;
        QHBoxLayout *itemLayout = new QHBoxLayout(itemWidget);
        itemLayout->setContentsMargins(15, 10, 15, 10);
        itemLayout->setSpacing(15);

        QLabel *avatar = new QLabel;
        avatar->setFixedSize(56, 56);
        const QString displayName = req.nick.isEmpty() ? req.name : req.nick;
        QPixmap avatarPix = makeAvatar(displayName.left(2), QColor("#FF6B6B"), 56);
        avatar->setPixmap(avatarPix);
        avatar->setStyleSheet("border-radius: 28px;");

        QWidget *textWidget = new QWidget;
        QVBoxLayout *textLayout = new QVBoxLayout(textWidget);
        textLayout->setContentsMargins(0, 0, 0, 0);
        textLayout->setSpacing(5);

        QLabel *nameLabel = new QLabel(displayName);
        nameLabel->setStyleSheet(
            "QLabel {"
            "    font-size: 16px;"
            "    font-weight: bold;"
            "    color: #333;"
            "}"
        );

        QLabel *snippetLabel = new QLabel(req.desc);
        snippetLabel->setStyleSheet(
            "QLabel {"
            "    font-size: 14px;"
            "    color: #666;"
            "    max-width: 300px;"
            "}"
        );
        snippetLabel->setWordWrap(true);

        textLayout->addWidget(nameLabel);
        textLayout->addWidget(snippetLabel);

        QPushButton *acceptButton = new QPushButton("接受");
        acceptButton->setFixedSize(60, 35);
        acceptButton->setStyleSheet(
            "QPushButton {"
            "    background: #4CAF50;"
            "    color: white;"
            "    border: none;"
            "    border-radius: 6px;"
            "    font-size: 14px;"
            "    font-weight: bold;"
            "}"
            "QPushButton:hover {"
            "    background: #45a049;"
            "}"
        );

        QPushButton *rejectButton = new QPushButton("拒绝");
        rejectButton->setFixedSize(60, 35);
        rejectButton->setStyleSheet(
            "QPushButton {"
            "    background: #f44336;"
            "    color: white;"
            "    border: none;"
            "    border-radius: 6px;"
            "    font-size: 14px;"
            "    font-weight: bold;"
            "}"
            "QPushButton:hover {"
            "    background: #da190b;"
            "}"
        );

        connect(acceptButton, &QPushButton::clicked, this, [this, req]() {
            if (m_friendManager) m_friendManager->replyFriendRequest(req.uid, true);
        });
        connect(rejectButton, &QPushButton::clicked, this, [this, req]() {
            if (m_friendManager) m_friendManager->replyFriendRequest(req.uid, false);
        });

        itemLayout->addWidget(avatar);
        itemLayout->addWidget(textWidget);
        itemLayout->addStretch();
        itemLayout->addWidget(acceptButton);
        itemLayout->addWidget(rejectButton);

        item->setSizeHint(itemWidget->sizeHint());
        m_contactsList->insertItem(insertRow, item);
        m_contactsList->setItemWidget(item, itemWidget);
        ++insertRow;
    }
}

// 将真实数据渲染到“我的好友”分组
void MainWindow::renderMyFriends(const QList<FriendUser> &friends)
{
    if (!m_contactsList) return;

    // 先移除之前渲染的 my_friend 项
    for (int i = m_contactsList->count() - 1; i >= 0; --i) {
        QListWidgetItem *it = m_contactsList->item(i);
        if (it->data(Qt::UserRole).toString() == "my_friend") {
            delete m_contactsList->takeItem(i);
        }
    }

    int myHeaderRow = -1;
    for (int i = 0; i < m_contactsList->count(); ++i) {
        QListWidgetItem *it = m_contactsList->item(i);
        if (it->data(Qt::UserRole).toString() == "my_friends_header") {
            myHeaderRow = i;
            break;
        }
    }
    int insertRow = (myHeaderRow >= 0) ? (myHeaderRow + 1) : m_contactsList->count();

    for (const FriendUser &u : friends) {
        QListWidgetItem *item = new QListWidgetItem;
        item->setData(Qt::UserRole, "my_friend");
        item->setData(Qt::UserRole + 1, u.uid);
        item->setData(Qt::UserRole + 2, u.nick.isEmpty() ? u.name : u.nick);

        QWidget *itemWidget = new QWidget;
        QHBoxLayout *itemLayout = new QHBoxLayout(itemWidget);
        itemLayout->setContentsMargins(15, 10, 15, 10);
        itemLayout->setSpacing(15);

        QLabel *avatar = new QLabel;
        avatar->setFixedSize(56, 56);
        const QString displayName = u.nick.isEmpty() ? u.name : u.nick;
        QPixmap avatarPix = makeAvatar(displayName.left(2), QColor("#90CAF9"), 56);
        avatar->setPixmap(avatarPix);
        avatar->setStyleSheet("border-radius: 28px;");

        QLabel *nameLabel = new QLabel(displayName);
        nameLabel->setStyleSheet(
            "QLabel {"
            "    font-size: 16px;"
            "    font-weight: bold;"
            "    color: #333;"
            "}"
        );

        itemLayout->addWidget(avatar);
        itemLayout->addWidget(nameLabel);
        itemLayout->addStretch();

        item->setSizeHint(itemWidget->sizeHint());
        m_contactsList->insertItem(insertRow, item);
        m_contactsList->setItemWidget(item, itemWidget);
        ++insertRow;
    }
}

// 析构函数：清理资源
MainWindow::~MainWindow()
{
    delete ui;
}

// setupUi方法：初始化UI界面
// 
// 功能：
//   1. 设置窗口标题、大小、样式
//   2. 创建并设置各UI组件（头部、导航、消息列表、联系人界面、底部导航栏）
//   3. 创建聊天对话框（初始隐藏）
//   4. 创建好友管理器并连接信号
//   5. 从UserMgr获取当前用户信息并设置到好友管理器
void MainWindow::setupUi()
{
    // 设置窗口属性
    setWindowTitle("聊天系统");
    setMinimumSize(1200, 800);
    resize(1400, 900);
    
    // 设置窗口样式
    setStyleSheet("QMainWindow { background-color: #f5f5f5; }");
    
    // 创建中央部件
    m_centralWidget = new QWidget(this);
    setCentralWidget(m_centralWidget);
    
    // 创建主布局
    m_mainLayout = new QVBoxLayout(m_centralWidget);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);
    
    // 设置各个区域
    setupHeader();
    setupNavigation();
    setupMessageList();
    setupContactsWidget();
    setupBottomBar();
    
    // 创建聊天对话框（初始隐藏）
    m_chatDialog = new ChatDialog(this);
    m_chatDialog->hide();
    
    // 创建好友管理器
    m_friendManager = new FriendManager(this);
    
    // 从UserMgr获取当前用户信息
    // 注意：如果能进入主窗口，说明已经登录成功，uid应该已经被设置
    // （HTTP登录成功时就会设置UserMgr，所以这里uid应该有效）
    int currentUid = UserMgr::GetInstance()->GetUid();
    
    qDebug() << "[MainWindow] 初始化好友管理器";
    qDebug() << "[MainWindow] 当前用户ID:" << currentUid;
    qDebug() << "[MainWindow] 当前用户名:" << UserMgr::GetInstance()->GetName();
    
    // 防御性检查：如果能进入主窗口，理论上已经登录了，uid应该有效
    // 如果真的无效，说明登录流程有问题，但在Release模式下不崩溃，只记录错误
    if (currentUid <= 0) {
        qCritical() << "[MainWindow] 严重错误: 用户ID无效！如果能进入主窗口，说明已经登录，"
                    << "uid应该已经被设置。这可能是一个bug，请检查登录流程。";
        // 在Debug模式下使用断言立即发现问题
        Q_ASSERT_X(false, "MainWindow::setupUi", 
                   "用户ID无效！登录流程可能存在问题，请检查 logindialog.cpp 中是否正确调用了 UserMgr::SetUid()");
    }
    
    // 读取GateServer的配置信息,之前写死了url
    QString fileName = "config.ini";
    QString app_path = QCoreApplication::applicationDirPath();
    QString config_path = QDir::toNativeSeparators(app_path + QDir::separator() + fileName);
    QSettings settings(config_path, QSettings::IniFormat);

    // 从配置文件读取GateServer的地址和端口
    QString gate_host = settings.value("GateServer/host").toString();
    QString gate_port = settings.value("GateServer/port").toString();

    // 设置服务器地址
    QString baseUrl = QString("http://%1:%2").arg(gate_host, gate_port);
    m_friendManager->setServerUrl(baseUrl);
    // 原来是
    // m_friendManager->setServerUrl("http://localhost:8080");
    m_friendManager->setCurrentUser(currentUid); // 设置当前用户ID
    
    // qDebug() << "[MainWindow] 服务器地址已设置为: http://localhost:8080";
    
    // 连接好友管理器信号
    connect(m_friendManager, &FriendManager::searchResultsReceived, 
            this, [this](const QList<FriendUser> &users) {
                // 处理搜索结果
                qDebug() << "收到搜索结果:" << users.size() << "个用户";
            });
    
    connect(m_friendManager, &FriendManager::friendRequestsReceived, 
            this, [this](const QList<FriendRequest> &requests) {
                // 处理好友申请列表
                qDebug() << "收到好友申请:" << requests.size() << "个申请";
                renderFriendRequests(requests);
            });
    
    connect(m_friendManager, &FriendManager::myFriendsReceived, 
            this, [this](const QList<FriendUser> &friends) {
                // 处理我的好友列表
                qDebug() << "收到我的好友:" << friends.size() << "个好友";
                renderMyFriends(friends);
            });
    
    connect(m_friendManager, &FriendManager::errorOccurred, 
            this, [this](const QString &error) {
                qDebug() << "好友管理器错误:" << error;
            });

    // 同意/拒绝成功后自动刷新“新朋友”和“我的好友”
    connect(m_friendManager, &FriendManager::friendRequestReplied,
            this, [this](bool success) {
                if (!success) return;
                if (m_friendManager) {
                    m_friendManager->getFriendRequests();
                    m_friendManager->getMyFriends();
                }
            });

    // 文本聊天下行（1019）：展示到 ChatDialog
    connect(TcpMgr::GetInstance(), &TcpMgr::sig_text_notify,
            this, [this](int fromUid, int toUid, qint64 serverMsgId,
                         const QString &msgId, const QString &content,
                         const QString &traceparent) {
                qDebug() << "[MainWindow] sig_text_notify received from=" << fromUid << " to=" << toUid 
                         << " msgId=" << msgId << " myUid=" << UserMgr::GetInstance()->GetUid() << " content=" << content;
                
                // 仅当这是发给当前登录用户的消息时处理
                if (UserMgr::GetInstance()->GetUid() != toUid) {
                    qDebug() << "[MainWindow] message not for me, ignoring";
                    return;
                }

                // 存入本地 DB (去重)
                MessageInfo msg;
                msg.msg_id = serverMsgId > 0 ? serverMsgId : msgId.toLongLong();
                msg.from_uid = fromUid;
                msg.to_uid = toUid;
                msg.content = content;
                msg.status = 0; 
                msg.create_time = QString::number(QDateTime::currentMSecsSinceEpoch());
                msg.type = 0;

                bool isNew = LocalDb::GetInstance()->SaveMessage(msg);
                if (!isNew) {
                    qDebug() << "[MainWindow] Duplicate message ignored: " << msgId;
                }

                // Precise ACK: acknowledge only the server id of this notification.
                QJsonObject ackRoot;
                ackRoot["uid"] = UserMgr::GetInstance()->GetUid();
                ackRoot["traceparent"] = traceparent;
                QJsonArray ackIds;
                if (serverMsgId > 0) {
                    ackIds.append(serverMsgId);
                }
                ackRoot["ids"] = ackIds;
                
                QString ackJson = QString::fromUtf8(QJsonDocument(ackRoot).toJson(QJsonDocument::Compact));
                emit TcpMgr::GetInstance()->sig_send_data(ReqId::ID_NOTIFY_TEXT_CHAT_MSG_RSP, ackJson);

                // 如果是重复消息，就不需要更新 UI 了 (除非 UI 没显示出来，但通常 DB 有了就是有了)
                if (!isNew) return;

                // 尝试使用 uid 作为窗口标题；若你有 uid->name 映射，可替换为好友昵称
                QString contactTitle = QString::number(fromUid);
                
                // [Fix Duplicate Display]
                // 检查窗口是否已经打开且对应当前好友
                bool isChatOpen = (m_chatDialog && m_chatDialog->isVisible() && m_chatDialog->getCurrentContactUid() == fromUid);

                if (isChatOpen) {
                    // 窗口开着 -> 直接追加新消息 (不需要重载历史)
                    qDebug() << "[MainWindow] chat open, appending message";
                    m_chatDialog->addMessage(content, false);
                    m_chatDialog->raise();
                    m_chatDialog->activateWindow();
                } else {
                    // 窗口没开 -> 打开窗口 (openChatDialog 会自动加载历史记录，包含刚才存的那条)
                    qDebug() << "[MainWindow] chat closed, opening and loading history";
                    openChatDialog(contactTitle, fromUid);
                    
                    // 重新绑定发送信号 (只有新打开/切换窗口时才需要)
                    QObject::disconnect(m_chatDialog, &ChatDialog::messageSent, nullptr, nullptr);
                    connect(m_chatDialog, &ChatDialog::messageSent, this,
                            [this, fromUid](const QString &toUser, const QString &text) {
                                Q_UNUSED(toUser);
                                QJsonObject root;
                                root["fromuid"] = UserMgr::GetInstance()->GetUid();
                                root["touid"] = fromUid;
                                root["traceparent"] = UserMgr::GetInstance()->GetTraceparent();
                                QJsonArray arr;
                                QJsonObject elem;
                                elem["content"] = text;
                                elem["msgid"] = QString::number(QDateTime::currentMSecsSinceEpoch());
                                arr.append(elem);
                                root["text_array"] = arr;

                                // 发送前存入本地 DB
                                MessageInfo selfMsg;
                                selfMsg.msg_id = elem["msgid"].toString().toLongLong();
                                selfMsg.from_uid = UserMgr::GetInstance()->GetUid();
                                selfMsg.to_uid = fromUid;
                                selfMsg.content = text;
                                selfMsg.status = 0;
                                selfMsg.create_time = QString::number(QDateTime::currentMSecsSinceEpoch());
                                selfMsg.type = 0;
                                LocalDb::GetInstance()->SaveMessage(selfMsg);

                                QString json = QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
                                qDebug() << "[TextChat][UI->TCP] send 1017 json=" << json;
                                emit TcpMgr::GetInstance()->sig_send_data(ReqId::ID_TEXT_CHAT_MSG_REQ, json);
                            });
                }

                // 会话列表更新/新增
                bool found = false;
                for (int i = 0; i < m_messageList->count(); ++i) {
                    QListWidgetItem *it = m_messageList->item(i);
                    if (it && it->data(Qt::UserRole).toInt() == fromUid) {
                        it->setText(contactTitle + "  -  " + content);
                        it->setData(Qt::UserRole + 1, contactTitle);
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    QListWidgetItem *item = new QListWidgetItem(contactTitle + "  -  " + content);
                    item->setData(Qt::UserRole, fromUid);
                    item->setData(Qt::UserRole + 1, contactTitle);
                    m_messageList->addItem(item);
                }
            },
            Qt::UniqueConnection);

    // 新增：TCP 通知与 HTTP 刷新串联
    // 收到“好友申请”实时通知后，拉取“新朋友”列表
    connect(TcpMgr::GetInstance(), &TcpMgr::sig_friend_apply,
            this, [this]() {
                // [FriendNotify]
                qDebug() << "[FriendNotify][UI] recv sig_friend_apply -> HTTP getFriendRequests()";
                if (m_friendManager) m_friendManager->getFriendRequests();
            });

    // 收到“好友回复结果”（同意/拒绝）实时通知后，刷新“我的好友”和“新朋友”
    connect(TcpMgr::GetInstance(), &TcpMgr::sig_friend_reply,
            this, [this](int fromUid, bool agree) {
                // [FriendNotify]
                qDebug() << "[FriendNotify][UI] recv sig_friend_reply fromUid=" << fromUid
                         << " agree=" << agree << " -> HTTP getMyFriends()+getFriendRequests()";
                if (m_friendManager) {
                    m_friendManager->getMyFriends();
                    m_friendManager->getFriendRequests();
                }
            });
}

// setupHeader方法：设置头部区域
// 
// 功能：
//   1. 创建头部区域控件（用户头像、等级、状态、搜索框）
//   2. 设置样式（蓝色渐变背景）
//   3. 连接搜索框的信号到onSearchChanged槽函数
void MainWindow::setupHeader()
{
    // 创建头部区域
    m_headerWidget = new QWidget;
    m_headerWidget->setFixedHeight(120);
    m_headerWidget->setStyleSheet(
        "QWidget {"
        "    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
        "        stop:0 #4A90E2, stop:1 #357ABD);"
        "    border: none;"
        "}"
    );
    
    QHBoxLayout *headerLayout = new QHBoxLayout(m_headerWidget);
    headerLayout->setContentsMargins(20, 15, 20, 15);
    
    // 用户信息区域
    QWidget *userInfoWidget = new QWidget;
    userInfoWidget->setStyleSheet("background: transparent;");
    QHBoxLayout *userInfoLayout = new QHBoxLayout(userInfoWidget);
    userInfoLayout->setContentsMargins(0, 0, 0, 0);
    userInfoLayout->setSpacing(15);
    
    // 用户头像
    m_userAvatar = new QLabel;
    m_userAvatar->setFixedSize(60, 60);
    m_userAvatar->setPixmap(makeAvatar("用户", QColor("#FF6B6B"), 60));
    m_userAvatar->setStyleSheet(
        "QLabel {"
        "    border: 2px solid white;"
        "    border-radius: 30px;"
        "    background: transparent;"
        "}"
    );
    
    // 用户等级和状态
    QWidget *userTextWidget = new QWidget;
    userTextWidget->setStyleSheet("background: transparent;");
    QVBoxLayout *userTextLayout = new QVBoxLayout(userTextWidget);
    userTextLayout->setContentsMargins(0, 0, 0, 0);
    userTextLayout->setSpacing(5);
    
    // m_userLevel 和 m_userStatus 已删除
    
    userInfoLayout->addWidget(m_userAvatar);
    userInfoLayout->addWidget(userTextWidget);
    userInfoLayout->addStretch();
    
    // 搜索框
    m_searchEdit = new QLineEdit;
    m_searchEdit->setPlaceholderText("搜索");
    m_searchEdit->setFixedHeight(35);
    m_searchEdit->setStyleSheet(
        "QLineEdit {"
        "    background: rgba(255, 255, 255, 0.2);"
        "    border: 1px solid rgba(255, 255, 255, 0.3);"
        "    border-radius: 17px;"
        "    padding: 0 15px;"
        "    color: white;"
        "    font-size: 14px;"
        "}"
        "QLineEdit::placeholder {"
        "    color: rgba(255, 255, 255, 0.7);"
        "}"
    );
    
    headerLayout->addWidget(userInfoWidget);
    headerLayout->addWidget(m_searchEdit);
    
    m_mainLayout->addWidget(m_headerWidget);
    
    // 连接搜索信号
    connect(m_searchEdit, &QLineEdit::textChanged, this, &MainWindow::onSearchChanged);
}

// setupNavigation方法：设置导航标签
// 
// 功能：
//   1. 创建"消息"和"联系人"标签页
//   2. 设置标签页样式
//   3. 连接标签切换信号到onTabChanged槽函数
void MainWindow::setupNavigation()
{
    // 创建导航标签
    m_navTabs = new QTabWidget;
    m_navTabs->setFixedHeight(50);
    m_navTabs->setStyleSheet(
        "QTabWidget::pane {"
        "    border: none;"
        "    background: white;"
        "}"
        "QTabBar::tab {"
        "    background: white;"
        "    border: none;"
        "    padding: 15px 30px;"
        "    font-size: 16px;"
        "    color: #666;"
        "}"
        "QTabBar::tab:selected {"
        "    color: #4A90E2;"
        "    border-bottom: 2px solid #4A90E2;"
        "}"
    );
    
    m_navTabs->addTab(new QWidget, "消息");
    m_navTabs->addTab(new QWidget, "联系人");
    // 删除"空间"标签页
    
    m_mainLayout->addWidget(m_navTabs);
    
    // 连接标签切换信号
    connect(m_navTabs, &QTabWidget::currentChanged, this, &MainWindow::onTabChanged);
}

// setupMessageList方法：设置消息列表
// 
// 功能：
//   1. 创建消息列表控件
//   2. 设置列表样式（白色背景、悬停效果、选中样式）
//   3. 连接选中信号到onContactSelected槽函数
void MainWindow::setupMessageList()
{
    // 创建消息列表
    m_messageList = new QListWidget;
    m_messageList->setStyleSheet(
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
        "QListWidget::item:selected {"
        "    background: #e3f2fd;"
        "}"
    );
    
    m_mainLayout->addWidget(m_messageList);
    
    // 连接选择信号
    connect(m_messageList, &QListWidget::currentRowChanged, this, &MainWindow::onContactSelected);
}

// setupContactsWidget方法：设置联系人界面
// 
// 功能：
//   1. 创建联系人界面（初始隐藏）
//   2. 创建统一的联系人列表控件
//   3. 添加"新朋友"和"我的好友"分组标题
//   4. 设置模拟数据（好友申请列表和我的好友列表）
//   5. 连接列表点击信号，处理分组展开/折叠和好友操作
void MainWindow::setupContactsWidget()
{
    m_contactsWidget = new QWidget;
    m_contactsWidget->setStyleSheet("QWidget { background: white; }");
    m_contactsWidget->hide();

    m_contactsLayout = new QVBoxLayout(m_contactsWidget);
    m_contactsLayout->setContentsMargins(0, 0, 0, 0);
    m_contactsLayout->setSpacing(0);

    m_contactsList = new QListWidget;
    m_contactsList->setStyleSheet(
        "QListWidget {"
        "    background: white;"
        "    border: none;"
        "    outline: none;"
        "}"
        "QListWidget::item {"
        "    border: none;"
        "    border-bottom: 1px solid #f0f0f0;"
        "    padding: 15px 20px;"
        "    min-height: 50px;"
        "    font-size: 16px;"
        "    color: #333;"
        "}"
        "QListWidget::item:hover {"
        "    background: #f8f9fa;"
        "}"
        "QListWidget::item:selected {"
        "    background: #e3f2fd;"
        "    color: #4A90E2;"
        "}"
    );

    QListWidgetItem *newFriendsHeader = new QListWidgetItem("新朋友");
    newFriendsHeader->setData(Qt::UserRole, "new_friends_header");
    newFriendsHeader->setBackground(QColor("#f5f5f5"));
    newFriendsHeader->setForeground(QColor("#666"));
    m_contactsList->addItem(newFriendsHeader);
    m_newFriendsHeaderRow = 0;

    QListWidgetItem *myFriendsHeader = new QListWidgetItem("我的好友");
    myFriendsHeader->setData(Qt::UserRole, "my_friends_header");
    myFriendsHeader->setBackground(QColor("#f5f5f5"));
    myFriendsHeader->setForeground(QColor("#666"));
    m_contactsList->addItem(myFriendsHeader);
    m_myFriendsHeaderRow = 1;

    m_contactsLayout->addWidget(m_contactsList);
    m_mainLayout->addWidget(m_contactsWidget);

    // 点击“我的好友”项打开聊天（最小改动，仅处理好友项）
    connect(m_contactsList, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        if (!item) return;
        const QString type = item->data(Qt::UserRole).toString();
        if (type == "my_friend") {
            QString username = item->data(Qt::UserRole + 2).toString();
            if (username.isEmpty()) username = item->data(Qt::UserRole + 1).toString();
            int touid = item->data(Qt::UserRole + 1).toInt();
            openChatDialog(username, touid);

            // 重新连接 ChatDialog 发出的发送信号，将其转为协议 1017 的 TCP 包
            QObject::disconnect(m_chatDialog, &ChatDialog::messageSent, nullptr, nullptr);
            connect(m_chatDialog, &ChatDialog::messageSent, this,
                    [this, touid](const QString &toUser, const QString &text) {
                        Q_UNUSED(toUser);
                        QJsonObject root;
                        root["fromuid"] = UserMgr::GetInstance()->GetUid();
                        root["touid"] = touid;
                        root["traceparent"] = UserMgr::GetInstance()->GetTraceparent();
                        QJsonArray arr;
                        QJsonObject elem;
                        elem["content"] = text;
                        elem["msgid"] = QString::number(QDateTime::currentMSecsSinceEpoch());
                        arr.append(elem);
                        root["text_array"] = arr;

                        // 发送前存入本地 DB
                        MessageInfo selfMsg;
                        selfMsg.msg_id = elem["msgid"].toString().toLongLong();
                        selfMsg.from_uid = UserMgr::GetInstance()->GetUid();
                        selfMsg.to_uid = touid;
                        selfMsg.content = text;
                        selfMsg.status = 0;
                        selfMsg.create_time = QString::number(QDateTime::currentMSecsSinceEpoch());
                        selfMsg.type = 0;
                        LocalDb::GetInstance()->SaveMessage(selfMsg);

                        QString json = QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
                        qDebug() << "[TextChat][UI->TCP] send 1017 json=" << json;
                        emit TcpMgr::GetInstance()->sig_send_data(ReqId::ID_TEXT_CHAT_MSG_REQ, json);
                    });
        }
    });
}

// setupBottomBar方法：设置底部导航栏
// 
// 功能：
//   1. 创建底部导航栏控件
//   2. 创建多个功能按钮（菜单、添加好友、上传等）
//   3. 为添加好友按钮连接onAddFriendClicked槽函数
void MainWindow::setupBottomBar()
{
    // 创建底部导航栏
    m_bottomBar = new QWidget;
    m_bottomBar->setFixedHeight(60);
    m_bottomBar->setStyleSheet(
        "QWidget {"
        "    background: white;"
        "    border-top: 1px solid #e0e0e0;"
        "}"
    );
    
    m_bottomLayout = new QHBoxLayout(m_bottomBar);
    m_bottomLayout->setContentsMargins(20, 10, 20, 10);
    m_bottomLayout->setSpacing(30);
    
    // 添加底部按钮
    QStringList buttonIcons = {"☰", "👤+", "⬆", "🎮", "⚡", "☁", "⊞"};
    for (int i = 0; i < buttonIcons.size(); ++i) {
        QPushButton *btn = new QPushButton(buttonIcons[i]);
        btn->setFixedSize(40, 40);
        btn->setStyleSheet(
            "QPushButton {"
            "    background: transparent;"
            "    border: none;"
            "    font-size: 20px;"
            "    color: #666;"
            "}"
            "QPushButton:hover {"
            "    background: #f0f0f0;"
            "    border-radius: 20px;"
            "}"
        );
        
        // 为添加好友按钮连接信号
        if (i == 1) { // 👤+ 按钮
            connect(btn, &QPushButton::clicked, this, &MainWindow::onAddFriendClicked);
        }
        
        m_bottomLayout->addWidget(btn);
    }
    
    m_bottomLayout->addStretch();
    
    m_mainLayout->addWidget(m_bottomBar);
}

void MainWindow::populateNewFriends()
{
    // 清空现有数据
    if (m_newFriendsList) m_newFriendsList->clear();
    // 已移除模拟数据，实际渲染由 renderFriendRequests() 负责
    return;
}

void MainWindow::populateMyFriends()
{
    // 清空现有数据
    if (m_myFriendsList) m_myFriendsList->clear();
    // 已移除模拟数据，实际渲染由 renderMyFriends() 负责
    return;
}

// populateContacts方法：填充联系人列表
// 
// 功能：
//   1. 创建示例联系人数据（模拟与联系人的最后一条消息）
//   2. 为每个联系人创建列表项（包含头像、名称、消息摘要、时间、未读数）
//   3. 设置样式（头像圆形、文本样式、未读消息数红色背景）
void MainWindow::populateContacts()
{
    // 移除示例联系人数据，列表由真实消息或会话逻辑填充
    m_messageList->clear();
    return;
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
//   创建一个圆形的、带有文字的头像图片
QPixmap MainWindow::makeAvatar(const QString &text, const QColor &bg, int size)
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

// onContactSelected方法：处理联系人选择
// 
// 参数：
//   - row: 选中的行号
// 
// 功能：
//   1. 检查行号是否有效
//   2. 获取选中的联系人名称
//   3. 打开与选定联系人的聊天对话框
void MainWindow::onContactSelected(int row)
{
    if (row < 0) return;
    
    QListWidgetItem *item = m_messageList->item(row);
    if (!item) return;
    
    QString contactName = item->data(Qt::UserRole + 1).toString();
    int contactUid = item->data(Qt::UserRole).toInt();
    m_currentContact = contactName;
    
    // 打开聊天对话框
    openChatDialog(contactName, contactUid);
}

// onSearchChanged方法：处理搜索框内容变化
// 
// 参数：
//   - text: 搜索关键词
// 
// 功能：
//   根据关键词过滤消息列表，只显示包含关键词的联系人（不区分大小写）
void MainWindow::onSearchChanged(const QString &text)
{
    for (int i = 0; i < m_messageList->count(); ++i) {
        QListWidgetItem *item = m_messageList->item(i);
        QString name = item->data(Qt::UserRole + 1).toString();
        m_messageList->setRowHidden(i, !name.contains(text, Qt::CaseInsensitive));
    }
}

// onTabChanged方法：处理标签页切换
// 
// 参数：
//   - index: 切换到的标签页索引（0=消息，1=联系人）
// 
// 功能：
//   1. 切换到消息界面：显示消息列表，隐藏联系人界面
//   2. 切换到联系人界面：显示联系人界面，隐藏消息列表，并从服务器加载真实数据
void MainWindow::onTabChanged(int index)
{
    // 处理标签切换
    if (index == 0) {
        // 消息界面
        m_messageList->show();
        m_contactsWidget->hide();
    } else if (index == 1) {
        // 联系人界面
        m_messageList->hide();
        m_contactsWidget->show();
        
        // 从服务器加载真实数据
        if (m_friendManager) {
            m_friendManager->getFriendRequests();
            m_friendManager->getMyFriends();
        }
    }
}

void MainWindow::onSidebarButtonClicked(QAbstractButton *button)
{
    // 处理侧边栏按钮点击
    Q_UNUSED(button);
}

// openChatDialog方法：打开聊天对话框
// 
// 参数：
//   - contactName: 联系人名称
// 
// 功能：
//   1. 检查是否已有聊天对话框，没有则创建
//   2. 设置当前联系人
//   3. 显示并激活对话框
void MainWindow::openChatDialog(const QString &contactName, int contactUid)
{
    if (!m_chatDialog) {
        m_chatDialog = new ChatDialog(this);
    }
    m_chatDialog->setCurrentContact(contactName, contactUid);
    m_chatDialog->show();
    m_chatDialog->raise();
    m_chatDialog->activateWindow();
}

// onAddFriendClicked方法：处理添加好友按钮点击
// 
// 功能：
//   1. 打开添加好友对话框
//   2. 设置好友管理器
//   3. 连接信号，处理好友申请被接受的事件
void MainWindow::onAddFriendClicked()
{
    // 打开添加好友对话框
    AddFriendDialog *addFriendDialog = new AddFriendDialog(this);
    addFriendDialog->setAttribute(Qt::WA_DeleteOnClose);
    
    // 设置好友管理器
    if (m_friendManager) {
        addFriendDialog->setFriendManager(m_friendManager);
    }
    
    addFriendDialog->show();
    
    // 连接信号
    connect(addFriendDialog, &AddFriendDialog::friendRequestAccepted, this, [this](int uid, const QString &username) {
        // 从新朋友列表移除，添加到我的好友列表
        for (int i = 0; i < m_newFriendsList->count(); ++i) {
            QListWidgetItem *item = m_newFriendsList->item(i);
            if (item && item->data(Qt::UserRole).toInt() == uid) {
                m_newFriendsList->takeItem(i);
                break;
            }
        }
        
        // 添加到我的好友列表
        QListWidgetItem *newItem = new QListWidgetItem;
        newItem->setData(Qt::UserRole, uid);
        newItem->setData(Qt::UserRole + 1, username);
        
        // 创建好友项控件
        QWidget *itemWidget = new QWidget;
        QHBoxLayout *itemLayout = new QHBoxLayout(itemWidget);
        itemLayout->setContentsMargins(15, 10, 15, 10);
        itemLayout->setSpacing(15);
        
        // 头像
        QLabel *avatar = new QLabel;
        avatar->setFixedSize(56, 56);
        QPixmap avatarPix = makeAvatar(username.left(2), QColor("#90CAF9"), 56);
        avatar->setPixmap(avatarPix);
        avatar->setStyleSheet("border-radius: 28px;");
        
        // 昵称
        QLabel *nameLabel = new QLabel(username);
        nameLabel->setStyleSheet(
            "QLabel {"
            "    font-size: 16px;"
            "    font-weight: bold;"
            "    color: #333;"
            "}"
        );
        
        itemLayout->addWidget(avatar);
        itemLayout->addWidget(nameLabel);
        itemLayout->addStretch();
        
        newItem->setSizeHint(itemWidget->sizeHint());
        m_myFriendsList->addItem(newItem);
        m_myFriendsList->setItemWidget(newItem, itemWidget);
        
        // 重新排序
        populateMyFriends();
    });
}

void MainWindow::toggleNewFriendsList(QListWidget *contactsList, const QVector<Contact> &friendRequests)
{
    // 检查是否已经展开
    bool isExpanded = false;
    int headerRow = -1;
    
    for (int i = 0; i < contactsList->count(); ++i) {
        QListWidgetItem *item = contactsList->item(i);
        if (item && item->data(Qt::UserRole).toString() == "new_friends_header") {
            headerRow = i;
            // 检查下一项是否是好友申请
            if (i + 1 < contactsList->count()) {
                QListWidgetItem *nextItem = contactsList->item(i + 1);
                if (nextItem && nextItem->data(Qt::UserRole).toString() == "friend_request") {
                    isExpanded = true;
                }
            }
            break;
        }
    }
    
    if (isExpanded) {
        // 折叠：移除所有好友申请项
        for (int i = contactsList->count() - 1; i > headerRow; --i) {
            QListWidgetItem *item = contactsList->item(i);
            if (item && item->data(Qt::UserRole).toString() == "friend_request") {
                contactsList->takeItem(i);
            } else {
                break; // 遇到其他类型项就停止
            }
        }
    } else {
        // 展开：添加好友申请项
        for (const Contact &contact : friendRequests) {
            QListWidgetItem *item = new QListWidgetItem;
            item->setData(Qt::UserRole, "friend_request");
            item->setData(Qt::UserRole + 1, contact.uid);
            item->setData(Qt::UserRole + 2, contact.name);
            
            // 创建好友申请项控件
            QWidget *itemWidget = new QWidget;
            QHBoxLayout *itemLayout = new QHBoxLayout(itemWidget);
            itemLayout->setContentsMargins(15, 10, 15, 10);
            itemLayout->setSpacing(15);
            
            // 头像
            QLabel *avatar = new QLabel;
            avatar->setFixedSize(56, 56);
            QPixmap avatarPix = makeAvatar(contact.name.left(2), QColor("#FF6B6B"), 56);
            avatar->setPixmap(avatarPix);
            avatar->setStyleSheet("border-radius: 28px;");
            
            // 文本信息
            QWidget *textWidget = new QWidget;
            QVBoxLayout *textLayout = new QVBoxLayout(textWidget);
            textLayout->setContentsMargins(0, 0, 0, 0);
            textLayout->setSpacing(5);
            
            QLabel *nameLabel = new QLabel(contact.name);
            nameLabel->setStyleSheet(
                "QLabel {"
                "    font-size: 16px;"
                "    font-weight: bold;"
                "    color: #333;"
                "}"
            );
            
            QLabel *snippetLabel = new QLabel(contact.snippet);
            snippetLabel->setStyleSheet(
                "QLabel {"
                "    font-size: 14px;"
                "    color: #666;"
                "    max-width: 300px;"
                "}"
            );
            snippetLabel->setWordWrap(true);
            
            textLayout->addWidget(nameLabel);
            textLayout->addWidget(snippetLabel);
            
            // 接受和拒绝按钮
            QPushButton *acceptButton = new QPushButton("接受");
            acceptButton->setFixedSize(60, 35);
            acceptButton->setStyleSheet(
                "QPushButton {"
                "    background: #4CAF50;"
                "    color: white;"
                "    border: none;"
                "    border-radius: 6px;"
                "    font-size: 14px;"
                "    font-weight: bold;"
                "}"
                "QPushButton:hover {"
                "    background: #45a049;"
                "}"
            );
            
            QPushButton *rejectButton = new QPushButton("拒绝");
            rejectButton->setFixedSize(60, 35);
            rejectButton->setStyleSheet(
                "QPushButton {"
                "    background: #f44336;"
                "    color: white;"
                "    border: none;"
                "    border-radius: 6px;"
                "    font-size: 14px;"
                "    font-weight: bold;"
                "}"
                "QPushButton:hover {"
                "    background: #da190b;"
                "}"
            );
            
            // 连接按钮信号
            connect(acceptButton, &QPushButton::clicked, this, [this, contact, contactsList, item]() {
                onAcceptFriendRequest(contact.uid, contact.name);
                // 从列表中移除该项
                int row = contactsList->row(item);
                contactsList->takeItem(row);
            });
            
            connect(rejectButton, &QPushButton::clicked, this, [this, contact, contactsList, item]() {
                // 从列表中移除该项
                int row = contactsList->row(item);
                contactsList->takeItem(row);
            });
            
            itemLayout->addWidget(avatar);
            itemLayout->addWidget(textWidget);
            itemLayout->addStretch();
            itemLayout->addWidget(acceptButton);
            itemLayout->addWidget(rejectButton);
            
            item->setSizeHint(itemWidget->sizeHint());
            contactsList->insertItem(headerRow + 1, item);
            contactsList->setItemWidget(item, itemWidget);
            headerRow++; // 更新插入位置
        }
    }
}

void MainWindow::toggleMyFriendsList(QListWidget *contactsList, const QVector<Contact> &myFriends)
{
    // 检查是否已经展开
    bool isExpanded = false;
    int headerRow = -1;
    
    for (int i = 0; i < contactsList->count(); ++i) {
        QListWidgetItem *item = contactsList->item(i);
        if (item && item->data(Qt::UserRole).toString() == "my_friends_header") {
            headerRow = i;
            // 检查下一项是否是我的好友
            if (i + 1 < contactsList->count()) {
                QListWidgetItem *nextItem = contactsList->item(i + 1);
                if (nextItem && nextItem->data(Qt::UserRole).toString() == "my_friend") {
                    isExpanded = true;
                }
            }
            break;
        }
    }
    
    if (isExpanded) {
        // 折叠：移除所有我的好友项
        for (int i = contactsList->count() - 1; i > headerRow; --i) {
            QListWidgetItem *item = contactsList->item(i);
            if (item && item->data(Qt::UserRole).toString() == "my_friend") {
                contactsList->takeItem(i);
            } else {
                break; // 遇到其他类型项就停止
            }
        }
    } else {
        // 展开：添加我的好友项
        for (const Contact &contact : myFriends) {
            QListWidgetItem *item = new QListWidgetItem;
            item->setData(Qt::UserRole, "my_friend");
            item->setData(Qt::UserRole + 1, contact.name);
            
            // 创建好友项控件
            QWidget *itemWidget = new QWidget;
            QHBoxLayout *itemLayout = new QHBoxLayout(itemWidget);
            itemLayout->setContentsMargins(15, 10, 15, 10);
            itemLayout->setSpacing(15);
            
            // 头像
            QLabel *avatar = new QLabel;
            avatar->setFixedSize(56, 56);
            QPixmap avatarPix = makeAvatar(contact.name.left(2), QColor("#90CAF9"), 56);
            avatar->setPixmap(avatarPix);
            avatar->setStyleSheet("border-radius: 28px;");
            
            // 昵称
            QLabel *nameLabel = new QLabel(contact.name);
            nameLabel->setStyleSheet(
                "QLabel {"
                "    font-size: 16px;"
                "    font-weight: bold;"
                "    color: #333;"
                "}"
            );
            
            itemLayout->addWidget(avatar);
            itemLayout->addWidget(nameLabel);
            itemLayout->addStretch();
            
            item->setSizeHint(itemWidget->sizeHint());
            contactsList->insertItem(headerRow + 1, item);
            contactsList->setItemWidget(item, itemWidget);
            headerRow++; // 更新插入位置
        }
    }
}

// onAcceptFriendRequest方法：处理接受好友申请
// 
// 参数：
//   - uid: 好友ID
//   - username: 好友用户名
// 
// 功能：
//   1. 记录日志（调试用）
//   2. 可以扩展为发送网络请求到服务器接受好友申请
//   3. 从"新朋友"列表中移除，添加到"我的好友"列表
void MainWindow::onAcceptFriendRequest(int uid, const QString &username)
{
    // 这里可以添加接受好友申请的逻辑
    // 比如发送网络请求到服务器
    qDebug() << "接受好友申请:" << uid << username;
}


