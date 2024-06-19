#include<iostream>
// #include<cstdlib>
#include<sys/types.h>
#include<sys/stat.h>
#include<fcntl.h>
// #include<fstream>
#include<sys/mman.h>

using namespace std;

#define CXL_DAX_DEV "/dev/dax0.0"
#define ARRAY_SIZE 100
static inline void *get_cxl_mm(size_t mmap_size){
    cout << mmap_size << endl;
    int dev_fd = open("/dev/dax0.0", O_RDWR);
    if(dev_fd <= 0){
        perror("file error\n");
        exit(-1);
    }
    void *buf = NULL;
    // buf = mmap(NULL,mmap_size,PROT_READ | PROT_WRITE,MAP_SHARED,dev_fd,0);
    // if(buf == MAP_FAILED){
    //     perror("mmap");
    //     printf("ERROR %d\n",errno);
    //     exit(-1);
    // }
    // fprintf(stdout,"CXL DEV is open\n");
    return buf;
}

int main(){
    int* buf = (int*) get_cxl_mm(ARRAY_SIZE);
    
    for(int i=0;i<ARRAY_SIZE;i++){
        buf[i] = i;
    }
    return 0;
}