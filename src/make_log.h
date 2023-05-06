#ifndef _MAKE_LOG_H_
#define _MAKE_LOG_H_

#include <chrono>
#include <cstdarg>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <string>

extern std::mutex log_lock;
enum class LogLevel { DEBUG, INFO, WARNING, ERROR, FATAL };

void dumpmsg_to_file(const char *module_name, const char *proc_name,
                     const std::string &filename, int line,
                     const std::string &funcname, LogLevel level,
                     const char *fmt, ...);

void make_path(const std::string &module_name, const std::string &proc_name);

//`do-while(false)`循环是一种技巧，可以将多个语句组成一个单独的块，并在不引入新的作用域的情况下将它们组合在一起。
// 在宏中使用这个技巧可以避免出现因宏展开而引入的副作用。
// 具体而言，`do-while(false)`循环可以确保宏中的所有语句被视为单个语句，从而可以避免生成空语句的警告。
#define LOG_DEBUG(module_name, proc_name, ...)                                \
  do {                                                                        \
    dumpmsg_to_file(module_name, proc_name, __FILE__, __LINE__, __FUNCTION__, \
                    LogLevel::DEBUG, __VA_ARGS__);                            \
  } while (false)

#define LOG_INFO(module_name, proc_name, ...)                                 \
  do {                                                                        \
    dumpmsg_to_file(module_name, proc_name, __FILE__, __LINE__, __FUNCTION__, \
                    LogLevel::INFO, __VA_ARGS__);                             \
  } while (false)

#define LOG_WARNING(module_name, proc_name, ...)                              \
  do {                                                                        \
    dumpmsg_to_file(module_name, proc_name, __FILE__, __LINE__, __FUNCTION__, \
                    LogLevel::WARNING, __VA_ARGS__);                          \
  } while (false)

#define LOG_ERROR(module_name, proc_name, ...)                                \
  do {                                                                        \
    dumpmsg_to_file(module_name, proc_name, __FILE__, __LINE__, __FUNCTION__, \
                    LogLevel::ERROR, __VA_ARGS__);                            \
  } while (false)

#define LOG_FATAL(module_name, proc_name, ...)                                \
  do {                                                                        \
    dumpmsg_to_file(module_name, proc_name, __FILE__, __LINE__, __FUNCTION__, \
                    LogLevel::FATAL, __VA_ARGS__);                            \
  } while (false)
#

#endif
