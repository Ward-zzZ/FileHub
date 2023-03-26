#ifndef MYSQL_UTIL_H
#define MYSQL_UTIL_H

#define SQL_MAX_LEN 1024

using namespace std;
using namespace rapidjson;

const char *const MYSQL_LOG_MODULE = "cgi";
const char *const MYSQL_LOG_PROC = "mysql";
// 获取数据库信息
int get_mysql_info(string &user, string &pwd, string &db);

// 连接数据库
MYSQL *mysql_conn();

// 处理数据库查询结果，结果集保存在buf，只处理一条记录，一个字段, 如果buf为nullptr，无需保存结果集，只做判断有没有此记录
// 返回值： 0成功并保存记录集，1没有记录集，2有记录集但是没有保存，-1失败
int process_result_one(MYSQL *conn, const char *sql_cmd, char *buf);

#endif
