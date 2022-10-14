#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

static unsigned long int next = 1;
// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void
proc_mapstacks(pagetable_t kpgtbl)
{
  struct proc *p;
  
  for(p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    if(pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int) (p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// initialize the proc table.
void
procinit(void)
{
  struct proc *p;
  
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");
      p->state = UNUSED;
      p->kstack = KSTACK((int) (p - proc));
  }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu*
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
myproc(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int
allocpid()
{
  int pid;
  
  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;
  p->createTime = ticks;
  p->priority = 60;
  p->timesScheduled = 0;
  p->trace = 0;
  p->tickets = 1;
  p->niceness = 5;
  p->lastSlept = -1;
  p->lastScheduled = 0;
  p->timeRun = 0;
  # ifdef MLFQ
  p->timeRanInQueue = 0;
  # endif
  p->timeSlept = 0;
  p->alarmFreq = 0;
  p->lastAlarm = 0;
  p->alarmRunning = 0;

  // Allocate a trapframe page.
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;
  p->rtime = 0;
  p->etime = 0;
  p->ctime = ticks;

  # ifdef MLFQ

  p->entryTime = ticks;
  p->queue = 0;
  p->timeRanInQueue = 0;

  # endif

  return p;
}

struct proc* getProc(int pid){
  struct proc *p;
  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->pid == pid) {
      return p;
    }
    release(&p->lock);
  }
  return 0;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
}

// Create a user page table for a given process, with no user memory,
// but with trampoline and trapframe pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe page just below the trampoline page, for
  // trampoline.S.
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// assembled from ../user/initcode.S
// od -t xC ../user/initcode
uchar initcode[] = {
  0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
  0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
  0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
  0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
  0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
  0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

// Set up first user process.
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;
  
  // allocate one user page and copy initcode's instructions
  // and data into it.
  uvmfirst(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // prepare for the very first "return" from kernel to user.
  p->trapframe->epc = 0;      // user program counter
  p->trapframe->sp = PGSIZE;  // user stack pointer

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
waitx(uint64 addr, uint* wtime, uint* rtime)
{
  struct proc *np;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(np = proc; np < &proc[NPROC]; np++){
      if(np->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&np->lock);

        havekids = 1;
        if(np->state == ZOMBIE){
          // Found one.
          pid = np->pid;
          *rtime = np->rtime;
          *wtime = np->etime - np->ctime - np->rtime;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&np->xstate,
                                  sizeof(np->xstate)) < 0) {
            release(&np->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(np);
          release(&np->lock);
          release(&wait_lock);
          return pid;
        }
        release(&np->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || p->killed){
      release(&wait_lock);
      return -1;
    }

    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

void
update_time()
{
  struct proc* p;
  for (p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if (p->state == RUNNING) {
      p->rtime++;
    }
    release(&p->lock); 
  }
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  if(np->parent)
  {
    np->trace = np->parent->trace;
    np->tickets = np->parent->tickets;
  }
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
exit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);
  
  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;
  p->etime = ticks;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// get number of digits in a number (makes for nicer formatting later)
int
num_digits(int num)
{
  int digits = 0;
  if (num == 0)
    return 1;
  while (num > 0) {
    num /= 10;
    digits++;
  }
  return digits;
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(pp = proc; pp < &proc[NPROC]; pp++){
      if(pp->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&pp->lock);

        havekids = 1;
        if(pp->state == ZOMBIE){
          // Found one.
          pid = pp->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                  sizeof(pp->xstate)) < 0) {
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(pp);
          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || killed(p)){
      release(&wait_lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

// age all the processes if MLFQ is enabled
# ifdef MLFQ
void
age(void)
{
  struct proc *p;
  for(p = proc; p < &proc[NPROC]; p++)
  {
    acquire(&p->lock);
    if (p->state == RUNNABLE || p->state == SLEEPING)
    {
      if (p->queue){
        int limit;
        if (p->queue == 1)
          limit = 50;
        else if (p->queue == 2)
          limit = 100;
        else if (p->queue == 3)
          limit = 150;
        else
          limit = 200;
        // p->timeInQueue = ticks - p->entryTime
        int waitTime = (int)(ticks - p->entryTime - p->timeRanInQueue);

        if (waitTime >= limit)
        {
          p->queue--; 
          p->timeRanInQueue = 0;
          p->entryTime = ticks;
        }
      }
    }
    release(&p->lock);
  }
}
# endif

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  
  c->proc = 0;
  for(;;){
    srand(ticks);
    // Avoid deadlock by ensuring that devices can interrupt.
    intr_on();

    #ifdef RR
    // printf("RR\n");
    for(p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if(p->state == RUNNABLE) {
        // Switch to chosen process.  It is the process's job
        // to release its lock and then reacquire it
        // before jumping back to us.
        p->state = RUNNING;
        c->proc = p;
        swtch(&c->context, &p->context);
      
        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
      }

      release(&p->lock);
    }
    #endif

    #ifdef FCFS
    struct proc *min = 0;
    for (p = proc; p < &proc[NPROC]; p++)
    {
      acquire(&p->lock);
      if (p->state == RUNNABLE && (min == 0 || p->createTime < min->createTime))
      {
        min = p;
      }
      release(&p->lock);
    }

    if (min)
    {
      acquire(&min->lock);
      if (min->state == RUNNABLE)
      {
        min->state = RUNNING;
        c->proc = min;
        swtch(&c->context, &min->context);
        c->proc = 0;
      }
      release(&min->lock);
    }
    #endif

    #ifdef LBS
    // get total tickets
    int totalTickets = 0;
    for (p = proc; p < &proc[NPROC]; p++)
    {
      acquire(&p->lock);
      if (p->state == RUNNABLE)
      {
        totalTickets += p->tickets;
      }
      release(&p->lock);
    }

    int random = rand() % totalTickets;

    struct proc *winner = 0;
    for (p = proc; p < &proc[NPROC]; p++)
    {
      acquire(&p->lock);
      if (p->state == RUNNABLE && !winner)
      {
        random -= p->tickets;
        if (random < 0)
        {
          winner = p;
        }
      }

      if(p != winner)
        release(&p->lock);
    }

    if (winner)
    {
      if (winner->state == RUNNABLE)
      {
        winner->state = RUNNING;
        c->proc = winner;
        swtch(&c->context, &winner->context);
        c->proc = 0;
      }
      release(&winner->lock);
    }

    #endif


    #ifdef PBS
    // printf("PBS\n");
    struct proc *max = 0;
    for (p = proc; p < &proc[NPROC]; p++)
    {
      acquire(&p->lock);
      if (p->state == RUNNABLE)
      {
        if(!max)
        {
          max = p;
        }
        else
        {
          int newDp = getDP(p);
          int maxDp = getDP(max);

          if(newDp < maxDp)
          {
            max = p;
          }
          else if(newDp == maxDp)
          {
            if(p->timesScheduled < max->timesScheduled)
            {
              max = p;
            }
            else if (p->timesScheduled == max->timesScheduled)
            {
              if(p->createTime < max->createTime)
              {
                max = p;
              }
            }
          }
        }
      }
      release(&p->lock);
    }

    if (max)
    {
      acquire(&max->lock);
      if (max->state == RUNNABLE)
      {
        max->state = RUNNING;
        c->proc = max;
        max->timesScheduled++;
        max->lastScheduled = ticks;
        swtch(&c->context, &max->context);
        c->proc = 0;
      }
      release(&max->lock);
    }

    #endif

    # ifdef MLFQ

    // aging for processes
    // age();
    int limit;

    // if the process has been in the queue for long enough, demote it
    for (p = proc; p < &proc[NPROC]; p++){
      acquire(&p->lock);
      if (p->state == RUNNING || p->state == RUNNABLE){
        if (p->queue == 0)
          limit = 1;
        else if (p->queue == 1)
          limit = 2;
        else if (p->queue == 2)
          limit = 4;
        else if (p->queue == 3)
          limit = 8;
        else
          limit = 16;
        if (p->timeRanInQueue >= limit && p->queue < 4)
        {
          p->queue++;
          p->entryTime = ticks;
          p->timeRanInQueue = 0;          
        }
      }
      release(&p->lock);
    }
    age();
    
    // select the process to run
    struct proc *procToRun = 0;
    for (p = proc; p < &proc[NPROC]; p++)
    {
      acquire(&p->lock);
      if (p->state == RUNNABLE && p->queue == 0)
      {
        if (!procToRun)
        {
          procToRun = p;
        }
        else
        {
          if (p->queue < procToRun->queue)
          {
            procToRun = p;
          }
          else if (p->queue == procToRun->queue)
          {
            if (p->entryTime < procToRun->entryTime)
            {
              procToRun = p;
            }
          }
        }
      }
      release(&p->lock);
    }

    // loop over queue = 1 now
    for (p = proc; p < &proc[NPROC]; p++)
    {
      acquire(&p->lock);
      if (p->state == RUNNABLE && p->queue == 1)
      {
        if (!procToRun)
        {
          procToRun = p;
        }
        else
        {
          if (p->queue < procToRun->queue)
          {
            procToRun = p;
          }
          else if (p->queue == procToRun->queue)
          {
            if (p->entryTime < procToRun->entryTime)
            {
              procToRun = p;
            }
          }
        }
      }
      release(&p->lock);
    }

    // loop over queue = 2 now
    for (p = proc; p < &proc[NPROC]; p++)
    {
      acquire(&p->lock);
      if (p->state == RUNNABLE && p->queue == 2)
      {
        if (!procToRun)
        {
          procToRun = p;
        }
        else
        {
          if (p->queue < procToRun->queue)
          {
            procToRun = p;
          }
          else if (p->queue == procToRun->queue)
          {
            if (p->entryTime < procToRun->entryTime)
            {
              procToRun = p;
            }
          }
        }
      }
      release(&p->lock);
    }

    // loop over queue = 3 now
    for (p = proc; p < &proc[NPROC]; p++)
    {
      acquire(&p->lock);
      if (p->state == RUNNABLE && p->queue == 3)
      {
        if (!procToRun)
        {
          procToRun = p;
        }
        else
        {
          if (p->queue < procToRun->queue)
          {
            procToRun = p;
          }
          else if (p->queue == procToRun->queue)
          {
            if (p->entryTime < procToRun->entryTime)
            {
              procToRun = p;
            }
          }
        }
      }
      release(&p->lock);
    }

    // if process found yet, run it
    if (procToRun)
    {
      acquire(&procToRun->lock);
      if (procToRun->state == RUNNABLE)
      {
        procToRun->state = RUNNING;
        c->proc = procToRun;
        // procToRun->entryTime = ticks;
        procToRun->lastScheduled = ticks; 
        swtch(&c->context, &procToRun->context);
        c->proc = 0;
      }
      release(&procToRun->lock);
    }

    // process not found in queues 0,1,2,3; now run round robin (rr) over queue 4
    for(p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if(p->state == RUNNABLE && p->queue == 4) {
        // Switch to chosen process.  It is the process's job
        // to release its lock and then reacquire it
        // before jumping back to us.
        p->state = RUNNING;
        c->proc = p;
        swtch(&c->context, &p->context);
      
        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
      }

      release(&p->lock);
    }

    # endif
  }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&p->lock))
    panic("sched p->lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  p->timeRun += ticks - p->lastScheduled;
  # ifdef MLFQ
  p->timeRanInQueue += ticks - p->lastScheduled;
  # endif

  if(p->timeSlept + p->timeRun)
    p->niceness = (10 * (p->timeSlept)) / (p->timeSlept + p->timeRun);

  sched();
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  static int first = 1;

  // Still holding p->lock from scheduler.
  release(&myproc()->lock);

  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    first = 0;
    fsinit(ROOTDEV);
  }

  usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock);  //DOC: sleeplock1
  release(lk);

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;
  p->lastSlept = ticks;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void
wakeup(void *chan)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    if(p != myproc()){
      acquire(&p->lock);
      if(p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE;
        p->timeSlept += ticks - p->lastSlept;
        if(p->timeSlept + p->timeRun)
          p->niceness = (10 * (p->timeSlept)) / (p->timeSlept + p->timeRun);
        p->lastSlept = -1;
      }
      release(&p->lock);
    }
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kill(int pid)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

void
setkilled(struct proc *p)
{
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

int
killed(struct proc *p)
{
  int k;
  
  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{

  // #ifdef MLFQ
  // while (1)
  //   {
  // #endif

  static char *states[] = {
  [UNUSED]    "Unused  ",
  [USED]      "Used    ",
  [SLEEPING]  "Sleeping",
  [RUNNABLE]  "Runnable",
  [RUNNING]   "Running ",
  [ZOMBIE]    "Zombie  "
  };
  struct proc *p;
  char *state;

  printf("\n");

  #ifdef RR
  printf("Procdump: Round Robin Scheduler\n\n");
  #endif

  #ifdef FCFS
  printf("Procdump: First Come First Serve Scheduler\n\n");
  #endif

  #ifdef PBS
  printf("Procdump: Priority Based Scheduler\n\n");
  #endif

  #ifdef LBS
  printf("Procdump: Lottery Based Scheduler\n\n");
  #endif
  
  #ifndef MLFQ
  printf("PID        State          Time Run       Time Slept      Name       \n");
  #endif

  #ifdef MLFQ
  printf("Procdump: Multi Level Feedback Queue Scheduler %d\n\n", ticks);
  printf("PID        State          Queue      Time Run       Time Wait      Lastsched                Name       \n");
  #endif

  #ifndef MLFQ
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???     ";
    int len_pid = num_digits(p->pid);
    int len_timerun = num_digits(p->timeRun);
    int len_timeslept = num_digits(p->timeSlept);
    printf("%d", p->pid);
    // print spaces such that the next column is aligned
    for(int i = 0; i < 11 - len_pid; i++)
      printf(" ");
    printf("%s       ", state);
    printf("%d", p->timeRun);
    for(int i = 0; i < 15 - len_timerun; i++)
      printf(" ");
    printf("%d", p->timeSlept);
    for(int i = 0; i < 16 - len_timeslept; i++)
      printf(" ");
    printf("%s\n", p->name);    
  }
  printf("\n");
  #endif

  #ifdef MLFQ
  // this time, keep running the loop, sleeping for 0.5 seconds
    for(p = proc; p < &proc[NPROC]; p++){
      if(p->state == UNUSED)
        continue;
      if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
        state = states[p->state];
      else
        state = "???     ";
      uint waitTime = ticks - p->entryTime - p->timeRanInQueue;
      
      int len_pid = num_digits(p->pid);
      int len_timerun = num_digits(p->timeRanInQueue);
      int len_timeslept = num_digits(waitTime);
      int len_queue = num_digits(p->queue);
      int len_lastsched = num_digits(p->lastScheduled);
      printf("%d", p->pid);
      // print spaces such that the next column is aligned
      for(int i = 0; i < 11 - len_pid; i++)
        printf(" ");
      printf("%s       ", state);
      printf("%d", p->queue);
      for(int i = 0; i < 11 - len_queue; i++)
        printf(" ");
      printf("%d", p->timeRanInQueue);
      for(int i = 0; i < 14 - len_timerun; i++)
        printf(" ");
      printf("%d", waitTime);
      for(int i = 0; i < 15 - len_timeslept; i++)
        printf(" ");
      printf("%d", p->lastScheduled);
      for(int i = 0; i < 15 - len_lastsched; i++)
        printf(" ");


      printf("%s\n", p->name);    
    }
    printf("\n");
  #endif

  // #ifdef MLFQ
  //   // sleep(&p->chan, &p->lock);
  //   }
  // #endif
  
}

int rand(void) 
{
    next = next * 1103515245 + 12345;
    return (unsigned int)(next/65536) % 32768;
}

void srand(unsigned int seed)
{
  if(seed == 0)
    next = seed;
}

int getDP(struct proc *p)
{
  int dp = p->priority - p->niceness + 5;
  if (dp > 100)
  {
    dp = 100;
  }

  if (dp < 0)
  {
    dp = 0;
  }

  return dp;
}