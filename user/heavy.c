#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

int main(int argc, char *argv[])
{
    for(int i = 0; i < 1000 * 500000 ; i++)
    {
        printf("\0");
    }

    printf("Done\n");
    return 0;
}
