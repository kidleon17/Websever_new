#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H

#include <iostream>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#include <queue>

#include "locker.h"

template<typename T> 
class block_queue {
public: 
    block_queue(int max_size): m_max_size(max_size) {}
    ~block_queue() {}

    void clear() {
        m_mutex.lock();
        while(!q.empty()) {
            q.pop();
        }
        m_mutex.unlock();
    }

    bool full() {
        m_mutex.lock();
        int size = q.size();
        m_mutex.unlock();
        return size >= m_max_size;
    }

    bool empty() {
        m_mutex.lock();
        int size = q.size();
        m_mutex.unlock();
        return size == 0;
    }

    bool front(T &value) {
        m_mutex.lock();
        int size = q.size();
        if(size == 0) {
            m_mutex.unlock();
            return false;
        } else {
            value = q.front();
            m_mutex.unlock();
            return true;
        }
    }

    bool back(T &value) {
        m_mutex.lock();
        int size = q.size();
        if(size == 0) {
            m_mutex.unlock();
            return false;
        } else {
            value = q.back();
            m_mutex.unlock();
            return true;
        }
    }

    int size() {
        m_mutex.lock();
        int size = q.size();
        m_mutex.unlock();
        return size;       
    }

    int max_size() {
        m_mutex.lock();
        int size = m_max_size;
        m_mutex.unlock();
        return size;  
    }

    bool push(const T& item) {
        m_mutex.lock();
        if(q.size() >= m_max_size) {
            m_cond.broadcast();
            m_mutex.unlock();
            return false;
        }
        
        q.push(item);
        m_cond.broadcast();
        m_mutex.unlock();
        return true;
    }
    
    bool pop(T& item) {
        m_mutex.lock();
        while(q.size() <= 0) { //没有元素可弹出。等待条件变量
            if(!m_cond.wait(m_mutex.get())) {
                m_mutex.unlock();
                return false;
            }
        }

        item = q.front();
        q.pop();
        m_mutex.unlock();
        return true;
    } 

    bool pop(T& item, int ms_timeout) {
        struct timespec t = {0, 0};
        struct timeval now = {0, 0};
        gettimeofday(&now, NULL);
        m_mutex.lock();
        if(q.size() <= 0) {
            t.tv_sec = now.tv_sec + ms_timeout / 1000;
            t.tv_nsec = (ms_timeout % 1000) * 1000;
            if(!m_cond.timewait(m_mutex.get(), t)) {
                m_mutex.unlock();
                return false;
            }
        }
        item = q.front();
        q.pop();
        m_mutex.unlock();
        return true;
    }
private:
    locker m_mutex;
    cond m_cond;
    std :: queue<T> q;

    int m_max_size;
};
#endif