#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>

#include "threadpool.h"
#include "http_conn.h"
#include "lst_timer.h"

const int MAX_FD = 65536; //最大文件描述符
const int MAX_EVENT_NUMBER = 10000; //最大事件数
const int TIMESLOT = 5; //超时单位

class Webserver {
public:
    Webserver();
    ~Webserver();
    void init(int port, std :: string user, std :: string passwd, std :: string databaseName
    , int log_write, int opt_linger, int trigmode, int sql_num,int thread_num, int close_log, int actor_model);

    void thread_pool();
    void sql_pool();
    void log_write();
    void eventListen();
    void trig_mode();
    void eventLoop();
    void timer(int connfd, struct sockaddr_in client_address);
    void adjust_timer(util_timer* timer);
    void deal_timer(util_timer* timer, int sockfd);
    bool dealclinetdata();
    bool dealwithsignal(bool& timeout, bool& stop_server);
    void dealwithread(int sockfd);
    void dealwithwrite(int sockfd);

public:
    //基本信息
    int m_port;
    char* m_root;
    int m_log_write;
    int m_close_log;
    int m_actor_model;

    int m_pipefd[2];
    int m_epollfd;
    http_conn *users;

    //数据库
    connection_pool *m_connPool;
    std :: string m_user;
    std :: string m_passwd;
    std :: string m_databaseName;
    int m_sql_num;

    //线程池
    threadpool<http_conn> *m_pool;
    int m_thread_num;

    //epoll_event
    epoll_event events[MAX_EVENT_NUMBER];

    int m_listenfd;
    int m_OPT_LINGER;
    int m_TRIGMode;
    int m_LISTENTrigmode;
    int m_CONNTrigmode;

    //定时器
    client_data *users_timer;
    Utils utils;
};
#endif