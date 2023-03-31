/**
 * @file login_cgi.cpp
 * @brief  处理登陆的CGI程序
 * @author ward
 * @version 1.0
 * @date 2023年3月26日
 */
#include <iostream>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <random>
#include <mysql/mysql.h>
#include <sw/redis++/redis++.h>
#include "rapidjson/document.h"
#include "rapidjson/istreamwrapper.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "fcgi_config.h"
#include "fcgi_stdio.h"
#include "make_log.h"
#include "mysql_util.h"
#include "cgi_util.h"

const char *const LOGIN_LOG_MODULE = "cgi";
const char *const LOGIN_LOG_PROC = "login";

using namespace std;
using namespace rapidjson;
using namespace sw::redis;

// 解析json包，获得包括用户名、密码
int get_login_info(char *login_buf, char *user, char *pwd) {

    // 解析json包
    Document doc;
    doc.Parse(login_buf);

    if (!doc.IsObject()) {
        LOG_ERROR(LOGIN_LOG_MODULE, LOGIN_LOG_PROC, "JSON 解析失败！");
        return -1;
    }

    // 用户
    if (!doc.HasMember("userName") || !doc["userName"].IsString()) {
        LOG_ERROR(LOGIN_LOG_MODULE, LOGIN_LOG_PROC, "缺少或类型错误的字段：userName");
        return -1;
    }
    strcpy(user, doc["userName"].GetString());

    // 密码
    if (!doc.HasMember("passWord") || !doc["passWord"].IsString()) {
        LOG_ERROR(LOGIN_LOG_MODULE, LOGIN_LOG_PROC, "缺少或类型错误的字段：passWord");
        return -1;
    }
    strcpy(pwd, doc["passWord"].GetString());
    return 0;
}

// 查询数据库，验证用户名和密码是否正确
int check_user_pwd(MYSQL *conn, const char *user, const char *pwd) {
    char sql_cmd[SQL_MAX_LEN] = {0};
    int ret = -1;
    // 验证数据库连接是否成功
    if (conn == NULL) {
        LOG_ERROR(LOGIN_LOG_MODULE, LOGIN_LOG_PROC, "no mysql connection");
        return -1;
    }

    // sql语句，查找某个用户对应的密码
    sprintf(sql_cmd, "select password from user where name=\"%s\"", user);

    // deal result
    char tmp[PWD_LEN] = {0};

    // 返回值： 0成功并保存记录集，1没有记录集，2有记录集但是没有保存，-1失败
    int result = process_result_one(conn, sql_cmd, tmp); // 执行sql语句，结果集保存在tmp
    if (result == 0 && strcmp(tmp, pwd) == 0) {
        ret = 0;
    } else {
        LOG_INFO(LOGIN_LOG_MODULE, LOGIN_LOG_PROC, "user or password error");
        ret = -1;
    }

    return ret;
}

char *generateToken(const char *secret_key) {
    // 获取当前时间的毫秒数
    auto timestamp = chrono::duration_cast<chrono::milliseconds>(
            chrono::system_clock::now().time_since_epoch())
            .count();

    // 生成一个随机数作为salt
    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<> dis(0, 999999);
    int salt = dis(gen);

    // 将时间戳和salt拼接成字符串
    stringstream ss;
    ss << timestamp << salt;
    string str = ss.str();

    // 对字符串进行哈希，生成token
    hash <string> hasher;
    size_t hash = hasher(string(secret_key) + str);
    stringstream token_ss;
    token_ss << hash;
    string token = token_ss.str();

    // 将token复制到新的字符数组中
    char *result = new char[token.length() + 1];
    strcpy(result, token.c_str());
    return result;
}

// 生成token并保存到redis中
int set_token(Redis &redis, char *user, char token[], size_t token_len) {
    // 生成token字符串
    string new_token = generateToken(user);
    LOG_INFO(LOGIN_LOG_MODULE, LOGIN_LOG_PROC, "user: %s, token: %s", user, new_token.c_str());

    // 如果连接失败，抛出异常
    try {
        // 将user和token作为键值对存入redis数据库中
        redis.setex(user, 86400, new_token); // 设置过期时间为24小时
        // 将token字符串复制到传入的数组中
        strncpy(token, new_token.c_str(), token_len);
        LOG_INFO(LOGIN_LOG_MODULE, LOGIN_LOG_PROC, "Redis Set: %s -> %s", user, token);
    }
    catch (const Error &e) {
        // 打印异常信息
        LOG_ERROR(LOGIN_LOG_MODULE, LOGIN_LOG_PROC, "Redis Error: %s\n", e.what());
        return -1;
    }

    return 0;
}


