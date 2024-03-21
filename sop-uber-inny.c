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

volatile sig_atomic_t last_signal = 0;

typedef struct {
    uint32_t x_start;
    uint32_t y_start;
    uint32_t x_end;
    uint32_t y_end;
} Message;

typedef struct{
    uint32_t d;
    pid_t pid;
} Message2;


void usage(const char* name)
{
    fprintf(stderr, "USAGE: %s N T\n", name);
    fprintf(stderr, "N: 1 <= N - number of drivers\n");
    fprintf(stderr, "T: 5 <= T - simulation duration\n");
    exit(EXIT_FAILURE);
}

void sethandler(void (*f)(int, siginfo_t *, void *), int sigNo)
{
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_sigaction = f;
    act.sa_flags = SA_SIGINFO;
    if (-1 == sigaction(sigNo, &act, NULL))
        ERR("sigaction");
}

void sigchld_handler(int sig, siginfo_t *s, void *p)
{
    pid_t pid;
    for (;;)
    {
        pid = waitpid(0, NULL, WNOHANG);
        if (pid == 0)
            return;
        if (pid <= 0)
        {
            if (errno == ECHILD)
                return;
            ERR("waitpid");
        }
    }
}

void sig_handler(int sig, siginfo_t *s, void *p)
{ last_signal = sig;}

void child_work(int n)
{
    mqd_t uber_tasks;
    srand(getpid());
    uint32_t to_sleep;
    unsigned prio;
    int x = rand()%2001 - 1000;
    int y = rand()%2001 - 1000;

    char name[20];
    memset(name,0,20);
    sprintf(name,"/uber_results_%d",getpid());

    //fprintf(stderr,"/uber_results_%d\n",getpid());
    mqd_t out;
    struct mq_attr attr2;
    attr2.mq_maxmsg = 1;
    attr2.mq_msgsize = sizeof(Message2);
    if ((out = TEMP_FAILURE_RETRY(mq_open(name, O_WRONLY |O_NONBLOCK| O_CREAT, 0600, &attr2))) == (mqd_t)-1)
        ERR("mq open in");
    //fprintf(stderr,"Driver [%d]: x = %d, y = %d\n",getpid(),x,y);
    struct mq_attr attr;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = sizeof(Message);

    if((uber_tasks = TEMP_FAILURE_RETRY(mq_open("/uber_tasks",O_RDONLY|O_CREAT,0600,&attr)))==(mqd_t)-1)
        ERR("mq_open");
    while(1)
    {
        Message m;
        if (TEMP_FAILURE_RETRY(mq_receive(uber_tasks, (char *)&m, sizeof(Message), &prio)) < 1)
            ERR("mq_receive");
        if(prio == 13)
            break;
        fprintf(stderr,"Driver [%d], (%d,%d) will do a course from (%d,%d) to (%d,%d)\n",getpid(),x,y,m.x_start,m.y_start,m.x_end,m.y_end);
        to_sleep = abs(x-m.x_start) + abs(y-m.y_start) + abs(m.x_start-m.x_end) + abs(m.y_start - m.y_end);

        x = m.x_end;
        y = m.y_end;

        Message2 m2;
        m2.pid = getpid();
        m2.d = to_sleep;

        if(TEMP_FAILURE_RETRY(mq_send(out,(const char*)&m2,sizeof(Message2),0)))
            ERR("mq_send");
        usleep(1000*to_sleep);
    }
    mq_close(out);
}

