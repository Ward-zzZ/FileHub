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

using namespace rapidjson;
using namespace std;
using namespace sw::redis;

#define MYFILES_LOG_MODULE "cgi"
#define MYFILES_LOG_PROC "myfiles"

thread_local FCGX_Request
request; // 定义线程局部变量

/**
 * @brief 从客户端请求中获取用户信息
 *
 * @param buf 客户端请求数据
 * @param user 用户名
 * @param token token
 *
 * @return int 0成功，-1失败
 */
int get_count_info(char *buf, char *user, char *token) {
    // | url      | [http://127.0.0.1:80/myfiles?cmd=count]
    // | post数据  | {   "user": "xxx",   "token": "xxxx"   } |
    Document doc;
    doc.Parse(buf);
    if (!doc.IsObject()) {
        LOG_ERROR(MYFILES_LOG_MODULE, MYFILES_LOG_PROC, "JSON 解析失败！");
        return -1;
    }

    if (!doc.HasMember("user") || !doc["user"].IsString()) {
        LOG_ERROR(MYFILES_LOG_MODULE, MYFILES_LOG_PROC, "缺少或类型错误的字段：userName");
        return -1;
    }
    strcpy(user, doc["user"].GetString());

    if (!doc.HasMember("token") || !doc["token"].IsString()) {
        LOG_ERROR(MYFILES_LOG_MODULE, MYFILES_LOG_PROC, "缺少或类型错误的字段：token");
        return -1;
    }
    strcpy(token, doc["token"].GetString());

    return 0;
}

/**
 * @brief 从客户端请求中获取用户信息
 *
 * @param buf 客户端请求数据
 * @param user 用户名
 * @param token token
 * @param start 起始位置
 * @param count 个数
 *
 * @return int 0成功，-1失败
 */
int get_fileslist_info(char *buf, char *user, char *token, int &start, int &count) {
    // | url      | [http://127.0.0.1:80/myfiles?cmd=count]
    // | post数据  | {   "user": "yoyo"  "token" : xxxx  "start" : 0 "count" : 10  } |
    Document doc;
    doc.Parse(buf);

    if (!doc.IsObject()) {
        LOG_ERROR(MYFILES_LOG_MODULE, MYFILES_LOG_PROC, "JSON 解析失败！");
        return -1;
    }

    if (!doc.HasMember("user") || !doc["user"].IsString()) {
        LOG_ERROR(MYFILES_LOG_MODULE, MYFILES_LOG_PROC, "缺少或类型错误的字段：userName");
        return -1;
    }
    strcpy(user, doc["user"].GetString());

    if (!doc.HasMember("token") || !doc["token"].IsString()) {
        LOG_ERROR(MYFILES_LOG_MODULE, MYFILES_LOG_PROC, "缺少或类型错误的字段：token");
        return -1;
    }
    strcpy(token, doc["token"].GetString());

    if (!doc.HasMember("start") || !doc["start"].IsInt()) {
        LOG_ERROR(MYFILES_LOG_MODULE, MYFILES_LOG_PROC, "缺少或类型错误的字段：start");
        return -1;
    }
    start = doc["start"].GetInt();

    if (!doc.HasMember("count") || !doc["count"].IsInt()) {
        LOG_ERROR(MYFILES_LOG_MODULE, MYFILES_LOG_PROC, "缺少或类型错误的字段：count\n");
        return -1;
    }
    count = doc["count"].GetInt();

    return 0;
}

/**
 * @brief 从数据库中获取用户文件个数
 *
 * @param conn 数据库连接
 * @param user 用户名
 *
 * @return long 用户文件个数
 */
long get_user_files_count(MYSQL *conn, char *user) {
    char sql_cmd[SQL_MAX_LEN] = {0};
    sprintf(sql_cmd, "select count from user_file_count where user=\"%s\"", user);

    char tmp[512] = {0};
    // 返回值： 0成功并保存记录集，1没有记录集，2有记录集但是没有保存，-1失败
    int ret2 = process_result_one(conn, sql_cmd, tmp); // 指向sql语句
    if (ret2 != 0) {
        LOG_ERROR(MYFILES_LOG_MODULE, MYFILES_LOG_PROC, "%s 操作失败\n", sql_cmd);
    }

    long nums = atol(tmp); // 字符串转长整形
    LOG_ERROR(MYFILES_LOG_MODULE, MYFILES_LOG_PROC, "User files's num = %ld\n", nums);
    return nums;
}

/**
 * @brief 从数据库中获取用户文件列表
 *
 * @param conn 数据库连接
 * @param cmd 指令
 * @param user 用户名
 * @param start 起始位置
 * @param count 个数
 *
 * @return int 0成功，-1失败
 */
