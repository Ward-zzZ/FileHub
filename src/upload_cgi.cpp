/**
 * @file upload_cgi.cpp
 * @brief 处理上传文件的cgi程序
 * @version 1.0
 * @date 2023-04-01
 * @author ward
 */
/**
 * @file myfiles_cgi.cpp
 * @brief 从数据库中获取用户文件数量和详细信息
 * @version 1.0
 * @date 2023-04-01
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
#include "fdfs_api.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>

using namespace rapidjson;
using namespace std;
using namespace sw::redis;

#define UPLOAD_LOG_MODULE "cgi"
#define UPLOAD_LOG_PROC "upload"

thread_local FCGX_Request
    request; // 定义线程局部变量

/**
 * @brief 从web服务器接收文件
 *
 * @param len 文件长度
 * @param user 用户名
 * @param filename 文件名
 * @param md5 文件md5
 * @param p_size 文件大小
 *
 * @return 0为成功，-1为失败
 */
int recv_save_file(long len, char *user, char *filename, char *md5, long *p_size)
{
  int ret = 0;
  char *file_buf = nullptr;
  char *begin = nullptr;
  char *p = nullptr, *q = nullptr, *k = nullptr;

  //==========> 开辟存放文件的 内存 <================
  file_buf = new char[len]{};
  if (!file_buf)
  {
    LOG_ERROR(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "new file_buf error!");
    return -1;
  }
  int ret2 = FCGX_GetStr(file_buf, len, request.in); // 从标准输入(web服务器)读取内容
  if (ret2 == 0)
  {
    LOG_ERROR(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "FCGX_GetStr(file_buf, len, request.in) err\n");
    delete[] file_buf;
    return -1;
  }

  //===========> 开始处理前端发送过来的post数据格式 <============
  /*
  ------WebKitFormBoundary88asdgewtgewx\r\n
  Content-Disposition: form-data; user="mike"; filename="xxx.jpg"; md5="xxxx"; size=10240\r\n
  Content-Type: application/octet-stream\r\n
  \r\n
  真正的文件内容\r\n
  ------WebKitFormBoundary88asdgewtgewx
  */
  char content_text[TEMP_BUF_MAX_LEN] = {0}; // 文件头部信息
  char boundary[TEMP_BUF_MAX_LEN] = {0};     // 分界线信息
  begin = file_buf;
  p = begin;

  // 1、找到第一个换行符,得到分界线, ------WebKitFormBoundary88asdgewtg
  p = strstr(begin, "\r\n"); // strstr函数返回子字符串在字符串中首次出现的位置
  if (p == NULL)
  {
    LOG_ERROR(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "Wrong no boundary!\n");
    delete[] file_buf;
    return -1;
  }
  // 拷贝分界线
  strncpy(boundary, begin, p - begin);
  boundary[p - begin] = '\0'; // 字符串结束符
  LOG_DEBUG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "boundary: [%s]\n", boundary);
  p += 2;             // 跳过\r\n
  len -= (p - begin); // 已经处理了p-begin的长度

  // 2、得到文件头部信息, Content-Disposition: form-data; user="mike"; filename="xxx.jpg"; md5="xxxx"; size=10240
  begin = p;
  // 在处理文本文件时，添加 "\r" 和 "\n" 的组合 "\r\n" 可以让文本文件在不同的平台上都能正常的显示和处理换行符。
  // 在 Windows 平台中，每行末尾都加上了回车符 "\r" 和 换行 "\n"， 因此 Windows 所使用的文本文件的行末尾符号是 "\r\n"；
  // 而在 Unix/Linux 平台上，每行末尾只加上换行符 "\n"，所以所使用的文本文件的行末尾符号是 "\n"。
  p = strstr(begin, "\r\n"); //\r\n是行结束符
  if (p == NULL)
  {
    LOG_ERROR(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "ERROR: get context text error, no filename?\n");
    delete[] file_buf;
    return -1;
  }
  strncpy(content_text, begin, p - begin);
  boundary[p - begin] = '\0'; // 字符串结束符
  LOG_DEBUG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "content_text: [%s]\n", content_text);
  p += 2;             // 跳过\r\n
  len -= (p - begin); // 已经处理了p-begin的长度

  // 3、获得文件的相关信息
  //========================================获取文件上传者
  // Content-Disposition: form-data; user="mike"; filename="xxx.jpg"; md5="xxxx"; size=10240\r\n
  //                                 ↑
  q = begin;
  q = strstr(begin, "user=");

  // Content-Disposition: form-data; user="mike"; filename="xxx.jpg"; md5="xxxx"; size=10240\r\n
  //                                       ↑
  q += strlen("user=");
  q++; // 跳过第一个"

  // Content-Disposition: form-data; user="mike"; filename="xxx.jpg"; md5="xxxx"; size=10240\r\n
  //                                           ↑
  k = strchr(q, '"');      // strchr函数返回字符串中第一次出现字符的位置
  strncpy(user, q, k - q); // 拷贝用户名
  user[k - q] = '\0';
  // 去掉一个字符串两边的空白字符
  trim_space(user);

  //========================================获取文件名字
  //"; filename="xxx.jpg"; md5="xxxx"; size=10240\r\n
  //   ↑
  begin = k;
  q = begin;
  q = strstr(begin, "filename=");

  //"; filename="xxx.jpg"; md5="xxxx"; size=10240\r\n
  //             ↑
  q += strlen("filename=");
  q++; // 跳过第一个"

  //"; filename="xxx.jpg"; md5="xxxx"; size=10240\r\n
  //                    ↑
  k = strchr(q, '"');
  strncpy(filename, q, k - q); // 拷贝文件名
  filename[k - q] = '\0';
  trim_space(filename);

  //========================================获取文件MD5码
  //"; md5="xxxx"; size=10240\r\n
  //   ↑
  begin = k;
  q = begin;
  q = strstr(begin, "md5=");

  //"; md5="xxxx"; size=10240\r\n
  //        ↑
  q += strlen("md5=");
  q++; // 跳过第一个"

  //"; md5="xxxx"; size=10240\r\n
  //            ↑
  k = strchr(q, '"');
  strncpy(md5, q, k - q); // 拷贝文件名
  md5[k - q] = '\0';
  trim_space(md5);

  //========================================获取文件大小
  //"; size=10240\r\n
  //   ↑
  begin = k;
  q = begin;
  q = strstr(begin, "size=");

  //"; size=10240\r\n
  //        ↑
  q += strlen("size=");

  //"; size=10240\r\n
  //             ↑
  k = strstr(q, "\r\n");
  char tmp[256] = {0};
  strncpy(tmp, q, k - q); // 内容
  tmp[k - q] = '\0';
  *p_size = strtol(tmp, NULL, 10); // 字符串转long

  begin = p;
  p = strstr(begin, "\r\n"); // 这里的\r\n是Content-Type: application/octet-stream\r\n
  p += 4;                    //\r\n\r\n
  len -= (p - begin);

  // 4、文件的真正内容
  begin = p;
  p = memstr(begin, len, boundary); // util_cgi.h， 找文件结尾
  if (p == NULL)
  {
    LOG_ERROR(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "memstr(begin, len, boundary) error\n");
    delete[] content_text;
    delete[] boundary;
    delete[] file_buf;
    return -1;
  }
  else
  {
    p = p - 2; //\r\n
  }

  //=====> 此时begin-->p两个指针的区间就是post的文件二进制数据
  //======>将数据写入文件中,其中文件名也是从post数据解析得来  <===========

  int fd = 0;
  fd = open(filename, O_CREAT | O_WRONLY, 0644);
  if (fd < 0)
  {
    LOG_ERROR(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "open %s error\n", filename);
    delete[] content_text;
    delete[] boundary;
    delete[] file_buf;
    return -1;
  }

  // ftruncate会将参数fd指定的文件大小改为参数length指定的大小
  ftruncate(fd, (p - begin));
  write(fd, begin, (p - begin));
  close(fd);

  delete[] content_text;
  delete[] boundary;
  delete[] file_buf;
  return ret;
}

