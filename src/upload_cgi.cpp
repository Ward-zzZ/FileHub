/**
 * @file upload_cgi.cpp
 * @brief 处理上传文件的cgi程序
 * @author ward
 * @version 2.0
 * @date 2023年5月4日
 */

#include <fcntl.h>
#include <mysql/mysql.h>
#include <sw/redis++/redis++.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "cgi_util.h"
#include "fcgi_config.h"
#include "fcgi_stdio.h"
// #include "fdfs_api.h"
#include "make_log.h"
#include "mysql_util.h"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

using namespace rapidjson;
using namespace std;
using namespace sw::redis;

const char *const UPLOAD_LOG_MODULE = "cgi";
const char *const UPLOAD_LOG_PROC = "upload";
thread_local FCGX_Request request;  // 定义线程局部变量

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
int recvSaveFile(long len, char *user, char *filename, char *md5,
                 long *p_size) {
  //===========> 前端发送过来的post数据的请求体数据 <============
  /*
  ------WebKitFormBoundary88asdgewtgewx\r\n
  Content-Disposition: form-data; user="mike"; filename="xxx.jpg";
  md5="xxxx";size=10240\r\n Content-Type: application/octet-stream\r\n \r\n
  真正的文件内容\r\n
  ------WebKitFormBoundary88asdgewtgewx
  */

  string request_body(len, '\0');
  string boundary;

  // 读取请求体数据(request.in)
  if (FCGX_GetStr(request_body.data(), len, request.in) == 0) {
    LOG_ERROR(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,
              "FCGX_GetStr(file_buf, len, request.in) err\n");
    return -1;
  }
  if (request_body.empty()) {
    LOG_ERROR(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "request_body is empty!\n");
    return -1;
  }
  LOG_DEBUG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "request_body: %s\n",
            request_body.c_str());

  // 获取分界线信息
  size_t boundary_end_pos = request_body.find("\r\n");
  if (boundary_end_pos == string::npos) {
    LOG_ERROR(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "post no boundary!\n");
    return -1;
  }
  boundary = request_body.substr(0, boundary_end_pos);
  LOG_DEBUG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "boundary: [%s]\n",
            boundary.c_str());

  // 获取文件的信息
  size_t user_start_pos = request_body.find("user=\"", boundary_end_pos) + 6;
  size_t user_end_pos = request_body.find("\"", user_start_pos);
  size_t user_len = user_end_pos - user_start_pos;
  if (user_start_pos == string::npos) {
    LOG_ERROR(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "user_start_pos error\n");
    return -1;
  }
  strncpy(user, request_body.substr(user_start_pos, user_len).c_str(),
          user_len);
  user[user_len] = '\0';
  trimSpace(user);

  size_t filename_start_pos =
      request_body.find("filename=\"", user_end_pos) + 10;
  size_t filename_end_pos = request_body.find("\"", filename_start_pos);
  size_t filename_len = filename_end_pos - filename_start_pos;
  if (filename_start_pos == string::npos) {
    LOG_ERROR(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "filename_start_pos error\n");
    return -1;
  }
  strncpy(filename,
          request_body.substr(filename_start_pos, filename_len).c_str(),
          filename_len);
  filename[filename_len] = '\0';
  trimSpace(filename);

  size_t md5_start_pos = request_body.find("md5=\"", filename_end_pos) + 5;
  size_t md5_end_pos = request_body.find("\"", md5_start_pos);
  size_t md5_len = md5_end_pos - md5_start_pos;
  if (md5_start_pos == string::npos) {
    LOG_ERROR(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "md5_start_pos error\n");
    return -1;
  }
  strncpy(md5, request_body.substr(md5_start_pos, md5_len).c_str(), md5_len);
  md5[md5_len] = '\0';
  trimSpace(md5);

  size_t size_start_pos = request_body.find("size=", md5_end_pos) + 5;
  size_t size_end_pos = request_body.find("\r\n", size_start_pos);
  *p_size =
      strtol(request_body.substr(size_start_pos, size_end_pos - size_start_pos)
                 .c_str(),
             nullptr, 10);
  LOG_DEBUG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,
            "user:[%s], filename:[%s], "
            "md5:[%s], size:[%ld]\n\n",
            user, filename, md5, *p_size);

  // 写入文件
  size_t content_start_pos = request_body.find("\r\n\r\n", size_end_pos) + 4;
  size_t content_end_pos = request_body.find(boundary, content_start_pos) - 2;
  if (content_start_pos == string::npos || content_end_pos == string::npos) {
    LOG_ERROR(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "content_start_pos error\n");
    return -1;
  }
  // todo: 这里加上user防止文件名重复可能会更好
  int fd = open(filename, O_CREAT | O_WRONLY, 0644);
  if (fd < 0) {
    LOG_ERROR(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "open %s error\n", filename);
    return -1;
  }

  ftruncate(fd, content_end_pos - content_start_pos);
  write(fd, request_body.data() + content_start_pos,
        content_end_pos - content_start_pos);
  close(fd);
  return 0;
}

