# $Id: Makefile, Broadcom SAI $
# $Copyright: Copyright 2018-2018 Broadcom Corporation.
# This program is the proprietary software of Broadcom Corporation
# and/or its licensors, and may only be used, duplicated, modified
# or distributed pursuant to the terms and conditions of a separate,
# written license agreement executed between you and Broadcom
# (an "Authorized License").  Except as set forth in an Authorized
# License, Broadcom grants no license (express or implied), right
# to use, or waiver of any kind with respect to the Software, and
# Broadcom expressly reserves all rights in and to the Software
# and all intellectual property rights therein.  IF YOU HAVE
# NO AUTHORIZED LICENSE, THEN YOU HAVE NO RIGHT TO USE THIS SOFTWARE
# IN ANY WAY, AND SHOULD IMMEDIATELY NOTIFY BROADCOM AND DISCONTINUE
# ALL USE OF THE SOFTWARE.
#
# Except as expressly set forth in the Authorized License,
#
# 1.     This program, including its structure, sequence and organization,
# constitutes the valuable trade secrets of Broadcom, and you shall use
# all reasonable efforts to protect the confidentiality thereof,
# and to use this information only in connection with your use of
# Broadcom integrated circuit products.
#
# 2.     TO THE MAXIMUM EXTENT PERMITTED BY LAW, THE SOFTWARE IS
# PROVIDED "AS IS" AND WITH ALL FAULTS AND BROADCOM MAKES NO PROMISES,
# REPRESENTATIONS OR WARRANTIES, EITHER EXPRESS, IMPLIED, STATUTORY,
# OR OTHERWISE, WITH RESPECT TO THE SOFTWARE.  BROADCOM SPECIFICALLY
# DISCLAIMS ANY AND ALL IMPLIED WARRANTIES OF TITLE, MERCHANTABILITY,
# NONINFRINGEMENT, FITNESS FOR A PARTICULAR PURPOSE, LACK OF VIRUSES,
# ACCURACY OR COMPLETENESS, QUIET ENJOYMENT, QUIET POSSESSION OR
# CORRESPONDENCE TO DESCRIPTION. YOU ASSUME THE ENTIRE RISK ARISING
# OUT OF USE OR PERFORMANCE OF THE SOFTWARE.
#
# 3.     TO THE MAXIMUM EXTENT PERMITTED BY LAW, IN NO EVENT SHALL
# BROADCOM OR ITS LICENSORS BE LIABLE FOR (i) CONSEQUENTIAL,
# INCIDENTAL, SPECIAL, INDIRECT, OR EXEMPLARY DAMAGES WHATSOEVER
# ARISING OUT OF OR IN ANY WAY RELATING TO YOUR USE OF OR INABILITY
# TO USE THE SOFTWARE EVEN IF BROADCOM HAS BEEN ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGES; OR (ii) ANY AMOUNT IN EXCESS OF
# THE AMOUNT ACTUALLY PAID FOR THE SOFTWARE ITSELF OR USD 1.00,
# WHICHEVER IS GREATER. THESE LIMITATIONS SHALL APPLY NOTWITHSTANDING
# ANY FAILURE OF ESSENTIAL PURPOSE OF ANY LIMITED REMEDY.$
#
# Make command configuration for SAI platform code.

core_files = $(notdir $(wildcard $(SAI_ROOT)/os/platform/common/*.c))
core_files += $(notdir $(wildcard $(PLATFORM_CODE)/*.c))

searchdirs = $(PLATFORM_CODE):$(realpath $(SAI_ROOT)/os/platform/):$(realpath $(SAI_ROOT)/os/platform/common):$(realpath $(SAI_ROOT)/driver/include):$(realpath $(SDK)/systems/bde/linux/include):$(realpath $(SDK)/systems/linux/kernel/modules/include)

vpath %.c $(searchdirs)
vpath %.h $(searchdirs)

ifneq ($(L7_PLATFORM), bcmsim)
override-target=unix-user
endif

include $(SDK_FLAGS_FILE)

export CFLAGS += -I$(SDK)/include $(SAI_CFLAGS) $(CFLAGS_SDK)
#
# Must include SDK in the path in order for platform code to get switch devids.
# No SDK APIs should be called by any code built by this make file.
#
export CPATH += $(searchdirs):$(realpath $(SDK)/include)

$(os_archive): $(core_files:.c=.o)
	$(RM) -f $@
	$(AR) cq $@ $^

#
# This rule builds the dependency files
#
%.d: %.c
	@set -e; $(RM) -f $@; \
	$(CC) -MM $(CFLAGS) $< > $@.$$$$; \
	$(SED) 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.$$$$ > $@; \
	$(RM) -f $@.$$$$

-include $(core_files:.c=.d)