int get_user_filelist(MYSQL *conn, char *cmd, char *user, int start, int count) {
    // 成功,返回文件列表信息
    // 失败：{"code": "015"}
    char sql_cmd[SQL_MAX_LEN] = {0};
    rapidjson::Document root;
    rapidjson::Value array(rapidjson::kArrayType);
    rapidjson::StringBuffer buffer;
    rapidjson::Writer <rapidjson::StringBuffer> writer(buffer);
    MYSQL_RES *res_set = NULL;

    if (conn == NULL) {
        // 验证数据库连接
        LOG_ERROR(MYFILES_LOG_MODULE, MYFILES_LOG_PROC, "msql_conn err\n");
        return_myfiles_status(0, -1);
        return -1;
    }

    // 多表指定行范围查询
    if (strcmp(cmd, "normal") == 0) // 获取用户文件信息
    {
        // sql语句
        sprintf(sql_cmd,
                "select user_file_list.*, file_info.url, file_info.size, file_info.type from file_info, user_file_list where user = '%s' and file_info.md5 = user_file_list.md5 limit %d, %d",
                user, start, count);
    } else if (strcmp(cmd, "pvasc") == 0) // 按下载量升序
    {
        // sql语句
        sprintf(sql_cmd,
                "select user_file_list.*, file_info.url, file_info.size, file_info.type from file_info, user_file_list where user = '%s' and file_info.md5 = user_file_list.md5  order by pv asc limit %d, %d",
                user, start, count);
    } else if (strcmp(cmd, "pvdesc") == 0) // 按下载量降序
    {
        // sql语句
        sprintf(sql_cmd,
                "select user_file_list.*, file_info.url, file_info.size, file_info.type from file_info, user_file_list where user = '%s' and file_info.md5 = user_file_list.md5 order by pv desc limit %d, %d",
                user, start, count);
    }
    LOG_ERROR(MYFILES_LOG_MODULE, MYFILES_LOG_PROC, "sql_cmd = %s \n", sql_cmd);
    if (mysql_query(conn, sql_cmd) != 0) {
        LOG_ERROR(MYFILES_LOG_MODULE, MYFILES_LOG_PROC, "%s 操作失败：%s\n", sql_cmd, mysql_error(conn));
        return_myfiles_status(-1, -1);
        return -1;
    }

    res_set = mysql_store_result(conn); // 获取结果集
    if (res_set == NULL) {
        LOG_ERROR(MYFILES_LOG_MODULE, MYFILES_LOG_PROC, "smysql_store_result error: %s!\n", mysql_error(conn));
        return_myfiles_status(-1, -1);
        return -1;
    }

    ulong line = 0;
    line = mysql_num_rows(res_set);// 返回结果集中的行数
    if (line == 0) // 没有结果
    {
        LOG_ERROR(MYFILES_LOG_MODULE, MYFILES_LOG_PROC, "mysql_num_rows(res_set) failed：%s\n", mysql_error(conn));
        return_myfiles_status(-1, -1);
        return -1;
    }

    MYSQL_ROW row;

    // mysql_fetch_row从使用mysql_store_result得到的结果结构中提取一行，并把它放到一个行结构中。
    // 当数据用完或发生错误时返回NULL.
    while ((row = mysql_fetch_row(res_set)) != NULL) {
        rapidjson::Value item(rapidjson::kObjectType);
        /*
        files =
        {
        "user": "yoyo",
        "md5": "e8ea6031b779ac26c319ddf949ad9d8d",
        "time": "2017-02-26 21:35:25",
        "filename": "test.mp4",
        "share_status": 0,
        "pv": 0,
        "url": "http://192.168.31.109:80/group1/M00/00/00/wKgfbViy2Z2AJ-FTAaM3As-g3Z0782.mp4",
        "size": 27473666,
         "type": "mp4"
        }
        */
        //-- user	文件所属用户
        if (row[0] != NULL) {
            item.AddMember("user", rapidjson::Value().SetString(row[0], root.GetAllocator()), root.GetAllocator());
        }

        //-- md5 文件md5
        if (row[1] != NULL) {
            item.AddMember("md5", rapidjson::Value().SetString(row[1], root.GetAllocator()), root.GetAllocator());
        }

        //-- creat time 文件创建时间
        if (row[2] != NULL) {
            item.AddMember("time", rapidjson::Value().SetString(row[2], root.GetAllocator()), root.GetAllocator());
        }

        //-- filename 文件名字
        if (row[3] != NULL) {
            item.AddMember("filename", rapidjson::Value().SetString(row[3], root.GetAllocator()), root.GetAllocator());
        }

        //-- shared_status 共享状态, 0为没有共享， 1为共享
        if (row[4] != NULL) {
            item.AddMember("share_status", atoi(row[4]), root.GetAllocator());
        }

        //-- pv 文件下载量，默认值为0，下载一次加1
        if (row[5] != NULL) {
            item.AddMember("pv", atol(row[5]), root.GetAllocator());
        }

        //-- url 文件url
        if (row[6] != NULL) {
            item.AddMember("url", rapidjson::Value().SetString(row[6], root.GetAllocator()), root.GetAllocator());
        }

        //-- size 文件大小, 以字节为单位
        if (row[7] != NULL) {
            item.AddMember("size", atol(row[7]), root.GetAllocator());
        }

        //-- type 文件类型： png, zip, mp4……
        if (row[8] != NULL) {
            item.AddMember("type", rapidjson::Value().SetString(row[8], root.GetAllocator()), root.GetAllocator());
        }

        array.PushBack(item, root.GetAllocator());
    }

    root.AddMember("files", array, root.GetAllocator());
    root.Accept(writer);

    LOG_INFO(MYFILES_LOG_MODULE, MYFILES_LOG_PROC, "查询结果：%s\n", buffer.GetString());
    FCGX_FPrintF(request.out, buffer.GetString());//向nginx返回结果

    // 完成所有对数据的操作后，调用mysql_free_result来善后处理
    if (res_set != NULL) {
        mysql_free_result(res_set);
    }

    return 0;
}

