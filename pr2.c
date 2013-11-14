// Joshua Kuiros
// CMPSC 473 
// Project 2
// 10/20/13


#include <stdio.h>
#include <setjmp.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <math.h>
#include <sys/timeb.h>
#include <setjmp.h>


// Constants
#define JB_SP 6
#define JB_PC 7
#define SECOND 1000000
#define MAX_NO_OF_THREADS 100
#define STACK_SIZE 4096
#define STATUS_SLEEPING 0
#define STATUS_READY 1
#define STATUS_SUSPENDED 2
#define STATUS_RUNNING 3
#define TIME_QUANTUM 1*SECOND
#define CLOCKS_PER_SEC 1000000


// Variables that Define Performance
static int run_time_limit = 15000;	/// max number of instructions for a thread to execute
static int current_schedule = 0;	// 0 = round robin, 1 = lottery scheduling


// Global Variables
static int number_of_threads = 0;	// real time number of threads
static int current_thread_count = 0;	// total number of threads created
static int total_weight = 1;
struct timeb t_start, t_stop;		// start, stop time for the current executing thread
typedef unsigned long address_t;


// Data Structures
typedef struct thread_weight		// define weighted lottery scheduling
{
	int min_weight;
	int max_weight;
}thread_weight;

typedef struct sleep_info		// sleeping time for a given thread
{
	int start_sleeping;
	int sleep_to;
	struct timeb start_s;
	int total;
}sleep_struct;

typedef struct wait_info		// waiting time for a given thread
{
	int start_waiting;
	int stop_waiting;
	struct timeb start_w, stop_w;
	int total;
}wait_struct;

typedef struct TCB			// thread control block
{
	
	address_t pc;
	address_t sp;
	thread_weight weight;
	sleep_struct sleep_time;
	wait_struct wait_time;
	sigjmp_buf jbuf;
	int thrd_id;
	int thrd_status;
	int number_of_bursts;
	int number_of_waits;
	int number_of_sleeps;
	int execution_time;
	int time_to_sleep;

	struct TCB *next;

}TCB;


// Linked list variables 
TCB *head_of_list = NULL;
TCB *tail_of_list = NULL;
TCB *current_thread = NULL;


// Function Prototypes
void append_to_list(TCB *new_tcb);
void Dispatch(int sig);
void print_linked_list();
void yieldCPU(void);
void GetStatus(TCB *current);
void CleanUp();
void SleepThread(int sec);
int GetMyId();
void f( void );	
void g( void );	
int CreateThread (void (*f) (void));
TCB *find_tcb(int random_number);
void Go();


// Address Translation Code
#ifdef __x86_64__
// code for 64 bit Intel arch

//A translation required when using an address of a variable
//Use this as a black box in your code.
address_t translate_address(address_t addr)
{
    address_t ret;
    asm volatile("xor    %%fs:0x30,%0\n"
                 "rol    $0x11,%0\n"
                 : "=g" (ret)
                 : "0" (addr));
    return ret;
}

#else
// code for 32 bit Intel arch

#define JB_SP 4
#define JB_PC 5

//A translation required when using an address of a variable
//Use this as a black box in your code.
address_t translate_address(address_t addr)
{
    address_t ret;
    asm volatile("xor    %%gs:0x18,%0\n"
                 "rol    $0x9,%0\n"
                 : "=g" (ret)
                 : "0" (addr));
    return ret;
}

#endif


// Functions

// returns status of a given thread containing info about wait, execution, and sleep time
void GetStatus(TCB *current)
{
	int avg_ex;
	int avg_wait;
	int total_sleep;

	printf("Thread: %d\n", current->thrd_id);

	if(current->number_of_bursts > 0)
	{
		avg_ex = (current->execution_time)/(current->number_of_bursts);
	}
	else
	{
		avg_ex = 0;
	}

	if(current->number_of_waits > 0)
	{
		avg_wait = (current->wait_time.total)/(current->number_of_waits);
	}
	else
	{
		avg_wait = 0;
	}
	if(current->number_of_sleeps > 0)
	{
		total_sleep = (current->sleep_time.total);
	}
	else
	{
		total_sleep = 0;
	}
	
	printf("    Avg. Execution Time    %d\n", avg_ex);
	printf("      Number of bursts         %d\n", current->number_of_bursts);	
	printf("    Avg. Waiting Time      %d\n", avg_wait);
	printf("      Number of waits          %d\n", current->number_of_waits);
	printf("    Total Sleeping Time    %d\n", total_sleep);
	printf("      Number of sleeps          %d\n", current->number_of_sleeps);

}


