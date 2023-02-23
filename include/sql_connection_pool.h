#ifndef CONNECTION_POOL_H
#define CONNECTION_POOL_H

#include <stdio.h>
#include <list>
#include <error.h>
#include <string.h>
#include <iostream>
#include <string>
#include "locker.h"
#include "mysql/mysql.h"
#include "log.h"

class connection_pool {
public:
    MYSQL *getConnection(); //获得数据库连接
    bool releaseConnection(MYSQL *conn); //释放连接
    int getFreeconn(); //获取连接
    void destroyPool(); //销毁所有连接
        
    //单例
    static connection_pool* getInstance();
    void init(std :: string url, std :: string user, std :: string passwd, std :: string databaseName, int port, int MaxConn, int close_log);
private:
    connection_pool();
    ~connection_pool();

    int m_Maxconn; //最大连接数
    int m_CurConn; //当前已使用的连接数
    int m_FreeConn; //当前空闲的连接数
    locker lock;
    std :: list<MYSQL* > connList; //连接池
    sem reserve;

public:
    std :: string m_url; // 主机地址
    std :: string port; //数据库端口号
    std :: string m_User; //数据库的用户名
    std :: string m_passwd; //数据库密码
    std :: string m_databaseName; //数据库名称
    int m_close_log; //日志开关
};

class connectionRAII {
public:
    connectionRAII(MYSQL **con, connection_pool *conPool);
    ~connectionRAII();
private:
    MYSQL *conRAII;
    connection_pool *poolRAII;
};
#endif