#include <sys/epoll.h>
#include "tty_handler.h"
#include "rc_utils.h"

#define OFFSET(structure, member) ((int64_t)&((structure *)0)->member)

static int epollFd;
static const int EPOLL_SIZE_HINT = 8;
static const int EPOLL_MAX_EVENTS = 16;
static const char header[] = {'S', 'K', 'Y', 'D', 'R', 'O', 'I', 'D', ':', '\0'};

typedef struct {
    uint8_t header[9];
    uint8_t function;
    uint8_t length;
} header_t;

typedef struct {
    uint8_t bcc;
} checksum_t;

typedef struct {
    header_t header;
    uint8_t data[0xff];
    checksum_t sum;
} package_t;

#define OFF_IN_HEADER(member) OFFSET(header_t, member)

TTYHandler::TTYHandler(struct gnd_service_config *config)
: Handler(config)
{
}

TTYHandler::~TTYHandler()
{
    if (mFd > 0)
        close(mFd);
}

int TTYHandler::initialize()
{
    if (mSender->openSocket(mConfig->ip, mConfig->port) < 0) {
        ALOGE("open socket failed, ip: %s.", mConfig->ip);
        return -1;
    }

    mFd = tty_port_init(mConfig->sbus_ports[0]);
    if (mFd < 0) {
        ALOGE("Open serial port %s failed.", mConfig->sbus_ports[0]);
        return mFd;
    }

    startPollThread();

    return 0;
}

void TTYHandler::startPollThread()
{
    pthread_t thread;
    if (pthread_create(&thread, NULL, pollThreadFunc, (void *)this)) {
        ALOGE("Failed to create thread to read tty.");
        return;
    }
}

void *TTYHandler::pollThreadFunc(void *arg)
{
    TTYHandler *handler = (TTYHandler *)arg;

    epollFd = epoll_create(EPOLL_SIZE_HINT);
    if (epollFd < 0) {
        ALOGE("Could not create epoll fd: %s, end poll thread.", strerror(errno));
        return NULL;
    }

    epoll_data_t data;
    data.fd = handler->mFd;
    add_epoll_fd(epollFd, handler->mFd, data);

    int eventCount;
    struct epoll_event eventItems[EPOLL_MAX_EVENTS];
    uint8_t buffer[1024];
    uint8_t tty_data[sizeof(package_t)];
    int count;
    uint8_t length = 0;
    unsigned int total_size = 0, bytes_to_copy = 0;
    bool header_found = false;

    ALOGD("Entering tty epoll loop.");
    while (1) {
        eventCount = epoll_wait(epollFd, eventItems, EPOLL_MAX_EVENTS, -1);
        for (int i = 0; i < eventCount; i++) {
            const struct epoll_event& eventItem = eventItems[i];

            if (eventItem.events & EPOLLIN) {
                count = read(handler->mFd, buffer, 1024);
                int pos = 0;
                while (pos < count) {
                    /*
                     * 1.copy header
                     * 2.copy data+sum
                     * 3.process one package
                     */
                    if (bytes_to_copy == 0) {
                        if (!header_found) {/*find next package header*/
                            if (buffer[pos] == header[0]) {/* find start byte of package header */
                                bytes_to_copy = sizeof(header_t);
                            } else {
                                pos++;
                            }
                        }
                    } else {
                        int bytes_left = count - pos;
                        if (bytes_left >= (int)bytes_to_copy) {
                            memcpy(tty_data + total_size, buffer + pos, bytes_to_copy);
                            pos += bytes_to_copy;
                            total_size += bytes_to_copy;

                            if (!header_found) {
                                if (strncmp((char *)tty_data, header, sizeof(header) - 1) == 0) {
                                    header_found = true;
                                    length = tty_data[OFF_IN_HEADER(length)];
                                    bytes_to_copy = length + sizeof(checksum_t);/* data + sum */
                                } else {
                                    bytes_to_copy = 0;
                                    total_size = 0;
                                }
                            } else {
                                /* got a complete package */
                                handler->processPackage(tty_data, total_size);

                                pos += bytes_to_copy;
                                length = 0;
                                bytes_to_copy = 0;
                                total_size = 0;
                                header_found = false;
                            }
                        } else {
                            memcpy(tty_data + total_size, buffer + pos, bytes_left);
                            bytes_to_copy -= bytes_left;
                            total_size += bytes_left;
                            pos += bytes_left;
                        }
                    }
                }
            }
        }
    }

    close(epollFd);
    return NULL;
}

int TTYHandler::processPackage(uint8_t buffer[], unsigned int size)
{
    package_t pkg;
    struct rc_msg msg;
    uint8_t length;
    uint16_t values[16];
    int i, j;

    if (bcc_sum(buffer, size - sizeof(checksum_t)) != buffer[size - sizeof(checksum_t)]) {
        ALOGE("Checksum not matched, ignore this package.");
        return -1;
    }

    memcpy(&pkg.header, buffer, sizeof(header_t));
    length = pkg.header.length;
    memcpy(pkg.data, buffer + sizeof(header_t), length);

    if (pkg.header.function == 0xb1) {
        for (i = 0, j = 0; i < length && j < 16; i += 2, j++) {
            values[j] = (pkg.data[i] << 8) | (pkg.data[i + 1]);
        }
        pack_rc_msg(0, values, &msg);
        mSender->sendMessage(&msg);
    } else {
        //todo
    }

    return 0;
}
