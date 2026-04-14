# AI Agent Gateway (C++ 高性能异步智能体网关)

🚀 **一个基于 C++ 原生特性的高并发、带状态 (Stateful) 的大模型 API 异步网关。**

在大多数 AI Agent 框架（如 LangChain）依赖 Python 导致多轮对话并发受限的背景下，本项目从零手写底层网络调度与记忆隔离。通过原生线程池与 $O(1)$ 的 LRU 淘汰算法，实现了极低延迟的多用户上下文管理，为 AI Agent 提供了一个坚如磐石的 C++ 运行时中间件。

## ✨ 核心亮点 (Key Features)

- **⚡ 高吞吐异步调度 (Asynchronous Dispatching)**
  抛弃低效的同步阻塞模型。基于 C++11 原生 `std::thread` 构建自定义任务队列，主线程以极低开销瞬间派发网络请求，后台 Worker 线程池全异步接管 LLM 的网络 I/O。
- **🧠 O(1) 线程安全记忆池 (Memory Management & LRU)**
  告别无脑拼接。针对多轮对话的 Token 膨胀痛点，基于 `std::unordered_map` 与双向链表 `std::list::splice` 手写了工业级的 **LRU (最近最少使用) 缓存淘汰引擎**。精准控制内存用量，确保高并发下的多用户上下文严格隔离与安全读写。
- **🛡️ 健壮的网络防腐层与组装逻辑 (Prompt Parsing & Network)**
  封装底层 HTTPS 握手与大模型鉴权，内置超时熔断与错误隔离机制。哪怕大模型服务端宕机或网络断开，网关也绝不崩溃，且自动拒绝将错误信息污染至用户的核心记忆池。
- **📝 RAII 工业级双写日志库 (Dual-write Logger)**
  抛弃 `std::cout`，自主实现基于 RAII 生命周期的线程安全日志组件。支持控制台 ANSI 颜色高亮与本地 `gateway.log` 文件自动追加，精准记录系统运行时的并发轨迹与网络耗时。

## 🏗️ 架构数据流 (Architecture)

```mermaid
graph TD
    UserA((用户 1)) -->|Prompt| MainThread[主线程分发器]
    UserB((用户 2)) -->|Prompt| MainThread
    
    subgraph 异步网关引擎 (C++ Gateway Engine)
        MainThread -->|入队| TaskQueue[任务队列]
        TaskQueue -->|异步抢占| Worker1[Worker 线程]
        TaskQueue -->|异步抢占| Worker2[Worker 线程]
        
        Worker1 <-->|读写上下文| LRU[(LRU 记忆池<br>O1哈希+双向链表)]
        Worker2 <-->|读写上下文| LRU
    end
    
    Worker1 -->|组装报文 HTTP POST| DeepSeek[DeepSeek 大模型]
    Worker2 -->|组装报文 HTTP POST| DeepSeek