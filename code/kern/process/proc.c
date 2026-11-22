#include <proc.h>
#include <kmalloc.h>
#include <string.h>
#include <sync.h>
#include <pmm.h>
#include <error.h>
#include <sched.h>
#include <elf.h>
#include <vmm.h>
#include <trap.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

/* ------------- process/thread mechanism design&implementation -------------
(an simplified Linux process/thread mechanism )
introduction:
  ucore implements a simple process/thread mechanism. process contains the independent memory sapce, at least one threads
for execution, the kernel data(for management), processor state (for context switch), files(in lab6), etc. ucore needs to
manage all these details efficiently. In ucore, a thread is just a special kind of process(share process's memory).
------------------------------
process state       :     meaning               -- reason
    PROC_UNINIT     :   uninitialized           -- alloc_proc
    PROC_SLEEPING   :   sleeping                -- try_free_pages, do_wait, do_sleep
    PROC_RUNNABLE   :   runnable(maybe running) -- proc_init, wakeup_proc,
    PROC_ZOMBIE     :   almost dead             -- do_exit

-----------------------------
process state changing:

  alloc_proc                                 RUNNING
      +                                   +--<----<--+
      +                                   + proc_run +
      V                                   +-->---->--+
PROC_UNINIT -- proc_init/wakeup_proc --> PROC_RUNNABLE -- try_free_pages/do_wait/do_sleep --> PROC_SLEEPING --
                                           A      +                                                           +
                                           |      +--- do_exit --> PROC_ZOMBIE                                +
                                           +                                                                  +
                                           -----------------------wakeup_proc----------------------------------
-----------------------------
process relations
parent:           proc->parent  (proc is children)
children:         proc->cptr    (proc is parent)
older sibling:    proc->optr    (proc is younger sibling)
younger sibling:  proc->yptr    (proc is older sibling)
-----------------------------
related syscall for process:
SYS_exit        : process exit,                           -->do_exit
SYS_fork        : create child process, dup mm            -->do_fork-->wakeup_proc
SYS_wait        : wait process                            -->do_wait
SYS_exec        : after fork, process execute a program   -->load a program and refresh the mm
SYS_clone       : create child thread                     -->do_fork-->wakeup_proc
SYS_yield       : process flag itself need resecheduling, -- proc->need_sched=1, then scheduler will rescheule this process
SYS_sleep       : process sleep                           -->do_sleep
SYS_kill        : kill process                            -->do_kill-->proc->flags |= PF_EXITING
                                                                 -->wakeup_proc-->do_wait-->do_exit
SYS_getpid      : get the process's pid

*/

// the process set's list
list_entry_t proc_list;

#define HASH_SHIFT 10
#define HASH_LIST_SIZE (1 << HASH_SHIFT)
#define pid_hashfn(x) (hash32(x, HASH_SHIFT))

// has list for process set based on pid
static list_entry_t hash_list[HASH_LIST_SIZE];

// idle proc
struct proc_struct *idleproc = NULL;
// init proc
struct proc_struct *initproc = NULL;
// current proc
struct proc_struct *current = NULL;

static int nr_process = 0;

void kernel_thread_entry(void);
void forkrets(struct trapframe *tf);
void switch_to(struct context *from, struct context *to);

// set_proc_name - set the name of proc
char *
set_proc_name(struct proc_struct *proc, const char *name)
{
    memset(proc->name, 0, sizeof(proc->name));
    return memcpy(proc->name, name, PROC_NAME_LEN);
}

// get_proc_name - get the name of proc
char *
get_proc_name(struct proc_struct *proc)
{
    static char name[PROC_NAME_LEN + 1];
    memset(name, 0, sizeof(name));
    return memcpy(name, proc->name, PROC_NAME_LEN);
}

