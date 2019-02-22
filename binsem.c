/*****************************************************************************
This file implements a binary semaphore as described in the course's book (
Modern Operating Systems, 4th edition, p. 133, figure 2-29).
 ****************************************************************************/
#include <signal.h>
#include <unistd.h>

#include "binsem.h"

/*
 * as described in the header, s is assumed to never be NULL, it is
 * to the caller to make sure this is true (this goes for the rest of
 * the functions in this file).
 */
void binsem_init(sem_t *s, int init_val){
    if (init_val > 0)
        *s = 1;
    else
        *s = 0;
    return;
}

/*
 * implemented as described in page 133 (fig. 2-29) of the course's book (Modern
 * Operating Systems, 4th edition), only since this is a semaphore after all,
 * the unlocked state is 1 and not 0, and no wake is used since the scheduler is
 * responsible for waking up the next thread and only one thread is running at
 * any given time.
 */
void binsem_up(sem_t *s){
    xchg(s,1);
}

/*
 * also implemented after the description in the book (figure 2-29), if the
 * state is locked when trying to access the binary semaphore, the next thread
 * shall be awaken instantaneously, while the current one should go to sleep, so
 * the alarm set by the current thread is first canceled by calling alarm(0),
 * and an instant alarm is raised to invoke a switch by the scheduler.
 */
int binsem_down(sem_t *s){
    if (xchg(s,0) == 0){
        alarm(0);
        return raise(SIGALRM);
    }
    else
        return 0;
}
