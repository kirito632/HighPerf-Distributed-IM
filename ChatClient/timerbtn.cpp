#include "timerbtn.h"

#include<QDebug>

// 构造函数：初始化倒计时按钮（无文本）
// 
// 功能：
//   1. 初始化倒计时器（默认180秒）
//   2. 创建QTimer并连接timeout信号到更新文本的lambda
TimerBtn::TimerBtn(QWidget *parent)
    : QPushButton(parent), _counter(180), _totalSec(180)
{
    _timer = new QTimer(this);

    // 连接定时器信号到更新文本的lambda
    connect(_timer, &QTimer::timeout, [this]() {
        _counter--;
        if (_counter <= 0) {
            reset();
            return;
        }
        this->setText(QString::number(_counter) + "s");
    });
}

// 构造函数：初始化倒计时按钮（带初始文本）
// 
// 参数：
//   - text: 按钮初始文本
//   - parent: 父控件
// 
// 功能：
//   1. 初始化倒计时器（默认180秒）
//   2. 设置按钮初始文本
//   3. 创建QTimer并连接timeout信号
TimerBtn::TimerBtn(const QString &text, QWidget *parent)
    : QPushButton(text, parent), _counter(180), _totalSec(180)
{
    _timer = new QTimer(this);

    connect(_timer, &QTimer::timeout, [this]() {
        _counter--;
        if (_counter <= 0) {
            reset();
            return;
        }
        this->setText(QString::number(_counter) + "s");
    });
}

// 析构函数：停止定时器
TimerBtn::~TimerBtn()
{
    _timer->stop();
}

// mouseReleaseEvent方法：处理鼠标释放事件
// 
// 功能：
//   1. 检查是否为左键点击
//   2. 发送clicked信号（但不会自动开始倒计时）
//   3. 由外部调用start()来开始倒计时
void TimerBtn::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        qDebug() << "TimerBtn clicked (waiting external start)!";
        emit clicked();  // 只发信号，不直接开始倒计时
    }
    QPushButton::mouseReleaseEvent(e);
}

// start方法：开始倒计时
// 
// 功能：
//   1. 禁用按钮（防止重复点击）
//   2. 重置计数器为总秒数（180秒）
//   3. 更新按钮文本为"180s"
//   4. 启动定时器（每秒触发一次）
void TimerBtn::start()
{
    this->setEnabled(false);  // 禁用按钮
    _counter = _totalSec;     // 重置计数器
    this->setText(QString::number(_counter) + "s");  // 更新文本
    _timer->start(1000);       // 启动定时器，每隔1秒触发一次
}

// reset方法：重置按钮
// 
// 功能：
//   1. 停止定时器
//   2. 重置计数器为180秒
//   3. 启用按钮
//   4. 恢复原始文本"获取验证码"
void TimerBtn::reset()
{
    _timer->stop();        // 停止定时器
    _counter = _totalSec;  // 重置计数器
    this->setEnabled(true); // 启用按钮
    this->setText("获取验证码");  // 恢复原始文本
}
