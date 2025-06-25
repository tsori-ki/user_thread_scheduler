#include "uthreads.h"
#include <iostream>
#include <setjmp.h>
#include <unordered_map>
#include <unordered_set>
#include <signal.h>
#include <sys/time.h>
#include <stdlib.h>
#include <stdio.h>
#include <cstdlib> // For exit()
#include <queue>
#include <memory> // For smart pointers
#include <cstring> // For memcpy

#ifdef __x86_64__
/* code for 64 bit Intel arch */

typedef unsigned long address_t;
#define JB_SP 6
#define JB_PC 7

/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
address_t translate_address(address_t addr)
{
    address_t ret;
    asm volatile("xor    %%fs:0x30,%0\n"
                 "rol    $0x11,%0\n"
                 : "=g"(ret)
                 : "0"(addr));
    return ret;
}

#else
/* code for 32 bit Intel arch */

typedef unsigned int address_t;
#define JB_SP 4
#define JB_PC 5

/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
address_t translate_address(address_t addr)
{
  address_t ret;
  asm volatile("xor    %%gs:0x18,%0\n"
               "rol    $0x9,%0\n"
      : "=g"(ret)
      : "0"(addr));
  return ret;
}

#endif

// Add near the top with other globals
static char* g_stack_memory = nullptr;     // Pre-allocated memory for all stacks
static bool* g_stack_in_use = nullptr;     // Tracks which stack slots are in use

// Scheduler states (must match the conceptual RUNNING/READY/BLOCKED)
enum State
{
    RUNNING,
    READY,
    BLOCKED
};

// Thread Control Block (TCB) structure
struct TCB
{
    int id;
    int quantums;
    char* stack;           // Pointer to this thread's stack in the global array
    int stack_index;       // Index of this thread's stack in the array
    State state;
    jmp_buf env;
    int wake_time;
    bool explicitly_blocked;

    TCB(int tid)
        : id(tid), quantums(0), stack(nullptr), stack_index(-1),
          state(READY), wake_time(-1), explicitly_blocked(false)
    {
    }

    // No need for a destructor that frees the stack
    ~TCB() {}
};

static std::unordered_map<int, TCB*> threads; // Changed from unique_ptr to raw pointer
static std::queue<int> ready_queue;
static std::unordered_set<int> blocked_set;
static int current_tid;
static int total_quantums;
static int quantum_usecs;

// Helper function to remove a thread from the ready queue
void remove_from_ready_queue(int tid)
{
  std::queue<int> temp_queue;
  while (!ready_queue.empty())
  {
    int id = ready_queue.front();
    ready_queue.pop();
    if (id != tid)
    {
      temp_queue.push(id);
    }
  }
  ready_queue = temp_queue;
}

// Scheduler handler function
void scheduler_handler(int signum)
{
  // Block signals during context switch to prevent nested signal handling
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGVTALRM);
  sigprocmask(SIG_BLOCK, &mask, nullptr);

  // Save the current thread's context
  if (sigsetjmp(threads[current_tid]->env, 1) == 0)
  {
    // Context saved successfully, now handle scheduling

    // Increment total quantum count
    total_quantums++;

    // Check if any sleeping threads need to wake up
    for (auto it = blocked_set.begin(); it != blocked_set.end();)
    {
      int tid = *it;
      if (threads[tid]->wake_time > 0 && threads[tid]->wake_time <= total_quantums)
      {
        threads[tid]->wake_time = -1; // Reset wake time
        if (!threads[tid]->explicitly_blocked)
        {
          threads[tid]->state = READY;
          ready_queue.push(tid);
          it = blocked_set.erase(it); // Remove from blocked set
        }
      }
      else
      {
        ++it;
      }
    }
    // If current thread is still in RUNNING state, move to READY
    if (threads[current_tid]->state == RUNNING)
    {
      threads[current_tid]->state = READY;
      ready_queue.push(current_tid);
    }

    // Select next thread to run
    if (ready_queue.empty())
    {
      std::cerr << "thread library error: no threads to schedule\n";
      exit(1); // No threads are ready to run
    }

    // Get next thread from queue
    int next_tid = ready_queue.front();
    ready_queue.pop();

    // Update state and quantum count for the next thread
    current_tid = next_tid;
    threads[current_tid]->state = RUNNING;
    threads[current_tid]->quantums++;

    // Unblock signals before switching context
    sigprocmask(SIG_UNBLOCK, &mask, nullptr);

    // Switch to the selected thread
    siglongjmp(threads[current_tid]->env, 1);
  }

  // If we get here, we're returning from a context switch (siglongjmp)
  // Just unblock signals and continue execution
  sigprocmask(SIG_UNBLOCK, &mask, nullptr);
}

