#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <mqueue.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define ERR(source) \
    (fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))

#define MSG_MAX 10
#define MSG_SIZE 16

void usage(const char* name)
{
    fprintf(stderr, "USAGE: %s N T\n", name);
    fprintf(stderr, "N: 1 <= N - number of drivers\n");
    fprintf(stderr, "T: 5 <= T - simulation duration\n");
    exit(EXIT_FAILURE);
}

int random_integer(int from, int to)
{
    return from + rand() % (to - from + 1);
}

int manhattan_distance(int x1, int y1, int x2, int y2)
{
    return abs(x1 - x2) + abs(y1 - y2);
}

int msleep(long msec)
{
    struct timespec ts;
    int res;

    if (msec < 0)
    {
        errno = EINVAL;
        return -1;
    }

    ts.tv_sec = msec / 1000;
    ts.tv_nsec = (msec % 1000) * 1000000;

    do {
        res = nanosleep(&ts, &ts);
    } while (res && errno == EINTR);

    return res;
}

int child_work(struct mq_attr* attr_pointer, int children_count)
{
    srand(getpid());
    int X = random_integer(-1000, 1000);
    int Y = random_integer(-1000, 1000);
    printf("Process %d starts at X=%d, Y=%d\n", getpid(), X, Y);
    fflush(stdout);

    mqd_t mq = mq_open("/uber_tasks", O_CREAT | O_RDONLY, 0600, attr_pointer);
    if (mq < 0) {
        perror("mq_open()");
        return 1;
    }

    int task[4];

    while (1)
    {
        struct timespec ts;
        if (clock_gettime(CLOCK_REALTIME, &ts) < 0) {
            perror("clock_gettime()");
            return 1;
        }

        ts.tv_sec += 4 * children_count;

        if (mq_timedreceive(mq, (char*) task, sizeof(task), NULL, &ts) < 0) {
            if (errno == ETIMEDOUT) {
                printf("Process %d Timed out!\n", getpid());
                mq_close(mq);
                break;
            }
            perror("mq_receive()");
            return 1;
        }

        int X_client, Y_client, X_target, Y_target;
        X_client = task[0];
        Y_client = task[1];
        X_target = task[2];
        Y_target = task[3];

        int sleepytime = manhattan_distance(X, Y, X_client, Y_client) +
                manhattan_distance(X_client, Y_client, X_target, Y_target);

        X = X_target;
        Y = Y_target;
        printf("Process %d is taking a client from (%d, %d) to (%d, %d)\n", getpid(), X_client, Y_client, X_target, Y_target);
        fflush(stdout);
        msleep(sleepytime);
    }

    return EXIT_SUCCESS;
}

void create_children(int n, struct mq_attr* attr_pointer)
{
    pid_t s;
    int children_count = n;
    for (n--; n >= 0; n--)
    {
        if ((s = fork()) < 0)
            ERR("Fork:");
        if (!s)
        {
            child_work(attr_pointer, children_count);
            exit(EXIT_SUCCESS);
        }
    }
}

int main(int argc, char** argv)
{
    if(argc != 3) usage(argv[0]);
    int N = atoi(argv[1]);
    int T = atoi(argv[2]);
    if(N < 1 || T < 5) usage(argv[0]);

    T *= 1000;

    mq_unlink("/uber_tasks");

    struct mq_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.mq_msgsize = MSG_SIZE;
    attr.mq_maxmsg = MSG_MAX;

    create_children(N, &attr);

    mqd_t mq = mq_open("/uber_tasks", O_CREAT | O_WRONLY, 0600, &attr);
    if (mq < 0) {
        perror("mq_open()");
        return 1;
    }

    int task[4];

    while(T > 0)
    {
        int sleepytime = random_integer(500, 2000);
        T -= sleepytime;
        msleep(sleepytime);

        for(int i = 0; i < 4; ++i) task[i] = random_integer(-1000, 1000);
        if (mq_send(mq, (const char*) task, sizeof(task), 0) < 0) {
            perror("mq_send()");
            return 1;
        }
    }

    printf("Parent: No new tasks available.\n");
    fflush(stdout);

    while (wait(NULL) > 0)
        ;

    printf("Parent: Terminating!\n");
    fflush(stdout);
    mq_close(mq);

    return EXIT_SUCCESS;
}