// prints informaiton, deletes the linked list, and exits the program
void CleanUp()
{
	TCB *index = head_of_list;
	int count = 0;

	while(count < number_of_threads)			// suspend all threads
	{
		index->thrd_status = STATUS_SUSPENDED;

		GetStatus(index);
		
		index = index->next;
		count++;
	}

	index = head_of_list;
	TCB *next;
	while(index != tail_of_list)				// delete linked list
	{
		next = index->next;
		free(index);
		index = next;
	}

	exit(0);						// exit program

}


// sleeps current thread for given seconds
void SleepThread(int sec)
{
	printf("	SLEEPING\n");

	struct timeb temp_time;
	ftime(&temp_time);

	current_thread->number_of_sleeps++;

	current_thread->sleep_time.start_s = temp_time;
	current_thread->sleep_time.sleep_to = temp_time.millitm + sec;

	current_thread->thrd_status = STATUS_SLEEPING;

	yieldCPU();			
}


// returns the running threads id
int GetMyId()
{
	return current_thread->thrd_id;
	
}


// suspends a thread based on thread_id
int SuspendThread(int thread_id)
{
	TCB *index;
	int i = 0;

	// find thread with the given thread_id
	for(index = head_of_list; index->thrd_id != thread_id && i < number_of_threads; index = index->next)
	{
		i++;
	}
	
	if(i == number_of_threads)			// not found
	{
		return -1;
	}
	else						// found and suspended
	{
		index->thrd_status = STATUS_SUSPENDED;
		return 0;
	}
}


// resume a suspended thread based on thread_id
int ResumeThread(int thread_id)
{
	TCB *index;
	int i = 0;

	// find thread with the given thread_id
	for(index = head_of_list; index->thrd_id != thread_id && i < number_of_threads; index = index->next)
	{
		i++;
	}
	
	if(i == number_of_threads)			// not found
	{
		return -1;
	}
	else						// found and resumed
	{
		index->thrd_status = STATUS_READY;
		struct timeb current_time;
		ftime(&current_time);
		index->wait_time.start_waiting = current_time.time;
		return 0;
	}
}


// delete a thread based on thread_id
int  DeleteThread(int  thread_id) 
{
	if(head_of_list == NULL && tail_of_list == NULL)			// empty list	
	{
		printf("Error - attempting to delete on an empty list\n");
		return -1;
	}

	if(number_of_threads == 1 && head_of_list->thrd_id == thread_id)	// one item list
	{
		free(head_of_list);
		head_of_list = NULL;
		tail_of_list = NULL;
		current_thread = NULL;
		number_of_threads--;
		return 0;
	}

	TCB *index = head_of_list;
	TCB* temp = head_of_list->next;
	int count = 0;

	while(count < number_of_threads)			// find thread with thread_id
	{
		if(temp->thrd_id == thread_id)
		{
			if(temp == head_of_list)		// thread is head
			{
				head_of_list = temp->next;
			}
			if(temp == tail_of_list)		// thread is tail
			{
				tail_of_list = index;
			}
			if(temp == current_thread)		// thread is in list
			{
				current_thread = NULL;
			}
			printf("      Thread Deleted\n");
			index->next = temp->next;
			free(temp);
			number_of_threads--;
			return 0;
		}
		index = temp;
		temp = temp->next;
		count++;
	}

	printf("Error - thread does not exit\n");			
	return -1;
}


// handles context switching of threads
void yieldCPU(void)					
{
	printf("Switching Threads\n");

	ftime(&t_stop);				// stop current threads execution
	current_thread->execution_time += ( 1000.0 * (t_stop.time - t_start.time) + (t_stop.millitm - t_start.millitm));

	// print information
	printf("  Current Thread: %d\n", current_thread->thrd_id);
	printf("  Thread Status: %d\n", current_thread->thrd_status);
	printf("Execution Time: %ld\n", current_thread->execution_time);

	// pause to make output readable
	usleep(2*SECOND);
	
	raise(SIGVTALRM);
}


// thread representation f
void f(void)			
{
   int i=0;
	//SleepThread(5000);
	//SuspendThread(0);
    while(1)
   {
        ++i;
	printf("in f (%d)\n",i);
        if (i % 3 == 0) 
	{
           yieldCPU();
        }
	usleep(SECOND);
    }
}


// thread representation g
void g( void )
{
    int i = 0;

    while(1)
   {
        ++i;
	printf("in g (%d)\n",i);
        if (i % 3 == 0) 
	{
            yieldCPU();
        }
	usleep(SECOND);
    }
}