/**
 * @brief initializes the thread library.
 * @brief initializes the thread library.
 *
 * Once this function returns, the main thread (tid == 0) will be set as RUNNING. There is no need to
 * provide an entry_point or to create a stack for the main thread - it will be using the "regular" stack and PC.
 * You may assume that this function is called before any other thread library function, and that it is called
 * exactly once.
 * The input to the function is the length of a quantum in micro-seconds.
 * It is an error to call this function with non-positive quantum_usecs.
 *
 * @return On success, return 0. On failure, return -1.
 */
int uthread_init(int quantum_usecs)
{
  // Block signals during critical section
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGVTALRM);
  sigprocmask(SIG_BLOCK, &mask, nullptr);

  // 1. validate the input
  if (quantum_usecs <= 0)
  {
    std::cerr << "thread library error: quantum_usecs must be positive\n";
    sigprocmask(SIG_UNBLOCK, &mask, nullptr);
    return -1;
  }

  // 2. Allocate memory for all stacks at once
  g_stack_memory = new char[MAX_THREAD_NUM * STACK_SIZE];
  g_stack_in_use = new bool[MAX_THREAD_NUM]();  // Initialize all to false

  // Register cleanup handler
  std::atexit([]() {
      // Free thread control blocks
      for (auto& pair : threads)
      {
        delete pair.second;
      }
      threads.clear();

      // Free stack memory
      delete[] g_stack_memory;
      delete[] g_stack_in_use;
  });

  // 2. Install scheduler SIGVTALRM handler
  struct sigaction sa = {0}; // Declare and initialize sa
  sa.sa_handler = scheduler_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  if (sigaction(SIGVTALRM, &sa, nullptr) < 0)
  {
    perror("system error: sigaction");
    sigprocmask(SIG_UNBLOCK, &mask, nullptr);
    exit(1);
  }

  // 3. Set up the timer
  struct itimerval timer;
  timer.it_interval.tv_sec = quantum_usecs / 1000000;
  timer.it_interval.tv_usec = quantum_usecs % 1000000;
  timer.it_value = timer.it_interval;

  if (setitimer(ITIMER_VIRTUAL, &timer, nullptr) < 0)
  {
    perror("system error: setitimer");
    sigprocmask(SIG_UNBLOCK, &mask, nullptr);
    exit(1);
  }

  // 4. Create and register the main thread TCB
  TCB* main_t = new TCB(0); // Changed from unique_ptr
  main_t->state = RUNNING;
  main_t->quantums = 1;
  threads[0] = main_t; // Store pointer directly
  current_tid = 0;
  total_quantums = 1;

  sigprocmask(SIG_UNBLOCK, &mask, nullptr);
  return 0;
}

/**
 * @brief Creates a new thread, whose entry point is the function entry_point with the signature
 * void entry_point(void).
 *
 * The thread is added to the end of the READY threads list.
 * The uthread_spawn function should fail if it would cause the number of concurrent threads to exceed the
 * limit (MAX_THREAD_NUM).
 * Each thread should be allocated with a stack of size STACK_SIZE bytes.
 * It is an error to call this function with a null entry_point.
 *
 * @return On success, return the ID of the created thread. On failure, return -1.
 */
