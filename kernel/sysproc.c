#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64
sys_trace(void)
{
  int mask;
  argint(0, &mask);
  if(mask < 2)
  {
    return -1;
  }
  myproc()->trace = mask;
  return 0;
}

uint64
sys_settickets(void)
{
  int tickets;
  argint(0, &tickets);
  if(tickets < 1)
  {
    return -1;
  }

  acquire(&myproc()->lock);

  // printf("Tickets before: %d\n", myproc()->tickets);

  myproc()->tickets = tickets;
  
  // printf("Tickets after: %d\n", myproc()->tickets);
  
  release(&myproc()->lock);
  return 0;
}

uint64
sys_set_priority(void)
{
  int priority, process_id;
  argint(0, &priority);
  argint(1, &process_id);

  uint64 returnVal;
  struct proc *p;
  p = getProc(process_id);

  if(!p)
  {
    return -1;
  }

  returnVal = p->priority;
  p->priority = priority;
  p->niceness = 5;

  release(&p->lock);
  

  return returnVal;
}