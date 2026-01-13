#pragma once
// 可能会用上的C++标准库
#include <chrono>
#include <concepts>
#include <format>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <numbers>
#include <numeric>
#include <span>
#include <sstream>
#include <stack>
#include <unordered_map>
#include <vector>

// GLM
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
// 如果你惯用左手坐标系，在此定义GLM_FORCE_LEFT_HANDED
#include <glm.hpp>
#include <gtc/matrix_transform.hpp>

// stb_image.h
#include <stb_image.h>

// Vulkan
#ifdef _WIN32                      // 考虑平台是Windows的情况（请自行解决其他平台上的差异）
#define VK_USE_PLATFORM_WIN32_KHR  // 在包含vulkan.h前定义该宏，会一并包含vulkan_win32.h和windows.h
// #define NOMINMAX  // 定义该宏可避免windows.h中的min和max两个宏与标准库中的函数名冲突
#pragma comment(lib, "vulkan-1.lib")  // 链接编译所需的静态存根库
#endif
#include <vulkan/vulkan.h>

template <typename T>
class arrayRef {
    T* const pArray = nullptr;
    size_t count = 0;

public:
    // 从空参数构造，count为0
    arrayRef() = default;
    // 从单个对象构造，count为1
    arrayRef(T& data) : pArray(&data), count(1) {}
    // 从顶级数组构造
    template <size_t ElementCount>
    arrayRef(T (&data)[ElementCount]) : pArray(data), count(ElementCount)
    {}
    // 从指针和元素个数构造
    arrayRef(T* pData, size_t elementCount) : pArray(pData), count(elementCount) {}
    // 若T带const修饰，兼容从对应的无const修饰版本的arrayRef构造
    arrayRef(const arrayRef<std::remove_const_t<T>>& other)
        : pArray(other.Pointer()), count(other.Count())
    {}
    // Getter
    T* Pointer() const
    {
        return pArray;
    }
    size_t Count() const
    {
        return count;
    }
    // Const Function
    T& operator[](size_t index) const
    {
        return pArray[index];
    }
    T* begin() const
    {
        return pArray;
    }
    T* end() const
    {
        return pArray + count;
    }
    // Non-const Function
    // 禁止复制/移动赋值（arrayRef旨在模拟“对数组的引用”，用处归根结底只是传参，故使其同C++引用的底层地址一样，防止初始化后被修改）
    arrayRef& operator=(const arrayRef&) = delete;
};

// 用于控制该语句以下的代码仅执行一次
#define ExecuteOnce(...)                  \
    {                                     \
        static bool executed = false;     \
        if (executed) return __VA_ARGS__; \
        executed = true;                  \
    }
