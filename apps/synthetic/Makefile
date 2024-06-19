SMDK_ROOT_DIR := /home/czq/SMDK
CXLMALLOC_DIR=$(SMDK_ROOT_DIR)/lib/smdk_allocator
PNMLIB_DIR=$(SMDK_ROOT_DIR)/lib/PNMLibrary-pnm-v3.0.0/PNMLibrary

CXX = g++
LDXX = g++
CXXFLAGS += -std=c++1z -O2
CXXFLAGS += -march=native # required for avx-enhanced rmemcpy
# CXXFLAGS += -g -O0
CXXFLAGS += -rdynamic
CXXFLAGS += -I$(CXLMALLOC_DIR)/opt_api/include -I$(SMDK_ROOT_DIR)/src/test/include -I$(PNMLIB_DIR)/build/libs/include

INC += -I../../inc
libmidas_lib := ../../lib/libmidas++.a -lrt -ldl
LIBS += $(libmidas_lib)

INC += -Iinc
LIBS += -pthread
LDFLAGS += -L$(CXLMALLOC_DIR)/lib/
LIBS += -lc -lnuma -lsmalloc -lpnm #for dynamic link

APP_BIN = synthetic
APP_SRC = main.cpp
APP_OBJ = $(APP_SRC:%.cpp=%.o)
APP_DEP = $(APP_OBJ:.o=.d)

all: $(APP_BIN)

$(APP_BIN) : $(APP_OBJ)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LIBS) $(LDFLAGS)

%.o : %.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(LIBS) $(INC) $(LDFLAGS) -MMD -MP -c $< -o $@

ifneq ($(MAKECMDGOALS),clean)
-include $(APP_DEP)
endif

.PHONY : clean
clean :
	-rm -rf $(APP_BIN) $(APP_OBJ) $(APP_DEP)