int uthread_spawn(thread_entry_point entry_point)
{
  // Block signals during critical section
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGVTALRM);
  sigprocmask(SIG_BLOCK, &mask, nullptr);

  // 1. validate the input
  if (entry_point == nullptr)
  {
    std::cerr << "thread library error: entry_point is null\n";
    sigprocmask(SIG_UNBLOCK, &mask, nullptr);
    return -1;
  }

  if (threads.size() >= MAX_THREAD_NUM)
  {
    std::cerr << "thread library error: too many threads\n";
    sigprocmask(SIG_UNBLOCK, &mask, nullptr);
    return -1;
  }

  // 2. get smallest available thread ID
  int tid = 1;
  while (threads.count(tid) > 0)
  {
    ++tid;
    if (tid >= MAX_THREAD_NUM)
    {
      std::cerr << "thread library error: no available thread ID\n";
      sigprocmask(SIG_UNBLOCK, &mask, nullptr);
      return -1;
    }
  }

  // 3. Find an available stack slot
  int stack_index = -1;
  for (int i = 0; i < MAX_THREAD_NUM; i++) {
    if (!g_stack_in_use[i]) {
      stack_index = i;
      g_stack_in_use[i] = true;
      break;
    }
  }

  if (stack_index == -1) {
    std::cerr << "thread library error: no stack slots available\n";
    sigprocmask(SIG_UNBLOCK, &mask, nullptr);
    return -1;
  }

  // 4. Allocate and initialize TCB
  TCB* new_t = new TCB(tid);
  new_t->stack = &g_stack_memory[stack_index * STACK_SIZE];
  new_t->stack_index = stack_index;

  // 5. Get temporary context
  if (sigsetjmp(new_t->env, 1) == 0)
  {
    // Continue setup
  }
  else
  {
    sigprocmask(SIG_UNBLOCK, &mask, nullptr);
    entry_point();
    uthread_terminate(tid);
  }

  // 6. Set up stack and context
  address_t sp = (address_t)new_t->stack + STACK_SIZE - sizeof(address_t);

  // Patch the jmpbuf slots
  new_t->env->__jmpbuf[JB_SP] = translate_address(sp);
  new_t->env->__jmpbuf[JB_PC] = translate_address((address_t)entry_point);

  // Clear signal mask
  sigemptyset(&new_t->env->__saved_mask);

  // 7. Store in map and enqueue
  threads[tid] = new_t; // Store pointer directly
  threads[tid]->state = READY;
  ready_queue.push(tid);

  // Unblock signals
  sigprocmask(SIG_UNBLOCK, &mask, nullptr);
  return tid;
}

/**
 * @brief Terminates the thread with ID tid and deletes it from all relevant control structures.
 *
 * All the resources allocated by the library for this thread should be released. If no thread with ID tid exists it
 * is considered an error. Terminating the main thread (tid == 0) will result in the termination of the entire
 * process using exit(0) (after releasing the assigned library memory).
 *
 * @return The function returns 0 if the thread was successfully terminated and -1 otherwise. If a thread terminates
 * itself or the main thread is terminated, the function does not return.
 */
int uthread_terminate(int tid)
{
  // Block signals during critical section
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGVTALRM);
  sigprocmask(SIG_BLOCK, &mask, nullptr);

  // 1. Find the thread in our map
  auto it = threads.find(tid);
  if (it == threads.end())
  {
    std::cerr << "thread library error: thread ID " << tid << " does not exist\n";
    sigprocmask(SIG_UNBLOCK, &mask, nullptr);
    return -1;
  }

  // 2. If it's the main thread, clean up everything and exit
  if (tid == 0)
  {
    // Free all threads when main terminates
    for (auto& pair : threads)
    {
      delete pair.second; // Manually delete all TCBs
    }
    threads.clear();
    sigprocmask(SIG_UNBLOCK, &mask, nullptr);
    exit(0);
  }

  // 3. Terminating another thread
  if (tid != current_tid)
  {
    TCB* t = it->second;

    // a) If it was READY, remove from the ready queue
    if (t->state == READY)
    {
      remove_from_ready_queue(tid);
    }
      // b) If it was blocked, remove from the blocked set
    else if (t->state == BLOCKED)
    {
      blocked_set.erase(tid);
    }

    // c) Erase the TCB and free memory
    threads.erase(it);
    g_stack_in_use[t->stack_index] = false; // Mark stack as free
    delete t; // Manually delete the TCB

    sigprocmask(SIG_UNBLOCK, &mask, nullptr);
    return 0;
  }

  // 4. Terminating self (tid == current_tid)
  //    We must pick a new thread and jump into it before freeing our TCB.

  // a) If no other thread is ready, just exit
  if (ready_queue.empty())
  {
    // Free all threads and exit
    TCB* self = it->second;
    threads.erase(it);
    delete self;

    for (auto& pair : threads)
    {
      delete pair.second;
    }
    threads.clear();

    sigprocmask(SIG_UNBLOCK, &mask, nullptr);
    exit(0);
  }

  total_quantums++; // Increment total quantums

  // b) Dequeue the next thread
  int next_tid = ready_queue.front();
  ready_queue.pop();

  // IMPORTANT: Save context BEFORE deleting current thread
  jmp_buf next_env;
  memcpy(next_env, threads[next_tid]->env, sizeof(jmp_buf));

  // c) Switch state
  threads[next_tid]->state = RUNNING;
  threads[next_tid]->quantums++;

  // Store current TCB to delete after updating current_tid
  TCB* self = it->second;
  current_tid = next_tid;

  // Remove from threads map
  threads.erase(it);
  g_stack_in_use[self->stack_index] = false; // Mark stack as free

  // d) Unblock signals, delete self, and jump to next thread
  sigprocmask(SIG_UNBLOCK, &mask, nullptr);
  delete self; // Free memory AFTER we're done with it
  siglongjmp(next_env, 1);

  // Unreachable
  return 0;
}

