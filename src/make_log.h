#ifndef _MAKE_LOG_H_
#define _MAKE_LOG_H_
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstdarg>
#include <chrono>
#include <iomanip>
#include <mutex>
#include <filesystem>

extern std::mutex log_lock;
enum class LogLevel
{
  DEBUG,
  INFO,
  WARNING,
  ERROR,
  FATAL
};

void dumpmsg_to_file(const char *module_name, const char * proc_name, const std::string &filename,
                     int line, const std::string &funcname, LogLevel level, const char *fmt, ...);
void make_path(const std::string &module_name, const std::string &proc_name);

#define LOG_DEBUG(module_name, proc_name, ...)                                                               \
  {                                                                                                          \
    dumpmsg_to_file(module_name, proc_name, __FILE__, __LINE__, __FUNCTION__, LogLevel::DEBUG, __VA_ARGS__); \
  }

#define LOG_INFO(module_name, proc_name, ...)                                                               \
  {                                                                                                         \
    dumpmsg_to_file(module_name, proc_name, __FILE__, __LINE__, __FUNCTION__, LogLevel::INFO, __VA_ARGS__); \
  }

#define LOG_WARNING(module_name, proc_name, ...)                                                               \
  {                                                                                                            \
    dumpmsg_to_file(module_name, proc_name, __FILE__, __LINE__, __FUNCTION__, LogLevel::WARNING, __VA_ARGS__); \
  }

#define LOG_ERROR(module_name, proc_name, ...)                                                               \
  {                                                                                                          \
    dumpmsg_to_file(module_name, proc_name, __FILE__, __LINE__, __FUNCTION__, LogLevel::ERROR, __VA_ARGS__); \
  }

#define LOG_FATAL(module_name, proc_name, ...)                                                               \
  {                                                                                                          \
    dumpmsg_to_file(module_name, proc_name, __FILE__, __LINE__, __FUNCTION__, LogLevel::FATAL, __VA_ARGS__); \
  }


#endif
