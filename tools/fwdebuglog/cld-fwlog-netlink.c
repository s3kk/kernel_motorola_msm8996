/*
 * Copyright (c) 2013 The Linux Foundation. All rights reserved.
 *
 * Previously licensed under the ISC license by Qualcomm Atheros, Inc.
 *
 *
 * Permission to use, copy, modify, and/or distribute this software for
 * any purpose with or without fee is hereby granted, provided that the
 * above copyright notice and this permission notice appear in all
 * copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
 * AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * This file was originally distributed by Qualcomm Atheros, Inc.
 * under proprietary terms before Copyright ownership was assigned
 * to the Linux Foundation.
 */



#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <getopt.h>
#include <limits.h>
#include <asm/types.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/capability.h>
#include <linux/prctl.h>
#include <pwd.h>
#include <private/android_filesystem_config.h>
#include <hardware_legacy/wifi.h>
#include <sys/socket.h>
#include <linux/netlink.h>

#include <athdefs.h>
#include <a_types.h>
#include "dbglog.h"
#include "dbglog_host.h"

#include "event.h"
#include "msg.h"
#include "log.h"

#include "diag_lsm.h"
#include "diagpkt.h"
#include "diagcmd.h"
#include "diag.h"

#include "aniNlMsg.h"
#include "aniAsfHdr.h"
#include "aniAsfMem.h"


/* CAPs needed
 * CAP_NET_RAW   : Use RAW and packet socket
 * CAP_NET_ADMIN : NL broadcast receive
 */
const tANI_U32 capabilities = (1 << CAP_NET_RAW) | (1 << CAP_NET_ADMIN);

/* Groups needed
 * AID_INET      : Open INET socket
 * AID_NET_ADMIN : Handle NL socket
 * AID_QCOM_DIAG : Access DIAG debugfs
 * AID_WIFI      : WIFI Operation
 */
const gid_t groups[] = {AID_INET, AID_NET_ADMIN, AID_QCOM_DIAG, AID_WIFI};



const char options[] =
"Options:\n\
-f, --logfile=<Output log file> [Mandotory]\n\
-r, --reclimit=<Maximum number of records before the log rolls over> [Optional]\n\
-c, --console (prints the logs in the console)\n\
-s, --silent (No print will come when logging)\n\
-q, --qxdm  (prints the logs in the qxdm)\n\
The options can also be given in the abbreviated form --option=x or -o x. The options can be given in any order";

struct sockaddr_nl src_addr, dest_addr;
struct nlmsghdr *nlh = NULL;
struct iovec iov;
int sock_fd;
struct msghdr msg;

static FILE *fwlog_res;
static FILE *log_out;
const char *fwlog_res_file;
int max_records;
int record;
const char *progname;
char dbglogoutfile[PATH_MAX];
int optionflag = 0;

int rec_limit = 100000000; /* Million records is a good default */

static void
usage(void)
{
    fprintf(stderr, "Usage:\n%s options\n", progname);
    fprintf(stderr, "%s\n", options);
    exit(-1);
}

extern int parser_init();


extern int
dbglog_parse_debug_logs(u_int8_t *datap, u_int16_t len, u_int16_t dropped);

static unsigned int get_le32(const unsigned char *pos)
{
    return pos[0] | (pos[1] << 8) | (pos[2] << 16) | (pos[3] << 24);
}

static size_t reorder(FILE *log_in, FILE *log_out)
{
    unsigned char buf[RECLEN];
    size_t res;
    unsigned int timestamp, min_timestamp = -1;
    int pos = 0, min_pos = 0;

    pos = 0;
    while ((res = fread(buf, RECLEN, 1, log_in)) == 1) {
        timestamp = get_le32(buf);
        if (timestamp < min_timestamp) {
                min_timestamp = timestamp;
                min_pos = pos;
        }
        pos++;
    }
    printf("First record at position %d\n", min_pos);

    fseek(log_in, min_pos * RECLEN, SEEK_SET);
    while ((res = fread(buf, RECLEN, 1, log_in)) == 1) {
        printf("Read record timestamp=%u length=%u\n",
               get_le32(buf), get_le32(&buf[4]));
        if (fwrite(buf, RECLEN, res, log_out) != res)
               perror("fwrite");
    }

    fseek(log_in, 0, SEEK_SET);
    pos = min_pos;
    while (pos > 0 && (res = fread(buf, RECLEN, 1, log_out)) == 1) {
        pos--;
        printf("Read record timestamp=%u length=%u\n",
                get_le32(buf), get_le32(&buf[4]));
        if (fwrite(buf, RECLEN, res, log_out) != res)
                perror("fwrite");
    }

    return 0;
}

/*
 * Lower the privileges for security reason
 * the service will run only in system or diag mode
 *
 */
