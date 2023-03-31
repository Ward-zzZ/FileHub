#ifndef UTIL_CGI_H
#define UTIL_CGI_H

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
#define CFG_PATH "../conf/cfg.json" // 配置文件路径

// 从cfg.json中读取配置信息
int get_cfg_value(const char *cfgpath, const char *title, const char *key, string &value);

// 从请求中获取参数
int query_parse_key_value(const char *query, const char *key, char *value, int *value_len_p);

// 从redis中验证token
bool validate_token(sw::redis::Redis *redis, const char *user, const char *token);

#endif
