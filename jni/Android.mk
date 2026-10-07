LOCAL_PATH := $(call my-dir)

CORE_DIR := $(LOCAL_PATH)/..

DEBUG                    := 0
FRONTEND_SUPPORTS_RGB565 := 1
NEED_BPP                 := 32
NEED_BLIP                := 1
IS_X86                   := 0
FLAGS                    :=
HAVE_GLES                ?= 1

ifeq ($(TARGET_ARCH),x86)
  IS_X86 := 1
endif

include $(CORE_DIR)/Makefile.common

COREFLAGS := -funroll-loops $(INCFLAGS) -DMEDNAFEN_VERSION=\"0.9.26\" -DMEDNAFEN_VERSION_NUMERIC=926 -D__LIBRETRO__ -DHAVE_VR -DHAVE_OPENGLES2 -DINLINE="inline" $(FLAGS)
COREFLAGS += -DWANT_VB_EMU

GIT_VERSION := " $(shell git rev-parse --short HEAD || echo unknown)"
ifneq ($(GIT_VERSION)," unknown")
  COREFLAGS += -DGIT_VERSION=\"$(GIT_VERSION)\"
endif

include $(CLEAR_VARS)
LOCAL_MODULE       := retro
LOCAL_SRC_FILES    := $(SOURCES_CXX) $(SOURCES_C)
LOCAL_CFLAGS       := $(COREFLAGS)
LOCAL_CXXFLAGS     := $(COREFLAGS)
LOCAL_LDFLAGS      := -Wl,-version-script=$(CORE_DIR)/link.T
ifeq ($(HAVE_GLES),1)
  LOCAL_LDLIBS += -lGLESv2
endif
LOCAL_CPP_FEATURES := exceptions
include $(BUILD_SHARED_LIBRARY)
