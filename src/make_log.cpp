#include <string>
#include <cstdarg>
#include <chrono>
#include <iomanip>
#include <filesystem>
#include <mutex>
#include <fstream>

#include "make_log.h"

namespace fs = std::filesystem;
std::mutex log_lock;

/**
  * @brief 将日志信息写入到文件中
  *
  * @param module_name 模块名称
  * @param proc_name 进程名称
  * @param filename 文件名称
  * @param line 行号
  * @param funcname 函数名
  * @param level 日志级别
  * @param fmt 格式化字符串
  * @param ... 可变参数列表
*/
void dumpmsg_to_file(const char *module_name, const char *proc_name, const std::string &filename,
                     int line, const std::string &funcname, LogLevel level, const char *fmt, ...) {
    std::ostringstream mesg_stream;//流操作，可以将数据写入到字符串中
    std::ostringstream buf_stream;//用于将日志信息写入文件
    std::string filepath;

    // 根据日志级别选择不同的前缀
    std::string prefix;
    switch (level) {
        case LogLevel::DEBUG:
            prefix = "DEBUG";
            break;
        case LogLevel::INFO:
            prefix = "INFO";
            break;
        case LogLevel::WARNING:
            prefix = "WARNING";
            break;
        case LogLevel::ERROR:
            prefix = "ERROR";
            break;
        case LogLevel::FATAL:
            prefix = "FATAL";
            break;
        default:
            prefix = "";
            break;
    }
    buf_stream << "[" << prefix << "]" << " ";

    auto now = std::chrono::system_clock::now();//获取当前时间
    auto now_c = std::chrono::system_clock::to_time_t(now);
    auto now_tm = std::localtime(&now_c);
    mesg_stream << std::put_time(now_tm, "===%Y%m%d-%H%M%S,") << funcname << "[" << line << "]=== ";

    //将可变参数列表通过格式化字符串写入到字符串中
    char fmtmesg[4096] = {0};
    va_list ap;
    va_start(ap, fmt);
    vsprintf(fmtmesg, fmt, ap);
    va_end(ap);

    make_path(std::string(module_name), std::string(proc_name)); // 创建日志文件的目录
    buf_stream << mesg_stream.str() << fmtmesg << std::endl;
    filepath =
            "/home/ward/FileHub/logs/" + std::string(module_name) + "/" + std::to_string(now_tm->tm_year + 1900) + "/" +
            std::to_string(now_tm->tm_mon + 1) + "/" + std::to_string(now_tm->tm_mday) + "/" + std::string(proc_name) +
            "-" + std::to_string(now_tm->tm_mday) + ".log";

    std::lock_guard <std::mutex> lock(log_lock);//lock_guard可以自动加锁和解锁，在作用域结束时自动解锁
    std::ofstream outfile(filepath, std::ios_base::app);//以追加的方式打开文件
    outfile << buf_stream.str();
}

/**
 * @brief 创建日志文件的目录
 *
 * @param module_name 模块名称
 * @param proc_name 进程名称
*/
void make_path(const std::string &module_name, const std::string &proc_name) {
    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);
    auto now_tm = std::localtime(&now_c);

    fs::path top_dir("/home/ward/FileHub");
    fs::path second_dir = top_dir / "logs";
    fs::path third_dir = second_dir / module_name;
    fs::path y_dir = third_dir / std::to_string(now_tm->tm_year + 1900);
    fs::path m_dir = y_dir / std::to_string(now_tm->tm_mon + 1);
    fs::path d_dir = m_dir / std::to_string(now_tm->tm_mday);

    if (!fs::exists(d_dir)) {
        if (!fs::create_directories(d_dir)) {
            std::cerr << "Failed to create directory " << d_dir << std::endl;
        }
    }
}