/**
 * @brief 上传本地接收的文件到分布式存储
 * @param filename 文件名
 * @param fileid 文件id
 * @return 0 成功，-1 失败
 */
int upload_to_dstorage(char *filename, char *fileid)
{
  // 读取fdfs client 配置文件的路径
  string fdfs_cli_conf_path = "";
  get_cfg_value(CFG_PATH, "dfs_path", "client", fdfs_cli_conf_path);
  int res = fdfs_upload_file(fdfs_cli_conf_path.c_str(), filename, fileid);
  // 去掉一个字符串两边的空白字符
  trim_space(fileid);
  return res;
}

/**
 * @brief  封装文件存储在分布式系统中的 完整 url
 *
 * @param fileid        (in)    文件分布式id路径
 * @param fdfs_file_url (out)   文件的完整url地址
 *
 * @returns 0 成功，-1 失败
 */
int make_file_url(char *fileid, char *fdfs_file_url)
{
  int ret = 0;

  char *p = NULL;
  char *q = NULL;
  char *k = NULL;

  char fdfs_file_stat_buf[TEMP_BUF_MAX_LEN] = {0};
  char fdfs_file_host_name[HOST_NAME_LEN] = {0}; // storage所在服务器ip地址

  pid_t pid;
  int fd[2];

  // 无名管道的创建
  if (pipe(fd) < 0)
  {
    LOG_ERROR(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "pip error\n");
    return -1;
  }

  // 创建进程
  pid = fork();
  if (pid < 0) // 进程创建失败
  {
    LOG_ERROR(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "fork error\n");
    return -1;
  }

  if (pid == 0) // 子进程
  {
    // 关闭读端
    close(fd[0]);

    // 将标准输出 重定向 写管道
    dup2(fd[1], STDOUT_FILENO); // dup2(fd[1], 1);

    // 读取fdfs client 配置文件的路径
    string fdfs_cli_conf_path = "";
    get_cfg_value(CFG_PATH, "dfs_path", "client", fdfs_cli_conf_path);

    execlp("fdfs_file_info", "fdfs_file_info", fdfs_cli_conf_path.c_str(), fileid, NULL);

    // 执行失败
    LOG_ERROR(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "execlp fdfs_file_info error\n");

    close(fd[1]);
  }
  else // 父进程
  {
    // 关闭写端
    close(fd[1]);

    // 从管道中去读数据
    read(fd[0], fdfs_file_stat_buf, TEMP_BUF_MAX_LEN);
    LOG_INFO(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "get file_ip [%s] succ\n", fdfs_file_stat_buf);

    wait(NULL); // 等待子进程结束，回收其资源
    close(fd[0]);

    // 拼接上传文件的完整url地址--->http://host_name/group1/M00/00/00/D12313123232312.png
    p = strstr(fdfs_file_stat_buf, "source ip address: ");

    q = p + strlen("source ip address: ");
    k = strstr(q, "\n");

    strncpy(fdfs_file_host_name, q, k - q);
    fdfs_file_host_name[k - q] = '\0';

    // printf("host_name:[%s]\n", fdfs_file_host_name);

    // 读取storage_web_server服务器的端口
    string storage_web_server_port = "";
    get_cfg_value(CFG_PATH, "storage_web_server", "port", storage_web_server_port);
    strcat(fdfs_file_url, "http://");
    strcat(fdfs_file_url, fdfs_file_host_name);
    strcat(fdfs_file_url, ":");
    strcat(fdfs_file_url, storage_web_server_port.c_str());
    strcat(fdfs_file_url, "/");
    strcat(fdfs_file_url, fileid);

    // printf("[%s]\n", fdfs_file_url);
    LOG_INFO(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "file url is: %s\n", fdfs_file_url);
  }

  return ret;
}

