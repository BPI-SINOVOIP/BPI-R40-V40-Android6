
include ../../config.mk
include ../../Makefile.inc


################################################################################
## set flags for golobal compile and link setting.
################################################################################

CONFIG_FOR_COMPILE = $(LOCAL_CFLAGS)
CONFIG_FOR_LINK = 


################################################################################

BuildPath      = ./build
ObjCommonPath     = $(BuildPath)/obj
OutputPath     = ./
DepCommonPath = $(BuildPath)/dep
Target         = $(OutputPath)/libcdx_stream.so   ## output target.

################################################################################
## set the source files, object files and dependency files
################################################################################
## set the source path to VPATH.
SourcePath = $(shell find ./ -type d)
SvnPath = $(shell find ./ -type d | grep ".svn")
RtspPath = $(shell find ./ -type d | grep "rtsp")
SourcePath := $(filter-out $(SvnPath) $(BuildPath), $(SourcePath))
VPATH := $(SourcePath)

## set the source files.
SourceFiles  = $(foreach dirTmp,$(SourcePath),$(shell find $(dirTmp) -maxdepth 1 -name "*.c"))
SourceFiles_cpp  = $(foreach dirTmp,$(SourcePath),$(shell find $(dirTmp) -maxdepth 1 -name "*.cpp"))

## set the object files.
ObjectFiles = $(patsubst ./%.c, $(ObjCommonPath)/%.o ,$(SourceFiles))
ObjectFiles_cpp = $(patsubst ./%.cpp, $(ObjCommonPath)/%.o ,$(SourceFiles_cpp))
ObjectPath := $(dir $(ObjectFiles))
ObjectPath += $(dir $(ObjectFiles_cpp))

## set the dependency files.
DependFiles = $(patsubst ./%.c, $(DepCommonPath)/%.d ,$(SourceFiles))
DependFiles_cpp = $(patsubst ./%.cpp, $(DepCommonPath)/%.d ,$(SourceFiles_cpp))
DependFilePath := $(dir $(DependFiles))
DependFilePath += $(dir $(DependFiles_cpp))

ifneq ($(BuildPath),wildcard($(BuildPath)))
a := $(shell mkdir -p $(BuildPath))
endif
ifneq ($(ObjectPath),wildcard($(ObjectPath)))
a := $(shell mkdir -p $(ObjectPath))
endif
ifneq ($(DependFilePath),wildcard($(DependFilePath)))
a := $(shell mkdir -p $(DependFilePath))
endif
################################################################################
## set flags for compile and link
################################################################################

## set the include path for compile flags.
SourceIncludePath = $(foreach dir,$(SourcePath),-I$(dir)) \
                    -I../BASE/include \
                    -I../../          \
                    -I../PARSER/include \
                    -I../../CODEC/VIDEO/DECODER/include/ \
                    -I../../CODEC/AUDIO/DECODER/include/ \
                    -I../../CODEC/SUBTITLE/DECODER/include \
                    -I../../PLAYER/ \
                    -I../../EXTERNAL/include/zlib/ \
                    -I../../EXTERNAL/include/openssl/ \
                    -I../../EXTERNAL/include/libxml \
                    -I../../EXTERNAL/include/zlib  \
                    -I../../EXTERNAL/include/live  \
                    -I../../EXTERNAL/include
                                       
                    
ifeq ($(CONFIG_CC),$(OPTION_CC_GNUEABIHF))
SourceIncludePath += -I../../EXTERNAL/build-by-arm-linux-gnueabihf/openssl/include/
endif
ifeq ($(CONFIG_CC),$(OPTION_CC_GNUEABI))
SourceIncludePath += -I../../EXTERNAL/build-by-arm-none-linux-gnueabi/openssl/include/
endif

## set compile flags
CompileFlags := $(CONFIG_FOR_COMPILE) $(SourceIncludePath) -O3 -fPIC -ldl
ifeq ($(CONFIG_CHIP),$(OPTION_CHIP_1663))
	CompileFlags += -mfpu=neon -march=armv5te
else ifeq ($(CONFIG_CHIP),$(OPTION_CHIP_1689))
	CompileFlags += -march=armv8-a
else ifeq ($(CONFIG_CHIP),$(OPTION_CHIP_1718))
	CompileFlags += -march=armv8-a 
else
	CompileFlags += -mfpu=neon -march=armv7-a
endif

ifeq ($(CONFIG_CC),$(OPTION_CC_GNUEABI))
CompileFlags += -mfloat-abi=softfp -mfpu=neon 
endif

## set link flags
LoadFlags = $(CONFIG_FOR_LINK) -ldl -shared -lpthread

ifeq ($(CONFIG_CC),$(OPTION_CC_GNUEABIHF))
LoadFlags += -L../../EXTERNAL/lib32/lgnueabihf
endif
ifeq ($(CONFIG_CC),$(OPTION_CC_GNUEABI))
LoadFlags +=-L../../EXTERNAL/lib32/lgnueabi
endif
ifeq ($(CONFIG_CC),$(OPTION_CC_AARCH64))
LoadFlags +=-L../../EXTERNAL/lib64/laarch64
endif
LoadFlags += -lcrypto -lssl -lxml2 -lz

LoadFlags += -L../BASE/
LoadFlags += -lcdx_base

#LoadFlags += -L../PARSER/
#LoadFlags += -lcdx_parser
#LoadFlags += -Wl,--no-undefined

################################################################################
## make commands, all/clean/cleanall
################################################################################

## define commands for make, sush as all, clean
.PHONY: all clean cleantarget cleanall
all:$(Target)

clean:
	-rm -rf $(ObjCommonPath)/*
	-rm -f $(Target)

cleanall: clean
	-rm -rf $(DepCommonPath)/*
	-rm -rf $(BuildPath)



################################################################################
## define target dependencies.
################################################################################
## compile source files to object files.
#$(ObjectPath)/%.o:%.c
#	$(CC) $(CompileFlags) -o $@ -c $<
$(ObjectFiles):$(ObjCommonPath)/%.o:%.c
	$(CC) $(CompileFlags) -o $@ -c $<

$(ObjectFiles_cpp):$(ObjCommonPath)/%.o:%.cpp
	$(CC) $(CompileFlags) -o $@ -c $<

## link object files to the target share library.
#$(Target):$(ObjectFiles) $(ObjectFiles_cpp)
$(Target):$(ObjectFiles)
	$(CC) -o $@ $^ $(LoadFlags)

## set rules to generate .d files.
$(DependFiles):$(DepCommonPath)/%.d:%.c
	set -e; rm -f $@; \
	$(CC) -MM $(CompileFlags) $< > $@.$$$$; \
	sed 's,.*\.o[:]*,$(ObjCommonPath)\/$*\.o $@: ,g' < $@.$$$$ > $@; \
	rm -f $@.$$$$


$(DependFiles_cpp):$(DepCommonPath)/%.d:%.cpp
	set -e; rm -f $@; \
	$(CC) -MM $(CompileFlags) $< > $@.$$$$; \
	sed 's,.*\.o[:]*,$(ObjCommonPath)\/$*\.o $@: ,g' < $@.$$$$ > $@; \
	rm -f $@.$$$$
## include the .d files to set dependency rules.
ifneq ($(MAKECMDGOALS),clean)
ifneq ($(MAKECMDGOALS),cleantarget)
ifneq ($(MAKECMDGOALS),cleanall)
-include $(DependFiles)
endif
endif
endif


