#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

void
trapinit(void)
{
  initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//
void
usertrap(void)
{
  int which_dev = 0;

  // printf("Usertrap \n");

  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();
  
  // save user program counter.
  p->trapframe->epc = r_sepc();
  
  if(r_scause() == 8){
    // system call

    if(killed(p))
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    p->trapframe->epc += 4;

    // an interrupt will change sepc, scause, and sstatus,
    // so enable only now that we're done with those registers.
    intr_on();

    syscall();
  } 
  else if (r_scause() == 15) {
    int va = r_stval();
    if (va >= MAXVA)
    {
      setkilled(p);
      exit(-1);
    }
    if (va == 0)
    {
      setkilled(p);
      exit(-1);
    }

    pte_t* pte = walk(p->pagetable, va, 0);
    if (pte == 0)
    {
      setkilled(p);
      exit(-1);
    } 


    if (!(*pte & PTE_W) && !(*pte & PTE_COW))
    {
      setkilled(p);
      exit(-1);
    }


    // check for user and valid
    if ((*pte & PTE_W) || !(*pte & PTE_U) || !(*pte & PTE_V))
    {
      setkilled(p);
      exit(-1);
    }

    uint64 new = (uint64)kalloc();

    if (new == 0)
    {
      setkilled(p);
      exit(-1);
    }

    memmove((char *)new, (char *)PTE2PA(*pte), PGSIZE);
    // since this is the faulting process, we could assign it the new page, since we do not know which other child has it

    uint64 old = PTE2PA(*pte);
    *pte = PA2PTE(new) | PTE_V | PTE_R | PTE_W | PTE_U | PTE_X;
    *pte &= ~PTE_COW;

    kfree((char *)old);
  }
  else if((which_dev = devintr()) != 0){
    // ok
  } else {
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    setkilled(p);
  }

  if(killed(p))
    exit(-1);

  // give up the CPU if this is a timer interrupt.
  
  if(which_dev == 2)
  {
    // printf("\nUser clockintr at tick: %d\n", ticks);

    acquire(&p->lock);
    // printf("Alarm Frequency: %d\n", p->alarmFreq);
    if(p->alarmFreq && !p->alarmRunning && p->timeRun - p->lastAlarm >= p->alarmFreq)
    {
      p->alarmRunning = 1;
      uint64 Handler;
      Handler = p->alarmHandler;
      p->backupTrapFrame = kalloc();
      memmove(p->backupTrapFrame, p->trapframe, sizeof(struct trapframe));
      p->lastAlarm = p->timeRun;
      p->trapframe->epc = (uint64)Handler;
    }
    release(&p->lock);

    #if !defined(FCFS) && !defined(PBS)
    yield();
    #endif
  }
  
  usertrapret();
}

//
// return to user space
//
void
usertrapret(void)
{
  struct proc *p = myproc();

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(), so turn off interrupts until
  // we're back in user space, where usertrap() is correct.
  intr_off();

  // send syscalls, interrupts, and exceptions to uservec in trampoline.S
  uint64 trampoline_uservec = TRAMPOLINE + (uservec - trampoline);
  w_stvec(trampoline_uservec);

  // set up trapframe values that uservec will need when
  // the process next traps into the kernel.
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.
  
  // set S Previous Privilege mode to User.
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  w_sstatus(x);

  // set S Exception Program Counter to the saved user pc.
  w_sepc(p->trapframe->epc);

  // tell trampoline.S the user page table to switch to.
  uint64 satp = MAKE_SATP(p->pagetable);

  // jump to userret in trampoline.S at the top of memory, which 
  // switches to the user page table, restores user registers,
  // and switches to user mode with sret.
  uint64 trampoline_userret = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64))trampoline_userret)(satp);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void 
kerneltrap()
{

  // printf("Kerneltrap:");
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  
  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if((which_dev = devintr()) == 0){
    printf("scause %p\n", scause);
    printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
    panic("kerneltrap: Unknown interrupt");
  }

  // give up the CPU if this is a timer interrupt.
 
  
  if(which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING)
  {
    // printf("\nKernel yielding at tick: %d with process: %s\n", ticks, myproc()->name);  
    // struct proc *p = myproc();
    // acquire(&p->lock); 
    // if(p->alarmFreq && !p->alarmRunning && p->timeRun - p->lastAlarm >= p->alarmFreq)
    // {
    //   p->alarmRunning = 1;
    //   uint64 Handler;
    //   Handler = p->alarmHandler;
    //   p->backupTrapFrame = kalloc();
    //   memmove(p->backupTrapFrame, p->trapframe, sizeof(struct trapframe));
    //   p->lastAlarm = p->timeRun;
    //   p->trapframe->epc = (uint64)Handler;
    // }
    // release(&p->lock);

    #if !defined(FCFS) && !defined(PBS)
    yield();
    #endif
  }

  // printf("no: %d\n", which_dev);
  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_sepc(sepc);
  w_sstatus(sstatus);
}

void
clockintr()
{
  acquire(&tickslock);
  ticks++;
  update_time();
  // printf("\nCurrent Tick: %d\n", ticks);
  wakeup(&ticks);
  release(&tickslock);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int
devintr()
{
  uint64 scause = r_scause();

  if((scause & 0x8000000000000000L) &&
     (scause & 0xff) == 9){
    // this is a supervisor external interrupt, via PLIC.

    // irq indicates which device interrupted.
    int irq = plic_claim();

    if(irq == UART0_IRQ){
      uartintr();
    } else if(irq == VIRTIO0_IRQ){
      virtio_disk_intr();
    } else if(irq){
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if(irq)
      plic_complete(irq);

    return 1;
  } else if(scause == 0x8000000000000001L){
    // software interrupt from a machine-mode timer interrupt,
    // forwarded by timervec in kernelvec.S.

    if(cpuid() == 0){
      clockintr();
    }
    
    // acknowledge the software interrupt by clearing
    // the SSIP bit in sip.
    w_sip(r_sip() & ~2);

    return 2;
  } else {
    return 0;
  }
}

