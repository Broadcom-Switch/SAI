#*********************************************************************
#
# (C) Copyright Broadcom Corporation 2013-2014
#
#  Licensed under the Apache License, Version 2.0 (the "License");
#  you may not use this file except in compliance with the License.
#  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.
#
#*********************************************************************

#path and prefix of toolchain
#

export AR      = $(CROSS_COMPILE)ar
export AS      = $(CROSS_COMPILE)as
export CC      = $(CROSS_COMPILE)gcc
export CPP     = $(CROSS_COMPILE)g++
export LD      = $(CROSS_COMPILE)ld
export NM      = $(CROSS_COMPILE)nm
export OBJCOPY = $(CROSS_COMPILE)objcopy
export OBJDUMP = $(CROSS_COMPILE)objdump
export RANLIB  = $(CROSS_COMPILE)ranlib
export STRIP   = $(CROSS_COMPILE)strip
export SED     = sed
export RM      = rm

#
# Other variables:
# 	SAI_CFLAGS   - CFLAGS that only apply to SAI (i.e., not the SDK)
# 	dmode            - for debugging.  The following are valid values:
#                               d  -- turn off optimization and enable debug symbols to be generated.
#                                     This includes setting the SDK variables DEBUG_SYMBOLS
#                                     and DEBUG_OPTIMIZE such that our settings in CFLAGS
#                                     are used and not overridden by SDK settings.
# 				i  -- turn on only POSIX IEEE Std 1003.2 APIs
# 				m  -- turn on some make debugging
# 				p  -- turn on profiling code generation for gprof tool
# 				r  -- turn on debugging for the rpcgen script
# 				vv -- echoes the SDK compiler invocation commands
#				   -- any value will cause some make variables to be displayed
#


ifneq ($(dmode),)
   rpcgen_debug = --verbose
   ifeq ($(findstring d,$(dmode)),d)
      SAI_CFLAGS += -ggdb2 -O0
      export DEBUG_OPTIMIZE = -ggdb2 -O0
      SAI_DEBUG = 1
      SAI_CFLAGS += -DSAI_DEBUG
   else
      SAI_CFLAGS += -O2
   endif
   ifeq ($(findstring i,$(dmode)),i)
      # for now, not turning on ansi -- too many warnings
      #SAI_CFLAGS += -ansi -pedantic -D_POSIX_SOURCE -D_POSIX_C_SOURCE=2
      # _XOPEN_SOURCE implies POSIX support (see features.h)
      SAI_CFLAGS += -D_XOPEN_SOURCE=700
   endif
   ifeq ($(findstring m,$(dmode)),m)
      export DEBUG_PARMS += --debug=v,j,m
      SAI_CFLAGS += -DSAI_DEBUG
   endif
   ifeq ($(findstring p,$(dmode)),p)
      export CFLAGS += -pg
      export LDFLAGS += -pg
   endif
   ifeq ($(findstring r,$(dmode)),r)
      export rpcgen_debug = --debug
   endif
   ifeq ($(findstring vv,$(dmode)),)
      export QUIET = 1
   endif
else
    ifeq ($(DEBIAN_PKG),1)
      ifeq ($(BUILD_CDP),)
        SAI_CFLAGS += -ggdb2
        SAI_DEBUG = 1
        SAI_CFLAGS += -O2
      else
        SAI_CFLAGS += -O2
        export QUIET = 1
      endif
    else
      SAI_CFLAGS += -O2
      export QUIET = 1
    endif
endif

export dmode

ifeq ($(findstring d,$(dmode)),d)
  STRIP_FLAGS    = --version
else
   ifeq ($(DEBIAN_PKG),1)
     ifeq ($(BUILD_CDP),)
        STRIP_FLAGS    = --version
      else
        STRIP_FLAGS    = --strip-unneeded
      endif
    else
      STRIP_FLAGS    = --strip-unneeded
    endif
endif

SAI_CFGFLAGS += -DBCM_WARM_BOOT_SUPPORT

