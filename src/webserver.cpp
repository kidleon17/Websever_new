#include "webserver.h"

Webserver::Webserver() {
    //http_conn对象
    users = new http_conn[MAX_FD];

    //root 文件夹路径
    char server_path[200];
    getcwd(server_path, 200);
    char root[6] = "/root";
    m_root = (char*) malloc(strlen(server_path) + strlen(root) + 1);
    strcpy(m_root, server_path);
    strcat(m_root, root);

    //定时器
    users_timer = new client_data[MAX_FD];
}

Webserver :: ~Webserver() {
    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[1]);
    close(m_pipefd[0]);
    delete[] users;
    delete[] users_timer;
    delete m_pool;
}

void Webserver :: init(int port, std :: string user, std :: string passwd, 
            std :: string databaseName, int log_write, int opt_linger,
            int trigmode, int sql_num,int thread_num, int close_log, int actor_model) {
                m_port = port;
                m_user = user;
                m_passwd = passwd;
                m_databaseName = databaseName;
                m_sql_num = sql_num;
                m_thread_num = thread_num;
                m_log_write = log_write;
                m_close_log = close_log;
                m_OPT_LINGER = opt_linger;
                m_TRIGMode = trigmode;
                m_actor_model = actor_model;
            }

void Webserver :: trig_mode() {
    if(m_TRIGMode == 0) { // LT + LT
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 0;
    } else if(m_TRIGMode == 1) {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 1;
    } else if(m_TRIGMode == 2) {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 0;
    } else if(m_TRIGMode == 3) {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 1;
    }
}

void Webserver :: log_write() {
    if(m_close_log == 0) {
        //初始化
        if(m_log_write == 1) {
            Log :: get_instance() -> init("./ServerLog", m_close_log, 2000, 800000, 800);
        } else {
            Log ::get_instance() -> init("./ServerLog", m_close_log, 2000,800000, 0);
        }
    }
}

void Webserver :: sql_pool() {
    //数据库连接池初始化
    m_connPool = connection_pool :: getInstance();
    m_connPool -> init("localhost", m_user, m_passwd, m_databaseName, 3306, m_sql_num, m_close_log);

    //初始化数据库读表
    users -> initmysql_result(m_connPool);
}

void Webserver :: thread_pool() {
    //线程池
    m_pool = new threadpool<http_conn>(m_actor_model,m_connPool, m_thread_num);
}

void Webserver :: eventListen() {
    //流程
    m_listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(m_listenfd);

    //优雅的关闭连接
    if(m_OPT_LINGER == 0) {
        struct linger tmp = {0, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }

    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(m_port);
    address.sin_family = AF_INET;

    int flag = 1; //TIME_WAIT
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));

    ret = bind(m_listenfd, (struct sockaddr *) &address, sizeof(address));
    assert(ret >= 0);

    ret = listen(m_listenfd, 5);
    assert(ret >= 0);

    utils.init(TIMESLOT);

    epoll_event events[MAX_EVENT_NUMBER];
    m_epollfd = epoll_create(5);

    utils.addfd(m_epollfd, m_listenfd, false, m_LISTENTrigmode);

    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);
    assert(ret != -1);
    utils.setnonblocking(m_pipefd[1]);
    utils.addfd(m_epollfd, m_pipefd[0], false, 0);

    utils.addsig(SIGPIPE, SIG_IGN);
    utils.addsig(SIGALRM, utils.sig_handler, false);
    utils.addsig(SIGTERM, utils.sig_handler, false);

    alarm(TIMESLOT);

    Utils :: u_pipefd = m_pipefd;
    Utils :: u_epollfd = m_epollfd;
}

void Webserver :: timer(int connfd, struct sockaddr_in client_address) {
    users[connfd].init(connfd, client_address, m_root, m_CONNTrigmode, m_close_log, m_user, m_passwd, m_databaseName);

    //初始化client_data数据
    //创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器加入链表
    users_timer[connfd].address = client_address;
    users_timer[connfd].sockfd = connfd;
    util_timer *timer = new util_timer;
    timer -> user_data = &users_timer[connfd];
    timer -> cb_fun = cb_func;
    time_t cur = time(NULL);
    timer -> expire = cur + 3 * TIMESLOT;
    users_timer[connfd].timer = timer;
    utils.m_timer_lst.add_timer(timer);
}

//有数据传输需要调整定时器
void Webserver :: adjust_timer(util_timer *timer) {
    time_t cur = time(NULL);
    timer -> expire = cur + 3 * TIMESLOT;
    utils.m_timer_lst.adjust_timer(timer);

    LOG_INFO("%s", "adjust timer once");
}

