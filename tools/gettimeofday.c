#include <stdio.h>
#include <sys/time.h>

int
main(int argc, char **argv)
{
        struct timeval tv;
        struct timezone tz;

        if (gettimeofday(&tv, &tz))
                return 1;

        printf(
                "tv_sec=%ld tv_usec=%ld tz_minuteswest=%d tz_dsttime=%d\n",
                tv.tv_sec, tv.tv_usec, tz.tz_minuteswest, tz.tz_dsttime
        );

        return 0;
}