#SAI_SDK_FEATURE_LIST and DISPATCH_LIST depends on the modules present in this package
SAI_SDK_FEATURE_LIST += L3
ifeq ($(BUILD_CDP),)
SAI_SDK_FEATURE_LIST += CINT
endif
export SAI_SDK_FEATURE_LIST

ifeq ($(PLATFORM),)
  ifneq ($(L7_PLATFORM),)
    PLATFORM = $(L7_PLATFORM)
  else
    PLATFORM = x86-xgs5-deb80
  endif
endif
export PLATFORM

DISPATCH_LIST = RPC ESW
#Add the SAI related features here(Eg: to enable Table list control, add TBL_LIMIT)
SAI_FEATURE_LIST = 

SAI_SDKDEFS  += $(foreach feature,$(SAI_SDK_FEATURE_LIST), -DINCLUDE_$(feature))
SAI_SDKDEFS  += $(foreach dispatch,$(DISPATCH_LIST), -DSAI_$(dispatch)_SUPPORT)
SAI_SDKDEFS  += $(foreach dispatch,$(DISPATCH_LIST), -DBCM_$(dispatch)_SUPPORT)
SAI_SDKFLAGS += ${SAI_SDKDEFS}

SAI_CFLAGS += ${SAI_SDKDEFS}
SAI_CFLAGS += ${SAI_CFGFLAGS}
export SAI_CFLAGS

DISPATCH_FLAGS = $(SAI_SDKFLAGS)

ifneq (,$(SAI_FEATURE_LIST))
SAI_FEATDEFS += $(foreach feature,$(SAI_FEATURE_LIST), -DSAI_FEAT_$(feature))
DISPATCH_FLAGS += $(SAI_FEATDEFS)
endif

ifeq ($(DEBIAN_PKG),1)
export SAI_ROOT =  $(SAI_BASE)
else
  ifeq ($(GIT_REPO),1)
export SAI_ROOT =  $(SAI_BASE)
  else
export SAI_ROOT =  $(SAI_BASE)/ocp_sai
  endif
-include $(SAI_ROOT)/build_automation.inf
endif

ifeq ($(SDK_SRC),)
   SDK_SRC:=$(abspath $(SAI_ROOT)/sdk-src/)
endif

## Include all chip defines from SDK
include $(SDK_SRC)/$(PLATFORM)-sdk_flags.cfg	

ifeq ($(PLATFORM),x86-xgs5-deb80)
CFLAGS += -DOPENNSL_PHY_ROUTINES
endif

ifeq ($(L7_TARGETOS_VARIANT),)
  L7_TARGETOS_VARIANT = debian_8.0
endif
export L7_TARGETOS_VARIANT

ifeq ($(L7_TARGETOS_VERSION),)
	L7_TARGETOS_VERSION = 4.9.0-7
#L7_TARGETOS_VERSION = 4.9.0-8
#L7_TARGETOS_VERSION = 3.16.0.5
#L7_TARGETOS_VERSION = 3.16.7-ck2
endif
export L7_TARGETOS_VERSION

ifeq ($(KERNEL_TARGET_DIR),)
  KERNEL_TARGET_DIR = 3.16.0-5-amd64
endif

ifeq ($(L7_CPU),)
  L7_CPU = x86_64
endif
export L7_CPU

export KERNEL_PKG_SUBPATH=$(L7_TARGETOS_VARIANT)/$(L7_TARGETOS_VERSION)
export KERNEL_PREREQ=$(KERNEL_SRC)

ifeq ($(LIBRARIES_DIR),)
   LIBRARIES_DIR=$(abspath $(OUTPATH)/libraries)
endif
export LIBRARIES_DIR

ifeq ($(platform),)
   platform = x86-smp_generic_64-2_6
endif
export platform

BUILD_SDK_VARS=BUILD_KNET_CB=1
export BUILD_SDK_VARS

BUILD_SDK_CFLAGS= -Wno-unused-result -Wno-error=format -Wno-error=format-security
export BUILD_SDK_CFLAGS

ifeq ($(SDK_VER),)
   export SDK_VER=6.5.13
endif

