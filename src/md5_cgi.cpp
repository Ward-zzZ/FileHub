/**
 * @file md5_cgi.cpp
 * @brief 查询MD5值
 * @version 1.0
 * @date 2023-04-03
 * @author ward
 */
#include "fcgi_config.h"
#include "fcgi_stdio.h"
#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <mysql/mysql.h>
#include <sw/redis++/redis++.h>
#include "make_log.h"
#include "cgi_util.h"
#include "mysql_util.h"
#include <sys/time.h>
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

using namespace rapidjson;
using namespace std;
using namespace sw::redis;

#define MD5_LOG_MODULE "cgi"
#define MD5_LOG_PROC "md5"

thread_local FCGX_Request
    request; // 定义线程局部变量

/**
 * @brief 从客户端请求中获取用户信息
 *
 * @param buf 客户端请求数据
 * @param user 用户名
 * @param token token
 * @param md5 md5值
 * @param filename 文件名
 *
 * @return int 0成功，-1失败
 */
int get_md5_info(char *buf, char *user, char *token, char *md5, char *filename)
{
  // # url
  // http://127.0.0.1:80/md5
  // # post数据格式
  // {
  // user:xxxx,
  // token:xxxx,
  // md5:xxx,
  // fileName: xxx
  // }
  Document doc;
  doc.Parse(buf);
  if (!doc.IsObject())
  {
    LOG_ERROR(MD5_LOG_MODULE, MD5_LOG_PROC, "JSON 解析失败！");
    return -1;
  }

  if (!doc.HasMember("user") || !doc["user"].IsString())
  {
    LOG_ERROR(MD5_LOG_MODULE, MD5_LOG_PROC, "缺少或类型错误的字段:userName");
    return -1;
  }
  strcpy(user, doc["user"].GetString());

  if (!doc.HasMember("token") || !doc["token"].IsString())
  {
    LOG_ERROR(MD5_LOG_MODULE, MD5_LOG_PROC, "缺少或类型错误的字段:token");
    return -1;
  }
  strcpy(token, doc["token"].GetString());

  if (!doc.HasMember("md5") || !doc["md5"].IsString())
  {
    LOG_ERROR(MD5_LOG_MODULE, MD5_LOG_PROC, "缺少或类型错误的字段:md5");
    return -1;
  }
  strcpy(md5, doc["md5"].GetString());

  if (!doc.HasMember("filename") || !doc["filename"].IsString())
  {
    LOG_ERROR(MD5_LOG_MODULE, MD5_LOG_PROC, "缺少或类型错误的字段：filename");
    return -1;
  }
  strcpy(filename, doc["filename"].GetString());

  return 0;
}

/**
 * @brief 秒传处理
 *
 * @param conn 数据库连接
 * @param user 用户名
 * @param md5 md5值
 * @param filename 文件名
 *
 * @return int 0秒传成功{"code":"006"}，-1出错{"code":"007"}，-2此用户已拥有此文件{"code":"005"}， -3秒传失败{"code":"007"}
 */
int deal_md5(MYSQL *conn, char *user, char *md5, char *filename)
{
  // 查看数据库是否有此文件的md5
  // 如果没有，返回 {"code":"006"}， 代表不能秒传
  // 如果有
  // 1、修改file_info中的count字段，+1 （count 文件引用计数）
  //    update file_info set count = 2 where md5 = "bae488ee63cef72efb6a3f1f311b3743";
  // 2、user_file_list插入一条数据

  int ret = 0;
  int ret2 = 0;
  char tmp[512] = {0};
  char sql_cmd[SQL_MAX_LEN] = {0};
  char *out = NULL;

  // sql 语句，获取此md5值文件的文件计数器 count
  sprintf(sql_cmd, "select count from file_info where md5 = '%s'", md5);

  // 返回值： 0成功并保存记录集，1没有记录集，2有记录集但是没有保存，-1失败
  ret2 = process_result_one(conn, sql_cmd, tmp); // 执行sql语句
  if (ret2 == 0)                                 // 有结果，说明服务器上已经有此文件
  {
    int count = atoi(tmp); // 字符串转整型，文件计数器
    // 查看此用户是否已经有此文件，如果存在说明此文件已上传，无需再上传
    sprintf(sql_cmd, "select * from user_file_list where user = '%s' and md5 = '%s' and filename = '%s'", user, md5, filename);

    ret2 = process_result_one(conn, sql_cmd, NULL); // 执行sql语句，最后一个参数为NULL，只做查询
    if (ret2 == 2)                                  // 如果有结果，说明此用户已经保存此文件
    {
      LOG_INFO(MD5_LOG_MODULE, MD5_LOG_PROC, "%s[filename:%s, md5:%s]已存在\n", user, filename, md5);
      return_status("005");
      return -2; //-2此用户已拥有此文件
    }

    // 1、修改file_info中的count字段，+1 （count 文件引用计数）
    sprintf(sql_cmd, "update file_info set count = %d where md5 = '%s'", ++count, md5); // 前置++
    if (mysql_query(conn, sql_cmd) != 0)
    {
      LOG_ERROR(MD5_LOG_MODULE, MD5_LOG_PROC, "%s 操作失败： %s\n", sql_cmd, mysql_error(conn));
      return_status("007");
      return -1;
    }

    // 2、user_file_list, 用户文件列表插入一条数据
    struct timeval tv;
    struct tm *ptm;
    char time_str[128];

    gettimeofday(&tv, NULL);
    ptm = localtime(&tv.tv_sec);
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", ptm);

    // sql语句，插入用户文件列表
    sprintf(sql_cmd, "insert into user_file_list(user, md5, createtime, filename, shared_status, pv) values ('%s', '%s', '%s', '%s', %d, %d)", user, md5, time_str, filename, 0, 0);
    if (mysql_query(conn, sql_cmd) != 0)
    {
      LOG_ERROR(MD5_LOG_MODULE, MD5_LOG_PROC, "%s 操作失败： %s\n", sql_cmd, mysql_error(conn));
      return_status("007");
      return -1;
    }

    // 查询用户文件数量
    sprintf(sql_cmd, "select count from user_file_count where user = '%s'", user);
    count = 0;

    ret2 = process_result_one(conn, sql_cmd, tmp); // 指向sql语句
    if (ret2 == 1)                                 // 没有记录
    {
      // 用户之前没有上传过文件，插入一条数据
      sprintf(sql_cmd, " insert into user_file_count (user, count) values('%s', %d)", user, 1);
    }
    else if (ret2 == 0)
    {
      // 用户之前上传过文件，更新count字段
      count = atoi(tmp);
      sprintf(sql_cmd, "update user_file_count set count = %d where user = '%s'", count + 1, user);
    }

    if (mysql_query(conn, sql_cmd) != 0)
    {
      LOG_ERROR(MD5_LOG_MODULE, MD5_LOG_PROC, "%s 操作失败： %s\n", sql_cmd, mysql_error(conn));
      return_status("007");
      return -1;
    }
  }
  // 没有结果，秒传失败，需要上传文件
  else if (1 == ret2)
  {
    LOG_ERROR(MD5_LOG_MODULE, MD5_LOG_PROC, "秒传失败，需要上传文件\n");
    return_status("007");
    return -3;
  }
  // 查询文件md5值失败
  else
  {
    LOG_ERROR(MD5_LOG_MODULE, MD5_LOG_PROC, "查询文件md5值失败\n");
    return_status("007");
    return -1;
  }
  // 秒传成功
  LOG_INFO(MD5_LOG_MODULE, MD5_LOG_PROC, "秒传成功\n");
  return_status("006");
  return 0;
}