/**
 * @brief 上传本地接收的文件到分布式存储
 * @param filename 文件名
 * @param fileid 文件id
 * @return 0 成功，-1 失败
 */
// todo:添加调用api上传的方法，目前因为fdfs的变量名和chrono的变量名冲突，无法编译
// int uploadToStorage(char *filename, char *fileid) {
//   // 读取fdfs client 配置文件的路径
//   string fdfs_cli_conf_path = "";
//   getCfgValue(CFG_PATH, "dfs_path", "client", fdfs_cli_conf_path);
//   int res = fdfs_upload_file(fdfs_cli_conf_path.c_str(), filename, fileid);
//   // 去掉一个字符串两边的空白字符
//   trimSpace(fileid);
//   return res;
// }

int uploadToStorage(char *filename, char *fileid) {
  int ret = 0;

  pid_t pid;
  int fd[2];
  if (pipe(fd) < 0) {
    LOG_DEBUG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "pip error\n");
    ret = -1;
    goto END;
  }

  // 创建子进程，调用fdfs_upload_file上传文件
  pid = fork();
  if (pid < 0) {
    LOG_DEBUG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "fork error\n");
    ret = -1;
    goto END;
  }

  if (pid == 0)  // 子进程
  {
    // 关闭读端
    close(fd[0]);

    // 将标准输出 重定向 写管道
    dup2(fd[1], STDOUT_FILENO);

    // 读取fdfs client 配置文件的路径
    string fdfs_cli_conf_path = "";
    getCfgValue(CFG_PATH, "dfs_path", "client", fdfs_cli_conf_path);

    execlp("fdfs_upload_file", "fdfs_upload_file", fdfs_cli_conf_path.c_str(),
           filename, NULL);

    // 执行失败
    LOG_DEBUG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "execlp error\n");
    _exit(127);

    close(fd[1]);
  } else  // 父进程
  {
    // 关闭写端
    close(fd[1]);

    // 从管道中去读数据
    read(fd[0], fileid, TEMP_BUF_MAX_LEN);
    trimSpace(fileid);

    if (strlen(fileid) == 0) {
      LOG_DEBUG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "fdfs_upload_file error\n");
      ret = -1;
      goto END;
    }

    LOG_DEBUG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "fileid = %s\n", fileid);
    wait(nullptr);
    close(fd[0]);
  }

END:
  LOG_DEBUG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "\n");
  return ret;
}

/**
 * @brief  封装文件存储在分布式系统中的 完整 url
 *
 * @param fileid        (in)    文件分布式id路径
 * @param fdfs_file_url (out)   文件的完整url地址
 *
 * @returns 0 成功，-1 失败
 */
