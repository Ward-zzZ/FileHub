#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

int main()
{
  fs::space_info si = fs::space("/");

  std::cout << "Total space: " << si.capacity / (1024 * 1024) << " MB" << std::endl;
  std::cout << "Free space: " << si.free / (1024 * 1024) << " MB" << std::endl;
  std::cout << "Available space: " << si.available / (1024 * 1024) << " MB" << std::endl;

  return 0;
}
