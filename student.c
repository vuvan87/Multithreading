/*
 * student.c
 * Multithreaded OS Simulation for CS 2200, Project 6
 *
 * This file contains the CPU scheduler for the simulation.
 * Name: Van Vu
 */

#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include "os-sim.h"


/*
 * current[] is an array of pointers to the currently running processes.
 * There is one array element corresponding to each CPU in the simulation.
 *
 * current[] should be updated by schedule() each time a process is scheduled
 * on a CPU.  Since the current[] array is accessed by multiple threads, you
 * will need to use a mutex to protect it.  current_mutex has been provided
 * for your use.
 */
static pcb_t **current;
static pthread_mutex_t current_mutex;
static pcb_t* head = NULL;
static pthread_mutex_t queue_mutex;
static pthread_cond_t queue_not_empty;
static int size; 
static int time_slice;
static int scheduling_algorithm;
static int count_cpu;

static void enqueue(pcb_t* process) {
        printf("enqueue\n");

    pthread_mutex_lock(&queue_mutex);
    if (head == NULL) {
        head = process;
        size++;
    } else {
        pcb_t* temp;
        if (scheduling_algorithm == 2) { // static priority
            temp = head;
            if (temp->static_priority < process->static_priority) {
                process->next = temp;
                head = process;
            }
            else {
                while (temp->next != NULL && temp->next->static_priority > process->static_priority) {
                    temp = temp->next;
                }
                /*if there are no lower priority place it at the end*/
                process->next = temp->next;
                temp->next = process;
                size++;
            }
        } else { //FIFO and Round Robin algorithm
            temp = head;
            while (temp->next != NULL) {
                temp = temp->next;
            }
            process->next = NULL;
            temp->next = process;
            size++;
       }
    }
    pthread_cond_broadcast(&queue_not_empty);
    pthread_mutex_unlock(&queue_mutex);
}


static pcb_t* dequeue() {
    pcb_t* output;
    printf("dequeue\n");
    pthread_mutex_lock(&queue_mutex);
    output = head;
    if (head != NULL) {
        head = head->next;
        output->next = NULL;
    }
    size--;
    pthread_mutex_unlock(&queue_mutex);
    return output;
}
/*
 * schedule() is your CPU scheduler.  It should perform the following tasks:
 *
 *   1. Select and remove a runnable process from your ready queue which 
 *  you will have to implement with a linked list or something of the sort.
 *
 *   2. Set the process state to RUNNING
 *
 *   3. Call context_switch(), to tell the simulator which process to execute
 *      next on the CPU.  If no process is runnable, call context_switch()
 *      with a pointer to NULL to select the idle process.
 *  The current array (see above) is how you access the currently running
 *  process indexed by the cpu id. See above for full description.
 *  context_switch() is prototyped in os-sim.h. Look there for more information 
 *  about it and its parameters.
 */
static void schedule(unsigned int cpu_id)
{
    printf("schedule\n");
    pcb_t* runnableProcess = dequeue();
    if (runnableProcess != NULL) {
        runnableProcess->state = PROCESS_RUNNING;
    }
        pthread_mutex_lock(&current_mutex);
        current[cpu_id] = runnableProcess;
        pthread_mutex_unlock(&current_mutex);
        if (scheduling_algorithm == 1) { // Round Robin
            context_switch(cpu_id, runnableProcess, time_slice);
        } else { // FIFO and Static priority
            context_switch(cpu_id, runnableProcess, -1);
        }
}


/*
 * idle() is your idle process.  It is called by the simulator when the idle
 * process is scheduled.
 *
 * This function should block until a process is added to your ready queue.
 * It should then call schedule() to select the process to run on the CPU.
 */
extern void idle(unsigned int cpu_id)
{
    printf("idle\n");
    pthread_mutex_lock(&queue_mutex);
    while (head == NULL) {
        pthread_cond_wait(&queue_not_empty, &queue_mutex); //run infinite
    }
    pthread_mutex_unlock(&queue_mutex);
    schedule(cpu_id);

    /*
     * REMOVE THE LINE BELOW AFTER IMPLEMENTING IDLE()
     *
     * idle() must block when the ready queue is empty, or else the CPU threads
     * will spin in a loop.  Until a ready queue is implemented, we'll put the
     * thread to sleep to keep it from consuming 100% of the CPU time.  Once
     * you implement a proper idle() function using a condition variable,
     * remove the call to mt_safe_usleep() below.
     */
}


/*
 * preempt() is the handler called by the simulator when a process is
 * preempted due to its timeslice expiring.
 *
 * This function should place the currently running process back in the
 * ready queue, and call schedule() to select a new runnable process.
 */
