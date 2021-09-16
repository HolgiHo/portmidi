/* fast.c -- send many MIDI messages very fast. 
 *
 * This is a stress test created to explore reports of
 * pm_write() call blocking (forever) on Linux when
 * sending very dense MIDI sequences.
 *
 * Modified 8 Aug 2017 with -n to send expired timestamps
 * to test a theory about why Linux ALSA hangs in Audacity.
 *
 * Modified 9 Aug 2017 with -m, -p to test when timestamps are
 * wrapping from negative to positive or positive to negative.
 *
 * Roger B. Dannenberg, Aug 2017
 */

#include "portmidi.h"
#include "porttime.h"
#include "stdlib.h"
#include "stdio.h"
#include "string.h"
#include "assert.h"

#define OUTPUT_BUFFER_SIZE 0
#define DRIVER_INFO NULL
#define TIME_START Pt_Start(1, 0, 0) /* timer started w/millisecond accuracy */

#define STRING_MAX 80 /* used for console input */
// need to get declaration for Sleep()
#ifdef WIN32
#include "windows.h"
#else
#include <unistd.h>
#define Sleep(n) usleep(n * 1000)
#endif


int32_t latency = 0;
int32_t msgrate = 0;
int deviceno = -9999;
int duration = 0;
int expired_timestamps = FALSE;
int use_timeoffset = 0;

/* read a number from console */
/**/
int get_number(char *prompt)
{
    char line[STRING_MAX];
    int n = 0, i;
    printf("%s", prompt);
    while (n != 1) {
        n = scanf("%d", &i);
        fgets(line, STRING_MAX, stdin);
    }
    return i;
}


/* get_time -- the time reference. Normally, this will be the default
 *    time, Pt_Time(), but if you use the -p or -m option, the time 
 *    reference will start at an offset of -10s for -m, or 
 *    maximum_time - 10s for -p, so that we can observe what happens
 *    with negative time or when time changes sign or wraps (by 
 *    generating output for more than 10s).
 */
PmTimestamp get_time(void *info)
{
    PmTimestamp now = (PmTimestamp) (Pt_Time() + use_timeoffset);
    return now;
}


void fast_test()
{
    PmStream * midi;
	char line[80];
    PmEvent buffer[16];

    /* It is recommended to start timer before PortMidi */
    TIME_START;

    /* open output device */
    Pm_OpenOutput(&midi, 
                  deviceno, 
                  DRIVER_INFO,
                  OUTPUT_BUFFER_SIZE, 
                  get_time,
                  NULL,
                  latency);
    printf("Midi Output opened with %ld ms latency.\n", (long) latency);

    /* wait a sec after printing previous line */
    PmTimestamp start = get_time(NULL) + 1000;
    while (start > get_time(NULL)) {
        Sleep(10);
    }
    printf("sending output...\n");
    fflush(stdout); /* make sure message goes to console */

    /* every 10ms send on/off pairs at timestamps set to current time */
    PmTimestamp now = get_time(NULL);
    int msgcnt = 0;
    int pitch = 60;
    int printtime = 1000;
    /* if expired_timestamps, we want to send timestamps that have
     * expired. They should be sent immediately, but there's a suggestion
     * that negative delay might cause problems in the ALSA implementation
     * so this is something we can test using the -n flag.
     */
    if (expired_timestamps) {
        now = now - 2 * latency;
    }
    while (((PmTimestamp) (now - start)) < duration *  1000) {
        /* how many messages do we send? Total should be
         *     (elapsed * rate) / 1000
         */
        int send_total = (((PmTimestamp) ((now - start))) * msgrate) / 1000;
        /* always send until pitch would be 60 so if we run again, the
           next pitch (60) will be expected */
        while (msgcnt < send_total || pitch != 60) {
            if ((msgcnt & 1) == 0) {
                Pm_WriteShort(midi, now, Pm_Message(0x90, pitch, 100));
            } else {
                Pm_WriteShort(midi, now, Pm_Message(0x90, pitch, 0));
                /* play 60, 61, 62, ... 71, then wrap back to 60, 61, ... */
                pitch = (pitch - 59) % 12 + 60;
            }
            msgcnt += 1;
            if (((PmTimestamp) (now - start)) >= printtime) {
                printf("%d at %dms\n", msgcnt, now - start);
                fflush(stdout); /* make sure message goes to console */
                printtime += 1000; /* next msg in 1s */
            }
        }
        now = get_time(NULL);
    }
    /* close device (this not explicitly needed in most implementations) */
    printf("ready to close and terminate... (type RETURN):");
    fgets(line, STRING_MAX, stdin);
	
    Pm_Close(midi);
    Pm_Terminate();
    printf("done closing and terminating...\n");
}