// get_pid - alloc a unique pid for process
static int
get_pid(void)
{
    static_assert(MAX_PID > MAX_PROCESS);
    struct proc_struct *proc;
    list_entry_t *list = &proc_list, *le;
    static int next_safe = MAX_PID, last_pid = MAX_PID;
    if (++last_pid >= MAX_PID)
    {
        last_pid = 1;
        goto inside;
    }
    if (last_pid >= next_safe)
    {
    inside:
        next_safe = MAX_PID;
    repeat:
        le = list;
        while ((le = list_next(le)) != list)
        {
            proc = le2proc(le, list_link);
            if (proc->pid == last_pid)
            {
                if (++last_pid >= next_safe)
                {
                    if (last_pid >= MAX_PID)
                    {
                        last_pid = 1;
                    }
                    next_safe = MAX_PID;
                    goto repeat;
                }
            }
            else if (proc->pid > last_pid && next_safe > proc->pid)
            {
                next_safe = proc->pid;
            }
        }
    }
    return last_pid;
}


// forkret -- the first kernel entry point of a new thread/process
// NOTE: the addr of forkret is setted in copy_thread function
//       after switch_to, the current proc will execute here.
static void
forkret(void)
{
    forkrets(current->tf);
}

// hash_proc - add proc into proc hash_list
static void
hash_proc(struct proc_struct *proc)
{
    list_add(hash_list + pid_hashfn(proc->pid), &(proc->hash_link));
}

// find_proc - find proc frome proc hash_list according to pid
struct proc_struct *
find_proc(int pid)
{
    if (0 < pid && pid < MAX_PID)
    {
        list_entry_t *list = hash_list + pid_hashfn(pid), *le = list;
        while ((le = list_next(le)) != list)
        {
            struct proc_struct *proc = le2proc(le, hash_link);
            if (proc->pid == pid)
            {
                return proc;
            }
        }
    }
    return NULL;
}


// setup_kstack - alloc pages with size KSTACKPAGE as process kernel stack
static int
setup_kstack(struct proc_struct *proc)
{
    struct Page *page = alloc_pages(KSTACKPAGE);
    if (page != NULL)
    {
        proc->kstack = (uintptr_t)page2kva(page);
        return 0;
    }
    return -E_NO_MEM;
}

// put_kstack - free the memory space of process kernel stack
static void
put_kstack(struct proc_struct *proc)
{
    free_pages(kva2page((void *)(proc->kstack)), KSTACKPAGE);
}


// do_exit - called by sys_exit
//   1. call exit_mmap & put_pgdir & mm_destroy to free the almost all memory space of process
//   2. set process' state as PROC_ZOMBIE, then call wakeup_proc(parent) to ask parent reclaim itself.
//   3. call scheduler to switch to other process
int do_exit(int error_code)
{
    panic("process exit!!.\n");
}

// init_main - the second kernel thread used to create user_main kernel threads
static int
init_main(void *arg)
{
    cprintf("this initproc, pid = %d, name = \"%s\"\n", current->pid, get_proc_name(current));
    cprintf("To U: \"%s\".\n", (const char *)arg);
    cprintf("To U: \"en.., Bye, Bye. :)\"\n");
    return 0;
}

//LAB4 function2 （六个函数的顺序：1，alloc_proc；2，copy_mm；3，copy_thread；4，do_fork；5，kernel_thread；6，proc_init）
//全局问题：如何保证代码正确？
/*实验指导说明书里给出了要实现的函数以及数据结构，在ai完成代码后我需要做的主要是，
检查变量被调用时是否已经创建、调用失败时是否释放资源避免污染内存、函数返回时有哪些全局状态必须成立
以及半成品是否会被提前使用*/

