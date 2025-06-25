# user_thread_scheduler

Preemptive user-level thread library in **C++** with round-robin scheduling, context switching via sigsetjmp/siglongjmp, and virtual timing.

This project implements a preemptive **user-level thread library in C++**, supporting multiple threads and round-robin scheduling, with context switching and signal-based time slicing. No OS kernel thread API is used — the library provides its own thread management with fully-controlled scheduling logic.

---

## Core Features

- Round-Robin scheduler using virtual timer (setitimer / SIGVTALRM)  
- sigsetjmp / siglongjmp used for low-level context switching  
- Supports blocking, resuming, termination, and dynamic thread ID reuse  
- Thread states: RUNNING, READY, BLOCKED — managed with internal queues  
- Precise control over thread switching and signal masking

---

## Files

- `uthread.cpp`: Core thread library logic (scheduling, switching, timers)  
- `uthread.h`: Public API (not to be edited)  
- `main.cpp`: Test/demo driver for thread execution  
- `Makefile`: Compiles the library into `libuthreads.a`  
- `README.md`: Project overview and theoretical explanations

---

## Usage

To compile the project:

```bash
make