ifeq ($(SDK_PATCH_VER),)
  export SDK_PATCH_VER=p1
endif

ifneq (,$(wildcard $(SDK_SRC)/sdk-$(SDK_VER)-$(SDK_PATCH_VER).patch))
  SDK_SAI_PATCH_FILE=$(abspath $(SDK_SRC)/sdk-$(SDK_VER)-$(SDK_PATCH_VER).patch)
endif

export SDK_GPL_FILENAME := sdk-$(SDK_VER)-gpl-modules

ifneq (,$(wildcard $(SDK_SRC)/$(SDK_GPL_FILENAME).patch))
   SDK_GPL_SAI_PATCH_FILE:=$(abspath $(SDK_SRC)/$(SDK_GPL_FILENAME).patch)
endif

export SDK_FLAGS_FILE=$(OUTPATH)/sdk/sdk_flags.cfg

ifeq ($(MAKE_LOCAL),)
  export MAKE_LOCAL=$(abspath $(SDK_SRC)/build_sdk/Make.local)
endif

export NO_LOCAL_TARGETS=1
ifeq ($(SDK),)
   export SDK=$(OUTPATH)sdk
endif

ifeq ($(SDK_GPL),)
   export SDK_GPL:=$(OUTPATH)sdk-$(SDK_VER)-gpl-modules
endif

ifeq ($(DEBIAN_PKG),)
ifeq ($(KERNEL_SRC),)
	KERNEL_SRC=$(OUTPATH)/headers/usr/src/linux-headers-4.9.0-7-common
# KERNEL_SRC=$(OUTPATH)/headers/usr/src/linux-headers-4.9.0-8-common
# KERNEL_SRC=$(OUTPATH)/headers/usr/src/linux-headers-3.16.7-ckt7
# KERNEL_SRC=$(OUTPATH)/headers/usr/src/linux-headers-3.16.0-5-common
endif
else
ifeq ($(KERNEL_SRC),)
  $(error Please define KERNEL_SRC to point to local kernel headers)
endif
endif

export KERNEL_SRC

ifeq ($(KERNDIR),)
  KERNDIR = $(KERNEL_SRC)
endif
export KERNDIR

ifeq ($(PLATFORM_CODE),)
  PLATFORM_CODE = $(abspath $(SAI_ROOT)/os/platform/$(PLATFORM))
endif
export PLATFORM_CODE

ifeq ($(DEBIAN_PKG),)
	CROSS_COMPILE = $(SAI_CROSS_COMPILE)
	SAI_SDK_SRC = $(SAI_BINARIES_CACHE_PATH)/sdk-source-binaries/sdk-$(SDK_VER)
	SAI_SDK_BINARY_FILE = $(SAI_BINARIES_CACHE_PATH)/sdk-build-binaries/$(PLATFORM)/sdk-$(SDK_VER)-$(SDK_PATCH_VER)-$(L7_TARGETOS_VARIANT)-$(L7_TARGETOS_VERSION)/sdk-lib-$(SDK_VER)-$(SDK_PATCH_VER)-$(L7_TARGETOS_VARIANT)-$(L7_TARGETOS_VERSION).tgz
ifeq ($(BUILD_SDK),)
  ifeq ("$(wildcard $(SAI_SDK_BINARY_FILE))","")
    BUILD_SDK=1
  endif
endif
else
  BUILD_SDK=1
ifeq ($(SAI_SDK_SRC),)
  SAI_SDK_SRC=$(SDK_SRC)
endif
endif

export SDK_ROOT = $(SDK)

ifeq ($(CROSS_COMPILE),)
   CROSS_COMPILE = /usr/bin/
endif
export CROSS_COMPILE



INCLUDE_FLAGS :=
INCLUDE_FLAGS += -I$(SDK_ROOT)/include
ifneq ($(NO_POOL_CONFIG),1)
	CFLAGS += -DTH2_CPU_POOL_SETUP
