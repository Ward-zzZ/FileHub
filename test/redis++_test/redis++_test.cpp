#include <sw/redis++/redis++.h>
#include <iostream>

using namespace sw::redis;

int main()
{
  // 创建 Redis 对象，该对象是可移动但不可复制的。
  // 会在 Redis 对象析构时自动断开连接。
  auto redis = Redis("tcp://127.0.0.1:6379");

  try
  {
    // ***** STRING 命令 *****

    // 设置一个键值对
    redis.set("key", "value");

    // 获取键对应的值
    auto val = redis.get("key"); // val 的类型为 OptionalString。有关详细信息，请参见“API 参考”部分。
    if (val)
    {
      // 解引用 val 获取返回的 std::string 类型的值。
      std::cout << *val << std::endl;
    } // 否则键不存在。

    // ***** LIST 命令 *****

    // 将值推入列表。
    redis.lpush("list", {"a", "b", "c"});

    // 从列表中弹出一个值。
    val = redis.lpop("list");
    if (val)
    {
      std::cout << *val << std::endl;
    }
  }
  catch (const Error &e)
  {
    // 错误处理
    std::cerr << e.what() << std::endl;
  }

  return 0;
}