// create a thread based on the function pointer *f
int CreateThread (void (*f) (void))
{
	TCB* current_tcb = malloc(sizeof(TCB));

	if(current_tcb == NULL)					// malloc failed
	{
		current_tcb->thrd_id = -1;
		number_of_threads++;
	}
	else							// instantiate new threads tcb struct
	{
		current_tcb->thrd_id = current_thread_count++;
		current_tcb->pc = (address_t)f;
		current_tcb->sp = (address_t)malloc(STACK_SIZE);
		current_tcb->sp = current_tcb->sp + STACK_SIZE - sizeof(address_t);
		current_tcb->number_of_bursts = 0;
		current_tcb->number_of_waits = 0;
		current_tcb->number_of_sleeps = 0;
		current_tcb->execution_time = 0;
		
		current_tcb->sleep_time.sleep_to = 0;
		current_tcb->sleep_time.start_sleeping = 0;
		current_tcb->sleep_time.total = 0;

		current_tcb->wait_time.start_w.millitm = 0;
		current_tcb->wait_time.stop_w.millitm = 0;
		current_tcb->wait_time.total = 0;

		printf("set wait time %d\n", current_tcb->wait_time.total);

		current_tcb->next = NULL;
		current_tcb->weight.min_weight = 0;
		current_tcb->weight.max_weight = 0;
		

		struct timeb temp_time;
		ftime(&temp_time);
		current_tcb->wait_time.start_w = temp_time;
		current_tcb->thrd_status = STATUS_READY;   
		number_of_threads++;

		if(current_schedule == 1)			// calculate weights for round robin
		{
			current_tcb->weight.min_weight = total_weight;
			int exponent = current_tcb->thrd_id;
			current_tcb->weight.max_weight = total_weight + pow(2,exponent);
			total_weight = current_tcb->weight.max_weight;
			total_weight++;
		}

		if(number_of_threads >= MAX_NO_OF_THREADS)	// threads have exceeded limit
		{
			CleanUp();
		}
	}

    
   	sigsetjmp(current_tcb->jbuf,1);
    	(current_tcb->jbuf->__jmpbuf)[JB_SP] = translate_address(current_tcb->sp);
   	(current_tcb->jbuf->__jmpbuf)[JB_PC] = translate_address(current_tcb->pc);
	
	sigemptyset(&current_tcb->jbuf->__saved_mask);

	append_to_list(current_tcb);
}


// find thread with chosen "lottery ticket"
TCB *find_tcb(int random_number)
{
	TCB *index = head_of_list;
	int count = 0;

	while((count < number_of_threads))
	{
		if((random_number >= (index->weight.min_weight)) && (random_number <= (index->weight.max_weight)))
		{	
			break;		
		}
		index = index->next;
		count++;
	}

	if(count == number_of_threads)
	{
		return NULL;
	}
	else
	{
		return index;
	}
}


// check if any sleeping threads need to be woken up
void check_sleeping_threads()
{
	TCB *index = head_of_list;
	int count = 0;
	while(count < number_of_threads)
	{
		struct timeb temp_time;
		ftime(&temp_time);

		if((index->thrd_status == STATUS_SLEEPING) && (temp_time.time > current_thread->sleep_time.sleep_to))
		{
			current_thread->thrd_status = STATUS_READY;
			current_thread->wait_time.start_w = temp_time;
			current_thread->sleep_time.sleep_to = 0;
			current_thread->sleep_time.total +=  ( 1000.0 * (temp_time.time - current_thread->sleep_time.start_s.time ) + (temp_time.millitm - current_thread->sleep_time.start_s.millitm));
		}
		index = index->next;	
		count++;
	} 
}


