CXXFLAGS += -std=c++1z -O2
# CXXFLAGS += -Wall -Wextra
# CXXFLAGS += -g -O0
CXX = /mnt/ssd/yifan/tools/gcc10/bin/g++
LDXX = /mnt/ssd/yifan/tools/gcc10/bin/g++

INC += -Iinc
INC += -I/mnt/ssd/yifan/tools/boost_1_79_0

override LDFLAGS += -lrt -lpthread

lib_src = $(wildcard src/*.cpp)
lib_src := $(filter-out $(wildcard src/*main.cpp),$(lib_src))
lib_obj = $(lib_src:.cpp=.o)

test_src = $(wildcard test/test_*.cpp)
# test_src := $(filter-out $(wildcard test/boost*.cpp),$(test_src))
test_target = $(test_src:.cpp=)

daemon_src = $(wildcard daemon/*.cpp)
daemon_obj = $(daemon_src:.cpp=.o)

src = $(lib_src) $(test_src) $(daemon_src)
obj = $(src:.cpp=.o)
dep = $(obj:.o=.d)

daemon_main_src = $(daemon_src)
daemon_main_obj = $(daemon_obj)
test_resource_manager_src = test/test_resource_manager.cpp
test_resource_manager_obj = $(test_resource_manager_src:.cpp=.o)
test_object_src = test/test_object.cpp
test_object_obj = $(test_object_src:.cpp=.o)
test_slab_src = test/test_slab.cpp
test_slab_obj = $(test_slab_src:.cpp=.o)
test_sync_hashmap_src = test/test_sync_hashmap.cpp
test_sync_hashmap_obj = $(test_sync_hashmap_src:.cpp=.o)
test_hashmap_clear_src = test/test_hashmap_clear.cpp
test_hashmap_clear_obj = $(test_hashmap_clear_src:.cpp=.o)
test_log_src = test/test_log.cpp
test_log_obj = $(test_log_src:.cpp=.o)
test_large_alloc_src = test/test_large_alloc.cpp
test_large_alloc_obj = $(test_large_alloc_src:.cpp=.o)
test_parallel_evacuator_src = test/test_parallel_evacuator.cpp
test_parallel_evacuator_obj = $(test_parallel_evacuator_src:.cpp=.o)
test_concurrent_evacuator_src = test/test_concurrent_evacuator.cpp
test_concurrent_evacuator_obj = $(test_concurrent_evacuator_src:.cpp=.o)
test_concurrent_evacuator2_src = test/test_concurrent_evacuator2.cpp
test_concurrent_evacuator2_obj = $(test_concurrent_evacuator2_src:.cpp=.o)
test_concurrent_evacuator3_src = test/test_concurrent_evacuator3.cpp
test_concurrent_evacuator3_obj = $(test_concurrent_evacuator3_src:.cpp=.o)
test_skewed_hashmap_src = test/test_skewed_hashmap.cpp
test_skewed_hashmap_obj = $(test_skewed_hashmap_src:.cpp=.o)

.PHONY: all clean

all: bin/daemon_main bin/test_resource_manager bin/test_object bin/test_parallel_evacuator \
	bin/test_log bin/test_large_alloc \
	bin/test_sync_hashmap bin/test_hashmap_clear \
	bin/test_concurrent_evacuator bin/test_concurrent_evacuator2 bin/test_concurrent_evacuator3 \
	bin/test_skewed_hashmap


bin/daemon_main: $(daemon_main_obj)
	$(LDXX) -o $@ $^ $(LDFLAGS)

bin/test_resource_manager: $(test_resource_manager_obj) $(lib_obj)
	$(LDXX) -o $@ $^ $(LDFLAGS)

bin/test_object: $(test_object_obj) $(lib_obj)
	$(LDXX) -o $@ $^ $(LDFLAGS)

bin/test_slab: $(test_slab_obj) $(lib_obj)
	$(LDXX) -o $@ $^ $(LDFLAGS)

bin/test_sync_hashmap: $(test_sync_hashmap_obj) $(lib_obj)
	$(LDXX) -o $@ $^ $(LDFLAGS)

bin/test_hashmap_clear: $(test_hashmap_clear_obj) $(lib_obj)
	$(LDXX) -o $@ $^ $(LDFLAGS)

bin/test_log: $(test_log_obj) $(lib_obj)
	$(LDXX) -o $@ $^ $(LDFLAGS)

bin/test_large_alloc: $(test_large_alloc_obj) $(lib_obj)
	$(LDXX) -o $@ $^ $(LDFLAGS)

bin/test_parallel_evacuator: $(test_parallel_evacuator_obj) $(lib_obj)
	$(LDXX) -o $@ $^ $(LDFLAGS)

bin/test_concurrent_evacuator: $(test_concurrent_evacuator_obj) $(lib_obj)
	$(LDXX) -o $@ $^ $(LDFLAGS)

bin/test_concurrent_evacuator2: $(test_concurrent_evacuator2_obj) $(lib_obj)
	$(LDXX) -o $@ $^ $(LDFLAGS)

bin/test_concurrent_evacuator3: $(test_concurrent_evacuator3_obj) $(lib_obj)
	$(LDXX) -o $@ $^ $(LDFLAGS)

bin/test_skewed_hashmap: $(test_skewed_hashmap_obj) $(lib_obj)
	$(LDXX) -o $@ $^ $(LDFLAGS)

%.o: %.cpp Makefile
	$(CXX) $(CXXFLAGS) $(INC) -MMD -MP -c $< -o $@

ifneq ($(MAKECMDGOALS),clean)
-include $(dep)
endif

clean:
	$(RM) $(dep) $(obj) bin/*
