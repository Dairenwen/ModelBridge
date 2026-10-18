#include "Util.hpp"
int main()
{
    MB_LOG_WARN("请求超时：{} 秒", 30);
    MB_LOG_ERROR("模型调用失败：{}", "连接中断");
    return 0;
}
