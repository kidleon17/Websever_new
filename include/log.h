#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <iostream>
#include <stdarg.h>
#include <pthread.h>
#include "block_queue.h"

class Log {
public: 
    static Log *get_instance() {
        static Log instance;
        return &instance;
    }
    static void *flush_log_thread(void *args) {
        Log :: get_instance() -> async_write_log();
    }

    bool init (const char *filename, int close_log, int log_buf_size = 8192, int split_lines = 500000, int max_queue_size = 0);

    void write_log(int level, const char *format, ...);

    void flush(void);

private:
    Log();
    virtual ~Log();
    void async_write_log() {
        std :: string single_log;
        //取出一个日志，写进文件
        while(m_log_queue -> pop(single_log)) {
            m_mutex.lock();
            fputs(single_log.c_str(), m_fp);
            m_mutex.unlock();
        }
    }
private:
    char dir_name[128]; // 路径名
    char log_name[128]; // log文件名
    int m_split_lines; //最大行数
    int m_log_buf_size; // 日志缓冲区大小
    long long m_count; //日志行数记录
    int m_today; //记录当天时间
    FILE *m_fp; //打开log文件指针
    char *m_buf;
    block_queue<std :: string> *m_log_queue; //阻塞队列
    bool m_is_async; //是否同步标志位
    locker m_mutex;
    int m_close_log; //关闭日志
};

#define LOG_DEBUG(format, ...) if(m_close_log == 0) {Log :: get_instance() -> write_log(0, format, ##__VA_ARGS__); Log :: get_instance() -> flush();}
#define LOG_INFO(format, ...) if(m_close_log == 0) {Log :: get_instance() -> write_log(1, format, ##__VA_ARGS__); Log :: get_instance() -> flush();}
#define LOG_WARN(format, ...) if(m_close_log == 0) {Log :: get_instance() -> write_log(2, format, ##__VA_ARGS__); Log :: get_instance() -> flush();}
#define LOG_ERROR(format, ...) if(m_close_log == 0) {Log :: get_instance() -> write_log(3, format, ##__VA_ARGS__); Log :: get_instance() -> flush();}

#endif