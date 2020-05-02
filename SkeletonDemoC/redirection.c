/*

Как работи shell?
Пример на системните извиквания при пренасочване.

Имитира команда за shell "ps -a > demo.txt".

(c) народна песен на C, стил POSIX, по лиценза GNU GPL

*/

#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>

int main(void) {
    int pid, fd;
    pid=fork();


    if (pid > 0) {
        wait(NULL);
    }

    char* args[] = { "ps", "-a", (char *)0 };
    if (pid==0) {
        fd = open("demo.txt",O_WRONLY|O_CREAT,00640);
        close(STDOUT_FILENO);
        dup(fd);
        close(fd);
        execvp(args[0], args);
    }
    if (pid<0) perror("fork");
}
