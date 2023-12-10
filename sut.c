/**
 * User-level thread library
 * Author: Xu Chen
 * Date: 2023/11/05
 */
#include <pthread.h>
#include <signal.h>
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
  char *type;  // [yield | exit | read | write | open | close ]
} Task;

Task *cur_C_task;
Task *cur_I_task;

struct queue task_queue;
struct queue wait_queue;
pthread_t *c_exec_thread;
pthread_t *i_exec_thread;
pthread_mutex_t *queue_insertion_lock;
ucontext_t C_context;
ucontext_t *I_context;
char *I_stack;
FILE *FDT[30] = {
    NULL};  // this is file descriptor table, which hold 30 files' descriptors
char *rd_result;
bool termination_flag = false;

int open_file(char *filename) {
  FILE *fp;
  fp = fopen(filename, "a+");
  if (fp == NULL) {
    printf("Error opening file\n");
    return -1;
  }
  // if file already exists, return its index in the FDT
  for (int i = 0; i < 30; i++) {
    if (FDT[i] == fp) {
      return i;
    }
  }
  // if file does not exist, put it in the first available slot in the FDT
  for (int i = 0; i < 30; i++) {
    if (FDT[i] == NULL) {
      FDT[i] = fp;
      return i;
    }
  }
}

char *read_file(int fd, char *buf, int size) {
  FILE *fp = FDT[fd];
  if (fp == NULL) {
    printf("Error opening file\n");
    return NULL;
  }
  rd_result = (char *)malloc(sizeof(char) * size);
  if (rd_result == NULL) {
    printf("Memory allocation error\n");
    return NULL;
  }
  while (fgets(rd_result, size, fp) != NULL) {
    if (strlen(buf) + strlen(rd_result) >= size) break;
    // append every line in the file
    strcat(buf, rd_result);
  }
  if (strlen(buf) == 0) {
    printf("Error reading file\n");
    free(rd_result);
    return NULL;
  }
  return buf;
}

void write_file(int fd, char *buf, int size) {
  FILE *fp = FDT[fd];
  if (fp == NULL) {
    printf("Error writing file\n");
    return;
  }
  if (fputs(buf, fp) == EOF) {
    printf("Error writing file\n");
    return;
  }
  fflush(fp);
}

void close_file(int fd) {
  FILE *fp = FDT[fd];
  if (fp == NULL) {
    printf("Error closing file\n");
    return;
  }
  fclose(fp);
  free(rd_result);
  rd_result = NULL;
  // set its file descriptor table entry to NULL
  FDT[fd] = NULL;
}

void *C_EXEC(void *args) {
  while (true) {
    struct queue_entry *first_node = queue_peek_front(&task_queue);
    // sleep if there is no task
    if (first_node == NULL) {
      struct timespec ts;
      ts.tv_sec = 0;
      ts.tv_nsec = 100000;
      nanosleep(&ts, NULL);
      // set the current task to NULL
      cur_C_task = NULL;
      continue;
    }
    struct queue_entry *node = queue_pop_head(&task_queue);
    Task *task = (Task *)(node->data);
    cur_C_task = task;
    swapcontext(&C_context, &(task->context));
    // return from the task and resume the C_EXEC thread here
    if (strcmp(cur_C_task->type, "yield") == 0) {
      pthread_mutex_lock(queue_insertion_lock);
      queue_insert_tail(&task_queue, queue_new_node(cur_C_task));
      pthread_mutex_unlock(queue_insertion_lock);
    } else if (strcmp(cur_C_task->type, "exit") == 0) {
      free(cur_C_task->context.uc_stack.ss_sp);
      free(cur_C_task);
      cur_C_task = NULL;
    } else if (strcmp(cur_C_task->type, "open") == 0) {
      queue_insert_tail(&wait_queue, queue_new_node(cur_C_task));
    } else if (strcmp(cur_C_task->type, "read") == 0) {
      queue_insert_tail(&wait_queue, queue_new_node(cur_C_task));
    } else if (strcmp(cur_C_task->type, "write") == 0) {
      queue_insert_tail(&wait_queue, queue_new_node(cur_C_task));
    } else if (strcmp(cur_C_task->type, "close") == 0) {
      queue_insert_tail(&wait_queue, queue_new_node(cur_C_task));
    }
    // terminate the C_EXEC thread if there is no task in the queue and current
    // tasks are NULL
    if (termination_flag && cur_C_task == NULL && cur_I_task == NULL &&
        queue_peek_front(&task_queue) == NULL &&
        queue_peek_front(&wait_queue) == NULL) {
      cur_I_task = NULL;
      cur_C_task = NULL;
      pthread_exit(NULL);
    }
  }
}

