#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

int main(int argc, char *argv[])
{
  if(argc != 3)
  {
    printf("Usage: setpriority priority pid\n");
    return 1;
  }

  // printf("User program setpriority\n");
  set_priority(atoi(argv[1]), atoi(argv[2]));
  return 0;
}
