#include "http_conn.h"

#include "mysql/mysql.h"
#include <fstream>
//定义响应信息
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "You request has bad synatax or inherently impossible to satisify\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server\n";
const char* error_404_tilte = "Not Found";
const char* error_404_form = "The requested file was not found on this server\n";
const char* error_500_title = "Internal Error";
const char* errnr_500_form = "THe was an unusual problem serving the request file\n";

locker m_lock;
std :: map<std :: string, std :: string> users;

void http_conn :: initmysql_result(connection_pool *connPool) {
    //取出一个连接
    MYSQL *mysql = NULL;
    connectionRAII mysqlcon(&mysql, connPool);

    //检索username，passwd数据
    if(mysql_query(mysql, "SELECT username, passwd FROM user")) {
        LOG_ERROR("SELECT error: %s\n", mysql_error(mysql));
    }

    //检索结果
    MYSQL_RES *result = mysql_store_result(mysql);

    //返回结果的列数
    int num_fields = mysql_num_fields(result);

    //返回所有字段结构数组
    MYSQL_FIELD *fields = mysql_fetch_field(result);

    //存用户名和密码
    while(MYSQL_ROW row = mysql_fetch_row(result)) {
        std :: string temp1(row[0]);
        std :: string temp2(row[1]);
        users[temp1] = temp2;
    }
} 

int setnonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

