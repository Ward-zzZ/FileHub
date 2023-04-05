/**
 * @file reg_cgi.cpp
 * @brief  处理注册的CGI程序
 * @author ward
 * @version 1.0
 * @date 2023年3月24日
 */
#include <iostream>
#include <fstream>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mysql/mysql.h>
#include "rapidjson/document.h"
#include "rapidjson/istreamwrapper.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "fcgi_config.h"
#include "fcgi_stdio.h"
#include "make_log.h"

const char *const REG_LOG_MODULE = "cgi";
const char *const REG_LOG_PROC = "reg";
#define SQL_MAX_LEN 1024

using namespace std;
using namespace rapidjson;

// 解析json包，获得包括用户名、昵称、密码、邮箱
int get_reg_info(const char *reg_buf, char *user, char *nick_name, char *pwd, char *email)
{
  // 解析json包
  Document doc;
  doc.Parse(reg_buf);

  if (!doc.IsObject())
  {
    LOG_ERROR(REG_LOG_MODULE, REG_LOG_PROC, "JSON 解析失败！");
    return -1;
  }

  // 用户
  if (!doc.HasMember("userName") || !doc["userName"].IsString())
  {
    LOG_ERROR(REG_LOG_MODULE, REG_LOG_PROC, "缺少或类型错误的字段：userName");
    return -1;
  }
  strcpy(user, doc["userName"].GetString());

  // 昵称
  if (!doc.HasMember("nickName") || !doc["nickName"].IsString())
  {
    LOG_ERROR(REG_LOG_MODULE, REG_LOG_PROC, "缺少或类型错误的字段：nickName");
    return -1;
  }
  strcpy(nick_name, doc["nickName"].GetString());

  // 密码
  if (!doc.HasMember("firstPwd") || !doc["firstPwd"].IsString())
  {
    LOG_ERROR(REG_LOG_MODULE, REG_LOG_PROC, "缺少或类型错误的字段：firstPwd");
    return -1;
  }
  strcpy(pwd, doc["firstPwd"].GetString());

  // 邮箱
  if (!doc.HasMember("email") || !doc["email"].IsString())
  {
    LOG_ERROR(REG_LOG_MODULE, REG_LOG_PROC, "缺少或类型错误的字段：email");
    return -1;
  }
  strcpy(email, doc["email"].GetString());

  return 0;
}

// 获取数据库信息
int get_mysql_info(string &user, string &pwd, string &db)
{
  ifstream ifs("../conf/cfg.json");
  if (!ifs.is_open())
  {
    LOG_ERROR(REG_LOG_MODULE, REG_LOG_PROC, "Failed to open cfg.json");
    return -1;
  }

  IStreamWrapper isw(ifs); // 将文件流包装为流输入
  Document doc;
  doc.ParseStream(isw);

  if (!doc.HasMember("mysql"))
  {
    LOG_ERROR(REG_LOG_MODULE, REG_LOG_PROC, "Failed to find mysql section in cfg.json");
    return -2;
  }

  const Value &mysql = doc["mysql"];
  if (!mysql.HasMember("ip") || !mysql.HasMember("port") || !mysql.HasMember("database") ||
      !mysql.HasMember("user") || !mysql.HasMember("password"))
  {
    LOG_ERROR(REG_LOG_MODULE, REG_LOG_PROC, "Failed to find required mysql parameters in cfg.json");
    return -3;
  }

  user = mysql["user"].GetString();
  pwd = mysql["password"].GetString();
  db = mysql["database"].GetString();

  return 0;
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
      LOG_ERROR(REG_LOG_MODULE, REG_LOG_PROC, "mysql_query error! sql_cmd=%s", sql_cmd);
      ret = -1;
      break;
    }

    res_set = mysql_store_result(conn); // 生成结果集
    if (res_set == nullptr)
    {
      LOG_ERROR(REG_LOG_MODULE, REG_LOG_PROC, "mysql_store_result error!");
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

// 连接数据库
MYSQL *mysql_conn()
{
  string mysql_user;
  string mysql_pwd;
  string mysql_db;
  int ret = get_mysql_info(mysql_user, mysql_pwd, mysql_db); // 读取配置文件，获得数据库的用户名、密码、数据库名
  if (ret != 0)
  {
    LOG_DEBUG(REG_LOG_MODULE, REG_LOG_PROC, "get_mysql_info failed!");
    return nullptr;
  }
  LOG_INFO(REG_LOG_MODULE, REG_LOG_PROC, "mysql_user = %s, mysql_pwd = %s, mysql_db = %s", mysql_user.c_str(),
           mysql_pwd.c_str(), mysql_db.c_str());
  MYSQL *conn = mysql_init(NULL);
  if (conn == NULL)
  {
    return nullptr;
  }

  if (mysql_real_connect(conn, "localhost", mysql_user.c_str(), mysql_pwd.c_str(), mysql_db.c_str(), 0, NULL, 0) ==
      NULL)
  {
    LOG_ERROR(REG_LOG_MODULE, REG_LOG_PROC, "mysql_real_connect error! user=%s, pwd=%s, db=%s", mysql_user.c_str(),
              mysql_pwd.c_str(), mysql_db.c_str());
    mysql_close(conn);
    return nullptr;
  }

  return conn;
}

/**
 * @brief 注册用户
 * @param conn 数据库连接
 * @param reg_buf 注册信息
 * @return 0成功，-1失败， -2用户名已存在
 */
int user_register(MYSQL *conn, const char *reg_buf)
{
  int ret = 0;

  // 获取注册用户的信息
  char user[128];
  char nick_name[128];
  char pwd[128];
  char tel[128] = {0};
  char email[128];
  ret = get_reg_info(reg_buf, user, nick_name, pwd, email);
  if (ret != 0)
  {
    return ret;
  }
  LOG_INFO(REG_LOG_MODULE, REG_LOG_PROC, "user = %s, nick_name = %s, pwd = %s,email = %s", user, nick_name, pwd,
           email);

  char sql_cmd[SQL_MAX_LEN] = {0};
  sprintf(sql_cmd, "select * from user where name = '%s'", user);

  if (conn == NULL)
  {
    LOG_ERROR(REG_LOG_MODULE, REG_LOG_PROC, "no mysql connection");
    return -1;
  }

  // 查看该用户是否存在
  int ret2 = process_result_one(conn, sql_cmd, NULL);
  if (ret2 == 2)
  {
    // 如果存在
    LOG_INFO(REG_LOG_MODULE, REG_LOG_PROC, "用户%s已存在", user);
    return -2;
  }

  // 当前时间戳
  auto now = chrono::system_clock::now();
  auto t = chrono::system_clock::to_time_t(now);
  char time_str[128];
  strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&t));

  // sql 语句，插入注册信息
  sprintf(sql_cmd,
          "insert into user (name, nickname, password, phone, createtime, email) values ('%s', '%s', '%s', '%s', '%s', '%s')",
          user, nick_name, pwd, tel, time_str, email);

  if (mysql_query(conn, sql_cmd) != 0)
  {
    LOG_ERROR(REG_LOG_MODULE, REG_LOG_PROC, "插入失败：%s", mysql_error(conn));
    return -1;
  }

  return 0;
}

