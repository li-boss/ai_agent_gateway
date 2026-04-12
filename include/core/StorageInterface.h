// include/core/StorageInterface.h
#ifndef STORAGE_INTERFACE_H
#define STORAGE_INTERFACE_H

#include <string>

// 这是一个纯虚类（接口），定义了所有存储介质必须实现的标准动作
class StorageInterface{
public:
        virtual ~StorageInterface() = default;

        // 纯虚函数：强制派生类必须实现
        virtual std::string get_memory(int user_id) const = 0;
        virtual void append_memory(int user_id, const std::string& new_chat) = 0;
};



#endif // STORAGE_INTERFACE_H