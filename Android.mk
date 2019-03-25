LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_CLANG := true

LOCAL_SRC_FILES := main.cpp \
        air_service.cpp \
        gnd_service.cpp \
        config_loader.cpp \
        event_handler.cpp \
        key_config_manager.cpp \
        joystick_config_manager.cpp \
        message_sender.cpp

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
