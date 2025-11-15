#pragma once
#include<iostream>
template<typename T>
class Singleton
{
protected:
    Singleton() = default;
    Singleton(const Singleton&) = delete;
    Singleton& operator=(const Singleton&) = delete;

public:
    static std::shared_ptr<T> GetInstance() {
        //static std::shared_ptr<T> instance = std::make_shared<T>();  // std::make_shared 无法访问私有构造函数，改用 new 可以
        static std::shared_ptr<T> instance(new T());
        return instance;
    }

    ~Singleton() {
        std::cout << "this is singleton destruct " << std::endl;
    }
};