int
cnssdiagservice_cap_handle(void)
{
    int i;
    int err;

    struct __user_cap_header_struct cap_header_data;
    cap_user_header_t cap_header = &cap_header_data;
    struct __user_cap_data_struct cap_data_data;
    cap_user_data_t cap_data = &cap_data_data;

    cap_header->pid = 0;
    cap_header->version = _LINUX_CAPABILITY_VERSION;
    memset(cap_data, 0, sizeof(cap_data_data));

    if (prctl(PR_SET_KEEPCAPS, 1, 0, 0, 0) != 0)
    {
        printf("%d PR_SET_KEEPCAPS error:%s", __LINE__, strerror(errno));
        exit(1);
    }

    if (setgroups(sizeof(groups)/sizeof(groups[0]), groups) != 0)
    {
        printf("setgroups error %s", strerror(errno));
        return -1;
    }

    if (setgid(AID_SYSTEM))
    {
        printf("SET GID error %s", strerror(errno));
        return -1;
    }

    if (setuid(AID_SYSTEM))
    {
        printf("SET UID %s", strerror(errno));
        return -1;
    }

    /* Assign correct CAP */
    cap_data->effective = capabilities;
    cap_data->permitted = capabilities;
    /* Set the capabilities */
    if (capset(cap_header, cap_data) < 0)
    {
        printf("%d failed capset error:%s", __LINE__, strerror(errno));
        return -1;
    }
    return 0;
}

static void cleanup(void) {
    if (sock_fd)
        close(sock_fd);

    fwlog_res = fopen(fwlog_res_file, "w");

    if (fwlog_res == NULL) {
        perror("Failed to open reorder fwlog file");
        fclose(log_out);
        return;
    }

    reorder(log_out, fwlog_res);
out:
    fclose(fwlog_res);
    fclose(log_out);
}

static void stop(int signum)
{

    if(optionflag & LOGFILE_FLAG){
        printf("Recording stopped\n");
        cleanup();
    }
    exit(0);
}

