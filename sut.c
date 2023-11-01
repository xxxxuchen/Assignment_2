#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>

#include "./Support/queue/queue.h"
#define STACK_SIZE 1024 * 1024

typedef void (*sut_task_f)();
typedef struct task {
  ucontext_t context;
  sut_task_f fn;
  char *type;
} Task;

// typedef struct TCB {
//   Task *task;
//   char *type;
// } TCB;

Task *current_task;

void sut_init();
bool sut_create(sut_task_f fn);
void sut_yield();
void sut_exit();
int sut_open(char *file_name);
void sut_close(int fd);
void sut_write(int fd, char *buf, int size);
char *sut_read(int fd, char *buf, int size);
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
    current_task = task;
    swapcontext(&C_context, &(task->context));
    printf("return from task\n");
    if (strcmp(current_task->type, "yield") == 0) {
      queue_insert_tail(&task_queue, queue_new_node(current_task));
    } else if (strcmp(current_task->type, "exit") == 0) {
      printf("exit\n");
      free(current_task);
    } else if (strcmp(current_task->type, "open") == 0) {
      printf("open\n");
      queue_insert_tail(&wait_queue, queue_new_node(current_task));
    }
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

/* Yield sut_yield(): This causes the C-EXEC to take control. The user task's
context is saved in a task control block (TCB), and the context of C-EXEC is
loaded and started. The task is then placed at the back of the task ready
queue.*/
void sut_yield() {
  current_task->type = "yield";
  swapcontext(&(current_task->context), &C_context);
}

/* Exit/Terminate sut_exit(): This causes the C-EXEC to take control, similar to
the previous case. The major difference is that the TCB is not updated and the
task is not inserted back into the task ready queue.*/
void sut_exit() {
  current_task->type = "exit";
  setcontext(&C_context);
}

/*Open file sut_open(): This causes the C-EXEC to take control. The user task's
context is saved in a TCB, and the context of C-EXEC is loaded and started,
allowing the next user task to run. The current task is placed at the back of
the wait queue. The I-EXEC executes the function that opens the file, and the
result of the open is returned by sut_open(). This result is an integer, similar
to the file descriptor returned by the operating system. The OS file descriptor
can be used, or it can be mapped to another integer using a mapping table
maintained inside the I-EXEC.*/
int sut_open(char *file_name) {
  current_task->type = "open";
  swapcontext(&(current_task->context), &C_context);
  return 0;
}

void hello1() {
  int i;
  for (i = 0; i < 10; i++) {
    printf("Hello world!, this is SUT-One \n");
    sut_yield();
  }
  sut_exit();
}

void hello2() {
  int i;
  for (i = 0; i < 10; i++) {
    printf("Hello world!, this is SUT-Two \n");
    sut_yield();
  }
  sut_exit();
}

void hello3() {
  int i;
  for (i = 0; i < 10; i++) {
    printf("Hello world!, this is SUT-Three \n");
    sut_yield();
    sut_create(hello1);
  }
  sut_exit();
}
// for test
int main() {
  sut_init();
  sut_create(hello1);
  sut_create(hello2);
  sut_create(hello3);
  pthread_join(*c_exec_thread, NULL);
}