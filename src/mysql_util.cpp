#include <iostream>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mysql/mysql.h>
#include "rapidjson/document.h"
#include "rapidjson/istreamwrapper.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "mysql_util.h"
#include "make_log.h"

// 获取数据库信息
int get_mysql_info(string &user, string &pwd, string &db);

// 连接数据库
MYSQL *mysql_conn();

// 处理数据库查询结果，结果集保存在buf，只处理一条记录，一个字段, 如果buf为nullptr，无需保存结果集，只做判断有没有此记录
// 返回值： 0成功并保存记录集，1没有记录集，2有记录集但是没有保存，-1失败
int process_result_one(MYSQL *conn, const char *sql_cmd, char *buf);

// 获取数据库信息
int get_mysql_info(string &user, string &pwd, string &db)
{
  ifstream ifs("../conf/cfg.json");
  if (!ifs.is_open())
  {
    LOG_ERROR(MYSQL_LOG_MODULE, MYSQL_LOG_PROC, "Failed to open cfg.json");
    return -1;
  }

  IStreamWrapper isw(ifs); // 将文件流包装为流输入
  Document doc;
  doc.ParseStream(isw);

  if (!doc.HasMember("mysql"))
  {
    LOG_ERROR(MYSQL_LOG_MODULE, MYSQL_LOG_PROC, "Failed to find mysql section in cfg.json");
    return -2;
  }

  const Value &mysql = doc["mysql"];
  if (!mysql.HasMember("ip") || !mysql.HasMember("port") || !mysql.HasMember("database") || !mysql.HasMember("user") || !mysql.HasMember("password"))
  {
    LOG_ERROR(MYSQL_LOG_MODULE, MYSQL_LOG_PROC, "Failed to find required mysql parameters in cfg.json");
    return -3;
  }

  user = mysql["user"].GetString();
  pwd = mysql["password"].GetString();
  db = mysql["database"].GetString();

  return 0;
}

// 连接数据库
MYSQL *mysql_conn()
{
  string mysql_user;
  string mysql_pwd;
  string mysql_db;
  int ret = get_mysql_info(mysql_user, mysql_pwd, mysql_db); // 读取配置文件，获得数据库的用户名、密码、数据库名
  if (ret != 0)
  {
    LOG_DEBUG(MYSQL_LOG_MODULE, MYSQL_LOG_PROC, "get_mysql_info failed!");
    return nullptr;
  }
  LOG_INFO(MYSQL_LOG_MODULE, MYSQL_LOG_PROC, "mysql_user = %s, mysql_pwd = %s, mysql_db = %s", mysql_user.c_str(), mysql_pwd.c_str(), mysql_db.c_str());
  MYSQL *conn = mysql_init(NULL);
  if (conn == NULL)
  {
    return nullptr;
  }

  if (mysql_real_connect(conn, "localhost", mysql_user.c_str(), mysql_pwd.c_str(), mysql_db.c_str(), 0, NULL, 0) == NULL)
  {
    LOG_ERROR(MYSQL_LOG_MODULE, MYSQL_LOG_PROC, "mysql_real_connect error! user=%s, pwd=%s, db=%s", mysql_user.c_str(), mysql_pwd.c_str(), mysql_db.c_str());
    mysql_close(conn);
    return nullptr;
  }
  // 设置数据库编码，主要处理中文编码问题
  if (mysql_query(conn, "set names utf8") != 0)
  {
    LOG_DEBUG(MYSQL_LOG_MODULE, MYSQL_LOG_PROC, "set names utf8 failed!");
    mysql_close(conn);
    return nullptr;
  }
  return conn;
}

// 处理数据库查询结果，结果集保存在buf，只处理一条记录，一个字段, 如果buf为nullptr，无需保存结果集，只做判断有没有此记录
// 返回值： 0成功并保存记录集，1没有记录集，2有记录集但是没有保存，-1失败
int process_result_one(MYSQL *conn, const char *sql_cmd, char *buf)
{
  MYSQL_RES *res_set = nullptr; // 结果集结构的指针
  int ret = 0;

  do
  {
    if (mysql_query(conn, sql_cmd) != 0)
    {
      // 执行sql语句，执行成功返回0
      LOG_ERROR(MYSQL_LOG_MODULE, MYSQL_LOG_PROC, "mysql_query error! sql_cmd=%s", sql_cmd);
      ret = -1;
      break;
    }

    res_set = mysql_store_result(conn); // 生成结果集
    if (res_set == nullptr)
    {
      LOG_ERROR(MYSQL_LOG_MODULE, MYSQL_LOG_PROC, "mysql_store_result error!");
      ret = -1;
      break;
    }

    const unsigned long line = mysql_num_rows(res_set); // mysql_num_rows接受由mysql_store_result返回的结果结构集，并返回结构集中的行数
    if (line == 0)
    {
      ret = 1; // 1没有记录集
      break;
    }
    else if (line > 0 && buf == nullptr)
    {
      // 如果buf为nullptr，无需保存结果集，只做判断有没有此记录
      ret = 2; // 2有记录集但是没有保存
      break;
    }

    MYSQL_ROW row;
    if ((row = mysql_fetch_row(res_set)) != nullptr)
    {
      // mysql_fetch_row从结果结构中提取一行，并把它放到一个行结构中。当数据用完或发生错误时返回nullptr.
      if (row[0] != nullptr)
      {
        strcpy(buf, row[0]);
      }
    }

  } while (false);

  if (res_set != nullptr)
  {
    // 完成所有对数据的操作后，调用mysql_free_result来善后处理
    mysql_free_result(res_set);
  }

  return ret;
}
