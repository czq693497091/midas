#pragma once

#include "cxl_op.hpp"
namespace midas{

// void stats_print(char unit){
//     SmdkAllocator& allocator = SmdkAllocator::get_instance();
//     allocator.stats_print(unit);
// }

template<typename T, typename... Args> 
T* CXLAllocator::alloc_with_params(Args&&... args){
    SmdkAllocator& allocator = SmdkAllocator::get_instance();
    void* memory = allocator.malloc(SMDK_MEM_EXMEM, sizeof(T));
    if (!memory) throw std::bad_alloc(); // czq: 处理内存分配失败的情况
    T* obj = new (memory) T(std::forward<Args>(args)...); // czq: 使用 placement new 构造对象
    return obj;
}

template<typename T> 
void CXLAllocator::free(T* ptr){
    SmdkAllocator& allocator = SmdkAllocator::get_instance();
    allocator.free(SMDK_MEM_EXMEM, ptr);
}

// make_unique in cxl memory
template<typename T, typename... Args>
std::unique_ptr<T,std::function<void(T*)>> CXLAllocator::make_unique(Args&&... args){
    T* raw_ptr = alloc_with_params<T>(std::forward<Args>(args)...);
    auto deleter = [] (T* ptr) { CXLAllocator::free(ptr); };
    return std::unique_ptr<T, std::function<void(T*)>>(raw_ptr, deleter);
}


}
