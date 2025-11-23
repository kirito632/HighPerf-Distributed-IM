#pragma once
#include <iostream>
#include <memory>
template<typename T>
class Singleton
{
protected:
    Singleton() = default;
    Singleton(const Singleton&) = delete;
    Singleton& operator=(const Singleton&) = delete;

public:
    static std::shared_ptr<T> GetInstance() {
        static std::shared_ptr<T> instance(new T(), [](T* ptr) {
            delete ptr;
        });
        return instance;
    }

    ~Singleton() {
        std::cout << "this is singleton destruct " << std::endl;
    }
};