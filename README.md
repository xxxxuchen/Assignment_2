# User Level Thread Scheduler

## Overview
SUT is a simple api which supports multi-programming in user space. This library provide users ability to run their tasks concurrently in an event loop style. This is achieved by switching between tasks cooperatively and scheduled them in a FIFO queue.  However, multiple user level threads maps to a single kernel thread, which means IO task will block the compute task. Therefore, the library manages two types of executor: one for running compute tasks and another for IO tasks. Each executor is a kernel-level thread, while the tasks that run are user-level threads. By providing IO tasks their own executor, compute tasks will not be blocked.

## More Details
In this SUT Library the user level thread , sometimes refers to coroutine, is implemented by ucontext GNU C Library. It provides several useful api such as `getcontext`, `setcontext`, `makecontext`, and `swapcontext` to save and resume the state of a task thread. The C-EXEC is responsible for most activities in the SUT library, while the I-EXEC handles I/O operations. During initialization of the SUT library, two kernel-level threads are created to run C-EXEC and I-EXEC, respectively. Once the task is created, it is inserted into the task ready queue. The C-EXEC pulls the first task from the task ready queue and starts executing it. When a task issues a read request, it is added to a request queue along with the corresponding task that invoked it. When the response arrives, the task is moved from the wait queue to the task ready queue, and it will be scheduled to run at a future time. Similar handling applies to write operations and file opening/closing.
Below is a diagram illustrating the structure of the SUT library.

<img width="759" alt="Screenshot 2023-12-09 at 23 20 19" src="https://github.com/xxxxuchen/Thread-Scheduler/assets/119140869/f216d641-315a-4e39-bbe1-308799bb8d69">


## Usage
- `sut_init()` initializes the library. This function must be called before any other functions.
- `sut_shutdown()` shuts down the library. This function must be called after all other functions.
- `sut_create(fn)` creates a new user-level thread that runs the function `fn`. The new thread is put into the ready queue.
- `sut_yield()` give up (yield) the CPU and put the current task to the end of the ready queue. Must be called within a task.
- `sut_[open|close|read|write]` standard I/O operations. Put the current task to the end of the I/O queue and yield the CPU. Must be called within a task.
- `sut_exit()` terminates the current task. Must be called within a task.

## Compile
There are some sample tests in Support folder. Feel free to run them to see how the library works.
- `gcc sut.c Support/test1.c -o out`

You may also need to include the -pthread option when compiling.

