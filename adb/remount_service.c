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

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <unistd.h>
#include <cutils/properties.h>

#include "sysdeps.h"

#define  TRACE_TAG  TRACE_ADB
#include "adb.h"


static int system_ro = 1;
static int vendor_ro = 1;

/* Returns the device used to mount a directory in /proc/mounts */
static char *find_mount(const char *dir)
{
    int fd;
    int res;
    char *token = NULL;
    const char delims[] = "\n";
    char buf[4096];

    fd = unix_open("/proc/mounts", O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return NULL;

    buf[sizeof(buf) - 1] = '\0';
    adb_read(fd, buf, sizeof(buf) - 1);
    adb_close(fd);

    token = strtok(buf, delims);

    while (token) {
        char mount_dev[256];
        char mount_dir[256];
        int mount_freq;
        int mount_passno;

        res = sscanf(token, "%255s %255s %*s %*s %d %d\n",
                     mount_dev, mount_dir, &mount_freq, &mount_passno);
        mount_dev[255] = 0;
        mount_dir[255] = 0;
        if (res == 4 && (strcmp(dir, mount_dir) == 0))
            return strdup(mount_dev);

        token = strtok(NULL, delims);
    }
    return NULL;
}

static int hasVendorPartition()
{
    struct stat info;
    if (!lstat("/vendor", &info))
        if ((info.st_mode & S_IFMT) == S_IFDIR)
          return true;
    return false;
}

/* Init mounts /system as read only, remount to enable writes. */
static int remount(const char* dir, int* dir_ro)
{
    char *dev;
    int fd;
    int OFF = 0;

    if (dir_ro == 0) {
        return 0;
    }

    dev = find_mount(dir);

    if (!dev)
        return -1;

    fd = unix_open(dev, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return -1;

    ioctl(fd, BLKROSET, &OFF);
    adb_close(fd);

    *dir_ro = mount(dev, dir, "none", MS_REMOUNT, NULL);

    free(dev);

    return *dir_ro;
}

static void write_string(int fd, const char* str)
{
    writex(fd, str, strlen(str));
}

/* BEGIN Motorola, eMMC write protect feature */
int MOT_check_system_is_write_protected(int out)
{
    char buf[512];
    int size;
    int fd = unix_open("/proc/cmdline", O_RDONLY);

    if (fd < 0)
        return 0;

    buf[sizeof(buf) - 1] = '\0';
    size = adb_read(fd, buf, sizeof(buf) - 1);
    adb_close(fd);

    if (strstr(buf, "write_protect=1") != NULL) {
        char value[PROPERTY_VALUE_MAX];
        property_get("ro.boot.secure_hardware", value, "");

        write_string(out, "System folder is write protected. To disable use:\n");

        if (strcmp(value, "1") == 0) {
            write_string(out, "fastboot oem unlock\n");
        } else {
            write_string(out, "fastboot oem wptest disable\n");
        }
        return 1;
    }
    else if (strstr(buf, "write_protect=0") == NULL)
        write_string(out, "WARNING: System folder write protect state unknown!\n");

    return 0;
}

void remount_service(int fd, void *cookie)
{
    char buffer[200];
    if (MOT_check_system_is_write_protected(fd) == 0) {
        if (remount("/system", &system_ro)) {
            snprintf(buffer, sizeof(buffer), "remount of system failed: %s\n",strerror(errno));
            write_string(fd, buffer);
        }
    }

    if (hasVendorPartition()) {
        if (remount("/vendor", &vendor_ro)) {
            snprintf(buffer, sizeof(buffer), "remount of vendor failed: %s\n",strerror(errno));
            write_string(fd, buffer);
        }
    }

    if (!system_ro && (!vendor_ro || !hasVendorPartition()))
        write_string(fd, "remount succeeded\n");
    else {
        write_string(fd, "remount failed\n");
    }

    adb_close(fd);
}