//全局问题：我的这个模块在做什么？
/*lab4的目标是把内核从单一控制流，改造成可以并发运行的系统
lab4之前，kern_init初始化之后进入死循环，所有工作串行完成，没有进程和线程的概念，也没法并发执行
lab4引入了内核线程，每个内核线程都有自己的PCB、内核栈、上下文和运行状态，但是共享内核地址空间
PCB记录每个线程的信息比如身份（pid）；系统启动时没有父进程，所以第零个只能复用内核现场；第一个才能通过函数+参数正式创建
新线程没有上次的现场，所以要人为构造一个上下文：给它入口、栈、trapframe或context
线程被创建后进入就绪状态，挂在链表或者哈希表上，拥有唯一的pid，等待被调度器找到
cpu在多个线程之间切换需要保存当前线程的上下文、恢复下一个进程的上下文；对用户进程还要切换页表。调度器决定下一个进程是谁。体现了机制与策略分离的思想
本系统针对单核cpu开发，因此并没有真的同时执行多个线程，而是主动让出cpu，让多个线程轮流使用cpu，在宏观上并发
而我的模块2，负责的是前半段——线程从哪里来、PCB 和内核栈怎么建立、
第一次运行现场怎么构造、创建失败怎么回滚、创建成功后怎么登记为可运行线程*/

//什么是执行现场？上下文（context）+处理器状态（processor state）+执行上下文（execution context）
//在内核中他主要是
//struct trapframe：Trap 发生时保存的完整现场，32 个通用寄存器 + status / epc / badvaddr / cause；
//struct context：线程切换时保存的最小现场，ra、sp、s0~s11。
//PCB在内核是用来存放pid，name，parent，状态，栈等等许多数据的结构体

// alloc_proc - alloc a proc_struct and init all fields of proc_struct
static struct proc_struct *
alloc_proc(void)//负责解决PCB从哪里来、初始状态是否安全
//做的事：在内核里创建一个线程控制块PCB，并且保证这个PCB在没有被完整初始化之前不会被误当成可运行进程
{
    struct proc_struct *proc = kmalloc(sizeof(struct proc_struct));
    if (proc != NULL)
    {
        // 先把整块 PCB 清零，避免未初始化字段影响后续状态判断
        memset(proc, 0, sizeof(struct proc_struct));
        //这是c库自带的，意思是把proc开始这么多个字节全都写为0，因为不能保证内存是干净的

        proc->state = PROC_UNINIT;      // 刚创建，尚未进入调度
        proc->pid = -1;                 // 真正的 pid 由 do_fork 分配
        proc->runs = 0;                 // 运行次数
        proc->kstack = 0;               // 内核栈由 setup_kstack 分配
        proc->need_resched = 0;         // 新线程不主动让出 CPU
        proc->parent = NULL;            // 父进程由 do_fork 设置
        proc->mm = NULL;                // lab4 只创建内核线程，暂无用户地址空间
        proc->tf = NULL;                // trapframe 由 copy_thread 安排
        proc->pgdir = boot_pgdir_pa;    // 内核线程共享内核页目录
        proc->flags = 0;
        memset(proc->name, 0, sizeof(proc->name));

        // 链表节点必须先初始化，之后 list_add/hash_proc 才能安全使用
        //因为双向链表规定空节点的prev和next要指向自己，不是NULL，所以要list_init单独讨论
        list_init(&(proc->list_link));
        list_init(&(proc->hash_link));
    }
    return proc;
}

// copy_mm - 当前阶段只创建内核线程，不复制用户地址空间
//因为lab4只创建内核线程，所有内核线程都在内核态运行，共享内核地址空间，用户地址空间是后续的事
//current->mm的mm是用户地址空间信息，比如VMA链表、页目录等，而内核线程共享内核地址空间，没有用户地址空间
static int
copy_mm(uint32_t clone_flags, struct proc_struct *proc)
{
    (void)clone_flags;
    assert(current->mm == NULL);
    proc->mm = NULL;
    return 0;
}
//为什么要复制用户地址空间？为什么内核不用？对于用户而言，不同进程的同一个地址会映射到不同的物理页，但是他们必须互不影响，这就是进程隔离
//所以用c++的语言来解释的话，必须深拷贝，不能浅拷贝。而内核不一样，不需要独立的用户地址，只在内核态运行且共享内核地址

