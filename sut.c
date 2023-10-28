#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>
#include <unistd.h>

#include "./Support/queue/queue.h"
#define STACK_SIZE 1024 * 1024

typedef void (*sut_task_f)();
typedef struct task {
  ucontext_t context;
  sut_task_f fn;
} Task;

void sut_init();
bool sut_create(sut_task_f fn);
void sut_yield();
void sut_exit();
void sut_open(char *dest, int port);
void sut_write(char *buf, int size);
void sut_close();
char *sut_read();
void sut_shutdown();

struct queue task_queue;
struct queue wait_queue;
pthread_t *c_exec_thread;
pthread_t *i_exec_thread;
ucontext_t C_context;
ucontext_t I_context;

void *C_EXEC(void *args) {
  while (true) {
    // sleep if there is no task
    struct queue_entry *first_node = queue_peek_front(&task_queue);
    if (first_node == NULL) {
      struct timespec ts;
      ts.tv_sec = 0;
      ts.tv_nsec = 100000;
      nanosleep(&ts, NULL);
      continue;
    }
    struct queue_entry *node = queue_pop_head(&task_queue);
    Task *task = (Task *)node->data;
    swapcontext(&C_context, &(task->context));
    printf("return from task\n");
  }
}
void *I_EXEC(void *args) { printf("I_EXEC\n"); }

/* During initialization of the SUT library, the first action is to create two
 * kernel-level threads to run C-EXEC and I-EXEC respectively.*/
void sut_init() {
  task_queue = queue_create();
  wait_queue = queue_create();
  queue_init(&task_queue);
  queue_init(&wait_queue);
  c_exec_thread = (pthread_t *)malloc(sizeof(pthread_t));
  i_exec_thread = (pthread_t *)malloc(sizeof(pthread_t));
  pthread_create(c_exec_thread, NULL, C_EXEC, NULL);
  pthread_create(i_exec_thread, NULL, I_EXEC, NULL);
}

bool sut_create(sut_task_f fn) {
  Task *task = (Task *)malloc(sizeof(Task));
  task->fn = fn;
  if (getcontext(&task->context) == -1) {
    printf("getcontext error\n");
    return false;
  }
  char *stack = (char *)malloc(sizeof(char) * (STACK_SIZE));
  task->context.uc_stack.ss_sp = stack;
  task->context.uc_stack.ss_size = sizeof(char) * (STACK_SIZE);
  task->context.uc_link = 0;
  makecontext(&task->context, fn, 0);
  // TODO: protect the queue
  queue_insert_tail(&task_queue, queue_new_node(task));
  return true;
}

void hello1() {
  int i;
  for (i = 0; i < 10; i++) {
    printf("Hello world!, this is SUT-One \n");
    // sut_yield();
  }
  printf("hello1 exit\n");
  // sut_exit();
}

// void hello2() {
//   int i;
//   for (i = 0; i < 100; i++) {
//     printf("Hello world!, this is SUT-Two \n");
//     sut_yield();
//   }
//   sut_exit();
// }

// for test
int main() {
  sut_init();
  sut_create(hello1);
  pthread_join(*c_exec_thread, NULL);
}