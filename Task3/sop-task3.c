#define _GNU_SOURCE
#include <bits/types/siginfo_t.h>
#include <bits/types/sigset_t.h>
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
volatile sig_atomic_t SIGINT_flag = 0;

void sig_handler(int sig) { 
    last_signal = sig; 
    if(sig == SIGINT){
    SIGINT_flag = 1;
    }
}

void SIGUSR1_handler(int sig, siginfo_t* info, void* context) {
    last_signal = sig; 
}

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

void set_SIGUSR1_handler(void (*f)(int, siginfo_t*, void*), int sigNo)
{
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_flags = SA_SIGINFO;
    act.sa_sigaction = f;
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
    printf("\tn - number of children\n");
    exit(EXIT_FAILURE);
}

void child_work(){
    srand(getpid());

    printf("[%d] Child created.\n", getpid());

    sigset_t mask, oldmask;

    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR2);
    sigaddset(&mask, SIGINT);
    sigprocmask(SIG_UNBLOCK, &mask, &oldmask);

    sigdelset(&oldmask, SIGUSR1);
    sigdelset(&oldmask, SIGINT);

    //applied_mask = {SIGUSR1, ...}
    //oldmask = {SIGUSR2, ...}

    int local_counter = 0;

    sigsuspend(&oldmask);
    for(;;){
        if(last_signal == SIGINT || SIGINT_flag){

            printf("[%d] Child received SIGINT. Terminating.\n", getpid());

            char buf[32];

            char counter_str[32]; // New buffer for the string representation

            int len = snprintf(counter_str, sizeof(counter_str), "%d\n", local_counter);

            snprintf(buf, sizeof(buf), "%d.txt", getpid());
            int fd = open(buf, O_RDWR | O_CREAT | O_TRUNC, 0644);
            if(fd < 0){
                ERR("open");
            }
            if(bulk_write(fd, counter_str, len) == -1){
                ERR("bulk_write");
            }
            close(fd);

            break;
        }
        if(last_signal == SIGUSR1){
        ms_sleep(rand() % 101 + 100);
        local_counter++;
        printf("[%d] Counter: %d\n", getpid(), local_counter);
        kill(getppid(), SIGUSR1);
        }
        if(last_signal == SIGUSR2){
            sigsuspend(&oldmask);
        }
    }
}

void create_children(int child_num, pid_t *children_IDs){
    for(int i = 0; i < child_num; i++){
        pid_t pid = fork();
        if(pid < 0){
            ERR("Fork");
        }
        if(pid == 0){
            child_work();
            exit(EXIT_SUCCESS);
        }
        else children_IDs[i] = pid;
    }
}

void parent_work(sigset_t oldmask, pid_t* children_IDs, int child_num){
    int i = 0;
    if(kill(children_IDs[i], SIGUSR1) < 0){
        ERR("kill");
    }
    while(!SIGINT_flag){
        sigsuspend(&oldmask);
        if(last_signal == SIGINT || SIGINT_flag){
            break;
        }
        if(last_signal == SIGUSR1){

            int next = (i != child_num - 1) ? i + 1 : 0;
            printf("[%d] Parent received SIGUSR1. Sending SIGUSR2 to %d and SIGUSR1 to %d\n",getpid(), children_IDs[i], children_IDs[next]);
            kill(children_IDs[i], SIGUSR2);
            kill(children_IDs[next], SIGUSR1);
            last_signal = 0;
            if(i == child_num - 1){
            i = 0;
            }
            else i++;
            
        }
    }
    printf("[%d] Parent received SIGINT. Terminating program.\n", getpid());
    kill(0, SIGINT);
}

int main(int argc, char* argv[])
{
    if(argc != 2){
        usage(argc, argv);
    }

    int child_num = atoi(argv[1]);
    pid_t children_IDs[child_num];

    //int issue_counts[students_num];

    sethandler(sig_handler, SIGUSR1);
    sethandler(sig_handler, SIGUSR2);
    sethandler(sig_handler, SIGINT);

    sigset_t mask, oldmask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGUSR1);
    sigaddset(&mask, SIGUSR2);
    sigprocmask(SIG_BLOCK, &mask, &oldmask);


    create_children(child_num, children_IDs);
    parent_work(oldmask, children_IDs, child_num);

    // while ((child_pid = wait(&status)) > 0) {
    //     if (WIFEXITED(status)) {

    //         int issue_count = WEXITSTATUS(status); 
    //         final_pids[students_done] = child_pid;
    //         final_issues[students_done] = issue_count;
    //         students_done++;

    //     }
    // }

    while(wait(NULL) > 0);

    printf("Work finished\n\n");

    return EXIT_SUCCESS;
}