//新线程没有被运行过，所以没有“上一次的context和trapframe”，但是调度器切换线程需要context，Trap返回需要trapframe
//所以这个函数在自己的内核栈上构造一个“第一次被调度时能正确返回并且执行”的现场
static void
copy_thread(struct proc_struct *proc, uintptr_t esp, struct trapframe *tf)
{
    // 1trapframe 放在内核栈顶部；forkrets 会执行 move sp, a0 跳到它
    proc->tf = (struct trapframe *)(proc->kstack + KSTACKSIZE - sizeof(struct trapframe));
    *(proc->tf) = *tf;

    //3设置trapframe的返回值/栈指针
    // 子线程从 do_fork 返回 0，表示自己是子进程
    proc->tf->gpr.a0 = 0;

    // 内核线程没有用户栈，就让栈指针指向自己的 trapframe 顶部
    proc->tf->gpr.sp = (esp == 0) ? (uintptr_t)proc->tf : esp;

    //2设置第一次运行的context
    // 新线程第一次被调度时，switch_to 返回到 forkret，
    // forkret 再通过 forkrets/__trapret 回到 tf->epc
    proc->context.ra = (uintptr_t)forkret;
    proc->context.sp = (uintptr_t)(proc->tf);
}

// do_fork 
//要么全成功，要么全回退，不准出现一部分成功一部分失败
/*任何一步失败都要释放前面的资源：
alloc_proc 失败：直接返回；
setup_kstack 失败：释放 PCB；
copy_mm 失败：释放内核栈 + PCB。*/
int do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf)
{
    int ret = -E_NO_FREE_PROC;
    struct proc_struct *proc;

    if (nr_process >= MAX_PROCESS)
    {
        goto fork_out;
    }

    ret = -E_NO_MEM;

    // 1. 分配并初始化 PCB
    if ((proc = alloc_proc()) == NULL)
    {
        goto fork_out;
    }

    // 2. 记录父子关系（initproc 的 parent 必须是 idleproc）
    proc->parent = current;

    // 3. 分配内核栈
    if (setup_kstack(proc) != 0)
    {
        goto bad_fork_cleanup_proc;
    }

    // 4. 当前阶段不复制用户地址空间
    if (copy_mm(clone_flags, proc) != 0)
    {
        goto bad_fork_cleanup_kstack;
    }

    // 5. 设置 trapframe 和首次运行的 context
    copy_thread(proc, stack, tf);

    // 6. 分配 pid，并加入 hash 链表和进程链表
    proc->pid = get_pid();
    hash_proc(proc);
    list_add(&proc_list, &(proc->list_link));
    nr_process++;

    // 7. 设为就绪，返回子线程 pid
    wakeup_proc(proc);
    ret = proc->pid;

fork_out:
    return ret;

bad_fork_cleanup_kstack:
    put_kstack(proc);
bad_fork_cleanup_proc:
    kfree(proc);
    goto fork_out;
}

//解决一个普通函数如何变成线程的第一次运行
//内核线程的本质是一个函数+一个参数，但不立刻调用函数，而是先创建PCB和内核栈，然后挂到进程表，等到调度器选中，第一次被调度后从init_main开始执行
//所以kernel_thread要做的是把函数+参数转化成一个第一次被调度时能正确执行的初始上下文
int kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags)
{
    struct trapframe tf;//先创建一个临时trapframe，为未来的调度准备一个入口

    memset(&tf, 0, sizeof(struct trapframe));

    // kernel_thread_entry 从 s0 取函数、从 s1 取参数
    tf.gpr.s0 = (uintptr_t)fn;
    tf.gpr.s1 = (uintptr_t)arg;

    // 首次运行从 kernel_thread_entry 开始，返回 S 模式并开中断
    tf.epc = (uintptr_t)kernel_thread_entry;
    tf.status = (read_csr(sstatus) | SSTATUS_SPP | SSTATUS_SPIE) & ~SSTATUS_SIE;

    return do_fork(clone_flags | CLONE_VM, 0, &tf);
}

