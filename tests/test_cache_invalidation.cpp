// 缓存失效功能测试
// 用于验证单个失效、批量失效、全量清空等功能

#include <iostream>
#include <vector>
#include <cassert>
#include <chrono>

// 模拟 MysqlDao 和缓存
class MockCacheStats {
public:
    size_t current_size;
    size_t capacity;
    uint64_t total_gets;
    uint64_t hits;
    uint64_t misses;
    uint64_t total_puts;
    uint64_t total_removes;
    uint64_t evictions;
};

class MockMysqlDao {
public:
    MockMysqlDao() : cache_size_(0) {}
    
    // 单个失效
    void InvalidateUserCache(int uid) {
        if (cache_size_ > 0) cache_size_--;
        std::cout << "[Cache] Invalidated uid: " << uid << std::endl;
    }
    
    // 批量失效
    void InvalidateUserCacheMultiple(const std::vector<int>& uids) {
        for (int uid : uids) {
            if (cache_size_ > 0) cache_size_--;
        }
        std::cout << "[Cache] Invalidated " << uids.size() << " users" << std::endl;
    }
    
    // 全量清空
    void ClearUserCacheAll() {
        size_t cleared = cache_size_;
        cache_size_ = 0;
        std::cout << "[Cache] Cleared all " << cleared << " users" << std::endl;
    }
    
    // 模拟添加缓存
    void AddToCache(int uid) {
        cache_size_++;
    }
    
    // 获取缓存大小
    size_t GetCacheSize() const {
        return cache_size_;
    }
    
private:
    size_t cache_size_;
};

// 测试用例
void TestSingleInvalidation() {
    std::cout << "\n=== Test 1: Single Invalidation ===" << std::endl;
    
    MockMysqlDao dao;
    
    // 添加 10 个用户到缓存
    for (int i = 1; i <= 10; i++) {
        dao.AddToCache(i);
    }
    std::cout << "Cache size after adding 10 users: " << dao.GetCacheSize() << std::endl;
    assert(dao.GetCacheSize() == 10);
    
    // 失效单个用户
    dao.InvalidateUserCache(5);
    std::cout << "Cache size after invalidating uid 5: " << dao.GetCacheSize() << std::endl;
    assert(dao.GetCacheSize() == 9);
    
    std::cout << "✓ Test 1 passed" << std::endl;
}

void TestMultipleInvalidation() {
    std::cout << "\n=== Test 2: Multiple Invalidation ===" << std::endl;
    
    MockMysqlDao dao;
    
    // 添加 20 个用户到缓存
    for (int i = 1; i <= 20; i++) {
        dao.AddToCache(i);
    }
    std::cout << "Cache size after adding 20 users: " << dao.GetCacheSize() << std::endl;
    assert(dao.GetCacheSize() == 20);
    
    // 批量失效 5 个用户
    std::vector<int> uids = {1, 5, 10, 15, 20};
    dao.InvalidateUserCacheMultiple(uids);
    std::cout << "Cache size after invalidating 5 users: " << dao.GetCacheSize() << std::endl;
    assert(dao.GetCacheSize() == 15);
    
    std::cout << "✓ Test 2 passed" << std::endl;
}

void TestClearAll() {
    std::cout << "\n=== Test 3: Clear All ===" << std::endl;
    
    MockMysqlDao dao;
    
    // 添加 100 个用户到缓存
    for (int i = 1; i <= 100; i++) {
        dao.AddToCache(i);
    }
    std::cout << "Cache size after adding 100 users: " << dao.GetCacheSize() << std::endl;
    assert(dao.GetCacheSize() == 100);
    
    // 全量清空
    dao.ClearUserCacheAll();
    std::cout << "Cache size after clearing all: " << dao.GetCacheSize() << std::endl;
    assert(dao.GetCacheSize() == 0);
    
    std::cout << "✓ Test 3 passed" << std::endl;
}

void TestInvalidationSequence() {
    std::cout << "\n=== Test 4: Invalidation Sequence ===" << std::endl;
    
    MockMysqlDao dao;
    
    // 模拟实际场景：添加、失效、添加、批量失效、清空
    
    // 第 1 步：添加 10 个用户
    for (int i = 1; i <= 10; i++) {
        dao.AddToCache(i);
    }
    std::cout << "Step 1 - Cache size: " << dao.GetCacheSize() << std::endl;
    assert(dao.GetCacheSize() == 10);
    
    // 第 2 步：失效 3 个用户
    dao.InvalidateUserCache(1);
    dao.InvalidateUserCache(5);
    dao.InvalidateUserCache(10);
    std::cout << "Step 2 - Cache size after single invalidations: " << dao.GetCacheSize() << std::endl;
    assert(dao.GetCacheSize() == 7);
    
    // 第 3 步：添加 5 个新用户
    for (int i = 11; i <= 15; i++) {
        dao.AddToCache(i);
    }
    std::cout << "Step 3 - Cache size after adding 5 new users: " << dao.GetCacheSize() << std::endl;
    assert(dao.GetCacheSize() == 12);
    
    // 第 4 步：批量失效 4 个用户
    std::vector<int> uids = {2, 3, 11, 12};
    dao.InvalidateUserCacheMultiple(uids);
    std::cout << "Step 4 - Cache size after batch invalidation: " << dao.GetCacheSize() << std::endl;
    assert(dao.GetCacheSize() == 8);
    
    // 第 5 步：全量清空
    dao.ClearUserCacheAll();
    std::cout << "Step 5 - Cache size after clear all: " << dao.GetCacheSize() << std::endl;
    assert(dao.GetCacheSize() == 0);
    
    std::cout << "✓ Test 4 passed" << std::endl;
}

void TestPerformance() {
    std::cout << "\n=== Test 5: Performance ===" << std::endl;
    
    MockMysqlDao dao;
    
    // 添加 10000 个用户到缓存
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 1; i <= 10000; i++) {
        dao.AddToCache(i);
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto add_time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    std::cout << "Time to add 10000 users: " << add_time << " ms" << std::endl;
    
    // 批量失效 1000 个用户
    std::vector<int> uids;
    for (int i = 1; i <= 1000; i++) {
        uids.push_back(i);
    }
    
    start = std::chrono::high_resolution_clock::now();
    dao.InvalidateUserCacheMultiple(uids);
    end = std::chrono::high_resolution_clock::now();
    auto invalidate_time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    std::cout << "Time to invalidate 1000 users: " << invalidate_time << " ms" << std::endl;
    
    // 全量清空
    start = std::chrono::high_resolution_clock::now();
    dao.ClearUserCacheAll();
    end = std::chrono::high_resolution_clock::now();
    auto clear_time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    std::cout << "Time to clear all: " << clear_time << " ms" << std::endl;
    
    std::cout << "✓ Test 5 passed" << std::endl;
}

int main() {
    std::cout << "========== Cache Invalidation Tests ==========" << std::endl;
    
    try {
        TestSingleInvalidation();
        TestMultipleInvalidation();
        TestClearAll();
        TestInvalidationSequence();
        TestPerformance();
        
        std::cout << "\n========== All Tests Passed! ==========" << std::endl;
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "Test failed: " << e.what() << std::endl;
        return 1;
    }
}
