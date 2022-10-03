#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

int main(int argc, char *argv[])
{
  if (argc < 3)
  {
    printf("Usage: strace mask command [args]\n", argv[0]);
    return 1;
  }

  int mask = atoi(argv[1]);
  char *procName = argv[2];
  char *args[argc - 1];

  for (int i = 2; i < argc; i++)
  {
    args[i - 2] = argv[i];
  }
  args[argc - 2] = '\0';

  trace(mask);
  int pid = fork();
  
  if (pid == 0)
  {
    exec(procName, args);
  }
  else
  {
    int status;
    wait(&status);
  }

  return 0;
}
