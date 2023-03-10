#include "lst_timer.h"
#include "http_conn.h"

sort_timer_lst :: sort_timer_lst(): head(NULL), tail(NULL) {}
sort_timer_lst :: ~sort_timer_lst () {
        util_timer * tmp = head;
        while(tmp) {
            head = head -> next;
            delete tmp;
            tmp = head;
        }
    }
void sort_timer_lst ::add_timer(util_timer* timer) {
    if(!timer) {
        return;
    }
    if(!head) {
        head = timer;
        tail = timer;
        return;
    }
    if(timer->expire < head -> expire) {
        timer -> next = head;
        head -> prev = timer;
        head = timer;
        return;
    }
    add_timer(timer, head);
}

void sort_timer_lst ::adjust_timer (util_timer* timer) {
    if(timer == NULL) {
        return;
    }
    util_timer* tmp = timer -> next;
    if(!tmp || timer -> expire < tmp -> expire) {
        return;
    }
    if(timer == head) {
        head = head -> next;
        head ->prev = NULL;
        timer -> next = NULL;
        add_timer(timer, head);
    } else {
        timer -> prev -> next = timer -> next;
        timer -> next -> prev = timer -> prev;
        add_timer(timer, head);
    }
}
void sort_timer_lst ::delete_timer(util_timer *timer) {
    if(!timer) {
        return;
    }

    if(timer == head && head == tail) {
        delete timer;
        tail = NULL;
        head = NULL;
        return;
    }

    if(timer == tail) {
        timer -> prev -> next = NULL;
        tail = timer -> prev;
        delete timer;
        return;
    }

    timer -> next -> prev = timer -> prev;
    timer -> prev -> next = timer -> next;
    delete timer;
}

void sort_timer_lst ::tick() {
    if(!head) {
        return;
    }
    printf("time tick\n");
    time_t cur = time(NULL);
    util_timer *tmp = head;
    while(tmp) {
        if(cur < tmp -> expire) {
            break;
        }
        tmp -> cb_fun(tmp -> user_data);
        head = tmp -> next;
        if(head) {
            head -> prev = NULL;
        }
        delete tmp;
        tmp = head;
    }
}
void sort_timer_lst ::add_timer(util_timer* timer, util_timer *lst_head) {
    util_timer* prev = head;
    util_timer* tmp = head -> next;
    while(tmp) {
        if(tmp -> expire > timer -> expire) {
            timer -> next = tmp;
            timer -> prev = prev;
            prev -> next = timer;
            tmp -> prev = timer;
        }
        prev = tmp;
        tmp = tmp -> next;
    }
    if(!tmp) {
        prev -> next = timer;
        timer -> prev = prev;
        timer -> next = NULL;
        tail = timer;
    }
}

void Utils :: init(int timeslot) {
    m_TIMESLOT = timeslot;
}

int Utils :: setnonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFD, new_option);
    return old_option;
}

void Utils :: addfd(int epollfd, int fd, bool one_shot, int TRIGMode) {
    epoll_event event;
    event.data.fd = fd;

    if(TRIGMode == 1) {
        event.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
    } else {
        event.events = EPOLLIN | EPOLLRDHUP;
    }

    if(one_shot) {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

//????????????
void Utils :: sig_handler(int sig) {
    int save_errno = errno;
    int msg = sig;
    send(u_pipefd[1], (char* ) &msg, 1 ,0);
    errno = save_errno;
}

void Utils :: addsig(int sig, void (handler)(int), bool restart) {
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if(restart) {
        sa.sa_flags = SA_RESTART;
    }
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
} 

void Utils :: timer_handler() {
    m_timer_lst.tick();
    alarm(m_TIMESLOT);
}

void Utils :: show_error(int connfd, const char *info) {
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int Utils :: u_epollfd = 0;
int *Utils :: u_pipefd = 0;

class Utils;
void cb_func(client_data *user_data) {
    epoll_ctl(Utils :: u_epollfd, EPOLL_CTL_DEL, user_data -> sockfd, 0);
    assert(user_data);
    close(user_data -> sockfd);
    http_conn :: m_user_count--;
}
