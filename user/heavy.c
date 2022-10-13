#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

int main(int argc, char *argv[])
{
    for(int i = 0; i < 20 ; i++)
    {
        int pid;
        pid = fork();
        pid = fork();
        pid = fork();
        if (pid < 0) break;
        if (i%2 == 1) {
            sleep (2000);
        }
        else {
            for (int j = 0; j < 100000000; j++){
                printf("\0");
            }
        }
        exit(0);
    }

    printf("Done\n");
    return 0;
}
