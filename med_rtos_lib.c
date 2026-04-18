#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>
#include <signal.h>
#include <sys/time.h>

#define MAX_TASKS 10
#define STACK_SIZE 8192

/* ================= TASK ================= */

typedef void (*TaskFunction_t)(void *);

typedef struct {
    ucontext_t context;
    int active;
    int priority;
    char stack[STACK_SIZE];
} TCB_t;

TCB_t tasks[MAX_TASKS];
int task_count = 0;
int current_task = -1;

ucontext_t scheduler_context;

/* ================= SYNC ================= */

typedef struct {
    int count;
} Semaphore_t;

typedef struct {
    int locked;
} Mutex_t;

/* ================= QUEUE ================= */

typedef struct {
    int length;
    int item_size;
    int head, tail, count;
    char *buffer;
} Queue_t;

typedef Queue_t* QueueHandle_t;

/* ================= QUEUE ================= */

QueueHandle_t xQueueCreate(int len, int size)
{
    QueueHandle_t q = malloc(sizeof(Queue_t));
    q->buffer = malloc(len * size);

    q->length = len;
    q->item_size = size;
    q->head = q->tail = q->count = 0;

    return q;
}

int xQueueSend(QueueHandle_t q, void *item)
{
    if (q->count == q->length) return -1;

    memcpy(&q->buffer[q->head * q->item_size], item, q->item_size);
    q->head = (q->head + 1) % q->length;
    q->count++;
    return 0;
}

int xQueueReceive(QueueHandle_t q, void *item)
{
    if (q->count == 0) return -1;

    memcpy(item, &q->buffer[q->tail * q->item_size], q->item_size);
    q->tail = (q->tail + 1) % q->length;
    q->count--;
    return 0;
}

/* ================= SEMAPHORE ================= */

void xSemaphoreInit(Semaphore_t *s, int val)
{
    s->count = val;
}

void xSemaphoreTake(Semaphore_t *s)
{
    while (s->count <= 0);
    s->count--;
}

void xSemaphoreGive(Semaphore_t *s)
{
    s->count++;
}

/* ================= MUTEX ================= */

void xMutexInit(Mutex_t *m)
{
    m->locked = 0;
}

void xMutexLock(Mutex_t *m)
{
    while (__sync_lock_test_and_set(&m->locked, 1));
}

void xMutexUnlock(Mutex_t *m)
{
    __sync_lock_release(&m->locked);
}

/* ================= TASK ================= */

void task_wrapper(TaskFunction_t task, void *params)
{
    task(params);
    while (1);
}

void xTaskCreate(TaskFunction_t task, void *params, int priority)
{
    TCB_t *t = &tasks[task_count];

    getcontext(&t->context);
    t->context.uc_stack.ss_sp = t->stack;
    t->context.uc_stack.ss_size = sizeof(t->stack);
    t->context.uc_link = &scheduler_context;

    makecontext(&t->context, (void (*)(void))task_wrapper, 2, task, params);

    t->priority = priority;
    t->active = 1;
    task_count++;
}

/* ================= SCHEDULER ================= */

int pick_highest_priority()
{
    int best = -1;
    int best_prio = -1;

    for (int i = 0; i < task_count; i++)
    {
        if (tasks[i].active && tasks[i].priority > best_prio)
        {
            best_prio = tasks[i].priority;
            best = i;
        }
    }
    return best;
}

void scheduler()
{
    while (1)
    {
        int next = pick_highest_priority();

        if (next >= 0)
        {
            current_task = next;
            swapcontext(&scheduler_context, &tasks[next].context);
        }
    }
}

/* ================= PREEMPTION ================= */

void timer_handler(int sig)
{
    if (current_task >= 0)
    {
        swapcontext(&tasks[current_task].context, &scheduler_context);
    }
}

void start_timer()
{
    struct sigaction sa;
    struct itimerval timer;

    sa.sa_handler = timer_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGALRM, &sa, NULL);

    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = 50000; // 50ms

    timer.it_interval = timer.it_value;

    setitimer(ITIMER_REAL, &timer, NULL);
}

void vTaskStartScheduler()
{
    start_timer();
    getcontext(&scheduler_context);
    scheduler();
}

/* ================= ISR SIMULATION ================= */

void ISR_simulator()
{
    printf(">>> ISR Triggered!\n");
}

/* ================= DEMO ================= */

QueueHandle_t queue;
Semaphore_t sem;
Mutex_t mutex;

void producer(void *p)
{
    int val = 0;
    while (1)
    {
        xSemaphoreTake(&sem);

        xMutexLock(&mutex);
        val++;
        xQueueSend(queue, &val);
        printf("Produced: %d\n", val);
        xMutexUnlock(&mutex);

        xSemaphoreGive(&sem);
    }
}

void consumer(void *p)
{
    int val;
    while (1)
    {
        xSemaphoreTake(&sem);

        xMutexLock(&mutex);
        if (xQueueReceive(queue, &val) == 0)
            printf("Consumed: %d\n", val);
        xMutexUnlock(&mutex);

        xSemaphoreGive(&sem);
    }
}

/* ================= MAIN ================= */

int main()
{
    queue = xQueueCreate(5, sizeof(int));

    xSemaphoreInit(&sem, 1);
    xMutexInit(&mutex);

    xTaskCreate(producer, NULL, 2); // higher priority
    xTaskCreate(consumer, NULL, 1);

    vTaskStartScheduler();

    return 0;
}