int main(int argc, char *argv[])
{
    int res =0;
    unsigned char *eventbuf;
    unsigned char *dbgbuf;
    int c, rc = 0;
    char *mesg="Hello";
    struct dbglog_slot *slot;

    progname = argv[0];
    tANI_U16 diag_type = 0;
    tANI_U16 length = 0;
    tANI_U32 event_id = 0;
    tANI_U32 target_time = 0;
    unsigned int dropped = 0;
    unsigned int timestamp = 0;

    int option_index = 0;
    static struct option long_options[] = {
        {"logfile", 1, NULL, 'f'},
        {"reclimit", 1, NULL, 'r'},
        {"console", 0, NULL, 'c'},
        {"qxdm", 0, NULL, 'q'},
        {"silent", 0, NULL, 's'},
        { 0, 0, 0, 0}
    };

    while (1) {
        c = getopt_long (argc, argv, "f:scq:r:", long_options, &option_index);
        if (c == -1) break;

        switch (c) {
            case 'f':
                memset(dbglogoutfile, 0, PATH_MAX);
                memcpy(dbglogoutfile, optarg, strlen(optarg));
                optionflag |= LOGFILE_FLAG;
                break;

            case 'c':
                optionflag |= CONSOLE_FLAG;
                break;

            case 'q':
                optionflag |= QXDM_FLAG;
                break;

            case 'r':
                rec_limit = strtoul(optarg, NULL, 0);
                break;

            case 's':
                optionflag |= SILENT_FLAG;
                break;
            default:
                usage();
        }
    }

    if (!(optionflag & (LOGFILE_FLAG | CONSOLE_FLAG | QXDM_FLAG | SILENT_FLAG))) {
        usage();
        return -1;
    }

    if (optionflag == QXDM_FLAG) {
        /* Intialize the fd required for diag APIs */
        if (TRUE != Diag_LSM_Init(NULL))
        {
             perror("Failed on Diag_LSM_Init\n");
             return -1;
        }

        if(cnssdiagservice_cap_handle())
        {
            printf("Cap bouncing failed EXIT!!!");
            exit(1);
        }
    }

    do
    {
        /* 5 sec sleep waiting for driver to load */
        sleep(CNSS_DIAG_SLEEP_INTERVAL);
        rc = is_wifi_driver_loaded();
    } while(rc == 0);

    sock_fd = socket(PF_NETLINK, SOCK_RAW, CLD_NETLINK_USER);
    if (sock_fd < 0) {
        fprintf(stderr, "Socket creation failed sock_fd 0x%x \n", sock_fd);
        return -1;
    }

    memset(&src_addr, 0, sizeof(src_addr));
    src_addr.nl_family = AF_NETLINK;
    src_addr.nl_pid = getpid(); /* self pid */

    bind(sock_fd, (struct sockaddr *)&src_addr, sizeof(src_addr));

    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.nl_family = AF_NETLINK;
    dest_addr.nl_pid = 0; /* For Linux Kernel */
    dest_addr.nl_groups = 0; /* unicast */

    nlh = (struct nlmsghdr *)malloc(NLMSG_SPACE(RECLEN));
    if (nlh == NULL) {
        fprintf(stderr, "Cannot allocate memory \n");
        close(sock_fd);
        return -1;
    }
    memset(nlh, 0, NLMSG_SPACE(RECLEN));
    nlh->nlmsg_len = NLMSG_SPACE(RECLEN);
    nlh->nlmsg_pid = getpid();
    nlh->nlmsg_flags = 0;

    memcpy(NLMSG_DATA(nlh), mesg, strlen(mesg));

    iov.iov_base = (void *)nlh;
    iov.iov_len = nlh->nlmsg_len;
    msg.msg_name = (void *)&dest_addr;
    msg.msg_namelen = sizeof(dest_addr);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    /* Pass the pid info and register the service */
    sendmsg(sock_fd, &msg, 0);

    signal(SIGINT, stop);
    signal(SIGTERM, stop);

    if (optionflag & LOGFILE_FLAG) {

        if (rec_limit < RECLEN) {
            fprintf(stderr, "Too small maximum length (has to be >= %d)\n",
                    RECLEN);
            close(sock_fd);
            free(nlh);
            return -1;
        }
        max_records = rec_limit / RECLEN;
        printf("Storing last %d records\n", max_records);

        log_out = fopen(dbglogoutfile, "w");
        if (log_out == NULL) {
            perror("Failed to create output file");
            close(sock_fd);
            free(nlh);
            return -1;
        }

        fwlog_res_file = "./reorder";

        /* Read message from kernel */
        while ((res = recvmsg(sock_fd, &msg, 0)) > 0)  {
            if (res >= sizeof(struct dbglog_slot)) {
                dbgbuf = (unsigned char *)NLMSG_DATA(nlh);
            } else {
                fprintf(stderr, "Incorrect msg Length 0x%x \n", res);
                continue;
            }
            slot = (struct dbglog_slot *)dbgbuf;
            timestamp = get_le32((unsigned char *)&slot->length);
            length = get_le32((unsigned char *)&slot->length);
            dropped = get_le32((unsigned char *)&slot->dropped);
            if (!((optionflag & SILENT_FLAG) == SILENT_FLAG)) {
                /* don't like this have to fix it */
                printf("Read record timestamp=%u length=%u fw dropped=%u\n",
                       timestamp, length, dropped);
            }
            fseek(log_out, record * RECLEN, SEEK_SET);
            if ((res = fwrite(dbgbuf, RECLEN, 1, log_out)) != 1){
                perror("fwrite");
                break;
            }
            record++;
            if (record == max_records)
                    record = 0;
        }

    printf("Incomplete read: %d bytes\n", (int) res);
    cleanup();
    }

    if (optionflag & CONSOLE_FLAG) {

        parser_init();

        while ((res = recvmsg(sock_fd, &msg, 0)) > 0)  {
            if (res >= sizeof(struct dbglog_slot)) {
                dbgbuf = (unsigned char *)NLMSG_DATA(nlh);
            } else {
                fprintf(stderr, "Incorrect msg Length 0x%x \n", res);
                continue;
            }
            slot = (struct dbglog_slot *)dbgbuf;
            length = get_le32((unsigned char *)&slot->length);
            dropped = get_le32((unsigned char *)&slot->dropped);
            dbglog_parse_debug_logs(slot->payload, length, dropped);
        }
        close(sock_fd);
        free(nlh);
    }

    if (optionflag & QXDM_FLAG) {

        parser_init();

        while ((res = recvmsg(sock_fd, &msg, 0)) > 0)  {

            if (res >= sizeof(struct dbglog_slot)) {
                eventbuf = (unsigned char *)NLMSG_DATA(nlh);
            } else {
                fprintf(stderr, "Incorrect msg Length 0x%x \n", res);
                return -1;
            }

            dbgbuf = eventbuf;

            diag_type = *(tANI_U16*)eventbuf;
            eventbuf += sizeof(tANI_U16);

            length = *(tANI_U16*)eventbuf;
            eventbuf += sizeof(tANI_U16);

            if (diag_type == DIAG_TYPE_FW_EVENT) {
                target_time = *(tANI_U32*)eventbuf;
                eventbuf += sizeof(tANI_U32);

                event_id = *(tANI_U32*)eventbuf;
                eventbuf += sizeof(tANI_U32);

                if (length)
                    event_report_payload(event_id, length, eventbuf);
                else
                    event_report(event_id);
            } else if (diag_type == DIAG_TYPE_FW_LOG) {
                /* Do nothing for now */
            } else {
                slot =(struct dbglog_slot *)dbgbuf;
                length = get_le32((unsigned char *)&slot->length);
                dropped = get_le32((unsigned char *)&slot->dropped);
                dbglog_parse_debug_logs(slot->payload, length, dropped);
            }
        }

        /* Release the handle to Diag*/
        Diag_LSM_DeInit();
        close(sock_fd);
        free(nlh);
    }
    return 0;
}
