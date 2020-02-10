#ifndef TTYHANDLER_H
#define TTYHANDLER_H

#include "service.h"
#include "handler.h"
#include "message_sender.h"

using namespace std;

class TTYHandler : public Handler
{
public:
    TTYHandler(struct gnd_service_config *config);
    ~TTYHandler();

    /* override */
    virtual int initialize();

private:
    static void *pollThreadFunc(void *arg);
    void startPollThread();
    int processPackage(uint8_t buffer[], unsigned int size);

    int mFd;
};

#endif
