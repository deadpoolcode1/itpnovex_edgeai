#
# Copyright 2019 Cypress Semiconductor Corporation
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

LIBNAME := pal

print-%  : ; @echo $* = $($*)

# Use VERBOSE=1 to get full output
ifneq ($(VERBOSE),1)
  QUIET:=@
else
  QUIET:=
endif

ifeq ($(OS),Windows_NT)
  HOST_OS := Win32
else ifeq ($(shell uname -s),Linux)
  HOST_OS := Linux
  ifeq ($(shell uname -p),x86_64)
    HOST_OS := $(HOST_OS)64
  else
    HOST_OS := $(HOST_OS)32
  endif
endif

ifeq ($(ARMCC),1)
TOOLCHAIN_NAME := ARMCC
endif

ifeq ($(IAR),1)
TOOLCHAIN_NAME := IAR
endif

#Set the default; Only win32 and arm_gnu supported for now
TOOLCHAIN_NAME ?= IAR

PRJ_DIR := .
INC_DIRS := . 
INC_DIRS += ../../../../NetXDuo/App/
INC_DIRS += ../../../../NetXDuo/Target/
INC_DIRS += ../../../../Core/Inc/
INC_DIRS += ../../../../AZURE_RTOS/App/
INC_DIRS += ../../../../../../../../../Drivers/STM32N6xx_HAL_Driver/Inc/
INC_DIRS += ../../../../../../../../../Drivers/STM32N6xx_HAL_Driver/Inc/Legacy/
INC_DIRS += ../../../../../../../../../Middlewares/ST/netxduo/addons/dhcp/
INC_DIRS += ../../../../../../../../../Middlewares/ST/netxduo/link/inc/
INC_DIRS += ../../../../../../../../../Middlewares/ST/netxduo/common/drivers/ethernet/
INC_DIRS += ../../../../../../../../../Middlewares/ST/threadx/common/inc/
INC_DIRS += ../../../../../../../../../Drivers/CMSIS/Device/ST/STM32N6xx/Include/
INC_DIRS += ../../../../../../../../../Middlewares/ST/netxduo/common/inc/
INC_DIRS += ../../../../../../../../../Middlewares/ST/netxduo/ports/cortex_m55/iar/inc/
INC_DIRS += ../../../../../../../../../Middlewares/ST/threadx/ports/cortex_m55/iar/inc/
INC_DIRS += ../../../../../../../../../Drivers/CMSIS/Include/
INC_DIRS += ../../../../../../../../../Drivers/CMSIS/Device/ST/STM32N6xx/Include/Templates/
INC_DIRS += ../../../../../../../../../Drivers/BSP/Components/lan8742/
INC_DIRS += ../../../../../../../../../Middlewares/ST/netxduo/tsn/inc/
INC_DIRS += ../../../../../../../../../Middlewares/ST/netxduo/addons/dns/
INC_DIRS += ../../../../../../../../../Middlewares/ST/netxduo/addons/auto_ip/
INC_DIRS += ../../../../../../../../../Middlewares/ST/netxduo/addons/dns/
INC_DIRS += ../../../../../../../../../Drivers/BSP/STM32N6570-DK/
INC_DIRS += $(PRJ_DIR)/../netxduo-network-interface-integration/include/
INC_DIRS += $(PRJ_DIR)/../wifi-connection-manager/include/
INC_DIRS += $(PRJ_DIR)/../connectivity-utilities/
INC_DIRS += $(PRJ_DIR)/../connectivity-utilities/cy_log/
INC_DIRS += $(PRJ_DIR)/../connectivity-utilities/cy_string/
INC_DIRS += $(PRJ_DIR)/../connectivity-utilities/JSON_parser/
INC_DIRS += $(PRJ_DIR)/../connectivity-utilities/linked_list/
INC_DIRS += $(PRJ_DIR)/../connectivity-utilities/network/
INC_DIRS += $(PRJ_DIR)/../whd-bsp-integration/
INC_DIRS += $(PRJ_DIR)/../whd-bsp-integration/COMPONENT_NETXDUO/
INC_DIRS += $(PRJ_DIR)/../pal/core-lib/include/
INC_DIRS += $(PRJ_DIR)/../pal/core-lib/include/
INC_DIRS += $(PRJ_DIR)/../pal/hal/include/
INC_DIRS += $(PRJ_DIR)/../pal/common/Inc/
INC_DIRS += $(PRJ_DIR)/../pal/targets/TARGET_STM32/Inc/
INC_DIRS += $(PRJ_DIR)/../pal/abstraction-rtos/include/
INC_DIRS += $(PRJ_DIR)/../pal/abstraction-rtos/include/COMPONENT_THREADX/
INC_DIRS += $(PRJ_DIR)/../pal/abstraction-rtos/source/COMPONENT_THREADX/
INC_DIRS += $(PRJ_DIR)/../wifi-host-driver/
INC_DIRS += $(PRJ_DIR)/../wifi-host-driver/COMPONENT_WIFI5/inc/
INC_DIRS += $(PRJ_DIR)/../wifi-host-driver/COMPONENT_WIFI5/src/
INC_DIRS += $(PRJ_DIR)/../wifi-host-driver/COMPONENT_WIFI5/src/include/
INC_DIRS += $(PRJ_DIR)/../wifi-host-driver/COMPONENT_WIFI5/src/bus_protocols/
INC_DIRS += $(PRJ_DIR)/../wifi-host-driver/COMPONENT_WIFI5/resources/resource_imp/
INC_DIRS += $(PRJ_DIR)/../wifi-connection-manager/include/
INC_DIRS += $(PRJ_DIR)/../wifi-connection-manager/source/COMPONENT_43022/
INC_DIRS += $(PRJ_DIR)/../secure-sockets/include/
INC_DIRS += $(PRJ_DIR)/../secure-sockets/source/
INC_DIRS += $(PRJ_DIR)/../command-console/include/
INC_DIRS += $(PRJ_DIR)/../command-console/source/command_console/
INC_DIRS += $(PRJ_DIR)/../command-console/source/iperf/include/
INC_DIRS += $(PRJ_DIR)/../command-console/source/iperf/rtos/
INC_DIRS += $(PRJ_DIR)/../command-console/source/iperf_utility/
INC_DIRS += $(PRJ_DIR)/../command-console/source/wifi_utility/

