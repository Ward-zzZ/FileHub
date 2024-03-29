#include "mysql_util.h"

/**
 * @brief 连接redis
 *
 * @return Redis* 连接成功返回Redis对象，否则返回nullptr
 */
Redis *redisConn() {
  string redis_host;
  string redis_port;
  string redis_auth;
  getCfgValue(CFG_PATH, "redis", "host", redis_host);
  getCfgValue(CFG_PATH, "redis", "port", redis_port);
  getCfgValue(CFG_PATH, "redis", "password", redis_auth);
  LOG_INFO("cgi", "redis", "Redis:[ip=%s,port=%s]", redis_host.c_str(),
           redis_port.c_str());
  Redis *redis = new Redis("tcp://" + redis_host + ":" + redis_port);
  if (redis == nullptr) {
    LOG_ERROR(MYSQL_LOG_MODULE, MYSQL_LOG_PROC, "Failed to connect redis");
    return nullptr;
  }
  redis->auth(redis_auth);
  return redis;
}

/**
 * @brief 读取配置文件，获得数据库的主机、用户名、密码、数据库名
 *
 * @param user 数据库用户名
 * @param pwd 数据库密码
 * @param db 数据库名
 *
 * @return int 0成功，-1打开文件失败，-2找不到mysql配置文件，-3缺少mysql配置项
 */
int getMysqlInfo(string &host, string &user, string &pwd, string &db) {
  ifstream ifs(CFG_PATH);
  if (!ifs.is_open()) {
    LOG_ERROR(MYSQL_LOG_MODULE, MYSQL_LOG_PROC, "Failed to open cfg.json");
    return -1;
  }

  IStreamWrapper isw(ifs);  // 将文件流包装为流输入
  Document doc;
  doc.ParseStream(isw);

  if (!doc.HasMember("mysql")) {
    LOG_ERROR(MYSQL_LOG_MODULE, MYSQL_LOG_PROC,
              "Failed to find mysql section in cfg.json");
    return -2;
  }

  const Value &mysql = doc["mysql"];
  if (!mysql.HasMember("host") || !mysql.HasMember("port") ||
      !mysql.HasMember("database") || !mysql.HasMember("user") ||
      !mysql.HasMember("password")) {
    LOG_ERROR(MYSQL_LOG_MODULE, MYSQL_LOG_PROC,
              "Failed to find required mysql parameters in cfg.json");
    return -3;
  }

  host = mysql["host"].GetString();
  user = mysql["user"].GetString();
  pwd = mysql["password"].GetString();
  db = mysql["database"].GetString();

  return 0;
}

/**
 * @brief 连接mysql
 *
 * @return MYSQL* 连接成功返回MYSQL对象，否则返回nullptr
 */
MYSQL *mysqlConn() {
  string mysql_host;
  string mysql_user;
  string mysql_pwd;
  string mysql_db;
  int ret = getMysqlInfo(
      mysql_host, mysql_user, mysql_pwd,
      mysql_db);  // 读取配置文件，获得数据库的用户名、密码、数据库名
  if (ret != 0) {
    LOG_DEBUG(MYSQL_LOG_MODULE, MYSQL_LOG_PROC, "getMysqlInfo failed!");
    return nullptr;
  }
  LOG_INFO(MYSQL_LOG_MODULE, MYSQL_LOG_PROC,
           "mysql_user = %s, mysql_pwd = %s, mysql_db = %s", mysql_user.c_str(),
           mysql_pwd.c_str(), mysql_db.c_str());
  MYSQL *conn = mysql_init(nullptr);
  if (conn == nullptr) {
    LOG_ERROR(MYSQL_LOG_MODULE, MYSQL_LOG_PROC, "mysql_init error!");
    return nullptr;
  }

  if (mysql_real_connect(conn, mysql_host.c_str(), mysql_user.c_str(),
                         mysql_pwd.c_str(), mysql_db.c_str(), 0, nullptr,
                         0) == nullptr) {
    LOG_ERROR(MYSQL_LOG_MODULE, MYSQL_LOG_PROC,
              "mysql_real_connect error! user=%s, pwd=%s, db=%s",
              mysql_user.c_str(), mysql_pwd.c_str(), mysql_db.c_str());
    mysql_close(conn);
    return nullptr;
  }
  if (mysql_query(conn, "set names utf8") != 0) {
    LOG_DEBUG(MYSQL_LOG_MODULE, MYSQL_LOG_PROC, "set names utf8 failed!");
    mysql_close(conn);
    return nullptr;
  }

  return conn;
}

/**
 * @brief 处理数据库查询结果，结果集保存在buf，只处理一条记录，一个字段,
 * 如果buf为nullptr，无需保存结果集，只做判断有没有此记录
 *
 * @param conn 数据库连接
 * @param sql_cmd sql语句
 * @param buf 保存结果集的缓冲区
 *
 * @return 0成功并保存记录集，1没有记录集，2有记录集但是没有保存，-1失败
 */
int processResultOne(MYSQL *conn, const char *sql_cmd, char *buf) {
  MYSQL_RES *res_set = nullptr;  // 结果集结构的指针
  int ret = 0;

  do {
    if (mysql_query(conn, sql_cmd) != 0) {
      // 执行sql语句，执行成功返回0
      LOG_ERROR(MYSQL_LOG_MODULE, MYSQL_LOG_PROC,
                "mysql_query error! sql_cmd=%s", sql_cmd);
      ret = -1;
      break;
    }

    res_set = mysql_store_result(conn);  // 生成结果集
    if (res_set == nullptr) {
      LOG_ERROR(MYSQL_LOG_MODULE, MYSQL_LOG_PROC, "mysql_store_result error!");
      ret = -1;
      break;
    }

    const unsigned long line = mysql_num_rows(
        res_set);  // mysql_num_rows接受由mysql_store_result返回的结果结构集，并返回结构集中的行数
    if (line == 0) {
      ret = 1;  // 1没有记录集
      break;
    } else if (line > 0 && buf == nullptr) {
      // 如果buf为nullptr，无需保存结果集，只做判断有没有此记录
      ret = 2;  // 2有记录集但是没有保存
      break;
    }

    MYSQL_ROW row;
    if ((row = mysql_fetch_row(res_set)) != nullptr) {
      // mysql_fetch_row从结果结构中提取一行，并把它放到一个行结构中。当数据用完或发生错误时返回nullptr.
      if (row[0] != nullptr) {
        strcpy(buf, row[0]);  // 将结果集保存到buf
      }
    }

  } while (false);

  if (res_set != nullptr) {
    // 完成所有对数据的操作后，调用mysql_free_result来善后处理
    mysql_free_result(res_set);
  }

  return ret;
}