/**
 * @brief Blocks the thread with ID tid. The thread may be resumed later using uthread_resume.
 *
 * If no thread with ID tid exists it is considered as an error. In addition, it is an error to try blocking the
 * main thread (tid == 0). If a thread blocks itself, a scheduling decision should be made. Blocking a thread in
 * BLOCKED state has no effect and is not considered an error.
 *
 * @return On success, return 0. On failure, return -1.
 */
int uthread_block(int tid)
{
  // Block signals during critical section
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGVTALRM);
  sigprocmask(SIG_BLOCK, &mask, nullptr);

  // 1. validate the input
  if (tid == 0 || threads.find(tid) == threads.end())
  {
    std::cerr << "thread library error: invalid thread ID " << tid << "\n";
    sigprocmask(SIG_UNBLOCK, &mask, nullptr);
    return -1;
  }

  threads[tid]->explicitly_blocked = true; // Mark as explicitly blocked

  // 2. Check if already blocked
  if (threads[tid]->state == BLOCKED)
  {
    sigprocmask(SIG_UNBLOCK, &mask, nullptr);
    return 0; // No effect, already blocked
  }

  // If in READY state, remove from ready queue
  if (threads[tid]->state == READY)
  {
    remove_from_ready_queue(tid);
  }

  // 3. Block the thread and update state
  threads[tid]->state = BLOCKED;
  blocked_set.insert(tid);

  // 4. If blocking self, schedule next thread
  if (tid == current_tid)
  {
    sigprocmask(SIG_UNBLOCK, &mask, nullptr);
    scheduler_handler(SIGVTALRM);
  }
  // Unblock signals
  sigprocmask(SIG_UNBLOCK, &mask, nullptr);
  return 0;
}

/**
 * @brief Resumes a blocked thread with ID tid and moves it to the READY state.
 *
 * Resuming a thread in a RUNNING or READY state has no effect and is not considered as an error. If no thread with
 * ID tid exists it is considered an error.
 *
 * @return On success, return 0. On failure, return -1.
 */
int uthread_resume(int tid)
{
  // Block signals during critical section
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGVTALRM);
  sigprocmask(SIG_BLOCK, &mask, nullptr);

  // 1. validate the input
  if (threads.find(tid) == threads.end())
  {
    std::cerr << "thread library error: invalid thread ID " << tid << "\n";
    sigprocmask(SIG_UNBLOCK, &mask, nullptr);
    return -1;
  }

  // 2. Check if already in READY state
  if (threads[tid]->state == READY)
  {
    sigprocmask(SIG_UNBLOCK, &mask, nullptr);
    return 0; // No effect, already in READY state
  }

  threads[tid]->explicitly_blocked = false; // Reset explicitly blocked flag

  // 3. Check if not sleeping
  if (threads[tid]->state == BLOCKED && threads[tid]->wake_time < 0)
  {
    threads[tid]->state = READY;
    ready_queue.push(tid);
    blocked_set.erase(tid); // Remove from blocked set
    sigprocmask(SIG_UNBLOCK, &mask, nullptr);
    return 0; // No effect, already in READY state
  }

  // 4. If resuming self, schedule next thread
  if (tid == current_tid)
  {
    sigprocmask(SIG_UNBLOCK, &mask, nullptr);
    scheduler_handler(SIGVTALRM);
  }

  // Unblock signals
  sigprocmask(SIG_UNBLOCK, &mask, nullptr);
  return 0;
}

