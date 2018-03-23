#include <stdio.h>
#include <monitor.h>
#include <kmalloc.h>
#include <assert.h>
#include <sync.h>


// Initialize monitor.
void     
monitor_init (monitor_t * mtp, size_t num_cv) {
    int i;
    assert(num_cv>0);
    mtp->next_count = 0;
    mtp->cv = NULL;
    sem_init(&(mtp->mutex), 1); //unlocked
    sem_init(&(mtp->next), 0);
    mtp->cv =(condvar_t *) kmalloc(sizeof(condvar_t)*num_cv);
    assert(mtp->cv!=NULL);
    for(i=0; i<num_cv; i++){
        mtp->cv[i].count=0;
        sem_init(&(mtp->cv[i].sem),0);
        mtp->cv[i].owner=mtp;
    }
}

// Unlock one of threads waiting on the condition variable. 
void 
cond_signal (condvar_t *cvp) {
    //LAB7 EXERCISE1: 2015011308
    cprintf("cond_signal begin: cvp %x, cvp->count %d, cvp->owner->next_count %d\n", cvp, cvp->count, cvp->owner->next_count);
    if (cvp->count) {
        bool intr_flag;
        local_intr_save(intr_flag);
        up(&cvp->sem);
        cvp->owner->next_count++; // If put this before unlocking, we can keep the interruption on
        down(&cvp->owner->next);
        cvp->owner->next_count--;
        local_intr_restore(intr_flag);
    }
    cprintf("cond_signal end: cvp %x, cvp->count %d, cvp->owner->next_count %d\n", cvp, cvp->count, cvp->owner->next_count);
}

// Suspend calling thread on a condition variable waiting for condition Atomically unlocks 
// mutex and suspends calling thread on conditional variable after waking up locks mutex. Notice: mp is mutex semaphore for monitor's procedures
void
cond_wait (condvar_t *cvp) {
    //LAB7 EXERCISE1: 2015011308
    cprintf("cond_wait begin:  cvp %x, cvp->count %d, cvp->owner->next_count %d\n", cvp, cvp->count, cvp->owner->next_count);
    bool intr_flag;
    local_intr_save(intr_flag);
    up(cvp->owner->next_count ? &cvp->owner->next : &cvp->owner->mutex);
    cvp->count++; // If put this before unlocking, we can keep the interruption on
    down(&cvp->sem);
    cvp->count--;
    local_intr_restore(intr_flag);
    cprintf("cond_wait end:  cvp %x, cvp->count %d, cvp->owner->next_count %d\n", cvp, cvp->count, cvp->owner->next_count);
}
