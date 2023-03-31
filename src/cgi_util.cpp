#include <chrono>
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

using namespace std;
using namespace rapidjson;
using namespace sw::redis;

/**
 * @brief  从配置文件中获取指定的配置项
 *
 * @param  cfgpath 配置文件路径
 * @param  title 配置项的标题
 * @param  key 配置项的键
 * @param  value 配置项的值
 *
 * @return 0 成功, -1 失败
 */
int get_cfg_value(const char *cfgpath, const char *title, const char *key, string &value) {
    ifstream ifs(cfgpath);
    if (!ifs.is_open()) {
        LOG_ERROR(UTIL_LOG_MODULE, UTIL_LOG_PROC, "Failed to open cfg.json");
        return -1;
    }

    IStreamWrapper isw(ifs); // 将文件流包装为流输入
    Document doc;
    doc.ParseStream(isw);

    if (!doc.HasMember(title)) {
        LOG_ERROR(UTIL_LOG_MODULE, UTIL_LOG_PROC, "Failed to find %s in cfg.json", title);
        return -2;
    }

    const Value &redis = doc[title];
    if (!redis.HasMember(key)) {
        LOG_ERROR(UTIL_LOG_MODULE, UTIL_LOG_PROC, "Failed to find %s %s in cfg.json", title, key);
        return -3;
    }
    value = redis[key].GetString();

    return 0;
}

/**
 * @brief  从请求中获取指定的参数
 *
 * @param  query 请求参数
 * @param  key 参数的键
 * @param  value 参数的值
 * @param  value_len_p 参数的值的长度
 *
 * @return 0 成功, -1 失败
 */
int query_parse_key_value(const char *query, const char *key, char *value, int *value_len_p) {
    const char *temp; // 使用nullptr代替NULL
    const char *end;
    int value_len;

    temp = strstr(query, key); // strstr()函数用于在字符串中查找指定的子字符串，如果找到则返回子字符串的首地址，否则返回NULL。
    if (temp == nullptr)       // 使用nullptr代替NULL
    {
        LOG_ERROR(UTIL_LOG_MODULE, UTIL_LOG_PROC, "query_parse_key_value failed! key=%s", key);
        return -1;
    }

    temp += strlen(key) + 1; // strlen()函数用于计算字符串的长度，不包括字符串结束符'\0'。+1是为了跳过'='
    // get value
    end = temp;

    while (*end != '\0' && *end != '#' && *end != '&') {
        ++end; // 找到value的结束位置
    }
    value_len = end - temp;

    strncpy(value, temp, value_len);
    value[value_len] = '\0'; // 由于末尾是'\0'或者'\#'或者'\&',所以用'\0'覆盖表示字符串结束

    if (value_len_p != nullptr) {
        *value_len_p = value_len;
    }

    return 0;
}

/**
 * @brief  验证给定用户和 token 的有效性
 *
 * @param  redis Redis 数据库对象
 * @param  user 用户名
 * @param  token 待验证的 token
 *
 * @return true 成功, false 失败
 */
bool validate_token(sw::redis::Redis *redis, const char *user, const char *token) {
    try {
        // 从redis中获取指定用户的token
        sw::redis::OptionalString redis_token = redis->get(user);
        if (!redis_token) {
            // 用户不存在或token已过期
            return false;
        }

        // 验证token是否匹配
        if (strcmp(token, redis_token.value_or("").c_str()) != 0) {
            // token不匹配
            return false;
        }

        // token有效
        return true;
    }
    catch (const sw::redis::Error &e) {
        // 打印异常信息
        LOG_ERROR(UTIL_LOG_MODULE, UTIL_LOG_PROC, "Redis Error: %s\n", e.what());
        return false;
    }
}
g