// proc_init - 创建 idleproc 和第一个真正的内核线程 initproc
//要解决第一个线程没有父进程的问题
//普通线程可以通过复制当前线程创建，但是系统启动时没有父进程，也没有trapframe可以复制，只有一个kern_init
void proc_init(void)
{
    int i;

    // 初始化全局进程链表和 hash 表
    list_init(&proc_list);
    for (i = 0; i < HASH_LIST_SIZE; i++)
    {
        list_init(hash_list + i);
    }

    // 创建 idleproc：没有父进程，但是有执行现场，直接复用kernel_init的执行现场，不通过 fork
    if ((idleproc = alloc_proc()) == NULL)
    {
        panic("cannot alloc idleproc.\n");
    }

    idleproc->pid = 0;
    idleproc->state = PROC_RUNNABLE;
    idleproc->kstack = (uintptr_t)bootstack;
    idleproc->need_resched = 1;
    set_proc_name(idleproc, "idle");
    nr_process++;

    current = idleproc;

    // 创建 initproc：没有执行现场，但是有创建者currtnt可以当做父进程，所以kernel_thread 会构造临时 trapframe，
    // do_fork 再把它复制到 initproc 自己的内核栈上
    //(他的父进程就是创建它的idleproc)
    int pid = kernel_thread(init_main, "Hello world!!", 0);
    if (pid <= 0)
    {
        panic("create init_main failed.\n");
    }

    initproc = find_proc(pid);
    set_proc_name(initproc, "init");

    assert(idleproc != NULL && idleproc->pid == 0);
    assert(initproc != NULL && initproc->pid == 1);

}

//LAB4 function3 2510789
//全局问题：我的这个模块在做什么？
/*模块二结束时，系统里已经有 idleproc(pid 0) 和 initproc(pid 1)，但它们只是 PCB 里的 state 字段是 RUNNABLE，
CPU 并不会自动去跑；kern_init 最后一行调用 cpu_idle()，如果没有调度，就永远停在那个空转循环里。
模块三要做的就是把“能跑的线程”变成“正在跑的线程”，一共四层，职责严格分开：
  cpu_idle：待命，发现有人请求调度（need_resched）就进调度器，不决定选谁；
  schedule：策略，从 proc_list 里轮转挑一个 PROC_RUNNABLE，找不到就退回 idleproc；
  proc_run：机制，改 current、切 satp 并刷 TLB，然后把两个 context 交给下一层；
  switch_to：最底层，只保存/恢复 ra、sp、s0~s11，最后 ret，不含任何调度决策。
这种拆法就是“机制与策略分离”：以后换调度算法只改 schedule，换架构只改 switch_to。*/

//全局问题：如何保证代码正确？
/*三个必须同时成立的条件：
1）切换必须原子：选线程、改 current、切页表、换寄存器这一串不能被中断打断，
   否则中断处理看到的 current / 页表 / 寄存器现场可能互不匹配，所以都放在 local_intr_save/restore 里；
2）保存和恢复的“份数”必须配对：switch_to 存 14 个、取 14 个，偏移严格按 struct context 的字段顺序
   （ra、sp、s0~s11）；少一个就会让某个线程拿到别人的寄存器值，表现为“偶发跑飞”；
3）新线程的第一次运行必须接得上：模块二在 copy_thread 里把 context.ra 设成 forkret、
   tf->epc 设成 kernel_thread_entry，模块三的 switch_to 恢复出 ra/sp 之后，
   走 forkret → forkrets → __trapret → sret 这条链就能开始执行线程函数。*/

//全局问题：为什么 proc_run 里要写 satp？
/*satp 决定“用哪张页表”，也就是这个线程的虚拟地址翻译成哪些物理页。
lab4 里所有内核线程共享同一张 boot_pgdir，所以这里写进去的还是同一个值，看起来“没换”；
但语义上仍要求切页表，等以后有独立地址空间的用户进程，就是它真正起作用的地方。
注意两个坑：proc->pgdir 存的是页目录的物理地址，而 satp 要物理页号，所以右移 12 位；
以及不能用 libs/riscv.h 的 lsatp()——那是 Sv32 的写法（SATP32_MODE 在 bit 31），
Sv39 的 MODE 在 bit 63..60 且值为 8。*/