int makeFileUrl(char *fileid, char *fdfs_file_url) {
  int ret = 0;

  char *p = NULL;
  char *q = NULL;
  char *k = NULL;

  char fdfs_file_stat_buf[TEMP_BUF_MAX_LEN] = {0};
  char fdfs_file_host_name[HOST_NAME_LEN] = {0};  // storage所在服务器ip地址

  pid_t pid;
  int fd[2];

  // 无名管道的创建
  if (pipe(fd) < 0) {
    LOG_ERROR(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "pip error\n");
    return -1;
  }

  // 创建进程
  pid = fork();
  if (pid < 0)  // 进程创建失败
  {
    LOG_ERROR(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "fork error\n");
    return -1;
  }

  if (pid == 0)  // 子进程
  {
    // 关闭读端
    close(fd[0]);

    // 将标准输出 重定向 写管道
    dup2(fd[1], STDOUT_FILENO);  // dup2(fd[1], 1);

    // 读取fdfs client 配置文件的路径
    string fdfs_cli_conf_path = "";
    getCfgValue(CFG_PATH, "dfs_path", "client", fdfs_cli_conf_path);

    execlp("fdfs_file_info", "fdfs_file_info", fdfs_cli_conf_path.c_str(),
           fileid, NULL);

    // 执行失败
    LOG_ERROR(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,
              "execlp fdfs_file_info error\n");

    close(fd[1]);
  } else  // 父进程
  {
    // 关闭写端
    close(fd[1]);

    // 从管道中去读数据
    read(fd[0], fdfs_file_stat_buf, TEMP_BUF_MAX_LEN);
    LOG_INFO(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "get file_ip [%s] succ\n",
             fdfs_file_stat_buf);

    wait(NULL);  // 等待子进程结束，回收其资源
    close(fd[0]);

    // 拼接上传文件的完整url地址--->http://host_name/group1/M00/00/00/D12313123232312.png
    p = strstr(fdfs_file_stat_buf, "source ip address: ");

    q = p + strlen("source ip address: ");  // 这里得到的是本地ip地址
    k = strstr(q, "\n");

    strncpy(fdfs_file_host_name, q, k - q);
    fdfs_file_host_name[k - q] = '\0';

    // printf("host_name:[%s]\n", fdfs_file_host_name);

    // 读取storage_web_server服务器的端口
    string storage_web_server_port = "";
    getCfgValue(CFG_PATH, "storage_web_server", "port",
                storage_web_server_port);

    // todo:通过配置文件的映射进行修改（fdfs_file_info返回的是内网地址，因为目前只有一个storage，直接写入）
    strcpy(fdfs_file_host_name, "s5.s100.vip");  // 穿透地址
    strcat(fdfs_file_url, "http://");
    strcat(fdfs_file_url, fdfs_file_host_name);
    strcat(fdfs_file_url, ":");
    strcat(fdfs_file_url, storage_web_server_port.c_str());
    strcat(fdfs_file_url, "/");
    strcat(fdfs_file_url, fileid);

    // printf("[%s]\n", fdfs_file_url);
    LOG_INFO(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "file url is: %s\n\n",
             fdfs_file_url);
  }

  return ret;
}

int storeFileinfoToMysql(MYSQL *conn, char *user, char *filename, char *md5,
                         long size, char *fileid, char *fdfs_file_url) {
  time_t now;
  char create_time[TIME_STRING_LEN];
  char suffix[SUFFIX_LEN];
  char sql_cmd[SQL_MAX_LEN] = {0};

  getFileSuffix(filename, suffix);  // mp4, jpg, png

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
          "insert into file_info (md5, file_id, url, size, type, count) values "
          "('%s', '%s', '%s', '%ld', '%s', %d)",
          md5, fileid, fdfs_file_url, size, suffix, 1);

  if (mysql_query(conn, sql_cmd) != 0)  // 执行sql语句
  {
    LOG_ERROR(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "%s 插入失败: %s\n", sql_cmd,
              mysql_error(conn));
    return -1;
  }

  LOG_INFO(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "%s 文件信息插入成功\n\n",
           sql_cmd);

  // 获取当前时间
  now = time(NULL);
  strftime(create_time, TIME_STRING_LEN - 1, "%Y-%m-%d %H:%M:%S",
           localtime(&now));

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
          "insert into user_file_list(user, md5, createtime, filename, "
          "shared_status, pv) values ('%s', '%s', '%s', '%s', %d, %d)",
          user, md5, create_time, filename, 0, 0);
  if (mysql_query(conn, sql_cmd) != 0) {
    LOG_ERROR(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "%s 操作失败: %s\n", sql_cmd,
              mysql_error(conn));
    return -1;
  }

  // 查询用户文件数量
  sprintf(sql_cmd, "select count from user_file_count where user = '%s'", user);
  int ret2 = 0;
  char tmp[512] = {0};
  int count = 0;
  // 返回值： 0成功并保存记录集，1没有记录集，2有记录集但是没有保存，-1失败
  ret2 = processResultOne(conn, sql_cmd, tmp);  // 执行sql语句
  if (ret2 == 1)                                  // 没有记录
  {
    // 插入记录
    sprintf(sql_cmd,
            " insert into user_file_count (user, count) values('%s', %d)", user,
            1);
  } else if (ret2 == 0) {
    // 更新用户文件数量count字段
    count = atoi(tmp);
    sprintf(sql_cmd, "update user_file_count set count = %d where user = '%s'",
            count + 1, user);
  }

  if (mysql_query(conn, sql_cmd) != 0) {
    LOG_ERROR(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "%s 操作失败: %s\n", sql_cmd,
              mysql_error(conn));
    return -1;
  }

  return 0;
}