void show_usage()
{
    printf("Usage: fast [-h] [-l latency] [-r rate] [-d device] [-s dur] "
           "[-n] [-p] [-m]\n"
           ", where latency is in ms,\n"
           "        rate is messages per second,\n"
           "        device is the PortMidi device number,\n"
           "        dur is the length of the test in seconds,\n"
           "        -n means send timestamps in the past,\n"
           "        -p means use a large positive time offset,\n"
           "        -m means use a large negative time offset, and\n"
           "        -h means help.\n");
}

int main(int argc, char *argv[])
{
    int default_in;
    int default_out;
    int i = 0, n = 0;
    char line[STRING_MAX];
    int stream_test = 0;
    int latency_valid = FALSE;
    int rate_valid = FALSE;
    int device_valid = FALSE;
    int dur_valid = FALSE;
    
    if (sizeof(void *) == 8) 
        printf("Apparently this is a 64-bit machine.\n");
    else if (sizeof(void *) == 4) 
        printf ("Apparently this is a 32-bit machine.\n");
    
    if (argc <= 1) {
        show_usage();
    } else {
        for (i = 1; i < argc; i++) {
            if (strcmp(argv[i], "-h") == 0) {
                show_usage();
            } else if (strcmp(argv[i], "-l") == 0 && (i + 1 < argc)) {
                i = i + 1;
                latency = atoi(argv[i]);
                printf("Latency will be %ld\n", (long) latency);
                latency_valid = TRUE;
            } else if (strcmp(argv[i], "-r") == 0) {
                i = i + 1;
                msgrate = atoi(argv[i]);
                printf("Rate will be %d messages/second\n", msgrate);
                rate_valid = TRUE;
            } else if (strcmp(argv[i], "-d") == 0) {
                i = i + 1;
                deviceno = atoi(argv[i]);
                printf("Device will be %d\n", deviceno);
            } else if (strcmp(argv[i], "-s") == 0) {
                i = i + 1;
                duration = atoi(argv[i]);
                printf("Duration will be %d seconds\n", duration);
                dur_valid = TRUE;
            } else if (strcmp(argv[i], "-n") == 0) {
                printf("Sending expired timestamps (-n)\n");
                expired_timestamps = TRUE;
            } else if (strcmp(argv[i], "-p") == 0) {
                printf("Time offset set to 2147473648 (-p)\n");
                use_timeoffset = 2147473648;
            } else if (strcmp(argv[i], "-m") == 0) {
                printf("Time offset set to -10000 (-m)\n");
                use_timeoffset = -10000;
            } else {
                show_usage();
            }
        }
    }

    if (!latency_valid) {
        // coerce to known size
        latency = (int32_t) get_number("Latency in ms: "); 
    }

    if (!rate_valid) {
        // coerce from "%d" to known size
        msgrate = (int32_t) get_number("Rate in messages per second: ");
    }

    if (!dur_valid) {
        duration = get_number("Duration in seconds: ");
    }

    /* list device information */
    default_in = Pm_GetDefaultInputDeviceID();
    default_out = Pm_GetDefaultOutputDeviceID();
    for (i = 0; i < Pm_CountDevices(); i++) {
        char *deflt;
        const PmDeviceInfo *info = Pm_GetDeviceInfo(i);
        if (info->output) {
            printf("%d: %s, %s", i, info->interf, info->name);
            if (i == deviceno) {
                device_valid = TRUE;
                deflt = "selected ";
            } else if (i == default_out) {
                deflt = "default ";
            } else {
                deflt = "";
            }                      
            printf(" (%soutput)", deflt);
            printf("\n");
        }
    }
    
    if (!device_valid) {
        deviceno = get_number("Output device number: ");
    }

    fast_test();
    return 0;
}