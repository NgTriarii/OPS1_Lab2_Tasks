#include <bits/types/sigset_t.h>
#define _GNU_SOURCE
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), kill(0, SIGKILL), exit(EXIT_FAILURE))
#define MAX_COUNT 10

volatile sig_atomic_t last_signal;
volatile sig_atomic_t SIGUSR2_Flag = 0;

void sig_handler(int sig) { last_signal = sig; if(sig == SIGUSR2) SIGUSR2_Flag = 1; }

void sigchld_handler(int sig)
{
    pid_t pid;
    while (1)
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

ssize_t bulk_read(int fd, char* buf, size_t count)
{
    ssize_t c;
    ssize_t len = 0;
    do
    {
        c = TEMP_FAILURE_RETRY(read(fd, buf, count));
        if (c < 0)
            return c;
        if (c == 0)
            return len;  // EOF
        buf += c;
        len += c;
        count -= c;
    } while (count > 0);
    return len;
}

ssize_t bulk_write(int fd, char* buf, size_t count)
{
    ssize_t c;
    ssize_t len = 0;
    do
    {
        c = TEMP_FAILURE_RETRY(write(fd, buf, count));
        if (c < 0)
            return c;
        buf += c;
        len += c;
        count -= c;
    } while (count > 0);
    return len;
}

void sethandler(void (*f)(int), int sigNo)
{
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_handler = f;
    if (-1 == sigaction(sigNo, &act, NULL))
        ERR("sigaction");
}

void ms_sleep(unsigned int milli)
{
    time_t sec = (int)(milli / 1000);
    milli = milli - (sec * 1000);
    struct timespec ts = {0};
    ts.tv_sec = sec;
    ts.tv_nsec = milli * 1000000L;
    if (TEMP_FAILURE_RETRY(nanosleep(&ts, &ts)))
        ERR("nanosleep");
}

void usage(int argc, char* argv[])
{
    printf("%s n\n", argv[0]);
    printf("\t1 <= n <= 10 -- number of children\n");
    exit(EXIT_FAILURE);
}

void child_work(){
    srand(getpid());

    sigset_t mask, oldmask;

    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR2);

    sigprocmask(SIG_UNBLOCK, &mask, &oldmask);

    while(1){
        if(last_signal != SIGUSR2){
            ms_sleep(rand() % 101 + 100);
            if (kill(getppid(), SIGUSR1))
            ERR("kill");
            printf("[%d] signal sent\n", getpid());
        }
        if(last_signal == SIGUSR2){
            break;
        }  
    }
}

void create_children(int n){
    for(int i = 0; i < n; i++){
        pid_t pid = fork();
        if(pid < 0){
            ERR("Fork");
        }
        if(pid == 0){
            child_work();
            exit(EXIT_SUCCESS);
        }
    }
}

void parent_work(sigset_t oldmask){
    int count = 0;
    while(count < 100) {
        sigsuspend(&oldmask);
        if(last_signal == SIGUSR1){
            count++;
            printf("Signal count %d\n", count);
        }
    }
    kill(0, SIGUSR2);
}

int main(int argc, char* argv[])
{
    if(argc != 2){
        usage(argc, argv);
    }
    int child_num = atoi(argv[1]);
    if(child_num < 1 || child_num > MAX_COUNT){
        usage(argc, argv);
    }

    sethandler(sig_handler, SIGUSR1);
    sethandler(sig_handler, SIGUSR2);
    sethandler(sigchld_handler, SIGCHLD);

    sigset_t mask, oldmask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigaddset(&mask, SIGUSR1);
    sigaddset(&mask, SIGUSR2);
    sigprocmask(SIG_BLOCK, &mask, &oldmask);

    create_children(child_num);
    parent_work(oldmask);

    while(wait(NULL) > 0);

    printf("Process finished");

    return EXIT_SUCCESS;
}