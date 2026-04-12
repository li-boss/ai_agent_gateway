#ifndef MEMORY_ENGINE_H
#define MEMORY_ENGINE_H

#include "StorageInterface.h"
#include <string>
#include<list>
#include <unordered_map>
#include <shared_mutex>
#include <mutex>
class MemoryStorage:public StorageInterface{
public:
    
    explicit MemoryStorage(int capacity = 100) : capacity_(capacity) {}
    // 1. 获取用户的历史记忆 ,读操作
    // 面试考点：为什么这里要加 const？因为读取操作不应该修改类的状态]
    // 使用 override 关键字，让编译器帮你检查是否正确重写了虚函数
    std::string get_memory(int user_id) const override{
        // 【核心魔法 1：共享锁 (读锁)】
        // shared_lock 允许多个线程同时持有这把锁。
        // 只要没人写，1000 个线程同时来读，都不会互相阻塞！
        std::unique_lock<std::shared_mutex> lock(rw_mutex);
        
        //it返回迭代器，指向这个id
        auto it=cache_map_.find(user_id);
        if(it==cache_map_.end()){
            return "";//没找到返回空
        }
        
        // 找到了！通过 splice 将该节点从原位置直接“剪切”并贴到链表头部 (O(1) 操作)
        // 注意：这里的 const_cast 是因为 C++ 的 const 函数限制，工程上合理
        auto& non_const_list = const_cast<std::list<CacheNode>&>(cache_list_);
        non_const_list.splice(non_const_list.begin(), non_const_list, it->second);

        return it->second->memory_data;

    }

    // 2. 追加新记忆 (写操作)
    void append_memory(int user_id, const std::string& new_chat)override{
        // 【核心魔法 2：独占锁 (写锁)】
        // unique_lock 是极其霸道的。一旦它拿到了锁：
        // 1. 所有的写线程必须等它写完。
        // 2. 所有的读线程也必须等它写完！(防止读到写了一半的脏数据)
        std::unique_lock<std::shared_mutex>lock(rw_mutex);

        auto it=cache_map_.find(user_id);

        if(it!=cache_map_.end()){

            // 情况 A：老用户，更新内容，并把它挪到最前面
            it->second->memory_data+=new_chat+"\n";
            cache_list_.splice(cache_list_.begin(),cache_list_,it->second);

        }else{
            // 情况 B：新用户
            // 如果缓存已经满了，准备痛下杀手，淘汰最久未使用的那个（尾部元素）

            if(cache_map_.size()>=capacity_){
                int old_user=cache_list_.back().user_id; // 拿到尾部节点的用户 ID
                cache_list_.pop_back();                  // 砍掉尾部节点
                cache_map_.erase(old_user);              // 从哈希表中注销
            }

            // 插入新用户到头部
            cache_list_.emplace_front(user_id, new_chat + "\n");
            // 在哈希表中记录它的位置
            cache_map_[user_id] = cache_list_.begin();
        }

    }


private:
    
    // 核心存储结构：基于哈希表的 KV 存储
    //std::unordered_map<int,std::string>memory_storage;

    // 核心护盾：C++14 引入的读写锁
    // mutable 关键字是 C++ 的细节考点：它允许我们在 const 函数 (比如 get_memory) 中对这把锁进行加锁/解锁操作
    mutable std::shared_mutex rw_mutex;

    // 定义双向链表里存放的节点结构：存 UserID 和对应的记忆字符串
    struct CacheNode{
        int user_id;
        std::string memory_data;
        CacheNode(int id, const std::string& data) : user_id(id), memory_data(data) {}
    };

    // 缓存最大容量（比如最多记 100 个用户的对话）
    int capacity_;

    // 核心双向链表：头部是最新的，尾部是最老的
    std::list<CacheNode> cache_list_;

    // 核心哈希表：Key 是 UserID，Value 是指向链表节点的迭代器
    // 这样查找时就不需要遍历链表了，直接 O(1) 定位！
    std::unordered_map<int, std::list<CacheNode>::iterator> cache_map_;

};






#endif // MEMORY_ENGINE_H