int store_fileinfo_to_mysql(MYSQL *conn, char *user, char *filename, char *md5, long size, char *fileid,
                            char *fdfs_file_url)
{
  time_t now;
  char create_time[TIME_STRING_LEN];
  char suffix[SUFFIX_LEN];
  char sql_cmd[SQL_MAX_LEN] = {0};

  // 得到文件后缀字符串 如果非法文件后缀,返回"null"
  get_file_suffix(filename, suffix); // mp4, jpg, png

  // sql 语句
  /*
     -- =============================================== 文件信息表
     -- md5 文件md5
     -- file_id 文件id
     -- url 文件url
     -- size 文件大小, 以字节为单位
     -- type 文件类型： png, zip, mp4……
     -- count 文件引用计数， 默认为1， 每增加一个用户拥有此文件，此计数器+1
     */
  sprintf(sql_cmd,
          "insert into file_info (md5, file_id, url, size, type, count) values ('%s', '%s', '%s', '%ld', '%s', %d)",
          md5, fileid, fdfs_file_url, size, suffix, 1);

  if (mysql_query(conn, sql_cmd) != 0) // 执行sql语句
  {
    LOG_ERROR(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "%s 插入失败: %s\n", sql_cmd, mysql_error(conn));
    return -1;
  }

  LOG_INFO(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "%s 文件信息插入成功\n", sql_cmd);

  // 获取当前时间
  now = time(NULL);
  strftime(create_time, TIME_STRING_LEN - 1, "%Y-%m-%d %H:%M:%S", localtime(&now));

  /*
     -- =============================================== 用户文件列表
     -- user 文件所属用户
     -- md5 文件md5
     -- createtime 文件创建时间
     -- filename 文件名字
     -- shared_status 共享状态, 0为没有共享， 1为共享
     -- pv 文件下载量，默认值为0，下载一次加1
     */
  // sql语句
  sprintf(sql_cmd,
          "insert into user_file_list(user, md5, createtime, filename, shared_status, pv) values ('%s', '%s', '%s', '%s', %d, %d)",
          user, md5, create_time, filename, 0, 0);
  if (mysql_query(conn, sql_cmd) != 0)
  {
    LOG_ERROR(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "%s 操作失败: %s\n", sql_cmd, mysql_error(conn));
    return -1;
  }

  // 查询用户文件数量
  sprintf(sql_cmd, "select count from user_file_count where user = '%s'", user);
  int ret2 = 0;
  char tmp[512] = {0};
  int count = 0;
  // 返回值： 0成功并保存记录集，1没有记录集，2有记录集但是没有保存，-1失败
  ret2 = process_result_one(conn, sql_cmd, tmp); // 执行sql语句
  if (ret2 == 1)                                 // 没有记录
  {
    // 插入记录
    sprintf(sql_cmd, " insert into user_file_count (user, count) values('%s', %d)", user, 1);
  }
  else if (ret2 == 0)
  {
    // 更新用户文件数量count字段
    count = atoi(tmp);
    sprintf(sql_cmd, "update user_file_count set count = %d where user = '%s'", count + 1, user);
  }

  if (mysql_query(conn, sql_cmd) != 0)
  {
    LOG_ERROR(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "%s 操作失败: %s\n", sql_cmd, mysql_error(conn));
    return -1;
  }

  return 0;
}