void Webserver ::deal_timer(util_timer *timer, int sockfd) {
    timer -> cb_fun(&users_timer[sockfd]);
    if(timer) {
        utils.m_timer_lst.delete_timer(timer);
    }
    LOG_INFO("close fd %d", users_timer[sockfd].sockfd);
}

bool Webserver :: dealclinetdata() {
    struct sockaddr_in client_address;
    socklen_t client_addrlength = sizeof(client_address);
    if(m_LISTENTrigmode == 0) {
        int connfd = accept(m_listenfd, (struct sockaddr*) &client_address, &client_addrlength);
        if(connfd < 0) {
            LOG_ERROR("%s: errno is %d", "accept error", errno);
            return false;
        }

        if(http_conn:: m_user_count >= MAX_FD) {
            utils.show_error(connfd, "Internal server busy");
            LOG_ERROR("%s", "Internal server busy");
            return false;
        }
        timer(connfd, client_address);
    } else {
        while(1) {
            int connfd = accept(m_listenfd, (struct sockaddr*) &client_address, &client_addrlength);
            if(connfd < 0) {
                LOG_ERROR("%s:errno is %d", "accept error", errno);
                break;
            }
            if(http_conn :: m_user_count >= MAX_FD) {
                utils.show_error(connfd, "Internal server busy");
                LOG_ERROR("%s", "Internal server busy");
                break;
            }
            timer(connfd, client_address);
        }
        return false;
    }
    return true;
}

bool Webserver :: dealwithsignal(bool &timeout, bool &stop_server) {
    int ret = 0;
    int sig;
    char signals[1024];
    ret = recv(m_pipefd[0], signals, sizeof(signals), 0);
    if(ret == -1) {
        return false;
    } else if(ret == 0) {
        return false;
    } else {
        for(int i = 0; i < ret; i++) {
            switch(signals[i]) {
                case SIGALRM: {
                    timeout = true;
                    break;
                }
                case SIGTERM: {
                    stop_server = true;
                    break;
                }
            }
        }
    }
    return true;
}

void Webserver :: dealwithread(int sockfd) {
    util_timer *timer = users_timer[sockfd].timer;

    //reactor
    if(m_actor_model == 1) {
        if(timer) {
            adjust_timer(timer);
        }
        //检测到事件，放入请求队列
        m_pool -> append(users + sockfd, 0);

        while(true) {
            if(users[sockfd].improv == 1) {
                if(users[sockfd].timer_flag == 1) {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    } else {
        //proactor
        if(users[sockfd].read_once()) {
            LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].getaddress() -> sin_addr));

            //监测到读事件，将事件放入请求队列
            m_pool -> append_p(users + sockfd);

            if(timer) {
                adjust_timer(timer);
            }
        } else {
            deal_timer(timer, sockfd);
        }
    }    
}

void Webserver :: dealwithwrite(int sockfd) {
    util_timer *timer = users_timer[sockfd].timer;
    //reactor
    if(m_actor_model == 1) {
        if(timer) {
            adjust_timer(timer);
        }

        m_pool -> append(users + sockfd, 1);

        while (true) {
            if(users[sockfd].improv == 1) {
                if(users[sockfd].timer_flag) {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    } else {
        //proactor
        if(users[sockfd].write()) {
            LOG_INFO("send data to the client(%s)", inet_ntoa(users[sockfd].getaddress() -> sin_addr));
            if(timer) {
                adjust_timer(timer);
            }
        } else {
            deal_timer(timer, sockfd);
        }
    }
}

void Webserver :: eventLoop() {
    bool timeout = false;
    bool stop_server = false;

    while(!stop_server) {
        int number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
        if(number < 0 && errno != EINTR) {
            LOG_ERROR("%s", "epoll failure");
            break;
        }

        for(int i = 0; i < number; i++) {
            int sockfd = events[i].data.fd;

            //处理客户端连接
            if(sockfd == m_listenfd) {
                bool flag = dealclinetdata();
                if(flag = false) {
                    continue;
                }
            } else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                //服务器关闭连接，移除定时器
                util_timer *timer = users_timer[sockfd].timer;
                deal_timer(timer, sockfd);
            } else if((sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN)) {
                bool flag = dealwithsignal(timeout, stop_server);
                if(flag == false) {
                    LOG_ERROR("%s", "deal clientdata failure");
                }
            } else if(events[i].events & EPOLLIN) {//处理客户连接上接收到的数据
                dealwithread(sockfd);
            } else if(events[i].events & EPOLLOUT) {
                dealwithwrite(sockfd);
            }
        }
        if(timeout) {
            utils.timer_handler();

            LOG_INFO("%s", "timer tick");

            timeout = false;
        }
    }
}