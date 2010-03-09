/*
 * Copyright (C) 2008 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <linux/wireless.h>

#define LOG_TAG "SoftapController"
#include <cutils/log.h>

#include "SoftapController.h"
#include "sha1.h"

SoftapController::SoftapController() {
    mPid = 0;
    mSock = socket(AF_INET, SOCK_DGRAM, 0);
    if (mSock < 0)
        LOGE("Failed to open socket");
    memset(mIface, 0, sizeof(mIface));
}

SoftapController::~SoftapController() {
    if (mSock >= 0)
        close(mSock);
}

int SoftapController::getPrivFuncNum(char *iface, const char *fname) {
    struct iwreq wrq;
    struct iw_priv_args *priv_ptr;
    int i, ret;

    strncpy(wrq.ifr_name, iface, sizeof(wrq.ifr_name));
    wrq.u.data.pointer = mBuf;
    wrq.u.data.length = sizeof(mBuf) / sizeof(struct iw_priv_args);
    wrq.u.data.flags = 0;
    if ((ret = ioctl(mSock, SIOCGIWPRIV, &wrq)) < 0) {
        LOGE("SIOCGIPRIV failed: %d", ret);
        return ret;
    }
    priv_ptr = (struct iw_priv_args *)wrq.u.data.pointer;
    for(i=0;(i < wrq.u.data.length);i++) {
        if (strcmp(priv_ptr[i].name, fname) == 0)
            return priv_ptr[i].cmd;
    }
    return -1;
}

int SoftapController::startSoftap() {
    struct iwreq wrq;
    pid_t pid = 1;
    int fnum, ret = 0;

    LOGD("Softap start");
    if (mPid) {
        LOGE("Softap already started");
        errno = EBUSY;
        return -1;
    }
    if (mSock < 0) {
        LOGE("Failed to open socket");
        return -1;
    }
#if 0
   if ((pid = fork()) < 0) {
        LOGE("fork failed (%s)", strerror(errno));
        return -1;
    }
#endif
    /* system("iwpriv wl0.1 AP_BSS_START"); */
    if (!pid) {
        /* start hostapd */
        return ret;
    } else {
        fnum = getPrivFuncNum(mIface, "AP_BSS_START");
        if (fnum < 0) {
            LOGE("Softap start - function not supported");
            return -1;
        }
        strncpy(wrq.ifr_name, mIface, sizeof(wrq.ifr_name));
        wrq.u.data.length = 0;
        wrq.u.data.pointer = mBuf;
        wrq.u.data.flags = 0;
        ret = ioctl(mSock, fnum, &wrq);
        if (ret) {
            LOGE("Softap start - failed: %d", ret);
        }
        else {
           mPid = pid;
           LOGD("Softap start - Ok");
        }
    }
    return ret;

}

int SoftapController::stopSoftap() {
    struct iwreq wrq;
    int fnum, ret;

    if (mPid == 0) {
        LOGE("Softap already stopped");
        return 0;
    }
    if (mSock < 0) {
        LOGE("Softap stop - failed to open socket");
        return -1;
    }
    fnum = getPrivFuncNum(mIface, "WL_AP_STOP");
    if (fnum < 0) {
        LOGE("Softap stop - function not supported");
        return -1;
    }
    strncpy(wrq.ifr_name, mIface, sizeof(wrq.ifr_name));
    wrq.u.data.length = 0;
    wrq.u.data.pointer = mBuf;
    wrq.u.data.flags = 0;
    ret = ioctl(mSock, fnum, &wrq);
#if 0
    LOGD("Stopping Softap service");
    kill(mPid, SIGTERM);
    waitpid(mPid, NULL, 0);
#endif
    mPid = 0;
    LOGD("Softap service stopped: %d", ret);
    return ret;
}

bool SoftapController::isSoftapStarted() {
    return (mPid != 0 ? true : false);
}

int SoftapController::addParam(int pos, const char *cmd, const char *arg)
{
    if (pos < 0)
        return pos;
    if ((unsigned)(pos + strlen(cmd) + strlen(arg) + 1) >= sizeof(mBuf)) {
        LOGE("Command line is too big");
        return -1;
    }
    pos += sprintf(&mBuf[pos], "%s=%s,", cmd, arg);
    return pos;
}

/*
 * Arguments:
 *      argv[2] - wlan interface
 *      argv[3] - softap interface
 *      argv[4] - SSID
 *	argv[5] - Security
 *	argv[6] - Key
 *	argv[7] - Channel
 *	argv[8] - Preamble
 *	argv[9] - Max SCB
 */
int SoftapController::setSoftap(int argc, char *argv[]) {
    unsigned char psk[MAX_SHA1_LEN];
    char psk_str[2*MAX_SHA1_LEN+1];
    struct iwreq wrq;
    int fnum, ret, i = 0;
    char *ssid;

    if (mSock < 0) {
        LOGE("Softap set - failed to open socket");
        return -1;
    }
    if (argc < 4) {
        LOGE("Softap set - missing arguments");
        return -1;
    }

    fnum = getPrivFuncNum(argv[2], "WL_AP_CFG");
    if (fnum < 0) {
        LOGE("Softap set - function not supported");
        return -1;
    }

    strncpy(mIface, argv[3], sizeof(mIface));
    strncpy(wrq.ifr_name, argv[2], sizeof(wrq.ifr_name));

    /* Create command line */
    i = addParam(i, "ASCII_CMD", "AP_CFG");
    if (argc > 4) {
        ssid = argv[4];
    } else {
        ssid = (char *)"AndroidAP";
    }
    i = addParam(i, "SSID", ssid);
    if (argc > 5) {
        i = addParam(i, "SEC", argv[5]);
    } else {
        i = addParam(i, "SEC", "open");
    }
    if (argc > 6) {
        int j;
        pbkdf2_sha1(argv[6], ssid, strlen(ssid), 4096, psk, MAX_SHA1_LEN);
        for(j=0;(j < MAX_SHA1_LEN);j++) {
            sprintf(&psk_str[j<<1], "%02x", psk[j]);
        }
        psk_str[j<<1] = '\0';
        i = addParam(i, "KEY", psk_str);
    } else {
        i = addParam(i, "KEY", "12345678");
    }
    if (argc > 7) {
        i = addParam(i, "CHANNEL", argv[7]);
    } else {
        i = addParam(i, "CHANNEL", "6");
    }
    if (argc > 8) {
        i = addParam(i, "PREAMBLE", argv[8]);
    } else {
        i = addParam(i, "PREAMBLE", "0");
    }
    if (argc > 9) {
        i = addParam(i, "MAX_SCB", argv[9]);
    } else {
        i = addParam(i, "MAX_SCB", "8");
    }
    if ((i < 0) || ((unsigned)(i + 4) >= sizeof(mBuf))) {
        LOGE("Softap set - command is too big");
        return i;
    }
    sprintf(&mBuf[i], "END");

    wrq.u.data.length = strlen(mBuf) + 1;
    wrq.u.data.pointer = mBuf;
    wrq.u.data.flags = 0;
    /* system("iwpriv eth0 WL_AP_CFG ASCII_CMD=AP_CFG,SSID=\"AndroidAP\",SEC=\"open\",KEY=12345,CHANNEL=1,PREAMBLE=0,MAX_SCB=8,END"); */
    ret = ioctl(mSock, fnum, &wrq);
    if (ret) {
        LOGE("Softap set - failed: %d", ret);
    }
    else {
        LOGD("Softap set - Ok");
    }
    return ret;
}
