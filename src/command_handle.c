#include <sys/wait.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>
#include <json.h>
#include <fcntl.h>
#include <pthread.h>

#define BUFFERSIZE 1024
int run_command_block(const char * command, json_object * data,
                      void (*cb)(const char * line, int line_no, json_object * cb_data)){
    FILE * fp;
    fp = popen(command,"r");
    char line[BUFFERSIZE];
    if(fp == NULL)
        return 1;
    int line_no = 0;
    if (NULL != cb) {
        while(fgets(line,sizeof(line),fp)) {
            cb(line,line_no,data);
            line_no++;
        }
    }
    return pclose(fp);
}

/**
 * @param execute
 * @param args normally args[0] is same to execute and the last element should be NULL.
 */
int run_command_nonblock(const char * execute, char * const args[], pid_t * pid_task) {
    static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&mutex);
    int status = 0;
    int error = 0;
    const char * shm_mame = "openvz-daemon-task-pid";
    int shm_fd = shm_open(shm_mame, O_CREAT | O_TRUNC | O_RDWR, S_IRUSR|S_IWUSR);
    ftruncate(shm_fd, sizeof(pid_t));
    pid_t * shm = (pid_t *)mmap(NULL, sizeof(pid_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

    pid_t pid1 = fork();

    if (pid1 > 0) {
        /* parent process A */
        waitpid(pid1, &status, 0);
        *pid_task = *shm;
        shm_unlink(shm_mame);
        munmap(shm, sizeof(pid_t));
    } else if (0 == pid1) {
        /* child process B */
        pid_t pid2 = fork();
        if (pid2 > 0) {
            *shm = pid2;
            exit(0);
        } else if (0 == pid2) {
            pid_t pid3 = getpid();
            char filename_stdout[BUFFERSIZE];
            char filename_stderr[BUFFERSIZE];
            snprintf(filename_stdout, BUFFERSIZE, "/tmp/openvz-daemon-%d.out", pid3);
            snprintf(filename_stderr, BUFFERSIZE, "/tmp/openvz-daemon-%d.err", pid3);
            int fd_stdout = open(filename_stdout, O_WRONLY|O_CREAT, S_IRUSR);
            int fd_stderr = open(filename_stderr, O_WRONLY|O_CREAT, S_IRUSR);
            dup2(fd_stdout, 1);
            dup2(fd_stderr, 2);
            /* child process C */
            execvp(execute, args);
            close(fd_stdout);
            close(fd_stderr);
        } else {
            exit(1);
        }
    } else {
        error = 1;
    }
    pthread_mutex_unlock(&mutex);
    return error || status;
}