INC_DIRS += $(HAL_DIRS)

INC_FLAGS := $(addprefix -I,$(INC_DIRS))

LDFLAGS :=
ifeq ($(TOOLCHAIN_NAME),GCC)
COMPILER_FLAGS := -Wmisleading-indentation -Wnarrowing -Wsign-compare -fsigned-char -ffunction-sections -fdata-sections -fno-common -std=gnu11 -fdiagnostics-color -Werror  -DNDEBUG -ggdb -O2 -mthumb -mcpu=cortex-m4    -mlittle-endian
COMPILER_SPECIFIC_ARFLAGS_CREATE      := -rcs
LDFLAGS := -specs=nosys.specs -Wl,-Map,$(BUILD_FOLDER)/whd.map
DEP_FLAGS := -MMD
else ifeq ($(TOOLCHAIN_NAME),ARMCC)
COMPILER_FLAGS := --target=arm-arm-none-eabi -march=armv8-m.main -mfpu=fpv4-sp-d16 -Wall -Wnarrowing -Wsign-compare -fsigned-char -ffunction-sections -fdata-sections -fno-common -std=gnu11 -fdiagnostics-color -Werror  -DNDEBUG -ggdb -O2 -mthumb -mcpu=cortex-m4  -mlittle-endian  -fshort-enums
COMPILER_SPECIFIC_ARFLAGS_CREATE      := -rcs
DEP_FLAGS := -MMD
else ifeq ($(TOOLCHAIN_NAME),IAR)
COMPILER_FLAGS := --silent --char_is_signed -e --vla  --dlib_config full  -DNDEBUG --debug -Oh --cpu_mode thumb --cpu Cortex-M55  --endian little  --cmse -e --fpu=VFPv5_d16 -D STM32N657xx -D NX_INCLUDE_USER_DEFINE_FILE -D TX_INCLUDE_USER_DEFINE_FILE -D CY_USING_HAL -D CYBSP_WIFI_CAPABLE -D CY_NETWORK_DISABLE_DHCP_SERVER -D USE_HAL_DRIVER -D TX_SINGLE_MODE_NON_SECURE=1 -D "CY_STORAGE_WIFI_DATA=\".whd_fw\"" -D USE_NOR_MEMORY_MX66UW1G45G -D COMPONENT_NETXDUO -D BLHS_SUPPORT -D DM_43022C1 -D ULP_SUPPORT
## --require_prototypes
COMPILER_SPECIFIC_ARFLAGS_CREATE := --create -o
DEP_FLAGS := --dependencies=m +
LDFLAGS := --silent
endif

ifeq ($(TOOLCHAIN_NAME),GCC)
  CC := "$(TOOLCHAIN_PATH)arm-none-eabi-gcc"
  #LD := "$(TOOLCHAIN_PATH)arm-none-eabi-g++"
  AR := "$(TOOLCHAIN_PATH)arm-none-eabi-ar"
else ifeq ($(TOOLCHAIN_NAME),ARMCC)
  CC := $(TOOLCHAIN_PATH)armclang
  #LD := $(TOOLCHAIN_PATH)armlink
  AR := $(TOOLCHAIN_PATH)armar
else ifeq ($(TOOLCHAIN_NAME),IAR)
  CC := $(TOOLCHAIN_PATH)iccarm
  #LD := $(TOOLCHAIN_PATH)ilinkarm
  AR := $(TOOLCHAIN_PATH)iarchive
endif

BUILD_FOLDER := build
MKDIR = mkdir -p $1

rwildcard=$(wildcard $(addsuffix $2, $1)) $(foreach d,$(wildcard $(addsuffix *, $1)),$(call rwildcard,$d/,$2))
SRC_FOLDER := ./abstraction-rtos ./common ./targets 
SRC := $(filter %.c,$(call rwildcard,$(SRC_FOLDER),*.c))

OBJ := $(patsubst %.c,%.o, $(SRC))
OBJS = $(addprefix $(BUILD_FOLDER)/,$(OBJ))

CFLAGS := $(COMPILER_FLAGS) $(DEP_FLAGS) $(INC_FLAGS) $(EXTRA_FLAGS)

###################### Set the make variable ###########################################
$(BUILD_FOLDER)/lib$(LIBNAME).a : $(OBJS)
	@echo Archiving $@
	$(QUIET)$(AR) $(COMPILER_SPECIFIC_ARFLAGS_CREATE) $(BUILD_FOLDER)/lib$(LIBNAME).a $(OBJS)

$(BUILD_FOLDER)/%.o : %.c
	@echo Compiling $<
	$(QUIET)$(call MKDIR, $(@D))
	@echo $(CC) $(CFLAGS) -o $@ -c $<  > $@.opts
	$(QUIET)$(CC) $(CFLAGS) -o $@ -c $<

DEPS := $(OBJS:%.o=%.d)
-include $(DEPS)

clean :
	@echo Removing build/
	-$(QUIET)$(RM) -r build/

.PHONY: all clean
