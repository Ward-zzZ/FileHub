/**
 * @file reg_cgi.cpp
 * @brief  处理注册的CGI程序
 * @author ward
 * @version 2.0
 * @date 2023年5月1日
 */
#include <mysql/mysql.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>

#include "cgi_util.h"
#include "fcgi_config.h"
#include "fcgi_stdio.h"
#include "make_log.h"
#include "mysql_util.h"
#include "rapidjson/document.h"
#include "rapidjson/istreamwrapper.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

const char *const REG_LOG_MODULE = "cgi";
const char *const REG_LOG_PROC = "reg";
#define SQL_MAX_LEN 1024

using namespace std;
using namespace rapidjson;

// 解析json包，获得包括用户名、昵称、密码、邮箱
int getRegInfo(const char *reg_buf, char *user, char *nick_name, char *pwd,
               char *email) {
  // 解析json包
  Document doc;
  doc.Parse(reg_buf);

  if (!doc.IsObject()) {
    LOG_ERROR(REG_LOG_MODULE, REG_LOG_PROC, "JSON 解析失败！");
    return -1;
  }

  // 用户
  if (!doc.HasMember("userName") || !doc["userName"].IsString()) {
    LOG_ERROR(REG_LOG_MODULE, REG_LOG_PROC, "缺少或类型错误的字段：userName");
    return -1;
  }
  strcpy(user, doc["userName"].GetString());

  // 昵称
  if (!doc.HasMember("nickName") || !doc["nickName"].IsString()) {
    LOG_ERROR(REG_LOG_MODULE, REG_LOG_PROC, "缺少或类型错误的字段：nickName");
    return -1;
  }
  strcpy(nick_name, doc["nickName"].GetString());

  // 密码
  if (!doc.HasMember("firstPwd") || !doc["firstPwd"].IsString()) {
    LOG_ERROR(REG_LOG_MODULE, REG_LOG_PROC, "缺少或类型错误的字段：firstPwd");
    return -1;
  }
  strcpy(pwd, doc["firstPwd"].GetString());

  // 邮箱
  if (!doc.HasMember("email") || !doc["email"].IsString()) {
    LOG_ERROR(REG_LOG_MODULE, REG_LOG_PROC, "缺少或类型错误的字段：email");
    return -1;
  }
  strcpy(email, doc["email"].GetString());

  return 0;
}

/**
 * @brief 注册用户
 * @param conn 数据库连接
 * @param reg_buf 注册信息
 * @return 0成功，-1失败， -2用户名已存在
 */
int userRegister(MYSQL *conn, const char *reg_buf) {
  int ret = 0;

  // 获取注册用户的信息
  char user[128];
  char nick_name[128];
  char pwd[128];
  char tel[128] = {0};
  char email[128];
  ret = getRegInfo(reg_buf, user, nick_name, pwd, email);
  if (ret != 0) {
    return ret;
  }
  LOG_INFO(REG_LOG_MODULE, REG_LOG_PROC,
           "user = %s, nick_name = %s, pwd = %s,email = %s", user, nick_name,
           pwd, email);

  char sql_cmd[SQL_MAX_LEN] = {0};
  sprintf(sql_cmd, "select * from user where name = '%s'", user);

  if (conn == nullptr) {
    LOG_ERROR(REG_LOG_MODULE, REG_LOG_PROC, "no mysql connection");
    return -1;
  }

  // 查看该用户是否存在
  int ret2 = processResultOne(conn, sql_cmd, nullptr);
  if (ret2 == 2) {
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
          "insert into user (name, nickname, password, phone, createtime, "
          "email) values ('%s', '%s', '%s', '%s', '%s', '%s')",
          user, nick_name, pwd, tel, time_str, email);

  if (mysql_query(conn, sql_cmd) != 0) {
    LOG_ERROR(REG_LOG_MODULE, REG_LOG_PROC, "插入失败：%s", mysql_error(conn));
    return -1;
  }

  return 0;
}

int main() {
  FCGX_Init();
  FCGX_Request request;
  FCGX_InitRequest(&request, 0, 0);

  // 建立一个数据库连接,避免每次注册都要建立连接
  MYSQL *conn = mysqlConn();
  if (conn == nullptr) {
    LOG_DEBUG(REG_LOG_MODULE, REG_LOG_PROC, "mysqlConn failed!");
    return -1;
  }
  // 设置数据库编码，主要处理中文编码问题
  if (mysql_query(conn, "set names utf8") != 0) {
    LOG_DEBUG(REG_LOG_MODULE, REG_LOG_PROC, "set names utf8 failed!");
    mysql_close(conn);
    return -1;
  }

  while (FCGX_Accept_r(&request) == 0) {
    char *contentLength = FCGX_GetParam("CONTENT_LENGTH", request.envp);
    int len;

    FCGX_FPrintF(
        request.out,
        "Content-type: text/html\r\n\r\n");  // 告诉web服务器，返回的数据类型是html

    if (contentLength == nullptr) {
      len = 0;
      FCGX_FPrintF(
          request.out,
          "No data from standard input.<p>\n");  // 这是标准输出，会返回给web服务器
      LOG_INFO(REG_LOG_MODULE, REG_LOG_PROC, "len = %d", len);
    } else {
      len = atoi(contentLength);
      char buf[4 * 1024] = {0};
      int ret = 0;
      char *out = nullptr;

      ret =
          FCGX_GetStr(buf, len, request.in);  // 从标准输入(web服务器)读取请求体
      if (ret == 0) {
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
      ret = userRegister(conn, buf);
      if (ret == 0) {
        out = returnStatus("002");  // 以json格式的字符串返回
      } else if (ret == -1) {
        out = returnStatus("004");
      } else if (ret == -2) {
        out = returnStatus("003");
      }

      if (out != nullptr) {
        FCGX_FPrintF(request.out, "%s", out);  // 返回给web服务器
        LOG_INFO(REG_LOG_MODULE, REG_LOG_PROC, "out = %s", out);
        free(out);  // 释放内存
      }
    }

    FCGX_Finish_r(&request);
  }

  mysql_close(conn);
  return 0;
}
