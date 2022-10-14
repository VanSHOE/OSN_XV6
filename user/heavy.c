#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

int main(int argc, char *argv[])
{

    sleep(20);

    for(int i = 0; i < 4 ; i++)
    {
        int pid;
        pid = fork();
        // pid = fork();
        // pid = fork();
        if (pid < 0) break;
        if (i%2 == 0) {
            sleep (10);
        }
        else {
            // printf("start");
            for (int j = 0; j < 10000000; j++){
                printf("\0");
            }
            // printf("end");
        }
        // exit(0);
    }

    // printf("Done\n");
    return 0;
}
