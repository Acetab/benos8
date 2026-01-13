#include <irq.h>
#include <sched.h>
#include <printk.h>

static void dequeue_task_multilevel(struct run_queue *rq,
        struct task_struct *p)
{
    rq->nr_running--;
    list_del(&p->run_list);
}

static void enqueue_task_multilevel(struct run_queue *rq,
        struct task_struct *p)
{
    // 根据进程的level决定插入哪个队列
    if (p->level == 0) {
        list_add_tail(&p->run_list, &rq->rq_head);
    } else {
        // level 1 或其他情况放入第二级队列
        list_add_tail(&p->run_list, &rq->rq_head2);
    }
    rq->nr_running++;
}

static void task_tick_multilevel(struct run_queue *rq, struct task_struct *p)
{
    // 递减时间片
    if (--p->counter <= 0) {
        // 时间片用完，触发重新调度
        p->need_resched = 1;
        p->counter = DEF_COUNTER; // 重置时间片

        // 实验要求：每一个时间片到的被抢占的进程放到第二个就绪队列的末尾
        // 如果当前是第0级，时间片用完后降级为第1级
        if (p->level == 0) {
            p->level = 1; 
            // 注意：此时进程还在运行，其在队列中的位置移动将在 pick_next_task 中处理
        }
    }
}

static struct task_struct *pick_next_task_multilevel(struct run_queue *rq,
        struct task_struct *prev)
{
    struct task_struct *next = NULL;

    // 处理 prev 进程：如果它处于运行状态（说明是被抢占或时间片用完），
    // 需要把它放回对应队列的末尾（实现RR或降级后的排队）
    if (prev->state == TASK_RUNNING) {
        list_del(&prev->run_list); // 先从当前位置移除
        
        if (prev->level == 0) {
            list_add_tail(&prev->run_list, &rq->rq_head);
        } else {
            list_add_tail(&prev->run_list, &rq->rq_head2);
        }
    }

    // 调度算法核心：
    // 1. 优先调度第一级队列 (rq_head)
    if (!list_empty(&rq->rq_head)) {
        next = list_entry(rq->rq_head.next, struct task_struct, run_list);
    } 
    // 2. 第一级为空，调度第二级队列 (rq_head2)
    else if (!list_empty(&rq->rq_head2)) {
        next = list_entry(rq->rq_head2.next, struct task_struct, run_list);
    }
    // 3. 如果都为空（通常只有idle进程时不会发生，除非系统设计允许），回落到prev
    else {
        next = prev; 
    }

    // 只有当选出的next不是当前进程时才打印日志，避免刷屏
    if (next != prev) {
         // printk("Switch to pid %d (Level %d)\n", next->pid, next->level);
    }

    return next;
}

// 定义调度类结构体
const struct sched_class multilevel_sched_class = {
    .next = NULL,
    .dequeue_task = dequeue_task_multilevel,
    .enqueue_task = enqueue_task_multilevel,
    .task_tick = task_tick_multilevel,
    .pick_next_task = pick_next_task_multilevel,
};