/**
 * @brief Blocks the RUNNING thread for num_quantums quantums.
 *
 * Immediately after the RUNNING thread transitions to the BLOCKED state a scheduling decision should be made.
 * After the sleeping time is over, the thread should go back to the end of the READY queue.
 * If the thread which was just RUNNING should also be added to the READY queue, or if multiple threads wake up
 * at the same time, the order in which they're added to the end of the READY queue doesn't matter.
 * The number of quantums refers to the number of times a new quantum starts, regardless of the reason. Specifically,
 * the quantum of the thread which has made the call to uthread_sleep isn’t counted.
 * It is considered an error if the main thread (tid == 0) calls this function.
 *
 * @return On success, return 0. On failure, return -1.
 */
int uthread_sleep(int num_quantums)
{
  // Block signals during critical section
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGVTALRM);
  sigprocmask(SIG_BLOCK, &mask, nullptr);

  // 1. validate the input
  if (num_quantums <= 0 || current_tid == 0)
  {
    std::cerr << "thread library error: invalid sleep time or main thread\n";
    sigprocmask(SIG_UNBLOCK, &mask, nullptr);
    return -1;
  }

  // 2. Block the RUNNING thread
  threads[current_tid]->state = BLOCKED;
  threads[current_tid]->wake_time = total_quantums + num_quantums + 1;
  blocked_set.insert(current_tid);

  // 3. Schedule next thread
  scheduler_handler(SIGVTALRM);

  // Unblock signals
  sigprocmask(SIG_UNBLOCK, &mask, nullptr);
  return 0;
}

/**
 * @brief Returns the thread ID of the calling thread.
 *
 * @return The ID of the calling thread.
 */
int uthread_get_tid()
{
  // Block signals during critical section
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGVTALRM);
  sigprocmask(SIG_BLOCK, &mask, nullptr);
  int result = current_tid;
  // Unblock signals
  sigprocmask(SIG_UNBLOCK, &mask, nullptr);
  return result;
}

/**
 * @brief Returns the total number of quantums since the library was initialized, including the current quantum.
 *
 * Right after the call to uthread_init, the value should be 1.
 * Each time a new quantum starts, regardless of the reason, this number should be increased by 1.
 *
 * @return The total number of quantums.
 */
int uthread_get_total_quantums()
{
  // Block signals during critical section
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGVTALRM);
  sigprocmask(SIG_BLOCK, &mask, nullptr);
  int result = total_quantums;
  // Unblock signals
  sigprocmask(SIG_UNBLOCK, &mask, nullptr);

  return result;
}

/**
 * @brief Returns the number of quantums the thread with ID tid was in RUNNING state.
 *
 * On the first time a thread runs, the function should return 1. Every additional quantum that the thread starts should
 * increase this value by 1 (so if the thread with ID tid is in RUNNING state when this function is called, include
 * also the current quantum). If no thread with ID tid exists it is considered an error.
 *
 * @return On success, return the number of quantums of the thread with ID tid. On failure, return -1.
 */
int uthread_get_quantums(int tid)
{
  // Block signals during critical section
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGVTALRM);
  sigprocmask(SIG_BLOCK, &mask, nullptr);

  if (threads.find(tid) == threads.end())
  {
    std::cerr << "thread library error: invalid thread ID " << tid << "\n";
    sigprocmask(SIG_UNBLOCK, &mask, nullptr);
    return -1;
  }

  int result = threads[tid]->quantums;

  // Unblock signals
  sigprocmask(SIG_UNBLOCK, &mask, nullptr);

  return result;
}