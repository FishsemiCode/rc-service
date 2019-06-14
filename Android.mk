LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_CLANG := true

LOCAL_C_INCLUDES += \
        $(LOCAL_PATH)/include

LOCAL_SRC_FILES := src/main.cpp \
        src/rc_utils.cpp \
        src/air_service.cpp \
        src/gnd_service.cpp \
        src/config_loader.cpp \
        src/handler.cpp \
        src/event_handler.cpp \
        src/key_config_manager.cpp \
        src/joystick_config_manager.cpp \
        src/message_sender.cpp \
        src/board_control.cpp \
        src/data_handler.cpp

LOCAL_MODULE := rc_service

LOCAL_STATIC_LIBRARIES := \
        libcutils \
        liblog \
        libutils \

LOCAL_CFLAGS := -DLOG_TAG=\"rc_service\"
LOCAL_CFLAGS += -Wunused-parameter

LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_PATH := $(TARGET_OUT_EXECUTABLES)

include $(BUILD_EXECUTABLE)
