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
volatile sig_atomic_t sender_pid;

void sig_handler(int sig) { last_signal = sig; }

void SIGUSR1_handler(int sig, siginfo_t* info, void* context) {
    last_signal = sig; 
    sender_pid = info->si_pid;
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
    printf("%s p t [students' probabilities]\n", argv[0]);
    printf("\t1 <= p <= 10 -- number of parts, 1 < t <= 10 -- time to finish each task, [probabilities] -- probability of issues for each student\n");
    exit(EXIT_FAILURE);
}

int student_work(int probability, int parts, int difficulty, int ID){
    printf("[%d, %d] Probability of this student: %d\n", ID, getpid(), probability);
    srand(getpid());

    sigset_t mask, oldmask;

    sigemptyset(&mask);
    sigprocmask(SIG_UNBLOCK, &mask, &oldmask);
    sigdelset(&oldmask, SIGUSR2);

    int issue_count = 0;
    int problems = 0;
    int chance = 0;
    for(int i = 0; i < parts; i++){
        if((chance = rand() % 101) <= probability){
            problems = 50;
            issue_count++;
            printf("[%d, %d] Student has issues! (%d)\n", ID, getpid(), issue_count);
        }
        ms_sleep(100 * difficulty + problems);
        problems = 0;
        printf("[%d, %d] Student completed the task nr %d  (%d%%)\n", ID, getpid(), i + 1, chance);
        kill(getppid(), SIGUSR1);
        sigsuspend(&oldmask);
    }
    
    printf("[%d, %d] The student has finished all tasks!\n", ID, getpid());
    return issue_count;
}

void create_students(int n, int* probabilities, int parts, int difficulty, pid_t* IDs){
    for(int i = 0; i < n; i++){
        pid_t pid = fork();
        if(pid < 0){
            ERR("Fork");
        }
        if(pid == 0){
            int res = student_work(probabilities[i], parts, difficulty, i + 1);
            exit(res);
        }
        else IDs[i] = getpid();
    }
}

void teacher_work(sigset_t oldmask, pid_t* arr, int parts, int students){
    int confirmations = 0;
    int needed_total = parts * students;
    while(confirmations < needed_total){
        sigsuspend(&oldmask);
        if(last_signal == SIGUSR1){
            confirmations++;
            printf("[%d] Teacher confirms the task completion for student %d\n",getpid(), sender_pid);
            kill(sender_pid, SIGUSR2);
            last_signal = 0;
        }
    }
    kill(0, SIGUSR2);
}

int main(int argc, char* argv[])
{
    if(argc < 4){
        usage(argc, argv);
    }
    int parts = atoi(argv[1]);
    if(parts < 1 || parts > 10){
        usage( argc, argv);
    }
    int difficulty = atoi(argv[2]);
    if(difficulty < 1 || difficulty > 10){
        usage( argc, argv);
    }
    int students_num = argc - 3;
    pid_t student_IDs[students_num];
    int probabilities[students_num];
    for(int i = 0; i < students_num; i++){
        probabilities[i] = atoi(argv[i + 3]);
    }
    //int issue_counts[students_num];

    set_SIGUSR1_handler(SIGUSR1_handler, SIGUSR1);
    sethandler(sig_handler, SIGUSR2);
    //sethandler(sigchld_handler, SIGCHLD);

    sigset_t mask, oldmask;
    sigemptyset(&mask);
    //sigaddset(&mask, SIGCHLD);
    sigaddset(&mask, SIGUSR1);
    sigaddset(&mask, SIGUSR2);
    sigprocmask(SIG_BLOCK, &mask, &oldmask);


    create_students(students_num, probabilities, parts, difficulty, student_IDs);
    teacher_work(oldmask, student_IDs, parts, students_num);

    int final_issues[students_num];
    pid_t final_pids[students_num];
    int status;
    int students_done = 0;
    pid_t child_pid;

    printf("Laboratory finished\n\n");

    while ((child_pid = wait(&status)) > 0) {
        if (WIFEXITED(status)) {

            int issue_count = WEXITSTATUS(status); 
            final_pids[students_done] = child_pid;
            final_issues[students_done] = issue_count;
            students_done++;

        }
    }

    printf("No. | Student ID | Issue count\n");
    for(int j = 0; j < students_num; j++){
        printf("%3d | %10d | %3d\n", j + 1, final_pids[j], final_issues[j]);
    }

    return EXIT_SUCCESS;
}