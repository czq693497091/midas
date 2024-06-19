#pragma once
#include"smdk_opt_api.hpp"
#include<memory>
#include<new>
namespace midas{

class CXLAllocator{
    public:
        template<typename T, typename... Args>
        static std::unique_ptr<T,std::function<void(T*)>> make_unique(Args&&... args);
        // static void stats_print(char unit);
        // static void print_memory_stats(char unit);

    private:
        template<typename T, typename... Args> 
        static T* alloc_with_params(Args&&... args);

        template<typename T> 
        static void free(T* ptr);
};




}
#include "./impl/cxl_op.ipp"