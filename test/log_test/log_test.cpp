#include "make_log.h"

int main()
{
  // const char* module_name = "test_module";
  // const char *proc_name = "test_proc";
  const char* p = "hello mike";
  LOG_DEBUG("test_module", "test_proc", "test info[]");  // 打印日志
  LOG_DEBUG("test_module", "test_proc", "test info[%s]", p); // 打印日志
  // LOG_INFO(module_name, proc_name, "test info[%d]", -1); // 打印日志
  // LOG_WARNING(module_name, proc_name, "test info[%d][%s]", -1,p); // 打印日志
  return 0;
}
