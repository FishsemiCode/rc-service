#include "service.h"

int main(int argc, char *argv[])
{
    int ret;
    if (argc < 2) {
        ALOGE("%s:the count of parameter is not enough\n", __FUNCTION__);
        return -EINVAL;
    }

    if (!strncmp("air", argv[1], 3) && argc >= 3) {
        ALOGI("starting air rc service\n");
        ret = air_main(argc - 2, &argv[2]);
        if (ret < 0) {
            ALOGI("start air rc service failed\n");
            return ret;
        }
    } else if (!strncmp("gnd", argv[1], 3)) {
        ALOGI("starting gnd rc service\n");
    } else {
        ALOGI("invalid parameter, please check !!!\n");
    }
    return 0;
}
