#ifndef MYSQL_UTIL_H
#define MYSQL_UTIL_H

#include <mysql/mysql.h>
#include <sw/redis++/redis++.h>

#include <fstream>
#include <string>

#include "cgi_util.h"
#include "make_log.h"
#include "rapidjson/document.h"
#include "rapidjson/istreamwrapper.h"

using namespace std;
using namespace rapidjson;
using namespace sw::redis;

#define SQL_MAX_LEN 1024

const char *const MYSQL_LOG_MODULE = "cgi";
const char *const MYSQL_LOG_PROC = "databases";

// 获取数据库信息
int getMysqlInfo(string &host, string &user, string &pwd, string &db);

// 连接mysql
MYSQL *mysqlConn();

// 连接redis
sw::redis::Redis *redisConn();

// 处理数据库查询结果，结果集保存在buf，只处理一条记录，一个字段,
// 如果buf为nullptr，无需保存结果集，只做判断有没有此记录
int processResultOne(MYSQL *conn, const char *sql_cmd, char *buf);

#endif
