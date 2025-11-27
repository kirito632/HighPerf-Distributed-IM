#ifndef QOBJECTSINGLETON_H
#define QOBJECTSINGLETON_H

#include <iostream>

template<typename T>
class QObjectSingleton
{
protected:
    QObjectSingleton() = default;
    QObjectSingleton(const QObjectSingleton&) = delete;
    QObjectSingleton& operator=(const QObjectSingleton&) = delete;

public:
    static T* GetInstance() {
        static T instance;  // static 确保只有一个
        return &instance;
    }

    virtual ~QObjectSingleton() {
        std::cout << "QObjectSingleton destruct: " << typeid(T).name() << std::endl;
    }
};

#endif // QOBJECTSINGLETON_H