/**
 * @brief 向客户端返回处理结果
 *
 * @param num 用户文件个数
 * @param token_flag 验证标志：0为验证失败，1为验证成功，-1为获取文件列表失败
 */
void return_myfiles_status(long num, int token_flag) {
    Document doc;
    doc.SetObject();
    Document::AllocatorType &allocator = doc.GetAllocator();
    doc.AddMember("num", num, allocator);
    doc.AddMember("token", token_flag == 1 ? "110" : token_flag == -1 ? "015"
                                                                      : "111",
                  allocator); // 验证成功110，失败111
    StringBuffer buffer;
    Writer <StringBuffer> writer(buffer);
    doc.Accept(writer);
    char *out = strdup(buffer.GetString()); // 动态分配内存,strdup用于复制字符串
    if (out != nullptr) {
        FCGX_FPrintF(request.out, out);
        free(out);
    }
}

int main() {

    FCGX_Init();  // 初始化 FastCGI 环境
    request = {}; // 在主线程中初始化线程局部变量
    FCGX_InitRequest(&request, 0, 0);

    // 使用redis-plus-plus库提供的函数连接redis数据库，返回一个Redis对象
    Redis *redisconn = redis_conn();

    // 建立一个数据库连接,避免每次注册都要建立连接
    MYSQL *mysqlconn = NULL;
    mysqlconn = mysql_conn();
    if (mysqlconn == nullptr || redisconn == nullptr) {
        LOG_ERROR(MYFILES_LOG_MODULE, MYFILES_LOG_PROC, "mysql_conn or redis_conn failed!");
        return -1;
    }

    while (FCGX_Accept_r(&request) == 0) {
        char cmd[20];
        char user[USER_NAME_LEN];
        char token[TOKEN_LEN];
        char *query = FCGX_GetParam("QUERY_STRING", request.envp); // 从环境变量中获取请求参数

        query_parse_key_value(query, "cmd", cmd, nullptr);
        LOG_INFO(MYFILES_LOG_MODULE, MYFILES_LOG_PROC, "cmd = %s\n", cmd);

        const char *contentLength = FCGX_GetParam("CONTENT_LENGTH", request.envp);
        int len = (contentLength == nullptr) ? 0 : atoi(contentLength);

        FCGX_FPrintF(request.out, "Content-type: text/html\r\n\r\n");

        if (len <= 0) {
            FCGX_FPrintF(request.out, "No data from standard input.<p>\n");
            LOG_WARNING(MYFILES_LOG_MODULE, MYFILES_LOG_PROC, "len = 0, No data from standard input\n");
        } else {
            char buf[4 * 1024] = {0};
            int ret = FCGX_GetStr(buf, len, request.in); // 从标准输入(web服务器)读取内容
            if (ret == 0) {
                LOG_ERROR(MYFILES_LOG_MODULE, MYFILES_LOG_PROC, "FCGX_GetStr(file_buf, len, request.in) err\n");
                continue;
            }

            LOG_DEBUG(MYFILES_LOG_MODULE, MYFILES_LOG_PROC, "buf = %s\n", buf);

            // 1、统计用户文件个数并返回
            if (strcmp(cmd, "count") == 0) {
                get_count_info(buf, user, token);
                if (validate_token(redisconn, user, token)) {
                    // token验证成功，返回用户文件个数
                    return_myfiles_status(get_user_files_count(mysqlconn, user), 0);
                } else {
                    // token验证失败，返回错误码'111'
                    return_myfiles_status(-1, 1);
                }
            }
                // 2、获取用户文件信息并返回
                // 获取用户文件信息 127.0.0.1:80/myfiles&cmd=normal
                // 按下载量升序 127.0.0.1:80/myfiles?cmd=pvasc
                // 按下载量降序127.0.0.1:80/myfiles?cmd=pvdesc
            else {
                int start; // 文件起点
                int count; // 文件个数
                get_fileslist_info(buf, user, token, start, count);
                LOG_INFO(MYFILES_LOG_MODULE, MYFILES_LOG_PROC, "user = %s, token = %s, start = %d, count = %d\n", user,
                         token, start, count);

                if (validate_token(redisconn, user, token)) {
                    // token验证成功，返回用户文件信息
                    get_user_filelist(mysqlconn, cmd, user, start, count);
                } else {
                    // token验证失败，返回错误码'111'
                    return_myfiles_status(-1, 1);
                }
            }
        }

        FCGX_Finish_r(&request);
    }

    return 0;
}