// runs the thread scheduler
void Dispatch(int sig)
{

	check_sleeping_threads();

	printf("Alarm\n");

	if(current_schedule == 0)			// round robin
	{
		if(current_thread == NULL)
		{
			current_thread = head_of_list;
			head_of_list->thrd_status = STATUS_RUNNING;
			ftime(&t_start);

			current_thread->wait_time.stop_w = t_start;
			
			current_thread->wait_time.total += ( 1000.0 * (current_thread->wait_time.stop_w.time - current_thread->wait_time.start_w.time) + (current_thread->wait_time.stop_w.millitm - current_thread->wait_time.start_w.millitm));

			siglongjmp(head_of_list->jbuf, 1);
		}
		else
		{
			if( (current_thread->execution_time) > run_time_limit )
			{
				CleanUp();
			}

			if(sigsetjmp(current_thread->jbuf, 1) == 1)
			{
				ftime(&t_start);
				return;
			}

			struct timeb temp_time;

			ftime(&temp_time);
			
			current_thread->thrd_status = STATUS_READY;
			current_thread->wait_time.start_w = temp_time;

			current_thread = current_thread->next;
			while(current_thread->thrd_status != STATUS_READY)
				current_thread= current_thread->next;
			
			current_thread->thrd_status = STATUS_RUNNING;
			ftime(&t_start);
			current_thread->wait_time.stop_w = t_start;

			if(current_thread->wait_time.stop_w.millitm != 0)
			{
				current_thread->wait_time.total+= ( 1000.0 * (current_thread->wait_time.stop_w.time - current_thread->wait_time.start_w.time) + (current_thread->wait_time.stop_w.millitm - current_thread->wait_time.start_w.millitm));

				current_thread->number_of_waits++;
			}


			current_thread->number_of_bursts++;

			siglongjmp(current_thread->jbuf, 1);	
		
		}
	}
	else if (current_schedule == 1)			// weighted lottery
	{

		if(current_thread == NULL)
		{
			current_thread = head_of_list;
			head_of_list->thrd_status = STATUS_RUNNING;
			ftime(&t_start);

			current_thread->wait_time.stop_w = t_start;
			current_thread->wait_time.total += ( 1000.0 * (current_thread->wait_time.stop_w.time - current_thread->wait_time.start_w.time) + (current_thread->wait_time.stop_w.millitm - current_thread->wait_time.start_w.millitm));

			siglongjmp(head_of_list->jbuf, 1);
		}
		else
		{
			if( (current_thread->execution_time) > run_time_limit )
			{
				CleanUp();
			}

			if(sigsetjmp(current_thread->jbuf, 1) == 1)
			{
				return;
			}
		
			current_thread->thrd_status = STATUS_READY;
			struct timeb start_wait_time;
			ftime(&start_wait_time);
	
			current_thread->wait_time.start_w = start_wait_time;

			int mod_value = total_weight - 1;
			int chosen_number = ( rand() % mod_value )+ 1;

			TCB *selected_thread = NULL;
			
			do
			{
				selected_thread = find_tcb(chosen_number);
				chosen_number = ( rand() % mod_value ) + 1;
			}while(selected_thread->thrd_status != STATUS_READY);

			current_thread = selected_thread;
			ftime(&t_start);
			current_thread->wait_time.stop_w = t_start;
			
			
			if(current_thread->wait_time.stop_w.millitm != 0)
			{
				current_thread->wait_time.total += ( 1000.0 * (current_thread->wait_time.stop_w.time - current_thread->wait_time.start_w.time) + (current_thread->wait_time.stop_w.millitm - current_thread->wait_time.start_w.millitm));

				current_thread->number_of_waits++;
			}

			current_thread->thrd_status = STATUS_RUNNING;
			current_thread->number_of_bursts++;

			siglongjmp(current_thread->jbuf, 1);		
		}
	}
}


// print current linked list
void print_linked_list()
{
	TCB *index;

	if(head_of_list == NULL)
	{
		printf("*Empty list \n");
	}


	printf("  head %p\n", head_of_list);
	printf("  tail %p\n", tail_of_list);
	printf("  current %p\n", current_thread);

	int i = 0;
	for(index = head_of_list; i < number_of_threads; index = index->next)
	{
		printf("thread address: %p\n", index);
		printf("  id: %d\n", index->thrd_id);
		printf("    status: %d\n", index->thrd_status);
		printf("    pc address: %p\n", index->pc);
		printf("    sp address: %p\n", index->sp);
		printf("    next address: %p\n", index->next);
		printf("    min weight: %d\n", index->weight.min_weight);
		printf("    max weight: %d\n", index->weight.max_weight);

		i++;	
	}
}


// adds a newly created thread to the linked list
void append_to_list(TCB *new_tcb)
{
	
	
	if( head_of_list == NULL)
	{
		head_of_list = new_tcb;
		tail_of_list = new_tcb;
		new_tcb->next = head_of_list;

	}
	else
	{
		TCB *index;
		for( index = head_of_list; index != tail_of_list; index = index->next);
		index->next = new_tcb;
		tail_of_list = new_tcb;
		new_tcb->next = head_of_list;
	}
}


// runs program, never returns
void Go()
{
	signal(SIGVTALRM, Dispatch);

	srand(time(NULL));

	struct itimerval tv;
   	tv.it_value.tv_sec = 2; //time of first timer
	tv.it_value.tv_usec = 0; //time of first timer
	tv.it_interval.tv_sec = 2; //time of all timers but the first one
	tv.it_interval.tv_usec = 0; //time of all timers but the first one
	
	setitimer(ITIMER_VIRTUAL, &tv, NULL);
	 
	
	CreateThread(g);
	//CreateThread(g);
	CreateThread(f);

	print_linked_list();	
	
	while(1);
}


int main()
{
	Go();

	return 0;
}