extern void preempt(unsigned int cpu_id)
{
    pcb_t* process;
    pthread_mutex_lock(&current_mutex);
    process = current[cpu_id];
    pthread_mutex_unlock(&current_mutex);
    process->state = PROCESS_READY;
    enqueue(process);
    schedule(cpu_id);
}


/*
 * yield() is the handler called by the simulator when a process yields the
 * CPU to perform an I/O request.
 *
 * It should mark the process as WAITING, then call schedule() to select
 * a new process for the CPU.
 */
extern void yield(unsigned int cpu_id)
{
    printf("yeild\n");
    pthread_mutex_lock(&current_mutex);
    current[cpu_id]->state = PROCESS_WAITING;
    pthread_mutex_unlock(&current_mutex);
    schedule(cpu_id);
}


/*
 * terminate() is the handler called by the simulator when a process completes.
 * It should mark the process as terminated, then call schedule() to select
 * a new process for the CPU.
 */
extern void terminate(unsigned int cpu_id)
{
    printf("terminate\n");
    pthread_mutex_lock(&current_mutex);
    current[cpu_id]->state = PROCESS_TERMINATED;
    pthread_mutex_unlock(&current_mutex);
    schedule(cpu_id);
}


/*
 * wake_up() is the handler called by the simulator when a process's I/O
 * request completes.  It should perform the following tasks:
 *
 *   1. Mark the process as READY, and insert it into the ready queue.
 *
 *   2. If the scheduling algorithm is static priority, wake_up() may need
 *      to preempt the CPU with the lowest priority process to allow it to
 *      execute the process which just woke up.  However, if any CPU is
 *      currently running idle, or all of the CPUs are running processes
 *      with a higher priority than the one which just woke up, wake_up()
 *      should not preempt any CPUs.
 *  To preempt a process, use force_preempt(). Look in os-sim.h for 
 *  its prototype and the parameters it takes in.
 */
extern void wake_up(pcb_t *process)
{
    printf("wake up\n");
    process->state = PROCESS_READY;
    enqueue(process);
    if (scheduling_algorithm == 2) {
        int lowest_process_priority = 11;
        int found_preemp_cpu_index = -1;
        int found_idle_cpu_index = -1;
        int process_priority = process->static_priority;
        pthread_mutex_lock(&current_mutex);
        for (int i=0; i<count_cpu; i++)
        {
            /*if there exists idle cpu, we don't need to preemp anything*/
            if (NULL == current[i]) {
                found_idle_cpu_index = i;
                break;
            }
        if (process_priority > current[i]->static_priority) {
          if (current[i]->static_priority < lowest_process_priority) {
            // find min priority value and which cpu currently excecute that process
                    found_preemp_cpu_index = i;
                    lowest_process_priority = current[i]->static_priority; 

                }
            }
        }
        pthread_mutex_unlock(&current_mutex);
        //if found a low priority process currently run in found cpu->preempt it
        if (found_preemp_cpu_index >= 0 && found_idle_cpu_index < 0) {
            force_preempt(found_preemp_cpu_index);
        }
    }

}


/*
 * main() simply parses command line arguments, then calls start_simulator().
 * You will need to modify it to support the -r and -p command-line parameters.
 */
int main(int argc, char *argv[])
{
    printf("main\n");

    int cpu_count;

    /* Parse command-line arguments */
    if (argc < 2)
    {
        fprintf(stderr, "CS 2200 Project 4 -- Multithreaded OS Simulator\n"
            "Usage: ./os-sim <# CPUs> [ -r <time slice> | -p ]\n"
            "    Default : FIFO Scheduler\n"
            "         -r : Round-Robin Scheduler\n"
            "         -p : Static Priority Scheduler\n\n");
        return -1;
    }
     else if (argc == 4) {
        scheduling_algorithm = 1;
        time_slice = atoi(argv[3]); //
    } else if (argc == 2){
        scheduling_algorithm = 0; //FIFO
    } else {
        scheduling_algorithm = 2; // static 
    }

    cpu_count = atoi(argv[1]);
    count_cpu = cpu_count;
    pthread_cond_init(&queue_not_empty, 0);

    /* FIX ME - Add support for -r and -p parameters*/

    /* Allocate the current[] array and its mutex */
    current = malloc(sizeof(pcb_t*) * cpu_count);
    assert(current != NULL);
    for (int i = 0; i < cpu_count; i++) {
        current[i] = NULL;
    }
    pthread_mutex_init(&current_mutex, NULL);

    /* Start the simulator in the library */
    start_simulator(cpu_count);

    return 0;
}


