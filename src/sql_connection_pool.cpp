#include "sql_connection_pool.h"

MYSQL* connection_pool::getConnection() {
    MYSQL* con = NULL;

    if(connList.size() == 0) {
        return NULL;
    } else {

        reserve.wait();
        lock.lock();

        con = connList.front();
        connList.pop_front();

        --m_FreeConn;
        ++m_CurConn;

        lock.unlock();

        return con;
    }
} //获得数据库连接

bool connection_pool :: releaseConnection(MYSQL *conn) {
    if(conn == NULL) {
        return false;
    } else {
        lock.lock();
        connList.push_back(conn);
        ++m_FreeConn;
        --m_CurConn;
        lock.unlock();

        reserve.post();
        return true;
    }
} //释放连接

int connection_pool :: getFreeconn() {
    return this -> m_FreeConn;
} //获取连接

void connection_pool :: destroyPool() {
    lock.lock();
    
    for(auto it : connList) {
        mysql_close(it);
    }
    m_CurConn = 0;
    m_FreeConn = 0;
    connList.clear();

    lock.unlock();
} //销毁所有连接
    
//单例
connection_pool* connection_pool :: getInstance() {
    static connection_pool connPool;
    return &connPool;
}
void connection_pool ::  init(std :: string url, std :: string user, std :: string passwd, std :: string databaseName, int port, int MaxConn, int close_log) {
    m_url = url;
    m_User = user;
    m_passwd = passwd;
    m_databaseName = databaseName;
    m_close_log = close_log;

    for(int i = 0; i < MaxConn; i++) {
        MYSQL *con = NULL;
        con = mysql_init(con);

        if(con == NULL) {
            LOG_ERROR("MySQL Error");
            exit(1);
        }

        con = mysql_real_connect(con, url.c_str(), user.c_str(), passwd.c_str(), databaseName.c_str(), port, NULL, 0);

        if(con == NULL) {
            LOG_ERROR("MYSQL Error");
            exit(1);
        }

        connList.push_back(con);
        ++m_FreeConn;
    }
}
connection_pool :: connection_pool() {
    m_CurConn = 0;
    m_Maxconn = 0;
}
connection_pool :: ~connection_pool() {
    destroyPool();
}

connectionRAII :: connectionRAII(MYSQL **SQL, connection_pool *connPool) {
    *SQL = connPool -> getConnection();

    conRAII = *SQL;
    poolRAII = connPool;
}

connectionRAII :: ~connectionRAII() {
    poolRAII -> releaseConnection(conRAII);
}