void *I_EXEC(void *args) {
  I_context = (ucontext_t *)malloc(sizeof(ucontext_t));
  getcontext(I_context);
  I_stack = (char *)malloc(sizeof(char) * (STACK_SIZE));
  I_context->uc_stack.ss_sp = I_stack;
  I_context->uc_stack.ss_size = sizeof(char) * (STACK_SIZE);
  I_context->uc_link = 0;

  while (true) {
    struct queue_entry *first_node = queue_peek_front(&wait_queue);
    if (first_node == NULL) {
      struct timespec time;
      time.tv_sec = 0;
      time.tv_nsec = 100000;
      nanosleep(&time, NULL);
      continue;
    }
    struct queue_entry *node = queue_pop_head(&wait_queue);
    Task *task = (Task *)node->data;
    cur_I_task = task;
    if (strcmp(task->type, "open") == 0) {
      swapcontext(I_context, &(task->context));
      pthread_mutex_lock(queue_insertion_lock);
      queue_insert_tail(&task_queue, queue_new_node(cur_I_task));
      pthread_mutex_unlock(queue_insertion_lock);
    } else if (strcmp(task->type, "read") == 0) {
      swapcontext(I_context, &(task->context));
      pthread_mutex_lock(queue_insertion_lock);
      queue_insert_tail(&task_queue, queue_new_node(cur_I_task));
      pthread_mutex_unlock(queue_insertion_lock);
    } else if (strcmp(task->type, "write") == 0) {
      swapcontext(I_context, &(task->context));
      pthread_mutex_lock(queue_insertion_lock);
      queue_insert_tail(&task_queue, queue_new_node(cur_I_task));
      pthread_mutex_unlock(queue_insertion_lock);
    } else if (strcmp(task->type, "close") == 0) {
      swapcontext(I_context, &(task->context));
      pthread_mutex_lock(queue_insertion_lock);
      queue_insert_tail(&task_queue, queue_new_node(cur_I_task));
      pthread_mutex_unlock(queue_insertion_lock);
    }
    // current I/O task is done, set it to NULL
    cur_I_task = NULL;
  }
}

void sut_init() {
  queue_insertion_lock = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
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
  task->type = NULL;
  if (getcontext(&task->context) == -1) {
    printf("getcontext error\n");
    return false;
  }
  char *stack = (char *)malloc(sizeof(char) * (STACK_SIZE));
  task->context.uc_stack.ss_sp = stack;
  task->context.uc_stack.ss_size = sizeof(char) * (STACK_SIZE);
  task->context.uc_link = 0;
  makecontext(&task->context, fn, 0);
  pthread_mutex_lock(queue_insertion_lock);
  queue_insert_tail(&task_queue, queue_new_node(task));
  pthread_mutex_unlock(queue_insertion_lock);
  return true;
}

void sut_yield() {
  cur_C_task->type = "yield";
  swapcontext(&(cur_C_task->context), &C_context);
}

void sut_exit() {
  cur_C_task->type = "exit";
  swapcontext(&(cur_C_task->context), &C_context);
}

int sut_open(char *file_name) {
  cur_C_task->type = "open";
  swapcontext(&(cur_C_task->context), &C_context);
  int file_desc = open_file(file_name);
  swapcontext(&(cur_I_task->context), I_context);
  return file_desc;
}

char *sut_read(int fd, char *buf, int size) {
  cur_C_task->type = "read";
  swapcontext(&(cur_C_task->context), &C_context);
  char *data = read_file(fd, buf, size);
  swapcontext(&(cur_I_task->context), I_context);
  return data;
}

void sut_write(int fd, char *buf, int size) {
  cur_C_task->type = "write";
  swapcontext(&(cur_C_task->context), &C_context);
  write_file(fd, buf, size);
  swapcontext(&(cur_I_task->context), I_context);
}

void sut_close(int fd) {
  cur_C_task->type = "close";
  swapcontext(&(cur_C_task->context), &C_context);
  close_file(fd);
  swapcontext(&(cur_I_task->context), I_context);
}

void sut_shutdown() {
  termination_flag = true;
  pthread_join(*c_exec_thread, NULL);
  // c_exec_thread is done, cancel the i_exec_thread
  pthread_cancel(*i_exec_thread);
  free(c_exec_thread);
  free(i_exec_thread);
  free(cur_C_task);
  free(cur_I_task);
  free(queue_insertion_lock);
  free(I_context);
  free(I_stack);
}
