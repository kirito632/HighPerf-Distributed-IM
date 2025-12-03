#ifndef TIMERBTN_H
#define TIMERBTN_H

#include<QPushButton>
#include<QTimer>
#include<QMouseEvent>

// TimerBtn类：倒计时按钮
// 
// 作用：
//   1. 提供带倒计时功能的按钮
//   2. 用于"获取验证码"等场景，防止频繁点击
//   3. 倒计时期间禁用按钮
// 
// 使用场景：
//   - 注册/登录时获取验证码
//   - 需要在指定时间内防止重复操作的场景
class TimerBtn : public QPushButton
{
    // Q_OBJECT

public:
    // 构造函数：初始化倒计时按钮（无文本）
    // 
    // 功能：
    //   1. 初始化倒计时器（默认180秒）
    //   2. 连接定时器信号到更新文本的lambda
    explicit TimerBtn(QWidget* parent = nullptr);
    
    // 构造函数：初始化倒计时按钮（带初始文本）
    // 参数：
    //   - text: 按钮初始文本（如"获取验证码"）
    //   - parent: 父控件
    explicit TimerBtn(const QString &text, QWidget *parent = nullptr);
    
    // 析构函数：停止定时器
    ~TimerBtn();

    // 开始倒计时
    // 
    // 功能：
    //   1. 禁用按钮
    //   2. 重置计数器为总秒数（180秒）
    //   3. 更新按钮文本为"180s"
    //   4. 启动定时器（每秒触发一次）
    void start();
    
    // 重置按钮
    // 
    // 功能：
    //   1. 停止定时器
    //   2. 重置计数器
    //   3. 启用按钮
    //   4. 恢复原始文本"获取验证码"
    void reset();

protected:
    // 鼠标释放事件：处理点击
    // 
    // 功能：
    //   1. 检查是否为左键点击
    //   2. 发送clicked信号（但不会自动开始倒计时）
    //   3. 由外部调用start()来开始倒计时
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    // 定时器（每秒触发一次）
    QTimer* _timer;
    
    // 当前倒计时秒数
    int _counter;
    
    // 总倒计时秒数（构造时固定为180秒）
    int _totalSec;
};

#endif // TIMERBTN_H
