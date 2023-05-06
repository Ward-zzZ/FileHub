#ifndef UTIL_CGI_H
#define UTIL_CGI_H

#include <mysql/mysql.h>
#include <sw/redis++/redis++.h>

#include <chrono>
#include <fstream>
#include <iostream>

#include "fcgi_config.h"
#include "fcgi_stdio.h"
#include "make_log.h"
#include "mysql_util.h"
#include "rapidjson/document.h"
#include "rapidjson/istreamwrapper.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

using namespace std;
using namespace rapidjson;
using namespace sw::redis;

// 文件名字长度
const int FILE_NAME_LEN = 256;

// 临时缓冲区大小
const int TEMP_BUF_MAX_LEN = 512;

// 文件所存放storage的host_name长度
const int FILE_URL_LEN = 512;

// 主机ip地址长度
const int HOST_NAME_LEN = 30;

// 用户名字长度
const int USER_NAME_LEN = 128;

// 登陆token长度
const int TOKEN_LEN = 128;

// 文件md5长度
const int MD5_LEN = 256;

// 密码长度
const int PWD_LEN = 256;

// 时间戳长度
const int TIME_STRING_LEN = 25;

// 后缀名长度
const int SUFFIX_LEN = 8;

// 图片资源名字长度
const int PIC_NAME_LEN = 10;

// 图片资源url名字长度
const int PIC_URL_LEN = 256;

const char *const UTIL_LOG_MODULE = "cgi";
const char *const UTIL_LOG_PROC = "util";
#define CFG_PATH "../conf/cfg.json"  // 配置文件路径

// 去除字符串前后的空格
int trimSpace(char *inbuf);

// 寻找子串出现的位置
char *memstr(char *full_data, int full_data_len, char *substr);

// 获取文件后缀名
int getFileSuffix(const char *file_name, char *suffix);

// 从cfg.json中读取配置信息
int getCfgValue(const char *cfgpath, const char *title, const char *key,
                  string &value);

// 从请求中获取参数
int queryParseKeyValue(const char *query, const char *key, char *value,
                          int *value_len_p);

// 从redis中验证token
bool validateToken(sw::redis::Redis *redis, const char *user,
                    const char *token);

// 将状态码转为json格式
char *returnStatus(const char *status_num);

#endif
