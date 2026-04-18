#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>

#define MAX_TASKS 10
#define STACK_SIZE 8192

/* ================= TASK SYSTEM ================= */

typedef void (*TaskFunction_t)(void *);

typedef struct {
    ucontext_t context;
    int active;
    char stack[STACK_SIZE];
} TCB_t;

TCB_t tasks[MAX_TASKS];
int task_count = 0;
int current_task = 0;

ucontext_t scheduler_context;

/* ================= QUEUE SYSTEM ================= */

typedef struct {
    int length;
    int item_size;
    int head;
    int tail;
    int count;
    char *buffer;
} Queue_t;

typedef Queue_t* QueueHandle_t;

/* ================= QUEUE APIs ================= */

QueueHandle_t xQueueCreate(int length, int item_size)
{
    QueueHandle_t q = malloc(sizeof(Queue_t));
    if (!q) return NULL;

    q->buffer = malloc(length * item_size);
    if (!q->buffer)
    {
        free(q);
        return NULL;
    }

    q->length = length;
    q->item_size = item_size;
    q->head = 0;
    q->tail = 0;
    q->count = 0;

    return q;
}

int xQueueSend(QueueHandle_t q, void *item)
{
    if (q->count == q->length)
        return -1;

    memcpy(&q->buffer[q->head * q->item_size], item, q->item_size);

    q->head = (q->head + 1) % q->length;
    q->count++;
    return 0;
}

int xQueueReceive(QueueHandle_t q, void *item)
{
    if (q->count == 0)
        return -1;

    memcpy(item, &q->buffer[q->tail * q->item_size], q->item_size);

    q->tail = (q->tail + 1) % q->length;
    q->count--;
    return 0;
}

/* ================= TASK APIs ================= */

void task_wrapper(TaskFunction_t task, void *params)
{
    task(params);
    while (1); // tasks should not exit
}

void xTaskCreate(TaskFunction_t task, void *params)
{
    TCB_t *t = &tasks[task_count];

    getcontext(&t->context);

    t->context.uc_stack.ss_sp = t->stack;
    t->context.uc_stack.ss_size = sizeof(t->stack);
    t->context.uc_link = &scheduler_context;

    makecontext(&t->context, (void (*)(void))task_wrapper, 2, task, params);

    t->active = 1;
    task_count++;
}

/* ================= SCHEDULER ================= */

void scheduler()
{
    while (1)
    {
        for (int i = 0; i < task_count; i++)
        {
            if (tasks[i].active)
            {
                current_task = i;
                swapcontext(&scheduler_context, &tasks[i].context);
            }
        }
    }
}

void vTaskStartScheduler()
{
    getcontext(&scheduler_context);
    scheduler();
}

void vTaskDelay(int ticks)
{
    (void)ticks; // unused (cooperative)
    swapcontext(&tasks[current_task].context, &scheduler_context);
}

/* ================= DEMO ================= */

QueueHandle_t queue;

void producer(void *p)
{
    int val = 0;
    while (1)
    {
        val++;
        if (xQueueSend(queue, &val) == 0)
            printf("Produced: %d\n", val);

        vTaskDelay(1);
    }
}

void consumer(void *p)
{
    int val;
    while (1)
    {
        if (xQueueReceive(queue, &val) == 0)
            printf("Consumed: %d\n", val);

        vTaskDelay(1);
    }
}

/* ================= MAIN ================= */

int main()
{
    queue = xQueueCreate(5, sizeof(int));

    if (!queue)
    {
        printf("Queue creation failed\n");
        return -1;
    }

    xTaskCreate(producer, NULL);
    xTaskCreate(consumer, NULL);

    vTaskStartScheduler();

    return 0;
}