void parent_work(int n)
{
    mqd_t uber_tasks;
    struct mq_attr attr;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = sizeof(Message);

    if((uber_tasks = TEMP_FAILURE_RETRY(mq_open("/uber_tasks",O_WRONLY|O_CREAT|O_NONBLOCK,0600,&attr)))==(mqd_t)-1)
        ERR("mq_open");
    int to_sleep;
    srand(getpid());
    while (last_signal != SIGINT && last_signal != SIGALRM)
    {
        Message m;
        m.x_start = rand()%2001 - 1000;
        m.x_end = rand()%2001 - 1000;
        m.y_start = rand()%2001 - 1000;
        m.y_end = rand()%2001 - 1000;
        if (TEMP_FAILURE_RETRY(mq_send(uber_tasks, (const char *)&m, sizeof(Message), 0))) {
            if(errno == EAGAIN)
            {
                fprintf(stderr, "Order from (%d,%d) to (%d,%d) will not be accepted - no drivers available\n",m.x_start,m.y_start,m.x_end,m.y_end);
            }
            else
                ERR("mq_send");
        }
        to_sleep = rand()%1501+500;
        usleep(1000 * to_sleep);

    }
    mq_close(uber_tasks);
    if((uber_tasks = TEMP_FAILURE_RETRY(mq_open("/uber_tasks",O_WRONLY,0600,&attr)))==(mqd_t)-1)
        ERR("mq_open");
    Message m;
    for(int i = 0;i<n;i++)
    {
        if(mq_send(uber_tasks,(const char*)&m,sizeof(Message),13))
            ERR("mq_send");
    }
    if(mq_unlink("/uber_tasks"))
        ERR("mq_unlink");
    //kill(0,SIGINT);
    printf("[PARENT] Terminates \n");
}

void create_children(int n,pid_t* pids)
{
    while (n-- > 0) {
        pid_t pid = fork();
        switch (pid) {
            case 0:
                child_work(n);
                exit(EXIT_SUCCESS);
            case -1:
                ERR("Fork:");
        }
        pids[n] = pid;
    }
}

void func_thread(union sigval sv) {
    mqd_t *id = (mqd_t *) sv.sival_ptr;
    struct sigevent sev;
    sev.sigev_notify = SIGEV_THREAD;
    sev.sigev_value.sival_ptr = id;
    sev.sigev_notify_attributes = NULL;
    sev.sigev_notify_function = func_thread;
    if (mq_notify(*id, &sev) < 0) {
        ERR("mq_notify");
    }

    Message2 m;
    if(TEMP_FAILURE_RETRY(mq_receive(*id, (char*)&m,sizeof(Message2),NULL))<0)
        ERR("mq_receive");
    fprintf(stderr,"The driver [%d] drove a distance of %d\n",m.pid,m.d);
}

int main(int argc, char** argv)
{
    (void)argc;
    if(argc != 3)
        usage(argv[0]);
    int n = atoi(argv[1]);
    int t = atoi(argv[2]);
    if(n<1 || t<5)
        usage(argv[0]);

    pid_t* pids;
    if(NULL == (pids = (pid_t *)malloc(n*sizeof(pid_t))))
        ERR("malloc");

    sethandler(sigchld_handler, SIGCHLD);
    sethandler(sig_handler,SIGALRM);
    sethandler(sig_handler,SIGINT);
    create_children(n,pids);
    mqd_t in[n];

    for(int i = 0;i<n;i++)
    {
        char name[20];
        memset(name,0,20);
        sprintf(name,"/uber_results_%d",pids[i]);
        //fprintf(stderr,"/uber_results_%d\n",pids[i]);

        struct mq_attr attr;
        attr.mq_maxmsg = 1;
        attr.mq_msgsize = sizeof(Message2);
        if ((in[i] = TEMP_FAILURE_RETRY(mq_open(name, O_RDWR | O_NONBLOCK | O_CREAT, 0600, &attr))) == (mqd_t)-1)
            ERR("mq open in");
        static struct sigevent noti;
        noti.sigev_notify = SIGEV_THREAD;
        noti.sigev_notify_attributes = NULL;
        noti.sigev_value.sival_ptr = &(in[i]);
        noti.sigev_notify_function = func_thread;
        if (mq_notify(in[i], &noti) < 0)
            ERR("mq_notify");
    }
    alarm(t);
    parent_work(n);

    for(int i = 0;i<n;i++)
    {
        mq_close(in[i]);
        char name[20];
        memset(name,0,20);
        sprintf(name,"/uber_results_%d",pids[n-i-1]);
        if(mq_unlink(name))
            ERR("mq_unlink");
    }

    free(pids);
    return EXIT_SUCCESS;
}