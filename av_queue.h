/* 因为是模板类，所以声明和定义要放在一起 */
#pragma once
#include <queue>
#include <algorithm>

extern "C"
{
#include <SDL2/SDL.h>
#include <libavutil/log.h>
}

/* 支持多线程的单个元素进队出队的队列，用于如音/视频编码数据包队列、视频帧队列 */
template <typename T>
class AvQueue
{
private:
    std::queue<T> q;    // 队列
    const std::size_t q_len = 100;  // 队列长度
    SDL_mutex *mutex;   // 互斥锁
    SDL_cond *cond;     // 条件变量
    int running = 1;    // 用于外部停止队列的阻塞
public:
    AvQueue();
    AvQueue(std::size_t q_len);
    // 禁用拷贝构造函数和赋值运算符(浅拷贝对于互斥锁和条件变量是不安全的)
    AvQueue(const AvQueue&) = delete;
    AvQueue& operator=(const AvQueue&) = delete;
    ~AvQueue();
    void push(T element);   // 进队
    bool try_push(T element);   // 非阻塞进队
    T pop();                // 出队
    int size(){
        return this->q.size();
    }
    void stop(){    // 用于外部停止队列的阻塞
        this->running = 0;
        SDL_CondBroadcast(this->cond);
    }
    void clear(void(*callback)(void*)); // 回调函数, 用来自定义释放资源方式, nullptr代表不需要释放
};

// 支持多线程的单个元素进队出队的队列，用于如音/视频编码数据包队列、视频帧队列
template <typename T>
class AvBufferQueue{
private:
    T* q;
    const std::size_t q_len = 100;
    std::size_t q_size;
    SDL_mutex *mutex;
    SDL_cond *cond;
    int head;
    int tail;
    int running = 1;
public:
    AvBufferQueue();
    AvBufferQueue(std::size_t q_len);
    AvBufferQueue(const AvBufferQueue&) = delete;
    AvBufferQueue& operator=(const AvBufferQueue&) = delete;
    ~AvBufferQueue();
    void push(T* element, std::size_t len);  // 进队
    void pop(T* element, std::size_t len);   // 出队
    std::size_t size(){
        return this->q_size;
    }
    void stop(){        // 用于外部停止队列的阻塞
        this->running = 0;
        SDL_CondBroadcast(this->cond);
    }
    void clear(){
        SDL_LockMutex(this->mutex);
        this->q_size = 0;
        this->head = 0;
        this->tail = 0;
        SDL_CondBroadcast(this->cond);  // 因为相当于pop all, 还是要唤醒大家
        SDL_UnlockMutex(this->mutex);
    }
};

template <typename T>
AvQueue<T>::AvQueue(){
    this->mutex = SDL_CreateMutex();
    this->cond = SDL_CreateCond();
}

template <typename T>
AvQueue<T>::AvQueue(std::size_t q_len): q_len(q_len){
    this->mutex = SDL_CreateMutex();
    this->cond = SDL_CreateCond();
}

template <typename T>
AvQueue<T>::~AvQueue(){
    SDL_DestroyMutex(this->mutex);
    SDL_DestroyCond(this->cond);
}

// 入队, 将element放入this->q, 并唤醒等待的线程(this->running=0时不保证正确性)
template <typename T>
void AvQueue<T>::push(T element){
    SDL_LockMutex(this->mutex);
    while (this->running && this->q.size() >= this->q_len){   // 队列长度大于等于最大长度时等待
        av_log(nullptr, AV_LOG_DEBUG, "queue is full: %zu\n", this->q.size());
        SDL_CondWait(this->cond, this->mutex);
    }
    if (!this->running){
        SDL_UnlockMutex(this->mutex);
        return;
    }
    this->q.push(element);
    SDL_CondBroadcast(this->cond);
    SDL_UnlockMutex(this->mutex);
}