/**
 * @brief 向客户端返回状态码
 * @param status_num 状态码
 */
void return_status(const char *status_num)
{
  Document doc;
  doc.SetObject();
  Document::AllocatorType &allocator = doc.GetAllocator();

  doc.AddMember("code", Value(status_num, allocator).Move(), allocator);

  StringBuffer buffer;
  Writer<StringBuffer> writer(buffer);
  doc.Accept(writer);

  char *out = strdup(buffer.GetString()); // 动态分配内存,strdup用于复制字符串
  if (out != nullptr)
  {
    FCGX_FPrintF(request.out, out);
    free(out);
  }
}

int main()
{

  FCGX_Init();  // 初始化 FastCGI 环境
  request = {}; // 在主线程中初始化线程局部变量
  FCGX_InitRequest(&request, 0, 0);

  // 使用redis-plus-plus库提供的函数连接redis数据库，返回一个Redis对象
  Redis *redisconn = redis_conn();

  // 建立一个数据库连接,避免每次注册都要建立连接
  MYSQL *mysqlconn = NULL;
  mysqlconn = mysql_conn();
  if (mysqlconn == nullptr || redisconn == nullptr)
  {
    LOG_ERROR(MD5_LOG_MODULE, MD5_LOG_PROC, "mysql_conn or redis_conn failed!");
    return -1;
  }

  while (FCGX_Accept_r(&request) == 0)
  {
    const char *contentLength = FCGX_GetParam("CONTENT_LENGTH", request.envp);
    int len = (contentLength == nullptr) ? 0 : atoi(contentLength);

    FCGX_FPrintF(request.out, "Content-type: text/html\r\n\r\n");

    if (len <= 0)
    {
      FCGX_FPrintF(request.out, "No data from standard input.<p>\n");
      LOG_WARNING(MD5_LOG_MODULE, MD5_LOG_PROC, "len = 0, No data from standard input\n");
    }
    else
    {
      char buf[4 * 1024] = {0};
      int ret = FCGX_GetStr(buf, len, request.in); // 从标准输入(web服务器)读取内容
      if (ret == 0)
      {
        LOG_ERROR(MD5_LOG_MODULE, MD5_LOG_PROC, "FCGX_GetStr(file_buf, len, request.in) err\n");
        continue;
      }

      LOG_DEBUG(MD5_LOG_MODULE, MD5_LOG_PROC, "buf = %s\n", buf);

      char user[USER_NAME_LEN] = {0};
      char md5[256] = {0};
      char token[TOKEN_LEN] = {0};
      char filename[128] = {0};
      ret = get_md5_info(buf, user, token, md5, filename); // 解析json中信息
      if (ret != 0)
      {
        LOG_ERROR(MD5_LOG_MODULE, MD5_LOG_PROC, "get_md5_info(buf, user, token, md5, filename) err\n");
        continue;
      }
      LOG_INFO(MD5_LOG_MODULE, MD5_LOG_PROC, "user = %s, token = %s, md5 = %s, filename = %s\n", user, token, md5, filename);

      // 验证token
      if (validate_token(redisconn, user, token))
      {
        deal_md5(mysqlconn, user, md5, filename); // 秒传处理
      }
      else
      {
        LOG_ERROR(MD5_LOG_MODULE, MD5_LOG_PROC, "token验证失败\n");
        // token验证失败，返回错误码'111'
        return_status("111");
      }
    }

    FCGX_Finish_r(&request);
  }

  return 0;
}