endif
INCLUDE_FLAGS += -I$(SAI_ROOT)/include -I$(SAI_ROOT)/include/sai -I$(SAI_ROOT)/driver/include -DINCLUDE_L3 -DSAI_ONLY
CFLAGS += -Wno-unused-result -Wno-error=format -Wno-error=format-security -Wno-unused-but-set-variable -g -O0
CFLAGS += -fno-common -fno-strict-aliasing -funit-at-a-time -msoft-float -Wall 
CFLAGS += -DSTATIC=static -DLOG_TEST -DLOG_SAI -D_GNU_SOURCE -fPIC -ldl -rdynamic $(INCLUDE_FLAGS)
CFLAGS += -DPRINT_TO_SYSLOG -D_SHR_PBMP_WIDTH=256
CFLAGS += $(CHIPS) -DINCLUDE_DIAG_SHELL
CFLAGS += -D_DEFAULT_SOURCE
export CFLAGS

CPPFLAGS += -std=c++11 -fPIC
CPPFLAGS += -ggdb2
CPPFLAGS += -O2
export CPPFLAGS

# Fixit
VERSION := 1
RELEASE_NUMBER := 0
FILE=$(SAI_ROOT)/RELEASE
SAI_RELEASE=`head -n 1 $(FILE)`

#
SAI_BIN := $(OUTPATH)bin
SAI_OBJS := $(OUTPATH)objects
SAI_OS_OBJS := $(OUTPATH)os
SAI_DRIVER_OBJS := $(OUTPATH)saidriver

SAI_KERNEL_DIR := $(OUTPATH)headers
SAI_SDK := $(OUTPATH)sdk
SAI_GPL_DIR := $(OUTPATH)sdk-gpl
sai_src_dir:=$(SAI_ROOT)/src

export saidriver_archive = $(abspath $(LIBRARIES_DIR)/saidriver.a)
export os_archive = $(abspath $(LIBRARIES_DIR)/os.a)

vpath %.c $(realpath $(sai_src_dir)) 
vpath %.cc $(realpath $(sai_src_dir))
vpath %.o $(SAI_OBJS)/ 
#vpath %.o ./objects/ 
sai_source_files:= $(wildcard $(sai_src_dir)/brcm_*.c)
sai_cpp_source_files:= $(wildcard $(sai_src_dir)/brcm_*.cc)
sai_so_library := libsai.so
sai_archive := $(SAI_BIN)/libsai.a
sai_soname := $(sai_so_library).$(VERSION)
## Full name without directory
ifeq ($(BUILD_CDP),1)
sai_so_fullname_cdp:= cdp-$(sai_soname).$(RELEASE_NUMBER)
export VISIBILITY_FLAGS := -fvisibility=hidden
endif
sai_so_fullname:= $(sai_soname).$(RELEASE_NUMBER)
sai_libname:= sai

objects := $(addprefix $(SAI_OBJS)/, $(notdir $(sai_source_files:.c=.o)))
cpp_objects := $(addprefix $(SAI_OBJS)/, $(notdir $(sai_cpp_source_files:.cc=.o)))

.DEFAULT_GOAL := all

all: $(sai_so_fullname_cdp) $(sai_so_fullname)

$(objects):$(SAI_OBJS)/%.o : %.c | sdk  
	mkdir -p $(SAI_OBJS)
	$(CC) $(INCLUDE_FLAGS) $(CFLAGS_SAI_SDK) $(CFLAGS) $(SAI_CFLAGS) -c $^ -o $@ 

$(cpp_objects):$(SAI_OBJS)/%.o : %.cc | sdk
	mkdir -p $(SAI_OBJS)
	$(CPP) $(INCLUDE_FLAGS) $(CFLAGS_SAI_SDK) $(CPPFLAGS) $(SAI_CFLAGS) -c $^ -o $@


kernel-extract:
ifeq ($(DEBIAN_PKG),)
	mkdir -p $(SAI_KERNEL_DIR)
	@echo ""
	@echo "Extracting kernel headers ... please wait for completion."
	@echo ""
	$(SDK_SRC)/build_sdk/ebc -o build -c $(L7_CPU) -t $(L7_TARGETOS_VARIANT)
endif

