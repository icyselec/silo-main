/*
	Name: SILO Simulator
	Copyright: SILO Project
	Author: see AUTHOR file
	Date: 18-11-20 15:34 (DD-MM-YY)
	Description: simulator core
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <pthread.h>

#include "../include/define.h"
#include "../include/node/node.h"
#include "../include/simulator/simulator.h"
#include "../include/simulator/simulator_in.h"
#include "../include/simulator/thread.h"

 
// declaration
static int    SimuThreadInit(struct SystemThread *);
static void * thread_main(void *);
static void * thread_controller(void * p);

static inline void SimuThreadWait(struct SystemSimu *);
static inline void SimuThreadSignal(struct SystemSimu *);



struct Simulator * SimuCreate(void) {
    struct Simulator * s = malloc(sizeof(struct Simulator));
    if (s == NULL)
        return NULL;
    
    int init_cnd, init_mtx, init_thr;
    int init_tcnd, init_tmtx, init_tthr;
    
	s->simu.nextexec = (NODE**)malloc(BASICMEM);
	s->simu.nextemax = 0;
	s->simu.sentlist = (char *)malloc(BASICMEM);
	s->simu.needmake = true;

#ifdef DEFT_SIMU_MODE
    SimuTickMode(&s->simu);
#elseif
    SimuStepMode(&s->simu);
#endif


    init_cnd = pthread_cond_init(&s->simu.cond, NULL);
	init_mtx = pthread_mutex_init(&s->simu.mtx, NULL);
    init_tcnd = pthread_cond_init(&s->simu.thread.tcond, NULL);
    init_tmtx = pthread_mutex_init(&s->simu.thread.tmtx, NULL);

    init_tthr = pthread_create(&s->simu.thread.tcontrol, NULL, thread_controller, (void *)s);
    pthread_detach(s->simu.thread.tcontrol);
    
    SimuThreadInit(&s->simu.thread);
	init_thr = SimuThreadSetNum(&s->simu, DEFT_THREAD_NUMBER);
    

    
	if (s->simu.nextexec==NULL || s->simu.sentlist==NULL || init_tcnd || init_tmtx || init_tthr || init_cnd || init_mtx || init_thr) {
        if (s->simu.nextexec != NULL)
            free(s->simu.nextexec);
        if (s->simu.sentlist != NULL)
            free(s->simu.sentlist);
        if (!init_cnd)
            pthread_cond_destroy(&s->simu.cond);
        if (!init_mtx)
            pthread_mutex_destroy(&s->simu.mtx);
        if (!init_thr) {
            SimuThreadSetNum(&s->simu, 0);
            free(s->simu.thread.argptr);
            free(s->simu.thread.tidptr);
        }
        if (!init_tcnd)
            pthread_cond_destroy(&s->simu.thread.tcond);
        if (!init_tmtx)
            pthread_mutex_destroy(&s->simu.thread.tmtx);
        if (!init_tthr)
            pthread_cancel(s->simu.thread.tcontrol);
        return NULL;
    }
    return s;
}
int  SimuDelete(struct Simulator * s) {
    if (SimuThreadSetNum(&s->simu, 0))
        return 1;
    
    pthread_mutex_destroy(&s->simu.mtx);
    pthread_mutex_destroy(&s->simu.thread.mtx);
    pthread_mutex_destroy(&s->simu.thread.tmtx);
    pthread_cond_destroy(&s->simu.cond);
    pthread_cond_destroy(&s->simu.thread.cond);
    pthread_cond_destroy(&s->simu.thread.tcond);
    
    free(s->simu.thread.argptr);
    free(s->simu.thread.tidptr);
    free(s->simu.nextexec);
    free(s->simu.sentlist);
    
    free(s);
    
    return 0;
}

void SimuResetNextExec(struct Simulator * s) {
	for (NODEID i = 0; i < NodeGetLastID(&s->node); i++)
		s->simu.nextexec[i] = NULL;
    s->simu.nextemax = 0;
}
void SimuResetSentList(struct Simulator * s) {
	for (NODEID i = 0; i < NodeGetLastID(&s->node); i++)
		s->simu.sentlist[i] = false;
}
void SimuListofNextExec(struct Simulator * s) {
    for (NODEID i = 0; i < NodeGetLastID(&s->node); i++)
        printf("NextExec(%lld) : %p\n", i, s->simu.nextexec[i]);
}
void SimuListofSentList(struct Simulator * s) {
    printf("SentList : ");
    
    for (NODEID i = 0; i < NodeGetLastID(&s->node); i++)
        printf("%d ", s->simu.sentlist[i]);
}

void SendSignal(SENDFORM dest, SIGNAL signal) {
	dest.node->input[dest.port] = signal;
    dest.node->simu->simu.sentlist[dest.node->nodeid] = true;
}
void Transfer(SENDFORM dest, SIGNAL signal) {
	dest.node->input[dest.port] = signal;
    dest.node->simu->simu.sentlist[dest.node->nodeid] = true;
    dest.node->simu->simu.needmake = true;
}


NODEID SimuMakeList(struct Simulator * s) {
	NODEID i, j, k;
	
	for (i = j = 0, k = NodeGetLastID(&s->node); i < k; i++) {
        if (s->simu.sentlist[i]) {
            s->simu.sentlist[i] = false;
            s->simu.nextexec[j++] = NodeGetPtr(&s->node, i);
        }
	}
	s->simu.nextemax = j;
	s->simu.needmake = 0;
    printf("nextemax 0 : %lld\n", s->simu.nextemax);
    return j;
}

int SimuReSize(struct Simulator * s) {
	void * p, * q;
	
	p = realloc(s->simu.nextexec, sizeof(NODE*) * NodeGetLastID(&s->node));
	q = realloc(s->simu.sentlist, sizeof(char ) * NodeGetLastID(&s->node));
    
	if (p == NULL || q == NULL)
		return 1;
	s->simu.nextexec = p, s->simu.sentlist = q;
	return 0;
}

static int SimuThreadInit(struct SystemThread * t) {
	pthread_cond_init(&t->cond, NULL);
	pthread_mutex_init(&t->mtx, NULL);

	t->number = 0;
    
	t->argptr = (struct ThreadArgument **)malloc(0);
	t->tidptr = (pthread_t*)malloc(0);
	
	if (t->argptr==NULL || t->tidptr==NULL) {
        if (t->argptr != NULL)
            free(t->argptr);
        if (t->tidptr != NULL)
            free(t->tidptr);
        return 1;
    }
	return 0;
}

int SimuThreadSetNum(struct SystemSimu * s, unsigned long long n) {
	unsigned long long i;
	void * p, * q;
	
	if (n > s->thread.number) { // create thread
		p = realloc((void*)s->thread.argptr, sizeof(struct ThreadArgument *) * n);
		q = realloc((void*)s->thread.tidptr, sizeof(pthread_t) * n);
		if (p==NULL || q==NULL)
			return 1;
		s->thread.argptr = p;
		s->thread.tidptr = q;
        s->thread.endcount = n - s->thread.number;
		for (i = s->thread.number; i < n; i++) {
            s->thread.argptr[i] = malloc(sizeof(struct ThreadArgument));
            s->thread.argptr[i]->workid    = i;
            s->thread.argptr[i]->simulator = s;
			pthread_create(&s->thread.tidptr[i], NULL, thread_main, (void*)s->thread.argptr[i]);
            pthread_detach( s->thread.tidptr[i]);
		}
	}
	else if (n < s->thread.number) { // delete thread
		for (i = n; i < s->thread.number; i++) {
			pthread_cancel(s->thread.tidptr[i]);
            free(s->thread.argptr[i]);
        }
		p = realloc((void*)s->thread.argptr, sizeof(struct ThreadArgument) * n);
		q = realloc((void*)s->thread.tidptr, sizeof(pthread_t) * n);
		if (p==NULL || q==NULL)
			return 1;
		s->thread.argptr = p;
		s->thread.tidptr = q;
	}
	else
		return 1;
    
	s->thread.number = n;
	return 0;
}
unsigned long long SimuThreadGetNum(struct SystemThread * t) { return t->number; }

void SimuTickMode(struct SystemThread * t) { t->mode = 0; }
void SimuStepMode(struct SystemThread * t) { t->mode = 1; }
bool SimuGetSimMode(struct SystemThread * t) { return t->mode; }


inline int Simulate(struct Simulator * s) {
    pthread_mutex_lock(&s->simu.thread.tmtx);
    pthread_cond_signal(&s->simu.thread.tcond);
    pthread_mutex_unlock(&s->simu.thread.tmtx);
    return 0;
}

// don't change
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-noreturn"
static void * thread_main(void * p) {
    NODEID i, j;
    struct ThreadArgument * arg = (struct ThreadArgument *)p;
    struct Simulator      * s   = (struct Simulator      *)arg->simulator;
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    
    while (true) {
        SimuThreadWait(&s->simu); // this
        
        for (i = arg->workid; i < s->simu.nextemax; i += s->simu.thread.number)
            s->simu.nextexec[i]->function(s->simu.nextexec[i]);
        
        SimuThreadWait(&s->simu);
        
        for (i = j = arg->workid; i < NodeGetLastID(&s->node); i += s->simu.thread.number) {
            if (s->simu.sentlist[i]) {
                s->simu.sentlist[i] = 0;
                s->simu.nextexec[j] = NodeGetPtr(&s->node, i);
                j += s->simu.thread.number;
            }
        }
        for (i = j; i < NodeGetLastID(&s->node); i += s->simu.thread.number) {
            s->simu.nextexec[i] = NULL;
        }
        arg->makemx = j;
    }
    return (void *)NULL; // dummy
}
static void * thread_controller(void * p) {
    register NODEID i, h, l;

    struct Simulator * s = (struct Simulator *)p;
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    
    while (true) {
        pthread_cond_wait(&s->simu.thread.tcond, &s->simu.thread.tmtx);
        
        if (s->simu.needmake)
            SimuMakeList(s);
        
        SimuThreadSignal(&s->simu);
        SimuThreadSignal(&s->simu);
        
        for (i = h = 0, l = -1; i < s->simu.thread.number; i++) {
            if (s->simu.thread.argptr[i]->makemx > h) // find high
                h = s->simu.thread.argptr[i]->makemx;
            if (s->simu.thread.argptr[i]->makemx < l)
                l = s->simu.thread.argptr[i]->makemx;
        }
        
        while (l < h) {
            if (s->simu.nextexec[l] == NULL) {
                while (s->simu.nextexec[h] == NULL)
                    h--;
                if (l < h)
                    s->simu.nextexec[l++] = s->simu.nextexec[h--];
                else
                    continue;
            }
            else
                l++;
        }
        s->simu.needmake = false;
        s->simu.nextemax = l;
        printf("nextemax 1 : %lld", l);
    }
    return (void *)NULL;
}
// don't change
#pragma clang diagnostic pop



static inline void SimuThreadWait(struct SystemSimu * s) {
    pthread_mutex_lock(&s->thread.mtx); // critical section start

    if (!--s->thread.endcount) {
        pthread_mutex_lock(&s->mtx); // if not waiting, signal is will lost.
        pthread_cond_signal(&s->cond);
        pthread_mutex_unlock(&s->mtx);
    }
    pthread_cond_wait(&s->thread.cond, &s->thread.mtx); // this
    
    pthread_mutex_unlock(&s->thread.mtx); // critical section ended
}
static inline void SimuThreadSignal(struct SystemSimu * s) {
	pthread_mutex_lock(&s->mtx);
	
    // set thread_endcount current thread number
    s->thread.endcount = s->thread.number;
    
    // if the threads are not waiting, a deadlock occurs.
    // so this function has to wait.
    pthread_mutex_lock(&s->thread.mtx); // don't change
	pthread_cond_broadcast(&s->thread.cond);
	pthread_mutex_unlock(&s->thread.mtx); // don't change
    
    // waiting for threads
    pthread_cond_wait(&s->cond, &s->mtx);
    
	pthread_mutex_unlock(&s->mtx);
}
