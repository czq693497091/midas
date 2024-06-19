#include <iostream>
#include <memory>
// #include "smdk_opt_api.hpp"
#include "cxl_op.hpp"
// using namespace std;
// using namespace std;
// using namespace midas;
// using namesapce std;
#define ARRAY_SIZE 10000000
SmdkAllocator &allocator = SmdkAllocator::get_instance();
class MyClass
{
public:
    // 默认构造函数
    MyClass()
    {
        // 初始化代码
        std::cout << "create without params" << std::endl;
    }

    // // 带参数的构造函数
    MyClass(int value) : value_(value)
    {
        // 初始化代码，使用传入的参数
        std::cout << "create now with params" << std::endl;
    }

    MyClass(int val1, int val2) : value_(val1), val2_(val2)
    {
        // 初始化代码，使用传入的参数
        std::cout << "create now with params" << std::endl;
    }

    // 析构函数
    ~MyClass()
    {
        // 清理代码
        std::cout << "destory now" << std::endl;
    }

    // 成员函数示例
    void print()
    {
        std::cout << "value: " << value_ << " " << val2_ << std::endl;
    }

private:
    int value_;
    int val2_;
    int arr[ARRAY_SIZE];
};

// 定义一个函数，用于创建 MyClass 的实例并传递参数
// MyClass *create_with_params(int value)
// {
//     void *memory = allocator.malloc(SMDK_MEM_EXMEM, sizeof(MyClass));
//     if (!memory)
//     {
//         throw std::bad_alloc(); // 处理内存分配失败的情况
//     }
//     MyClass *obj = new (memory) MyClass(value); // 使用 placement new 构造对象
//     return obj;
// }

// void customDeleter(MyClass *ptr)
// {
//     std::cout << "Custom deleter called." << std::endl;
//     allocator.free(SMDK_MEM_EXMEM, ptr); // 使用 std::free 释放内存
// }



// template<typename T>
// void cxl_free(T* ptr)
// {
//     // std::cout << "Custom deleter called." << std::endl;
//     allocator.free(SMDK_MEM_EXMEM, ptr); // 使用自定义分配器释放内存
// }

// template<typename T, typename... Args>
// T* cxl_alloc_with_params(Args&&... args)
// {
//     void* memory = allocator.malloc(SMDK_MEM_EXMEM, sizeof(T));
//     if (!memory)
//     {
//         throw std::bad_alloc(); // 处理内存分配失败的情况
//     }
//     T* obj = new (memory) T(std::forward<Args>(args)...); // 使用 placement new 构造对象
//     return obj;
// }

void test_smart_pointer()
{
    // 使用智能指针进行分配

    // std::unique_ptr<MyClass,void (*)(MyClass*)> _unique_ptr;
    std::unique_ptr<MyClass, std::function<void(MyClass*)>> p1, p2;

    auto uniquePtr1 = midas::CXLAllocator::make_unique<MyClass>(10);
    p1 = midas::CXLAllocator::make_unique<MyClass>(10,20);
    p1 = midas::CXLAllocator::make_unique<MyClass>(20,30);
    p1 = midas::CXLAllocator::make_unique<MyClass>(20,30);
    // allocator.stats_print('M');
    // auto deleter = [&](MyClass *ptr)
    // { allocator.free(SMDK_MEM_EXMEM, ptr); };
    // MyClass *raw_ptr = create_with_params(10); // 和cxl操作相关的API，建议集中到一个文件里来写，否则太乱

    // MyClass *raw_ptr = cxl_alloc_with_params<MyClass>(10,20);

    // MyClass *raw_ptr = midas::CXLAllocator::alloc_with_params<MyClass>();

    // // std::unique_ptr<MyClass,std::function<void(MyClass*)>> tmp(raw_ptr,customDeleter);
    // std::unique_ptr<MyClass,std::function<void(MyClass*)>> tmp(raw_ptr,midas::CXLAllocator::free<MyClass>);
    // std::unique_ptr<MyClass, std::function<void(MyClass*)>> p3 = midas::CXLAllocator::make_unique<MyClass>();
    // std::cout << "start move" << std::endl;
    // // p_origin = std::move(tmp); // move本身不会触发删除器
    // p2 = midas::CXLAllocator::make_unique<MyClass>(10,20);
    // std::cout << "move over" << std::endl;
    

    // unique_ptr.reset(static_cast<MyClass*>(raw_ptr),deleter);

    // std::unique_ptr<MyClass> tmp_ptr(raw_ptr,deleter);
    // unique_ptr = std::move(tmp_ptr);
    // unique_ptr.get()->print();
    // raw_ptr->print();
    // p1.get()->print();
    // p2.get()->print();
    // void* ptr2 = allocator.malloc(SMDK_MEM_EXMEM,sizeof(MyClass)); // 如果是这样声明的就是没有被释放掉
    allocator.stats_print('M');
    // return allocator;
}

int main()
{
    std::cout << "This is a test for cxl alloc" << std::endl;
    smdk_memtype_t type = SMDK_MEM_NORMAL;
    test_smart_pointer();
    // std::cout << "over" << std::endl;
    std::cout << "test over" << std::endl;
    allocator.stats_print('M');
    return 0;
}