char *return_login_status(const char *status_num, const char *token) {
    Document doc;
    doc.SetObject();
    Document::AllocatorType &allocator = doc.GetAllocator();
    doc.AddMember("code", Value(status_num, allocator).Move(), allocator);
    doc.AddMember("token", Value(token, allocator).Move(), allocator);
    StringBuffer buffer;
    Writer <StringBuffer> writer(buffer);
    doc.Accept(writer);
    char *result = strdup(buffer.GetString()); // 动态分配内存,strdup用于复制字符串
    return result;
}

int main() {
    FCGX_Init();
    FCGX_Request request;
    FCGX_InitRequest(&request, 0, 0);

    LOG_INFO(LOGIN_LOG_MODULE, LOGIN_LOG_PROC, "login server start");

    // 从配置文件中读取redis服务器的ip和端口
    string redis_ip;
    string redis_port;
    get_cfg_value(CFG_PATH, "redis", "ip", redis_ip);
    get_cfg_value(CFG_PATH, "redis", "port", redis_port);
    LOG_INFO(LOGIN_LOG_MODULE, LOGIN_LOG_PROC, "Redis:[ip=%s,port=%s]", redis_ip.c_str(), redis_port.c_str());
    // 使用redis-plus-plus库提供的函数连接redis数据库，返回一个Redis对象
    auto redisconn = Redis("tcp://" + redis_ip + ":" + redis_port);

    // 建立一个数据库连接,避免每次注册都要建立连接
    MYSQL *conn = NULL;
    conn = mysql_conn();
    if (conn == NULL) {
        LOG_DEBUG(LOGIN_LOG_MODULE, LOGIN_LOG_PROC, "mysql_conn failed!");
        return -1;
    }

    while (FCGX_Accept_r(&request) == 0) {
        char *contentLength = FCGX_GetParam("CONTENT_LENGTH", request.envp);
        int len;

        FCGX_FPrintF(request.out, "Content-type: text/html\r\n\r\n"); // 告诉web服务器，返回的数据类型是html

        if (contentLength == nullptr) {
            len = 0;
            FCGX_FPrintF(request.out, "No data from standard input.<p>\n"); // 这是标准输出，会返回给web服务器
            LOG_INFO(LOGIN_LOG_MODULE, LOGIN_LOG_PROC, "len = %d", len);
        } else {
            // 获取登陆用户信息
            len = atoi(contentLength); // 字符串转整型
            char buf[4 * 1024] = {0};
            int ret = 0;
            char *out = nullptr;

            ret = FCGX_GetStr(buf, len, request.in); // 从标准输入(web服务器)读取内容
            if (ret == 0) {
                LOG_ERROR(LOGIN_LOG_MODULE, LOGIN_LOG_PROC, "FCGX_GetStr() err");
                continue;
            }
            LOG_INFO(LOGIN_LOG_MODULE, LOGIN_LOG_PROC, "buf = %s", buf);

            // 获取登陆用户的信息
            char user[512] = {0};
            char pwd[512] = {0};
            get_login_info(buf, user, pwd);
            LOG_INFO(LOGIN_LOG_MODULE, LOGIN_LOG_PROC, "user = %s, pwd = %s\n", user, pwd);

            // 登陆密码验证，成功返回0，失败返回-1
            ret = check_user_pwd(conn, user, pwd);
            if (ret == 0) // 登陆成功
            {
                char token[1024] = {0};
                // 生成token字符串

                if (set_token(redisconn, user, token, sizeof(token)) == -1) {
                    // 如果生成token失败，返回错误信息
                    LOG_ERROR(LOGIN_LOG_MODULE, LOGIN_LOG_PROC, "set_token failed!");
                    out = return_login_status("002", "set_token failed!");
                    continue;
                }
                LOG_INFO(LOGIN_LOG_MODULE, LOGIN_LOG_PROC, "token = %s", token);
                // 返回前端登陆情况， 000代表成功
                out = return_login_status("000", token);
            } else {
                // 返回前端登陆情况， 001代表失败
                out = return_login_status("001", "fail");
            }
            if (out != nullptr) {
                FCGX_FPrintF(request.out, "%s", out);
                LOG_INFO(LOGIN_LOG_MODULE, LOGIN_LOG_PROC, "out = %s\n", out);
                free(out);
            }
        }

        FCGX_Finish_r(&request);
    }

    mysql_close(conn);
    return 0;
}