template <typename T>
bool AvQueue<T>::try_push(T element){
    SDL_LockMutex(this->mutex);
    if (!this->running || this->q.size() >= this->q_len){
        SDL_UnlockMutex(this->mutex);
        return false;
    }
    this->q.push(element);
    SDL_CondBroadcast(this->cond);
    SDL_UnlockMutex(this->mutex);
    return true;
}

// 出队, 从this->q pop出一个元素, 并唤醒等待的线程(this->running=0时不保证正确性)
template <typename T>
T AvQueue<T>::pop(){
    SDL_LockMutex(this->mutex);
    while (this->running && this->q.empty()){   // 队列长度小于0时等待
        SDL_CondWait(this->cond, this->mutex);
    }
    if (!this->running){
        SDL_UnlockMutex(this->mutex);
        return T();
    }
    av_log(nullptr, AV_LOG_DEBUG, "queue size: %zu\n", this->q.size());
    T ret = this->q.front();
    this->q.pop();
    SDL_CondBroadcast(this->cond);
    SDL_UnlockMutex(this->mutex);
    return ret;
}

template <typename T>
void AvQueue<T>::clear(void(*callback)(void*)){
    SDL_LockMutex(this->mutex);
    if (callback){
        while (!this->q.empty()){
            callback(&(this->q.front()));
            this->q.pop();
        }
    }else{
        while (!this->q.empty()){
            this->q.pop();
        }
    }
    av_log(nullptr, AV_LOG_INFO, "queue clear, %zu\n", this->q.size());
    SDL_CondBroadcast(this->cond);
    SDL_UnlockMutex(this->mutex);
}

template <typename T>
AvBufferQueue<T>::AvBufferQueue(){
    this->q = new T[this->q_len];
    this->q_size = 0;
    this->head = 0;     // 队列头，左边，指向第一个元素
    this->tail = 0;     // 队列尾，右边，指向最后一个元素后一个位置
    this->mutex = SDL_CreateMutex();
    this->cond = SDL_CreateCond();
}

template <typename T>
AvBufferQueue<T>::AvBufferQueue(std::size_t q_len): q_len(q_len){
    this->q = new T[this->q_len];
    this->q_size = 0;
    this->head = 0;
    this->tail = 0;
    this->mutex = SDL_CreateMutex();
    this->cond = SDL_CreateCond();
}

template <typename T>
AvBufferQueue<T>::~AvBufferQueue(){
    delete[] this->q;
    SDL_DestroyMutex(this->mutex);
    SDL_DestroyCond(this->cond);
}

// 入队, 将长度为len的element放入this->q, 并唤醒等待的线程(this->running=0时不保证正确性)
template <typename T>
void AvBufferQueue<T>::push(T* element, std::size_t len){
    SDL_LockMutex(this->mutex);
    while (this->running && this->q_size+len >= this->q_len){
        SDL_CondWait(this->cond, this->mutex);
    }
    if (!this->running){
        SDL_UnlockMutex(this->mutex);
        return;
    }
    int l = std::min(len, this->q_len - this->tail);
    memcpy(this->q + this->tail, element, l);
    memcpy(this->q, element + l, len - l);
    this->tail = (this->tail + len) % this->q_len;
    this->q_size += len;
    SDL_CondBroadcast(this->cond);
    SDL_UnlockMutex(this->mutex);
}

// 出队, 从this->q pop出len个元素放入element地址, 并唤醒等待的线程(this->running=0时不保证正确性)
template <typename T>
void AvBufferQueue<T>::pop(T* element, std::size_t len){
    SDL_LockMutex(this->mutex);
    while (this->running && this->q_size < len){
        SDL_CondWait(this->cond, this->mutex);
    }
    if (!this->running){
        SDL_UnlockMutex(this->mutex);
        return;
    }
    int l = std::min(len, this->q_len - this->head);
    if (element){
        memcpy(element, this->q + this->head, l);
        memcpy(element + l, this->q, len - l);
    }   // 如果element为空则直接抛弃
    this->head = (this->head + len) % this->q_len;
    this->q_size -= len;
    SDL_CondBroadcast(this->cond);
    SDL_UnlockMutex(this->mutex);
}
