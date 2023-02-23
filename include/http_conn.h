#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/epoll.h>
#include <stdlib.h>
#include <map>

#include "mysql/mysql.h"
#include "locker.h"
#include "sql_connection_pool.h"
#include "lst_timer.h"
#include "log.h"

class http_conn {
public:
    static const int FILENAME_LEN = 200; // 文件名的最大长度
    static const int READ_BUFFER_SIZE = 2048; //读缓冲区大小
    static const int WRITE_BUFFER_SIZE = 1024; //写缓冲区大小
    //http 请求方法
    enum class METHOD {GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT, PATCH};
    enum class CHECK_STATE {CHECK_STATE_REQUEST_LINE,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };
    enum class HTTP_CODE {
        NO_REQUEST,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURCE,
        FORBIDDEN_REQUEST,
        FILE_REQUSET,
        INTERNAL_ERROR,
        CLOSE_CONNECTION
    };
    enum class LINE_STATUS {
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN
    };

public:
    http_conn() {}
    ~http_conn() {}
public: 
    //初始化新接受的连接
    void init(int sockfd, const sockaddr_in& addr, char*, int, int, std :: string user, std :: string passwd, std :: string sqlname);
    // 关闭连接
    void close_conn(bool real_close = true);
    //处理客户请求
    void process();
    //非阻塞读
    bool read_once();
    //非阻塞写
    bool write();

    sockaddr_in *getaddress() {
        return &m_address;
    }
    void initmysql_result(connection_pool *connPool);
    int timer_flag;
    int improv;

private:
    void init(); // 初始化连接
    //解析HTTP请求
    HTTP_CODE process_read();
    //填充HTTP应答
    bool process_write(HTTP_CODE ret);

    //分析HTTP请求
    HTTP_CODE parse_request_line(char* text);
    HTTP_CODE parse_headers(char* text);
    HTTP_CODE parse_content(char* text);
    HTTP_CODE do_request();
    char* get_line() {
        return m_read_buf + m_start_line;
    }
    LINE_STATUS parse_line();

    //生成http响应
    void unmap();
    bool add_response(const char* format, ...);
    bool add_content(const char* content);
    bool add_content_length(int content_length);
    bool add_content_type();
    bool add_status_line(int status, const char* title);
    bool add_headers(int content_length);
    bool add_linger();
    bool add_blank_line();

public:
    //所有的socket的事件都注册到一个epollfd上
    static int m_epollfd;
    //统计用户数量
    static int m_user_count;
    MYSQL *mysql;
    int m_state;

private:
    int m_sockfd;
    sockaddr_in m_address;

    //读缓冲区
    char m_read_buf[READ_BUFFER_SIZE];
    //标识读缓冲中已经读入的客户数据的最后一个字节的下个位置
    int m_read_idx;
    //正在分析的字符在缓冲区中的位置
    int m_checked_idx;
    //当前正在解析的行的起始位置
    int m_start_line;
    //写缓冲区
    char m_write_buf[WRITE_BUFFER_SIZE];
    //写缓冲区中待发送的字节数
    int m_write_idx;

    //主状态机当前所处的状态
    CHECK_STATE m_check_state;
    //请求方法
    METHOD m_method;

    //客户端请求的目标文件的完整路径
    char m_real_file[FILENAME_LEN];

    //客户端请求的目标文件的文件名
    char* m_url;
    
    //HTTP 协议的版本号
    char *m_version;
    //主机名
    char *m_host;
    //HTTP请求的消息体的长度
    int m_content_length;

    //HTTP 请求是否要求保持连接
    bool m_linger;

    //客户端请求的目标文件被mmap到内存中的起始位置
    char* m_file_address;
    //目标文件状态
    struct stat m_file_stat;

    //writev 所需
    struct  iovec m_iv[2];
    int m_iv_count;

    int cgi;
    char *m_string;
    int bytes_to_send;
    int bytes_have_send;
    char *doc_root;

    std::map<std::string, std::string> m_users;
    int m_TRIGMode;
    int m_close_log;

    char sql_user[100];
    char sql_passwd[100];
    char sql_name[100];
};

#endif