//---- 答辩问答（对应 cpu_idle / proc_run） ----
/*Q1：cpu_idle 和 idleproc 是一回事吗？
A ：不是。idleproc 是 0 号线程的 PCB（数据），cpu_idle 是它执行的函数（代码）。
    关系是：kern_init 最后调用 cpu_idle()，此时 current 就是 idleproc，
    也就是“idleproc 这个线程正在跑 cpu_idle 这个函数”。
    idleproc 特殊在它是复用启动现场造的（kstack = bootstack、没有自己的 trapframe），
    所以它不会被 forkret 启动；它被切走再切回来时，switch_to 恢复的是它自己在
    proc_run 里的返回地址，于是继续跑 cpu_idle 的循环。

Q2：forkret 是什么？为什么新线程要从它开始？
A ：proc.c 里的 static 函数：forkret() { forkrets(current->tf); }。
    copy_thread 把新线程的 context.ra 设成 forkret，所以 switch_to 的 ret 一跳就到这里；
    forkret 再把新线程的 trapframe 交给 trapentry.S 的 forkrets（sp = a0; j __trapret），
    由 __trapret + sret 跳进 tf->epc = kernel_thread_entry，线程函数才开始执行。
    注意此刻 current 已经在 proc_run 里改成新线程，所以 current->tf 是新线程自己的。

Q3：为什么 prev = current 必须在 current = proc 之前？
A ：switch_to 需要“旧线程的 context 地址”来保存现场；先改 current 就把旧线程指针丢了，
    现场无处可存，旧线程以后再也回不来。

Q4：satp 那一句的四个小问
   (a) 为什么要 >> 12？
       pgdir 存的是页目录的物理地址，而 satp 的 PPN 要的是“第几页”；
       一页 4KB = 2^12，所以右移 12 位等于除以 4096。这不是对齐。
   (b) 为什么不能用 libs/riscv.h 的 lsatp()？
       它是 Sv32 写法：SATP32_MODE = 0x80000000 把 MODE 放在 bit 31（Sv32 只有 1 位）；
       Sv39 的 MODE 是 4 位、在 bit 63..60、值为 8。用它会写坏 MODE 字段，页表直接失效。
   (c) flush_tlb() 能省吗？
       不能。换了 satp，但 TLB 里可能还缓存着旧地址空间的翻译，
       会出现“查的是新页表、访问却落到旧物理页”的情况。
   (d) 内核线程共用 boot_pgdir，这一句能删吗？
       不能。“切换进程 = 切换地址空间”是语义要求；现在值相同只是巧合，
       将来有独立地址空间的用户进程时，它就是唯一正确的来源。

Q5：local_intr_restore(intr_flag) 这一行，新创建的线程会执行到吗？
A ：不会。新线程从 forkret 开始跑，走的是另一条路；这一行只有“被切走又被切回来”的
    线程才会执行，而且用的是它自己那份 proc_run 栈帧里的 intr_flag。*/

/* cpu_idle - 空闲线程（idleproc）的执行主体
 *
 * kern_init() 的最后一步就是调用它，所以正常情况下它永远不返回
 *（proc.h 里也声明了 __attribute__((noreturn))）。
 * 它只做一件事：发现有人请求重新调度，就进调度器。
 * 选择“该跑谁”是 schedule() 的职责，cpu_idle 不参与决策。 */
void
cpu_idle(void) {
    while (1) {
        if (current->need_resched) {
            schedule();
        }
    }
}

/* proc_run - 让 proc 成为当前正在运行的线程
 *
 * 依次做三件事：
 *   1. current = proc         ：更新“我是谁”
 *   2. 切换页表（satp）+ 刷 TLB：更新“我看到哪个地址空间”
 *   3. switch_to(...)          ：更新“CPU 里存的是谁的寄存器”
 *
 * 这三件事必须在关中断的临界区里完成，否则中途来中断，
 * 中断处理看到的 current / 页表 / 寄存器现场可能互不匹配。 */