int main() {
  FCGX_Init();   // 初始化 FastCGI 环境
  request = {};  // 在主线程中初始化线程局部变量
  FCGX_InitRequest(&request, 0, 0);

  Redis *redisconn = redisConn();
  MYSQL *mysqlconn = mysqlConn();
  if (mysqlconn == nullptr || redisconn == nullptr) {
    LOG_ERROR(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,
              "mysqlConn or redis_conn failed!");
    return -1;
  }

  while (FCGX_Accept_r(&request) == 0) {
    int ret = 0;
    char filename[FILE_NAME_LEN] = {0};   // 文件名
    char user[USER_NAME_LEN] = {0};       // 文件上传者
    char md5[MD5_LEN] = {0};              // 文件md5码
    long size;                            // 文件大小
    char fileid[TEMP_BUF_MAX_LEN] = {0};  // 文件上传到fastDFS后的文件id
    char fdfs_file_url[FILE_URL_LEN] = {0};  // 文件所存放storage的host_name

    char cmd[20];
    char *query = FCGX_GetParam("QUERY_STRING",
                                request.envp);  // 从环境变量中获取请求参数

    queryParseKeyValue(query, "cmd", cmd, nullptr);
    LOG_INFO(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "cmd = %s\n", cmd);

    // todo: 请求头中不包含Content-Length字段，则可能无法精确获取请求体的长度
    const char *contentLength = FCGX_GetParam("CONTENT_LENGTH", request.envp);
    int len = (contentLength == nullptr) ? 0 : atoi(contentLength);

    FCGX_FPrintF(request.out,
                 "Content-type: text/html\r\n\r\n");  // 写入响应头

    if (len <= 0) {
      FCGX_FPrintF(request.out, "No data from standard input.<p>\n");
      LOG_WARNING(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,
                  "len = 0, No data from standard input\n");
    } else {
      //===============> 得到上传文件  <============
      if (recvSaveFile(len, user, filename, md5, &size) != 0) {
        ret = -1;
        goto END;
      }
      LOG_INFO(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,
               "%s成功上传[%s, 大小：%ld, md5码：%s]到本地\n", user, filename,
               size, md5);

      //===============> 将该文件存入fastDFS中,并得到文件的file_id
      //<============
      if (uploadToStorage(filename, fileid) < 0) {
        ret = -1;
        goto END;
      }

      //================> 得到文件所存放storage的host_name <=================
      if (makeFileUrl(fileid, fdfs_file_url) < 0) {
        ret = -1;
        goto END;
      }

      //===============> 将该文件的FastDFS相关信息存入mysql中 <======
      if (storeFileinfoToMysql(mysqlconn, user, filename, md5, size, fileid,
                               fdfs_file_url) < 0) {
        ret = -1;
        goto END;
      }

    END:
      unlink(filename);  // 删除本地临时存放的上传文件
      memset(filename, 0, FILE_NAME_LEN);
      memset(user, 0, USER_NAME_LEN);
      memset(md5, 0, MD5_LEN);
      memset(fileid, 0, TEMP_BUF_MAX_LEN);
      memset(fdfs_file_url, 0, FILE_URL_LEN);

      // 给前端返回，上传情况
      // 成功：{"code":"008"}
      // 失败：{"code":"009"}
      char *out = nullptr;
      if (ret == 0) {
        out = returnStatus("008");
      } else {
        out = returnStatus("009");
      }
      if (out != nullptr) {
        FCGX_FPrintF(request.out, "%s", out);  // 返回给web服务器
        LOG_INFO(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "%s\n", out);
        free(out);
      }
    }

    FCGX_Finish_r(&request);
  }

  return 0;
}