// 将状态码转换为json格式的字符串
char *return_status(const char *status_num)
{
  Document doc;
  doc.SetObject();
  Document::AllocatorType &allocator = doc.GetAllocator();

  doc.AddMember("code", Value(status_num, allocator).Move(), allocator);

  StringBuffer buffer;
  Writer<StringBuffer> writer(buffer);
  doc.Accept(writer);

  char *out = new char[buffer.GetSize()];
  memcpy(out, buffer.GetString(), buffer.GetSize());

  return out;
}

int main()
{
  FCGX_Init();
  FCGX_Request request;
  FCGX_InitRequest(&request, 0, 0);

  // 建立一个数据库连接,避免每次注册都要建立连接
  MYSQL *conn = NULL;
  conn = mysql_conn();
  if (conn == NULL)
  {
    LOG_DEBUG(REG_LOG_MODULE, REG_LOG_PROC, "mysql_conn failed!");
    return -1;
  }
  // 设置数据库编码，主要处理中文编码问题
  if (mysql_query(conn, "set names utf8") != 0)
  {
    LOG_DEBUG(REG_LOG_MODULE, REG_LOG_PROC, "set names utf8 failed!");
    mysql_close(conn);
    return -1;
  }

  while (FCGX_Accept_r(&request) == 0)
  {
    char *contentLength = FCGX_GetParam("CONTENT_LENGTH", request.envp);
    int len;

    FCGX_FPrintF(request.out, "Content-type: text/html\r\n\r\n"); // 告诉web服务器，返回的数据类型是html

    if (contentLength == nullptr)
    {
      len = 0;
      FCGX_FPrintF(request.out, "No data from standard input.<p>\n"); // 这是标准输出，会返回给web服务器
      LOG_INFO(REG_LOG_MODULE, REG_LOG_PROC, "len = %d", len);
    }
    else
    {
      len = atoi(contentLength); // 字符串转整型
      char buf[4 * 1024] = {0};
      int ret = 0;
      char *out = nullptr;

      ret = FCGX_GetStr(buf, len, request.in); // 从标准输入(web服务器)读取内容
      if (ret == 0)
      {
        LOG_INFO(REG_LOG_MODULE, REG_LOG_PROC, "FCGX_GetStr() err");
        continue;
      }
      LOG_INFO(REG_LOG_MODULE, REG_LOG_PROC, "buf = %s", buf);

      // 注册用户，成功返回0，失败返回-1, 该用户已存在返回-2
      /*
      注册：
      成功：{"code":"002"}
      该用户已存在：{"code":"003"}
      失败：{"code":"004"}
      */
      ret = user_register(conn, buf);
      if (ret == 0)
      {
        // 注册成功
        out = return_status("002"); // 以json格式的字符串返回
      }
      else if (ret == -1)
      {
        // 注册失败
        out = return_status("004");
      }
      else if (ret == -2)
      {
        // 用户已存在
        out = return_status("003");
      }

      if (out != nullptr)
      {
        FCGX_FPrintF(request.out, "%s", out); // 返回给web服务器
        LOG_INFO(REG_LOG_MODULE, REG_LOG_PROC, "out = %s", out);
        free(out); // 释放内存
      }
    }

    FCGX_Finish_r(&request);
  }

  mysql_close(conn);
  return 0;
}
