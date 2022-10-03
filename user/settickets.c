#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

int main(int argc, char *argv[])
{
  if (argc != 2)
  {
    printf("Usage: settickets <ticketsWanted>\n", argv[0]);
    return 1;
  }

  int tickets = atoi(argv[1]);
  settickets(tickets);
  return 0;
}
