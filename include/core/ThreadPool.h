// ============================================================================
// [NEW ADDITION AREA - START]
// 新增部分：添加了 <atomic>，并在 ThreadPool 类中引入了 active_workers 原子计数，
//           提供了 get_queue_size()、get_active_workers()、get_total_workers() 方法，
//           并在 Worker 线程执行任务前后分别递增与递减该计数器以统计负载。
// 修改日期：2026-06-14
// 范围：第一行至下面的 [NEW ADDITION AREA - END]
// ============================================================================

#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <vector>
#include <queue>
#include <thread>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <future>
#include <stdexcept>
#include <atomic>

class ThreadPool {
public:
    explicit ThreadPool(size_t threads);
    ~ThreadPool();

    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args)
         -> std::future<typename std::result_of<F(Args...)>::type>;

    // 负载查询接口
    size_t get_queue_size() {
        std::lock_guard<std::mutex> lock(queue_mutex);
        return tasks.size();
    }

    size_t get_active_workers() const {
        return active_workers.load();
    }

    size_t get_total_workers() const {
        return workers.size();
    }

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop;

    // 活跃工作线程计数
    std::atomic<size_t> active_workers{0};
};

inline ThreadPool::ThreadPool(size_t threads) : stop(false) {
    for (size_t i = 0; i < threads; ++i) {
        workers.emplace_back([this] {
            while (true) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(this->queue_mutex);
                    this->condition.wait(lock, [this] {
                        return this->stop || !this->tasks.empty();
                    });

                    if (this->stop && this->tasks.empty()) {
                        return;
                    }
                    task = std::move(this->tasks.front());
                    this->tasks.pop();
                }
                
                // 执行前递增活跃数
                active_workers++;
                task();
                // 执行后递减活跃数
                active_workers--;
            }
        });
    }
}

inline ThreadPool::~ThreadPool() {
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        stop = true;
    }
    condition.notify_all();

    for (std::thread &worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

template<class F, class... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args)
     -> std::future<typename std::result_of<F(Args...)>::type> {
    using return_type = typename std::result_of<F(Args...)>::type;
    
    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );
    
    std::future<return_type> res = task->get_future();
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        if (stop) {
            throw std::runtime_error("网关已关闭，禁止向线程池提交新任务!");
        }
        tasks.emplace([task]() { (*task)(); });
    }
    condition.notify_one();
    return res;
}

#endif // THREAD_POOL_H

// ============================================================================
// [NEW ADDITION AREA - END]
// ============================================================================


// ============================================================================
// 以下为原文件内容，已注释保留以防直接删除：
// ============================================================================
/*
#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include<vector>
#include<queue>
#include<thread>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <future>
#include <stdexcept>

class ThreadPool {
public:
    //构造函数：初始化时固定数量的worker线程
    explicit ThreadPool(size_t threads);

    //析构函数
    ~ThreadPool();

    //模板函数：向队列中投入任务
    template<class F,class... Args>
    auto enqueue(F&& f,Args&&... args)
         ->std::future<typename std::result_of<F(Args...)>::type>;

private:
    //工作线程组
    std::vector<std::thread> workers;

    //任务队列
    std::queue<std::function<void()>> tasks;

    //互斥锁
    std::mutex queue_mutex;
    //条件变量，阻塞线程
    std::condition_variable condition;

    //停止标志
    bool stop;

};

//构造函数
inline ThreadPool::ThreadPool(size_t threads):stop(false){
    for(size_t i=0;i<threads;++i){
        
            // emplace_back 直接在 vector 尾部构造线程，传入一个 Lambda 作为线程的运行逻辑
            workers.emplace_back([this]{

                while(true){
                    std::function<void()> task;
                    {
                        //加锁：准备查收任务队列
                        std::unique_lock<std::mutex> lock(this->queue_mutex);

                        // 条件变量：如果 stop 为 false 且队列为空，就在这里等待，并自动解开锁
                        //防止虚假唤醒
                        this->condition.wait(lock,[this]{
                            return this->stop||!this->tasks.empty();
                        });

                        if(this->stop&&this->tasks.empty()){
                            return;
                        }
                        //拿到任务，出队
                        task=std::move(this->tasks.front());
                        this->tasks.pop();
                    }
                    //离开大括号作用域，lock 自动解锁，让其他线程可以继续抢下一个任务
                    task();
                }
                
            });
        
        
    }
}

//析构函数
inline ThreadPool::~ThreadPool(){
    {std::unique_lock<std::mutex> lock(queue_mutex);
        stop=true;
    }
    condition.notify_all();

    for(std::thread &worker :workers){
        //阻塞等待，直到轮流析构之后再结束
        worker.join();
    }

}

//入队
template<class F,class... Args>
    auto ThreadPool::enqueue(F&& f,Args&&... args)
         ->std::future<typename std::result_of<F(Args...)>::type>
         {
            // 推导这个传入函数的返回值类型
            using return_type =typename std::result_of<F(Args...)>::type;
            
            //完美转发(std::forward):原封不动地保留参数的左值/右值属性
            //packaged_task:把函数和参数包起来，方便以后跨线程拿返回值
            auto task= std::make_shared<std::packaged_task<return_type()>>(
                std::bind(std::forward<F>(f),std::forward<Args>(args)...)
            );
            
            //拿到未来的结果凭证(网关主线程拿着这个凭证去等大模型 API 的返回结果)
            std::future<return_type> res=task->get_future();
            {
                
                //加锁，准备把仟务塞进队列
                std::unique_lock<std::mutex> lock(queue_mutex);
                
                //如果线程池已经停止，绝不允许再塞新请求，抛出异常
                if(stop){
                    throw std::runtime_error("网关已关闭，禁止向线程池提交新任务!");
                }
                
                //把装满参数 and 函数的 task 强行转成无参无返回值的 void()，塞入队列
                tasks.emplace([task](){(*task)();});

            }
            condition.notify_one();
            return res;
            }

#endif // THREAD_POOL_H
*/