sdk-extract: $(LIBRARIES_DIR) sdk-gpl
ifeq ($(wildcard $(SAI_SDK)/.*),)
	mkdir -p $(SAI_SDK)
	@echo ""
	@echo Extracting SDK   ... please wait for completion
	@echo ""
	tar -xzf  $(SAI_SDK_SRC)/sdk-all-*.tar.gz  -C $(SAI_SDK) --strip 1;
	@echo "SDK_SAI_PATCH_FILE=$(SDK_SAI_PATCH_FILE)"
ifneq (,$(SDK_SAI_PATCH_FILE))
	@echo Applying SAI SDK patch
	cd $(SAI_SDK); patch -p1 < $(SDK_SAI_PATCH_FILE)
endif
endif
ifneq ($(BUILD_SDK),1)
ifeq ($(wildcard $(LIBRARIES_DIR)/.*),)
	@echo ""
	@echo "Extracing pre-built SDK binaries... please wait for completion."
	@echo ""
	tar -xzf  $(SAI_SDK_BINARY_FILE) -C $(LIBRARIES_DIR) --strip 1;
endif
endif

saidriver: CFLAGS += $(DYNLIB_FLAGS) $(VISIBILITY_FLAGS)
saidriver: sdk
	@echo Making SAI platform driver
	mkdir -p $(abspath $(OUTPATH)/$@)
	mkdir -p $(SAI_OBJS)
	@echo $(SAI_ROOT)
	$(MAKE) $(DEBUG_PARMS) -C $(abspath $(OUTPATH)/$@) -f $(abspath $(SAI_ROOT)/driver/Makefile)

os: CFLAGS += $(DYNLIB_FLAGS) $(VISIBILITY_FLAGS)
os: sdk
	@echo Making SAI OS
	mkdir -p $(abspath $(OUTPATH)$@)
	$(MAKE) $(DEBUG_PARMS) -C $(abspath $(OUTPATH)$@) -f $(abspath $(SAI_ROOT)/os/Makefile)

$(SDK_FLAGS_FILE): $(add_targets) sdk-extract
ifeq ("$(wildcard $(SDK_FLAGS_FILE))","")
	@echo "Building SDK flags file"
	mkdir -p $(abspath $(OUTPATH)/sdk)
# Invoke SDK make system to create CFLAGS_SDK variable in $(SDK_FLAGS_FILE)
	$(MAKE) $(DEBUG_PARMS) -C $(SAI_SDK)/systems/linux/user/common/ get_sdk_flags
	cp -fp $(abspath $(SDK_FLAGS_FILE)) $(LIBRARIES_DIR)
endif

$(LIBRARIES_DIR):
	mkdir -p $@

sdk-gpl-extract: 
ifeq ($(BUILD_SDK),1)
ifeq ($(wildcard $(SDK_GPL)/.*),)
	@echo "Extracting SDK GPL tar ball"
	$(RM) -rf $(SDK_GPL)
	mkdir -p $(SDK_GPL)
	tar -xzf $(SAI_SDK_SRC)/$(SDK_GPL_FILENAME).tar.gz --strip 1 -C $(SDK_GPL) --exclude='*bmw*' --exclude='*jag*' --exclude='*nsx*' --exclude='*gtx*' --exclude='*raptor*'
ifneq (,$(SDK_GPL_SAI_PATCH_FILE))
	@echo Applying SAI SDK GPL patch
	cd $(SDK_GPL); patch -p1 < $(SDK_GPL_SAI_PATCH_FILE)
endif
endif
endif

sdk: kernel-extract sdk-gpl sdk-extract $(SDK_FLAGS_FILE)
	mkdir -p $(SAI_BIN)
ifeq ($(BUILD_SDK),1)
	@echo ""
	@echo "Copying SAI KNET callback implementation"
	@echo ""
	cp -f $(SDK_SRC)/knet-cb.c $(SAI_SDK)/systems/linux/kernel/modules/knet-cb/
	@echo ""
	@echo "Making SDK"
	@echo ""
	$(MAKE) $(DEBUG_PARMS) $(BUILD_SDK_VARS) -C $(SAI_SDK)/systems/linux/user/$(platform)/
