/*****************************************************************************
User Threads:
this file defines a simple library for creating & scheduling user-level threads.
 ****************************************************************************/
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>
#include <sys/time.h>
#include <unistd.h>

#include "ut.h"

#define QUANTUM 1
#define INTERVAL_MILLI 100000
#define INTERVAL_MICRO 100

static int release_memory(void);    /*see below*/
void thread_signals_handler(int); /*see below*/

/*
 * the initialization is redundant since static variables are guaranteed
 * to be automatically initialized to 0, but this is to assure the user.
 */
static ut_slot threads_table = NULL; /*the table that holds the threads' data*/
static volatile int threads_table_size = 0; /*number of threads*/
static tid_t next_position = 0; /*the next available index in the table*/
static volatile int curr_thread = 0; /*current thread running, by index*/
static unsigned long vtime = 0; /*used to keep track of threads running time*/

static struct sigaction old_sigaction; /*holds the sigaction originally assigned to SIGINT signal*/
static ucontext_t uc_out; /*holds the original context (main) before running ut_start*/


/*
 * behaves as described in the header, additionally, it makes sure that
 * the table is re-initialized and freed in case this is not the first
 * time this function is called for some reason (or future expansion). all
 * variables are also re-initialized, and an error is returned to the caller
 * in case the memory allocation fails.
 */
int ut_init(int tab_size) {
    if (tab_size > MAX_TAB_SIZE || tab_size < MIN_TAB_SIZE)
        tab_size = MAX_TAB_SIZE;
    if (threads_table)
        release_memory();
    threads_table_size = tab_size;
    next_position = 0;
    curr_thread = 0;
    threads_table = (ut_slot)malloc((tab_size) * sizeof(ut_slot_t));
    if (threads_table)
        return 0;
    else
        return SYS_ERR;
}

/*
 * behaves as described in the header file: creates a stack for the context's
 * usage, creates a new context and initializes it to point to the original
 * context, and initializes the thread's table entry additional fields and
 * advences the table's next available slot index.
 */
tid_t ut_spawn_thread(void (*func)(int), int arg){
    if (next_position == threads_table_size)
        return TAB_FULL;
    void *stack = (void *)malloc(STACKSIZE);
    if (!stack || getcontext(&(threads_table[next_position].uc)) == -1)
        return SYS_ERR;
    threads_table[next_position].uc.uc_link = &(uc_out);
    threads_table[next_position].uc.uc_stack.ss_sp = stack;
    threads_table[next_position].uc.uc_stack.ss_size = STACKSIZE;
    makecontext(&(threads_table[next_position].uc), (void(*)(void))func, 1, arg);
    threads_table[next_position].vtime = 0;
    threads_table[next_position].func = func;
    threads_table[next_position].arg = arg;
    return next_position++;
}

/*
 * frees the dynamically allocated data structures of this library,
 * which includes the stacks used by the ucontexts and the threads
 * table itself. should print an error in case the table was not
 * initialized or points to NULL.
 *
 * Returns:
 * SYS_ERR - if table was NULL pointer.
 */
static int release_memory(void){
    int i;
    if (threads_table){
        for (i = 0; i < threads_table_size; i++)
            free(threads_table[i].uc.uc_stack.ss_sp);
        free((void *)threads_table);
        return 0;
    }
    perror("Could not relase memory.\n");
    exit(EXIT_FAILURE);
}

/*
 * a handler for three different signals:
 * SIGALRM: when received, it creates a new alarm for the period defined by
 * QUANTUM, for the next context swap, then swaps between the current context
 * and the one that follows it in the threads table in a round robin manner.
 * SIGVTALRM: advances the time for the current thread and updates vtime.
 * SIGINT: extracts the original handler assigned to this signal, calls it,
 * then releases the dynamically allocated memory by calling "release_memory".
 * assuming pressing CTRL+C terminates the program.
 *
 * Parameters:
 * signal - the signal number to be handled.
 */
void thread_signals_handler(int signal){
    int last_thread;
    if (signal == SIGALRM){
        alarm(QUANTUM);
        last_thread = curr_thread;
        curr_thread =  ((curr_thread + 1) % threads_table_size);
        if (swapcontext(&(threads_table[last_thread].uc), &(threads_table[curr_thread].uc)) == -1){
            perror("\"swapcontext\" has failed.\n");
            exit(EXIT_FAILURE);
        }
    }
    else if (signal == SIGVTALRM){
        vtime += INTERVAL_MICRO;
        threads_table[curr_thread].vtime += INTERVAL_MICRO;
    }
    else if (signal == SIGINT){
        alarm(0);
        void (*old_handler)(int) = old_sigaction.sa_handler;
        if(old_handler) old_handler(SIGINT);
        release_memory();
    }
}

/*
 * behaves as described in the header. creates the itimerval struct used
 * to keep track of time and update it every INTERVAL milli seconds, also creates
 * the sigaction struct which assigns the "thread_signals_handler" to handle
 * the different signals, but before, the SIGINT handler, if assigned, is
 * stored aside in case the updated handler wants to call it when
 * CTRL+C are pressed. the virtual timer is then set and started. the function
 * stores the context it was called from (for any future use) and then starts
 * the initial alarm signal (to invoke handler) and swaps the current
 * context with the first one in the table.
 */
int ut_start(void){
    int error_count = 0;
    struct sigaction sa;
    struct itimerval itv;
    itv.it_interval.tv_sec = 0;
    itv.it_interval.tv_usec = INTERVAL_MILLI;
    itv.it_value = itv.it_interval;
    sa.sa_flags = SA_RESTART;
    if (sigfillset(&sa.sa_mask) == -1) return SYS_ERR;
    sa.sa_handler = thread_signals_handler;
    if (setitimer(ITIMER_VIRTUAL, &itv, NULL) == -1) return SYS_ERR;
    error_count += sigaction(SIGALRM, &sa, NULL);
    error_count += sigaction(SIGVTALRM, &sa, NULL);
    error_count += sigaction(SIGINT, NULL, &old_sigaction);
    error_count += sigaction(SIGINT, &sa, NULL);
    if (error_count != 0) return SYS_ERR;
    alarm(QUANTUM);
    swapcontext(&uc_out, &(threads_table[0].uc));
    return (SYS_ERR); /* if this line is ever reached, then swapcontext has failed */
}

/*
 * behaves as described in the header. in case the user tries to access an
 * out of bounds index in the threads table, zero is returned.
 */
unsigned long ut_get_vtime(tid_t tid){
    if (0 <= tid && tid < threads_table_size)
        return threads_table[tid].vtime;
    else
        return 0;
}