void addfd(int epollfd, int fd, bool one_shot, int TRIGMode) {
    epoll_event event;
    event.data.fd = fd;
    if(TRIGMode == 1) {
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP; 
    }
    else {
        event.events = EPOLLIN | EPOLLRDHUP;
    }
    if(one_shot) {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

void removefd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

void modfd(int epollfd, int fd, int ev, int TRIGMode) {
    epoll_event event;
    event.data.fd = fd;
    if(TRIGMode == 1) {
        event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    } else {
        event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
    }
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

int http_conn :: m_user_count = 0;
int http_conn :: m_epollfd = -1;

void http_conn :: close_conn(bool real_close) {
    if(real_close && m_sockfd != -1) {
        printf("close %d\n", m_sockfd);
        removefd(m_epollfd, m_sockfd);
        m_user_count--;
        m_sockfd = -1;
    }
} 

void http_conn :: init(int sockfd, const sockaddr_in& addr, char* root, int TRIGMode, int close_flag, std :: string user, std :: string passwd, std :: string sqlname) {
    m_sockfd = sockfd;
    m_address = addr;
    addfd(m_epollfd, sockfd, true, m_TRIGMode);
    m_user_count++;

    doc_root = root;
    m_TRIGMode = TRIGMode;
    m_close_log = close_flag;
    strcpy(sql_user, user.c_str());
    strcpy(sql_passwd, passwd.c_str());
    strcpy(sql_name, sqlname.c_str());

    init();
}

void http_conn :: init() {
    mysql = NULL;
    bytes_have_send = 0;
    bytes_to_send = 0;
    m_check_state = CHECK_STATE :: CHECK_STATE_REQUEST_LINE;
    m_linger = false;

    m_method = METHOD :: GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    cgi = 0;
    m_state = 0;
    timer_flag = 0;
    improv = 0;

    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}

http_conn :: LINE_STATUS http_conn :: parse_line() {
    char temp;
    for(; m_checked_idx < m_read_idx; ++m_checked_idx) {
        temp = m_read_buf[m_checked_idx];
        if(temp == '\r') {
            if(m_checked_idx + 1 == m_read_idx) {
                return LINE_STATUS :: LINE_OPEN;
            } else if(m_read_buf[m_checked_idx + 1] == '\n') {
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_STATUS :: LINE_OK;
            }
            return LINE_STATUS :: LINE_BAD;
        } else if(temp == '\n') {
            if(m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == '\r') {
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\n';
                return LINE_STATUS :: LINE_OK;
            }
            return LINE_STATUS :: LINE_BAD;
        }
    }
    return LINE_STATUS :: LINE_OPEN;
}

//循环读取客户数据，直到无数据可读或者对方关闭连接
//非阻塞ET要一次读完
bool http_conn :: read_once() {
    if(m_read_idx >= READ_BUFFER_SIZE) {
        return false;
    }

    int bytes_read = 0;
    if(m_TRIGMode == 0) {
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        m_read_idx += bytes_read;

        if(bytes_read <= 0) {
            return false;
        }
    } else {
        while (true) {
            bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
            if(bytes_read == -1) {
                if(errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                }
                return false;
            } else if (bytes_read == 0) {
                return false;
            }

            m_read_idx += bytes_read;
        }
    }
    return true;
}

//解析HTTP请求行，获取请求方法、目标URL，以及HTTP版本号
http_conn :: HTTP_CODE http_conn :: parse_request_line(char *text) {
    m_url = strpbrk(text, "\t");
    if(! m_url) {
        return HTTP_CODE :: BAD_REQUEST;
    }
    *m_url++ == '\0';

    char* method = text;
    if(strcasecmp(method, "GET") == 0) {
        m_method = METHOD :: GET;
    } else if(strcasecmp(method, "POST") == 0) {
        m_method = METHOD :: POST;
        cgi = 1;
    } else {
        return HTTP_CODE :: BAD_REQUEST;
    }

    m_url += strspn(m_url, " \t");
    m_version = strpbrk(m_url, " \t");
    if( !m_version) {
        return HTTP_CODE :: BAD_REQUEST;
    }
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");
    if(strcasecmp(m_version, "HTTP/1.1") != 0) {
        return HTTP_CODE :: BAD_REQUEST;
    }
    if( strncasecmp(m_url, "http://", 7) == 0) {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }
    if(!m_url || m_url[0] != '/') {
        return HTTP_CODE :: BAD_REQUEST;
    }
    //当url为/ 时显示判断界面
    if(strlen(m_url) == 1) {
        strcat(m_url, "judge.html");
    }

    m_check_state = CHECK_STATE :: CHECK_STATE_HEADER;
    return HTTP_CODE :: NO_REQUEST;
}

//解析HTTP请求的一个头部信息
http_conn :: HTTP_CODE http_conn :: parse_headers(char* text) {
    //空行，头部字段解析完毕
    if(text[0] == '\0') {
        //有请求消息体，还需要读取m_content_length字节的消息体，状态机转移到CHECK_STATE_CONTENT状态
        if(m_content_length != 0) {
            m_check_state = CHECK_STATE :: CHECK_STATE_CONTENT;
            return HTTP_CODE :: NO_REQUEST;
        }

        //已经获得了一个完整的HTTP请求
        return HTTP_CODE :: GET_REQUEST;
    } else if(strncasecmp(text, "Connection:", 11) == 0) { //处理Connection头部字段
        text += 11;
        text += strspn(text, " \t");
        if(strcasecmp(text, "keep-alive") == 0) {
            m_linger = true;
        }
    } else if(strncasecmp(text, "Content-Length:", 15) == 0) {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    } else if(strncasecmp(text, "Host:", 5) == 0) {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    } else {
        LOG_INFO("oop! unknow header %s\n", text);
    }
    return HTTP_CODE :: NO_REQUEST;
}

//判断HTTP请求的消息体是否被完整读入
http_conn :: HTTP_CODE http_conn :: parse_content(char *text) {
    if(m_read_idx >= (m_content_length + m_checked_idx)) {
        text[m_content_length] = '\0';
        m_string = text;
        return HTTP_CODE :: GET_REQUEST;
    }
    return HTTP_CODE :: NO_REQUEST;
}

http_conn :: HTTP_CODE http_conn :: process_read() {
    LINE_STATUS line_status = LINE_STATUS :: LINE_OK;
    HTTP_CODE ret = HTTP_CODE :: NO_REQUEST;
    char *text = 0;
    while(((m_check_state == CHECK_STATE :: CHECK_STATE_CONTENT) && (line_status == LINE_STATUS :: LINE_OK)) || ((line_status = parse_line()) == LINE_STATUS :: LINE_OK)) {
        text = get_line();
        m_start_line = m_checked_idx;
        printf("got 1 http line: %s\n", text);

        switch (m_check_state) {
            case CHECK_STATE :: CHECK_STATE_REQUEST_LINE: {
                ret = parse_request_line(text);
                if(ret == HTTP_CODE :: BAD_REQUEST) {
                    return HTTP_CODE :: BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE :: CHECK_STATE_HEADER: {
                ret = parse_headers(text);
                if(ret == HTTP_CODE :: BAD_REQUEST) {
                    return HTTP_CODE :: BAD_REQUEST;
                } else if(ret == HTTP_CODE :: GET_REQUEST) {
                    return do_request();
                }
                break;
            }
            case CHECK_STATE :: CHECK_STATE_CONTENT: {
                ret = parse_content(text);
                if(ret == HTTP_CODE :: GET_REQUEST) {
                    return do_request();
                }
                line_status = LINE_STATUS :: LINE_OPEN;
                break;
            }
            default: {
                return HTTP_CODE :: INTERNAL_ERROR;
            }
        }
    }
    return HTTP_CODE :: NO_REQUEST;
}

//得到一个完整的、正确的HTTP请求时，分析文件的属性，有目标文件存在，对所有用户可读，且不是目录，则使用mmap将其映射到内存地址m_file_address处，并告诉调用者获得文件成功
http_conn :: HTTP_CODE http_conn :: do_request() {
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    const char* p = strrchr(m_url, '/');

    //处理cgi
    if(cgi == 1 && (*(p + 1) == '2') || *(p + 1) == '3') {
        char flag = m_url[1];

        char *m_url_real = (char* )malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/");
        strcat(m_url_real, m_url + 2);
        strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
        free(m_url_real);

        char name[100], password[100];
        int i;
        for(i = 5; m_string[i] != '&'; ++i) {
            name[i - 5] = m_string[i];
        }
        name[i - 5] = '\0';
        int j = 0;
        for(i = i + 10; m_string[i] != '\0'; ++i, ++j) {
            password[j] = m_string[i];
        }
        name[j] = '\0';

        if(*(p + 1) == '3') {
            //注册
            char *sql_insert = (char *) malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "', '");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");
            
            if(users.find(name) == users.end()) {
                m_lock.lock();
                int res = mysql_query(mysql, sql_insert);
                users.insert({name, password});
                m_lock.unlock();

                if(!res) {
                    strcpy(m_url, "/log.html");
                } else  {
                    strcpy(m_url, "registerError.html");
                }
            } else {
                strcpy(m_url, "/registerError.html");
            }
        } else if(*(p + 1) == '2') {
            //登录，直接判断
            if(users.find(name) != users.end() && users[name] == password) {
                strcpy(m_url, "/welcome.html");
            } else {
                strcpy(m_url, "/logError.html");
            }
        }
    }

    if(*(p + 1) == '0') {
        char *m_url_real  = (char*) malloc(sizeof(char) * 100);
        strcpy(m_url_real, "/register.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    } else if(*(p + 1) == '1') {
        char *m_url_real = (char* ) malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/log.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    } else if(*(p + 1) == '5') {
        char *m_url_real = (char* ) malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/picture.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    } else if(*(p + 1) == '6') {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/video.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    } else if(*(p + 1) == '7') {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/fans.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    } else {
        strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
    }

    if(stat(m_real_file, &m_file_stat) < 0) {
        return HTTP_CODE ::NO_RESOURCE;
    }
    if(!(m_file_stat.st_mode & S_IROTH)) {
        return HTTP_CODE :: FORBIDDEN_REQUEST;
    }
    if(S_ISDIR(m_file_stat.st_mode)) {
        return HTTP_CODE :: BAD_REQUEST;
    }

    int fd = open(m_real_file, O_RDONLY);
    m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return HTTP_CODE :: FILE_REQUSET;
}

//对内存映射区执行mumap
void http_conn :: unmap() {
    if(m_file_address) {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

//写HTTP响应
bool http_conn :: write() {
    int temp = 0;
    if(bytes_to_send == 0) {
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        init();
        return true;
    }

    while(1) {
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if(temp <= -1) {
            //TCP 写缓冲没有空间，则等待下一轮EPOLLOUT事件。
            if(errno == EAGAIN) {
                modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
                return true;
            }
            unmap();
            return false;
        }

        bytes_to_send -= temp;
        bytes_have_send += temp;
        if(bytes_have_send >= m_iv[0].iov_len) {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send; 
        } else {
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
        }

        if(bytes_to_send <= 0) {
            unmap();
            modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);

            if(m_linger) {
                init();
                return true;
            } else {
                return false;
            }
        }
    }
}

//往写缓冲区写入待发送的数据
bool http_conn :: add_response(const char* format, ...) {
    if(m_write_idx >= WRITE_BUFFER_SIZE) {
        return false;
    }
    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
    if(len >= (WRITE_BUFFER_SIZE - 1- m_write_idx)) {
        return false;
    }
    m_write_idx += len;
    va_end(arg_list);

    LOG_INFO("requset: %s", m_write_buf);
    return true;
}

bool http_conn :: add_status_line(int status, const char* title) {
    return add_response("%s %d\r\n", "HTTP/1.1", status, title);
}

bool http_conn :: add_headers(int content_len) {
    return add_content_length(content_len) && add_linger() && add_blank_line();
}

bool http_conn :: add_content_type() {
    return add_response("Content-Type: %s\r\n", "text/html");
}

bool http_conn :: add_content_length(int content_len) {
    return add_response("Content-Length: %d\r\n", content_len);
}

bool http_conn :: add_linger() {
    return add_response("Connection: %s\r\n", (m_linger == true) ? "keep-alive" : "close");
}

bool http_conn :: add_blank_line() {
    return add_response("%s", "\r\n");
}

bool http_conn :: add_content(const char* content) {
    return add_response("%s", content);
}

bool http_conn :: process_write(HTTP_CODE ret) {
    switch (ret) {
        case HTTP_CODE :: INTERNAL_ERROR: {
            add_status_line(500, error_500_title);
            add_headers(strlen(errnr_500_form));
            if(!add_content(errnr_500_form)) {
                return false;
            }
            break;
        }
        case HTTP_CODE :: BAD_REQUEST: {
            add_status_line(400, error_400_title);
            add_headers(strlen(error_400_form));
            if(!add_content(error_400_form)) {
                return false;
            }
            break;
        }
        case HTTP_CODE :: NO_RESOURCE: {
            add_status_line(404, error_404_tilte);
            add_headers(strlen(error_404_form));
            if(!add_content(error_404_form)) {
                return false;
            }
            break;
        }
        case HTTP_CODE :: FORBIDDEN_REQUEST: {
            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            if(!add_content(error_403_form)) {
                return false;
            }
            break;
        }
        case HTTP_CODE :: FILE_REQUSET: {
            add_status_line(200, ok_200_title);
            if(m_file_stat.st_size != 0) {
                add_headers(m_file_stat.st_size);
                m_iv[0].iov_base = m_write_buf;
                m_iv[0].iov_len = m_write_idx;
                m_iv[1].iov_base = m_file_address;
                m_iv[1].iov_len = m_file_stat.st_size;
                m_iv_count = 2;
                bytes_to_send = m_write_idx + m_file_stat.st_size;
                return true;
            } else {
                const char* ok_string = "<html><>body></body></html>";
                add_headers(strlen(ok_string));
                if(!add_content(ok_string)) {
                    return false;
                }
            }
        }
        default: 
            return false;
    }
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}

//处理HTTP请求的入口函数
void http_conn :: process() {
    HTTP_CODE read_ret = process_read();
    if(read_ret == HTTP_CODE :: NO_REQUEST) {
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        return;
    }

    bool write_ret = process_write(read_ret);
    if(!write_ret) {
        close_conn();
    }

    modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
}