#ifndef MEMORY_ENGINE_H
#define MEMORY_ENGINE_H

#include "StorageInterface.h"
#include <string>
#include <unordered_map>
#include <shared_mutex>
#include <mutex>
class MemoryStorage:public StorageInterface{
public:
    // 1. 获取用户的历史记忆 ,读操作
    // 面试考点：为什么这里要加 const？因为读取操作不应该修改类的状态]
    // 使用 override 关键字，让编译器帮你检查是否正确重写了虚函数
    std::string get_memory(int user_id) const override{
        // 【核心魔法 1：共享锁 (读锁)】
        // shared_lock 允许多个线程同时持有这把锁。
        // 只要没人写，1000 个线程同时来读，都不会互相阻塞！
        std::shared_lock<std::shared_mutex> lock(rw_mutex);
        
        //it返回迭代器，指向这个id
        auto it=memory_storage.find(user_id);
        if(it!=memory_storage.end()){
            return it->second;
        }
        // 如果是新用户，返回空记忆
        return "";

    }

    // 2. 追加新记忆 (写操作)
    void append_memory(int user_id, const std::string& new_chat)override{
        // 【核心魔法 2：独占锁 (写锁)】
        // unique_lock 是极其霸道的。一旦它拿到了锁：
        // 1. 所有的写线程必须等它写完。
        // 2. 所有的读线程也必须等它写完！(防止读到写了一半的脏数据)
        std::unique_lock<std::shared_mutex>lock(rw_mutex);

        // 追加字符串，用换行符分割历史记录
        memory_storage[user_id]+= new_chat +"\n";
    }


private:
    
    // 核心存储结构：基于哈希表的 KV 存储
    std::unordered_map<int,std::string>memory_storage;

    // 核心护盾：C++14 引入的读写锁
    // mutable 关键字是 C++ 的细节考点：它允许我们在 const 函数 (比如 get_memory) 中对这把锁进行加锁/解锁操作
    mutable std::shared_mutex rw_mutex;

};






#endif // MEMORY_ENGINE_H