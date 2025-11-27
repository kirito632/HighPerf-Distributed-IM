#ifndef SINGLETON_H
#define SINGLETON_H
#include"global.h"

template<typename T>
class Singleton
{
protected:
    Singleton() = default;
    Singleton(const Singleton&) = delete;
    Singleton& operator=(const Singleton&) = delete;

public:
    static std::shared_ptr<T> GetInstance(){
        static std::shared_ptr<T> instance(new T());
        return instance;
    }

    ~Singleton(){
        std::cout<<"this is singleton destruct "<<std::endl;
    }
};

#endif // SINGLETON_H