/**
 * @brief 向客户端返回处理结果
 *
 * @param num 用户文件个数
 * @param token_flag 验证标志：0为验证失败，1为验证成功，-1为获取文件列表失败
 */
void return_myfiles_status(long num, int token_flag)
{
  Document doc;
  doc.SetObject();
  Document::AllocatorType &allocator = doc.GetAllocator();
  doc.AddMember("num", num, allocator);
  doc.AddMember("token", token_flag == 1 ? "110" : token_flag == -1 ? "015"
                                                                    : "111",
                allocator); // 验证成功110，失败111
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

// 向nginx返回状态码
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
    LOG_ERROR(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "mysql_conn or redis_conn failed!");
    return -1;
  }

  while (FCGX_Accept_r(&request) == 0)
  {
    int ret = 0;
    char filename[FILE_NAME_LEN] = {0};     // 文件名
    char user[USER_NAME_LEN] = {0};         // 文件上传者
    char md5[MD5_LEN] = {0};                // 文件md5码
    long size;                              // 文件大小
    char fileid[TEMP_BUF_MAX_LEN] = {0};    // 文件上传到fastDFS后的文件id
    char fdfs_file_url[FILE_URL_LEN] = {0}; // 文件所存放storage的host_name

    char cmd[20];
    char *query = FCGX_GetParam("QUERY_STRING", request.envp); // 从环境变量中获取请求参数

    query_parse_key_value(query, "cmd", cmd, nullptr);
    LOG_INFO(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "cmd = %s\n", cmd);

    const char *contentLength = FCGX_GetParam("CONTENT_LENGTH", request.envp);
    int len = (contentLength == nullptr) ? 0 : atoi(contentLength);

    FCGX_FPrintF(request.out, "Content-type: text/html\r\n\r\n"); // 写入响应头

    if (len <= 0)
    {
      FCGX_FPrintF(request.out, "No data from standard input.<p>\n");
      LOG_WARNING(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "len = 0, No data from standard input\n");
    }
    else
    {
      //===============> 得到上传文件  <============
      if (recv_save_file(len, user, filename, md5, &size) != 0)
      {
        ret = -1;
        goto END;
      }
      LOG_INFO(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "%s成功上传[%s, 大小：%ld, md5码：%s]到本地\n", user, filename,
               size, md5);

      //===============> 将该文件存入fastDFS中,并得到文件的file_id <============
      if (upload_to_dstorage(filename, fileid) < 0)
      {
        ret = -1;
        goto END;
      }

      //================> 删除本地临时存放的上传文件 <===============
      unlink(filename);

      //================> 得到文件所存放storage的host_name <=================
      if (make_file_url(fileid, fdfs_file_url) < 0)
      {
        ret = -1;
        goto END;
      }

      //===============> 将该文件的FastDFS相关信息存入mysql中 <======
      if (store_fileinfo_to_mysql(mysqlconn, user, filename, md5, size, fileid, fdfs_file_url) < 0)
      {
        ret = -1;
        goto END;
      }

    END:
      memset(filename, 0, FILE_NAME_LEN);
      memset(user, 0, USER_NAME_LEN);
      memset(md5, 0, MD5_LEN);
      memset(fileid, 0, TEMP_BUF_MAX_LEN);
      memset(fdfs_file_url, 0, FILE_URL_LEN);

      // 给前端返回，上传情况
      /*
         上传文件：
         成功：{"code":"008"}
         失败：{"code":"009"}
         */
      if (ret == 0) // 成功上传
      {
        return_status("008");
      }
      else // 上传失败
      {
        return_status("009");
      }
    }

    FCGX_Finish_r(&request);
  }

  return 0;
}
