#ifndef MYSQL_UTIL_H
#define MYSQL_UTIL_H

#define SQL_MAX_LEN 1024

const char *const MYSQL_LOG_MODULE = "cgi";
const char *const MYSQL_LOG_PROC = "mysql";

// 获取数据库信息
int get_mysql_info(string &user, string &pwd, string &db);

// 连接mysql
MYSQL *mysql_conn();

// 连接redis
sw::redis::Redis *redis_conn();

// 处理数据库查询结果，结果集保存在buf，只处理一条记录，一个字段, 如果buf为nullptr，无需保存结果集，只做判断有没有此记录
int process_result_one(MYSQL *conn, const char *sql_cmd, char *buf);

#endif
