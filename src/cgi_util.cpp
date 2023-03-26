#include <iostream>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <random>
#include <fstream>
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


// 从cfg.json中读取配置信息
int get_cfg_value(const char *cfgpath, const char *title, const char *key, string &value)
{
  ifstream ifs(cfgpath);
  if (!ifs.is_open())
  {
    LOG_ERROR(UTIL_LOG_MODULE, UTIL_LOG_PROC, "Failed to open cfg.json");
    return -1;
  }

  IStreamWrapper isw(ifs); // 将文件流包装为流输入
  Document doc;
  doc.ParseStream(isw);

  if (!doc.HasMember(title))
  {
    LOG_ERROR(UTIL_LOG_MODULE, UTIL_LOG_PROC, "Failed to find %s in cfg.json", title);
    return -2;
  }

  const Value &redis = doc[title];
  if (!redis.HasMember(key))
  {
    LOG_ERROR(UTIL_LOG_MODULE, UTIL_LOG_PROC, "Failed to find %s %s in cfg.json", title, key);
    return -3;
  }
  value = redis[key].GetString();

  return 0;
}
