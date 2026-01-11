#include <irq.h>
#include <sched.h>
#include <printk.h>

/* 从队列中移除进程 */
static void dequeue_task_simple(struct run_queue *rq,
        struct task_struct *p)
{
    rq->nr_running--;
    list_del(&p->run_list);
}

/* 将进程加入队列 */
static void enqueue_task_simple(struct run_queue *rq,
        struct task_struct *p)
{
    // 根据进程的 level 加入不同的队列
    if (p->level == 1) {
        // 第1级队列：通常是新进程或高优先级，放在队尾
        list_add_tail(&p->run_list, &rq->rq_head1);
        
        // 抢占逻辑：如果当前运行的进程是第2级，而新加入了第1级进程，标记需要调度
        if (rq->curr && rq->curr->level == 2) {
            rq->curr->need_resched = 1;
        }
    } else {
        // 第2级队列：被抢占过的进程，放在队尾
        // 确保 level 只有 1 或 2
        p->level = 2; 
        list_add_tail(&p->run_list, &rq->rq_head2);
    }
    rq->nr_running++;
}

/* 选择下一个要运行的进程 */
static struct task_struct *pick_next_task_simple(struct run_queue *rq,
        struct task_struct *prev)
{
    struct task_struct *next = NULL;

    // 优先调度第1级队列 (Level 1)
    if (!list_empty(&rq->rq_head1)) {
        next = list_entry(rq->rq_head1.next, struct task_struct, run_list);
    } 
    // 如果第1级队列为空，则调度第2级队列 (Level 2)
    else if (!list_empty(&rq->rq_head2)) {
        next = list_entry(rq->rq_head2.next, struct task_struct, run_list);
    }
    // 如果两个队列都为空，next 为 NULL (系统可能进入idle或出错，取决于是否有idle task)

    if (next) {
         // printk("%s: pick pid %d (level %d)\n", __func__, next->pid, next->level);
    }

    return next;
}

/* 时钟中断处理 */
static void task_tick_simple(struct run_queue *rq, struct task_struct *p)
{
    // 只有第1级队列的进程受时间片限制
    // 第2级队列按照 FCFS 调度 (只有被第1级抢占时才会停止)
    if (p->level == 1) {
        if (--p->counter <= 0) {
            // 时间片耗尽
            p->counter = 0; // 或者重置为 DEF_COUNTER
            
            // 降级：从第1级移动到第2级
            // 注意：此时进程还在运行中，并未在 pick_next_task 中被移除
            // 我们需要修改其元数据，并标记需要重新调度
            p->level = 2;
            p->need_resched = 1;
            
            // 将其从当前队列（实际上是在CPU上）逻辑“移动”到 Level 2 队列的末尾
            // 由于当前进程并没有在 rq 链表中被取出（pick_next_task 只是读取），
            // 但为了符合逻辑，我们在下一次 schedule() 的 enqueue/dequeue 流程中处理，
            // 或者在这里直接操作链表。
            
            // 在 Benos 的 schedule() 实现中，正在运行的进程(state=RUNNING)不会被自动 dequeue。
            // 因此我们需要手动将其移动到 rq_head2。
            // 这是一个临界区操作，task_tick 在中断上下文，schedule 在进程上下文(关中断)。
            // 这里的操作是安全的，因为中断发生时 CPU 在处理该进程。
            
            list_del(&p->run_list);
            list_add_tail(&p->run_list, &rq->rq_head2);
            
            // 重置时间片，虽然 FCFS 不需要时间片，但如果之后有扩展可能会用到
            p->counter = DEF_COUNTER; 
            
            // printk("pid %d time slice up, move to level 2\n", p->pid);
        }
    } 
    else if (p->level == 2) {
        // 第2级队列逻辑：
        // 如果第1级队列有任务，需要立即让出 CPU (抢占)
        if (!list_empty(&rq->rq_head1)) {
            p->need_resched = 1;
        }
    }
}

const struct sched_class simple_sched_class = {
    .next = NULL,
    .dequeue_task = dequeue_task_simple,
    .enqueue_task = enqueue_task_simple,
    .task_tick = task_tick_simple,
    .pick_next_task = pick_next_task_simple,
};