#	cp -fp $(abspath $(SAI_SDK)/build/linux/user/$(platform)/*.ko) $(SAI_BIN)
#	$(STRIP) $(STRIP_FLAGS) $(abspath $(SAI_BIN)/*.ko)
#	cp -fp $(abspath $(SAI_SDK)/build/linux/user/$(platform)/*.ko) $(LIBRARIES_DIR)
#	$(STRIP) $(STRIP_FLAGS) $(abspath $(LIBRARIES_DIR)/*.ko)
	cp -fp $(abspath $(SAI_SDK)/build/unix-user/$(platform))/*.a $(LIBRARIES_DIR)
	cp -fp $(abspath $(SAI_SDK)/build/unix-user/x86-smp_generic_64-2_6/version.o) $(LIBRARIES_DIR)
else
	@echo ""
	@echo "Copying relevant pre-built SDK files"
	mkdir -p $(SAI_SDK)/build/unix-user/x86-smp_generic_64-2_6/
	cp -fp $(LIBRARIES_DIR)/version.o $(SAI_SDK)/build/unix-user/x86-smp_generic_64-2_6/.
	cp -fp $(LIBRARIES_DIR)/*.ko $(SAI_BIN)
	@echo "Done"
endif

ifeq ($(SAI_PRODUCT_FAMILY), dnx)
SDK_GPL_FLAGS += -DBCM_PETRA_SUPPORT
endif

sdk-gpl: CFLAGS += $(SDK_GPL_FLAGS)
sdk-gpl: export SDK=$(SDK_GPL)
sdk-gpl: export SDKBUILD:=$(shell $(SDK_SRC)/build_sdk/relativepath $(SDK_GPL) $(SAI_GPL_DIR))
sdk-gpl: $(LIBRARIES_DIR) sdk-gpl-extract kernel-extract
ifeq ($(BUILD_SDK),1)
	@echo ""
	@echo "Copying SAI KNET callback implementation"
	@echo ""
	cp -f $(SDK_SRC)/knet-cb.c $(SDK_GPL)/systems/linux/kernel/modules/knet-cb/
	@echo ""
	@echo Making SDK-GPL modules
	mkdir -p $(SAI_BIN)
	$(MAKE) $(DEBUG_PARMS) $(BUILD_SDK_VARS) -C $(SDK_GPL)/systems/linux/user/$(platform)/
	cp -fp $(abspath $(OUTPATH)/$@/linux/user/$(platform)/*.ko) $(SAI_BIN)
	$(STRIP) $(STRIP_FLAGS) $(abspath $(SAI_BIN)/*.ko)
	cp -fp $(abspath $(OUTPATH)/$@/linux/user/$(platform)/*.ko) $(LIBRARIES_DIR)
	$(STRIP) $(STRIP_FLAGS) $(abspath $(LIBRARIES_DIR)/*.ko)
endif

sdk-lib-publish: sdk
ifeq ($(wildcard $(SAI_BINARIES_CACHE_PATH)/sdk-build-binaries/$(PLATFORM)/sdk-$(SDK_VER)-$(SDK_PATCH_VER)-$(L7_TARGETOS_VARIANT)-$(L7_TARGETOS_VERSION)/.*),)
	@echo "Publishing SDK library to fastpath-binaries-cache..."
	mkdir -p $(SAI_BINARIES_CACHE_PATH)/sdk-build-binaries/$(PLATFORM); mkdir -p $(SAI_BINARIES_CACHE_PATH)/sdk-build-binaries/$(PLATFORM)/sdk-$(SDK_VER)-$(SDK_PATCH_VER)-$(L7_TARGETOS_VARIANT)-$(L7_TARGETOS_VERSION); tar -czf $(SAI_BINARIES_CACHE_PATH)/sdk-build-binaries/$(PLATFORM)/sdk-$(SDK_VER)-$(SDK_PATCH_VER)-$(L7_TARGETOS_VARIANT)-$(L7_TARGETOS_VERSION)/sdk-lib-$(SDK_VER)-$(SDK_PATCH_VER)-$(L7_TARGETOS_VARIANT)-$(L7_TARGETOS_VERSION).tgz -C $(OUTPATH) libraries
endif
  
$(sai_so_fullname_cdp): $(objects) saidriver os
	$(CC) $(CFLAGS) $(CFLAGS_SAI_SDK) -shared -Wl,-soname,$(sai_soname) -o $(SAI_BIN)/$(sai_so_fullname_cdp) $(objects) $(SAI_SDK)/build/unix-user/x86-smp_generic_64-2_6/version.o -Wl,--whole-archive $(LIBRARIES_DIR)/*.a -Wl,--no-whole-archive -lc -lpthread -lm -lrt
	$(STRIP) --strip-unneeded $(SAI_BIN)/$(sai_so_fullname_cdp)

ifeq ($(SAI_DEBUG),)
$(sai_so_fullname): $(objects) $(sai_so_fullname_cdp) saidriver os
	$(CC) $(CFLAGS) $(CFLAGS_SAI_SDK) -shared -Wl,-soname,$(sai_soname) -o $(SAI_BIN)/$(sai_so_fullname) $(objects) $(SAI_SDK)/build/unix-user/x86-smp_generic_64-2_6/version.o -Wl,--whole-archive $(LIBRARIES_DIR)/*.a -Wl,--no-whole-archive -lc -lpthread -lm -lrt
else
$(sai_so_fullname): $(objects) $(cpp_objects) $(sai_so_fullname_cdp) saidriver os
	$(CC) $(CFLAGS) $(CFLAGS_SAI_SDK) -shared -Wl,-soname,$(sai_soname) -o $(SAI_BIN)/$(sai_so_fullname) $(objects) $(cpp_objects) $(SAI_SDK)/build/unix-user/x86-smp_generic_64-2_6/version.o -Wl,--whole-archive $(LIBRARIES_DIR)/*.a -Wl,--no-whole-archive -lc -lpthread -lm -lrt
endif

ifeq ($(SAI_DEBUG),)
	$(STRIP) --strip-unneeded $(SAI_BIN)/$(sai_so_fullname)
endif

ifeq ($(SAI_BZ2),1)
	bzip2 -c $(SAI_BIN)/$(sai_so_fullname) > $(SAI_BIN)/$(sai_so_fullname).bz2
endif
ifeq ($(SAI_GZ),1)
	gzip -c $(SAI_BIN)/$(sai_so_fullname) > $(SAI_BIN)/$(sai_so_fullname).gz
endif

ifeq ($(DEBIAN_PKG),)
include $(SAI_BASE)/ocp_sai/test/Makefile
endif


sai_lib: $(sai_so_fullname)

clean:
	@echo "Cleaning SAI files"
	$(RM) -f $(SAI_BIN)/* 
	$(RM) -f $(SAI_OBJS)/* 
	$(RM) -rf $(SAI_OS_OBJS)
	$(RM) -rf $(SAI_DRIVER_OBJS)

cleanall:
	@echo "Cleaning SAI files"
	$(RM) -f $(SAI_BIN)/* 
	$(RM) -f $(SAI_OBJS)/* 
	$(RM) -rf $(SAI_KERNEL_DIR)
	$(RM) -rf $(SAI_SDK)
	$(RM) -rf $(SAI_OS_OBJS)
	$(RM) -rf $(SAI_DRIVER_OBJS)
	$(RM) -rf $(LIBRARIES_DIR)
	$(RM) -rf $(SDK_GPL)
	$(RM) -rf $(SAI_GPL_DIR)

install:
	/usr/bin/install -m 644 -D $(SAI_BIN)/$(sai_so_fullname) $(DESTDIR)/usr/lib/$(sai_so_fullname)
	/usr/bin/install -d $(DESTDIR)/lib/modules/$(KERNEL_TARGET_DIR)/extras
	/usr/bin/install -m 644 $(SAI_BIN)/*.ko $(DESTDIR)/lib/modules/$(KERNEL_TARGET_DIR)/extras
	ln -s /usr/lib/$(sai_so_fullname) $(DESTDIR)/usr/lib/$(sai_soname)

odp-pkg:
	@echo "Packaging SAI ODP release..."
	pushd $(OUTPATH);cd $(SAI_ROOT); cd ..; tar --transform='s,ocp_sai\/,,' --transform='s,^,brcm-sai-odp/,' -czf $(OUTPATH)/brcm-sai-$(SAI_RELEASE)-odp.tgz --exclude=ocp_sai/src/opennsl ocp_sai/Makefile ocp_sai/debian ocp_sai/systemd ocp_sai/RELEASE ocp_sai/src/ ocp_sai/data ocp_sai/include ocp_sai/doc output/$(PLATFORM)/Makefile output/$(PLATFORM)/package.cfg  ocp_sai/os ocp_sai/driver ocp_sai/sdk-src; popd

cdp-pkg:
	@echo "Packaging SAI CDP release..."
	cp $(SAI_BINARIES_CACHE_PATH)/sdk-source-binaries/sdk-$(SDK_VER)/* $(SDK_SRC)
	cp $(SAI_BINARIES_CACHE_PATH)/kernel-binaries/$(L7_TARGETOS_VARIANT)/$(L7_TARGETOS_VERSION)/* $(SDK_SRC)
	pushd $(OUTPATH);cd $(SAI_ROOT); cd ..; tar --transform='s,ocp_sai\/,,' --transform='s,^,brcm-sai-cdp/,' -czf $(OUTPATH)/brcm-sai-$(SAI_RELEASE)-cdp.tgz --exclude=ocp_sai/src/opennsl ocp_sai/Makefile ocp_sai/debian ocp_sai/systemd ocp_sai/RELEASE ocp_sai/src/ ocp_sai/data ocp_sai/include ocp_sai/doc output/$(PLATFORM)/Makefile output/$(PLATFORM)/package.cfg ocp_sai/os ocp_sai/driver ocp_sai/sdk-src; popd
	@echo "brcm-sai-$(SAI_RELEASE)-cdp.tgz created!"

help:
	@echo "Supported make options:"
	@echo "======================"
	@echo "dump_vars"
	@echo "clean     : removes all generated files"
	@echo "cleanall  : removes all generated files including release tarballs"
	@echo "doc       : requires cd to include directory to generate API guide pdf"
	@echo "odp-pkg   : build odp based SAI package"
	@echo "cdp-pkg   : build cdp based SAI build package"

.SECONDEXPANSION:

#
# This rule builds the dependency files
#
%.d: %.c
	set -e; $(RM) -f $@; \
	$(CC) -MM $(CFLAGS) $< > $@.$$$$; \
	$(SED) 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.$$$$ > $@; \
	$(RM) -f $@.$$$$

dump_vars:	
	@echo SAI_ROOT            = $(SAI_ROOT)
	@echo PLATFORM_CODE       = $(PLATFORM_CODE)
	@echo SDK_ROOT            = $(SDK_ROOT)
	@echo SDK_SRC             = $(SDK_SRC)
	@echo SAI_SDK_SRC         = $(SAI_SDK_SRC)
	@echo SDK_VER	          = $(SDK_VER)
	@echo SDK_PATCH_VER       = $(SDK_PATCH_VER)
	@echo KERNEL_SRC          = $(KERNEL_SRC)
	@echo targets             = $(targets)
	@echo ""
	@echo CFLAGS              = $(CFLAGS)
	@echo ""
	@echo CROSS_COMPILE       = $(CROSS_COMPILE)
	@echo ""
	@echo OUTPATH             = $(OUTPATH)
	@echo ""
	@echo SAI_OBJS            = $(SAI_OBJS)
	@echo ""
	@echo ""
	@echo sai_source_files    = $(sai_source_files)
	@echo sai_cpp_src_files   = $(sai_cpp_source_files)
	@echo ""
	@echo ""
	@echo objects             = $(objects)
	@echo cpp_objects         = $(cpp_objects)


.PHONY: dump_vars install os saidriver