void
proc_run(struct proc_struct *proc) {
    if (proc != current) {
        bool intr_flag;
        struct proc_struct *prev = current;

        local_intr_save(intr_flag);
        {
            current = proc;

            /* ---- 切换页表：把 proc 的页目录交给 MMU ----
             * 先把两个东西“存的是什么”分清楚：
             *   proc->pgdir  存的是【页目录的物理地址】，例如 0x80209000
             *                （uintptr_t，64 位；因为物理内存从 0x80000000 起，
             *                  值很小，但完整写出来是 0x0000000080209000）
             *   satp         存的是【一份配置】，例如 0x8000000000080209
             *                它把“用哪种分页、根表在哪一页”打包进一个 64 位数，
             *                并不是一个地址，所以不能把 pgdir 直接丢给它。
             *
             * satp 这 64 位，硬件按三段读：
             *   bit 63..60 : MODE  —— 值为 8 表示 Sv39（三级页表）
             *   bit 59..44 : ASID  —— 地址空间编号，本实验用不到，填 0
             *   bit 43..0  : PPN   —— 页目录的“物理页号”（注意：不是物理地址）
             * 所以这一句做两件事：
             *   ① (uintptr_t)SATP_MODE_SV39 << 60 ：把 8 放到最高 4 位；
             *   ② proc->pgdir >> RISCV_PGSHIFT   ：把物理地址换算成页号（除以 4096）。
             * 最后用 | 拼起来：两段占的位不重叠，所以“按位或”就等于“拼接”。
             *
             * 用真实数字看一遍（pgdir = 0x80209000）：
             *   (uintptr_t)8 << 60   = 0x8000000000000000   （MODE=8 落在最高 4 位）
             *   0x80209000 >> 12     = 0x0000000000080209   （页号：每页 4096 字节）
             *   两段相或             = 0x8000000000080209   ← 这就是写进 satp 的值
             *
             * 三个容易错的点：
             *   · (uintptr_t) 不能省：8 << 60 若按 32 位 int 算会溢出（未定义行为）；
             *   · 不能把 0x80209000 直接写进去：低 12 位是页内偏移，satp 里没它的位置，
             *     必须右移 12 位变成“第几页”；
             *   · 不能用 libs/riscv.h 的 lsatp()：那是 Sv32 写法（MODE 在 bit 31、只有 1 位），
             *     拿到 Sv39 上会把 MODE 字段写坏，页表直接失效。 */
            write_csr(satp, ((uintptr_t)SATP_MODE_SV39 << 60) |
                                (proc->pgdir >> RISCV_PGSHIFT));

            /* ---- 清空 TLB（地址翻译缓存）----
             * flush_tlb() 就是一条 sfence.vma，作用是“作废本 hart 的 TLB 表项”。
             * 注意它是“清”，不是“刷新成新值”：清完之后，下一次访问内存时
             * CPU 查不到缓存，只能重新走一遍页表，于是把新 satp 对应的翻译重新缓存起来。
             *
             * 为什么必须做：改 satp 只代表“以后查页表用新表”，但 TLB 里可能还留着
             * 旧地址空间的翻译结果。不刷就会出现“页表已经换了、访问却仍落到旧物理页”。
             * 顺序也不能反：先写 satp，再刷 TLB；反过来等于白刷。 */
            flush_tlb();

            /* 保存 prev 的 ra/sp/s0~s11，并装入 proc 的。
             * 对刚创建、还没运行过的线程，它的 context.ra 是 forkret、
             * context.sp 是它自己的 trapframe，于是这次切换会让它
             * 从 forkret → forkrets → __trapret → sret → kernel_thread_entry
             * 这条链路开始第一次运行。 */
            switch_to(&(prev->context), &(proc->context));
        }
        local_intr_restore(intr_flag);
    }
}
