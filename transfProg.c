/*
Linux network simulator
	This program will read a topology file, generate a network of data structures with the linux kernel using malloc,
	establish all pointers and handlers, and will simulate packet flooding in a network as well as report 
	link diagnostics and will use random variables to determine loss/data corruption. It also determines
	latency with jitter (random at run time).
	
	It is a multi threaded application and there are semaphores created for every single link between nodes in the network,
	to make sure there can be full data concurrency.

AUTHOR : Johnny Nguyen

*/



#include <sys/wait.h>
#include "unistd.h"
#include <stdlib.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <pthread.h>
#include <semaphore.h>



static sem_t read_data_ptr; // binary semaphore

int max_threads;
int num_switches=0;
int num_hosts=0;
int num_links=0;
int num_transfers=0;
float temploss = 0;
float input_loss_max = 0;

int num_xfer_processed = 0;



//int links_with_node_present[num_links];
int* links_with_node_present;
int num_hops=0; // num hops = j

int found_path = 0; // when this is 1, path was found.
char recurse_nodeA[10];
char recurse_nodeB[10];
float throughput_arr[100];
float slowdown_arr[100];

// global semaphore pointer for accounts. We will have num_account semaphores of this.
//The reason is that there can be hundreds of accounts, and we only care about the accounts we are modifying
//values from in terms of shared state. i.e. a thread can be writing to accounts 3 and 4, 
//and another one can be writing to 5 and 6 concurrently.
//If we use only one "account" semaphore, only one account gets written to at a time, so only one thread does work at a time.
//which would result in bad parallelism.
sem_t* links_sem; 


struct node_host* hosts; // global host pointer
struct node_switch* switches; // global switch pointer
struct node_link* links;
struct transfer* data_xfers; // global pointer to buffer of transfer operations

struct node_host
{ 
	char hostName[10];
	char* dataBuffer;
};

struct node_switch
{
	char switchName[10];
	char* dataBuffer;
	int routingTable[100];
};

struct node_link
{
	char linkName[10];
	char node1[10];
	char node2[10];
	int dataRate;
	float loss;
	int delay;
};

struct transfer
{
	char nodeFrom[10];
	char nodeTo[10];
	char* data;
};

//terminal output functions
void red() {
	printf("\033[1;31m");
}

void green() {
	printf("\033[1;32m");
}

void green_thin() {
	printf("\033[0;32m");
}

void yellow() {
	printf("\033[1;33m");
}
void yellow_thin() {
	printf("\033[0;33m");
}

void blue() {
	printf("\033[1;34m");
}

void purple() {
	printf("\033[1;35m");
}

void cyan() {
	printf("\033[0;36m");
}

void boldwhite() {
	printf("\033[1;37m");
}

void boldcyan() {
	printf("\033[1;36m");
}

void reset() {
	printf("\033[0m");
}

//thread prototype
void* transfer_thread(void* threadNum);


void debug_data_structures(void);

void find_path(char nodeA[10], char nodeB[10]);


int main(int argc, char *argv[]) 
{

	//randomness!
	time_t t;
	srand((unsigned) time(&t));
  	
	char input_buffer[150];
	char entity_name[10];
	
	char account_balance[15]; 
	
	char accountFromNum[10];
	char accountToNum[10];
	char transferAmount[15];
	
	//open the file
	FILE *ip_file_ptr = fopen(argv[1],"r");
	
	boldwhite();
	printf("\n\nWelcome to Johnny Nguyen's network simulator and link diagnostic reporter.\n");
	
	reset();
	
	//process nodes and links
	//get the number of entities
	while(fgets(input_buffer, sizeof input_buffer+1, ip_file_ptr) != NULL)
	{
		if(input_buffer[0] == 't')
			num_transfers++;	// file pointer is now at our transfers.
		if(input_buffer[0] == 'h')
			num_hosts++;	
		if(input_buffer[0] == 's')
			num_switches++;	
		if(input_buffer[0] == 'l')
			num_links++;	
	}
	
	//global pointer to the nodes/links structs. this way, threads can access it.
	hosts = malloc(num_hosts * sizeof(*hosts)); // allocate the memory
	switches = malloc(num_switches * sizeof(*switches)); // allocate the memory
	links = malloc(num_links * sizeof(*links));
	data_xfers = malloc(num_transfers * sizeof(*data_xfers)); // allocate the memory
	links_with_node_present = malloc(num_links * sizeof(*links_with_node_present)); // allocate memory	
	
	//malloc data buffers
	for(int i=0;i<num_hosts;i++)
		hosts[i].dataBuffer = malloc(sizeof(char)); // at least 1 char. we will realloc in runtime.
	
	for(int i=0;i<num_switches;i++)
		switches[i].dataBuffer = malloc(sizeof(char)); // at least 1 char. we will realloc in runtime.
	
	rewind(ip_file_ptr); // reset the file pointer	
	
	//allocate memory for the semaphores
	links_sem = malloc(num_links * sizeof(*links_sem));
	
	
	//~~~~~~~~~~~~~~~~~process entities~~~~~~~~~~~~~~~~~~~~~~~~~
	//start reading the file again to populate our entities
	int newline_index;
	//hosts
	for(int i=0;i<num_hosts;i++)
	{
		fgets(input_buffer, sizeof input_buffer+1, ip_file_ptr);
		int j = 2;
		while(input_buffer[j-1] != '\n')
		{
			if(input_buffer[j] == '\n')
				newline_index = j; // save where the new line is
			j++;
		}
		// memcpy from start to the first space for host name
		memcpy(hosts[i].hostName,&input_buffer[2],newline_index-2); 
	}
	
	
	//switches
	for(int i=0;i<num_switches;i++)
	{
		fgets(input_buffer, sizeof input_buffer+1, ip_file_ptr);
		int j = 2;
		while(input_buffer[j-1] != '\n')
		{
			if(input_buffer[j] == '\n')
				newline_index = j; // save where the new line is
			j++;
		}
		
		// memcpy from start to the first space for host name
		memcpy(switches[i].switchName,&input_buffer[2],newline_index-2); 	
	}
	
	
	//links
	int space_index1, space_index2;
	int space_counter = 0;
	char linkName[10];
	char input_rate[10];
	char loss[10];
	char delay[10];
	for(int i=0;i<num_links;i++)
	{
		fgets(input_buffer, sizeof input_buffer+1, ip_file_ptr); // l node1 node2 rate
		
		sprintf(linkName, "link%d", i);
		strcpy(links[i].linkName, linkName); // copy link name to the struct.
		
		
		//parse link input
		int j = 2; //start after 'l' + ' '
		space_counter = 0;
		while(input_buffer[j-1] != '\n')
		{
			if(input_buffer[j] == ' ' && space_counter == 0)
			{
				space_index1 = j; // save where the first space is.
				space_counter++;
			}
			if(input_buffer[j] == ' ' && space_counter != 0)
				space_index2 = j; // save where the second space is.
			if(input_buffer[j] == '\n')
				newline_index = j; // save where the new line is
			j++;
		}
		
		
		memcpy(links[i].node1,&input_buffer[2],space_index1-2); // memcpy node1
		memcpy(links[i].node2,&input_buffer[space_index1+1], space_index2-space_index1-1); // memcpy node2 
		memcpy(input_rate,&input_buffer[space_index2+1],newline_index-space_index2); // memcpy from space to newline
		links[i].dataRate = atoi(input_rate); //convert input rate string to int and store it.
		input_loss_max = atof(argv[3]);
		temploss = (float) rand()/(RAND_MAX) * input_loss_max; // global that assigns the max based on user input
		
		
		links[i].loss = temploss;
		links[i].delay = rand() % 20+1;
		
		
	}
	
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	
	
	
	
	//we are now pointing to DATA TRANSFERS (packet forwarding!)
	//now we need to find how many transfers we have to allocate the memory
	for(int i=0;i<num_transfers;i++)
	{
		fgets(input_buffer, sizeof input_buffer+1, ip_file_ptr); // t nodeFrom nodeTo message
		//parse link input
		int j = 2; //start after 'l' + ' '
		space_counter = 0;
		while(input_buffer[j-1] != '\n')
		{
			if(input_buffer[j] == ' ' && space_counter == 0)
			{
				space_index1 = j; // save where the first space is.
				space_counter++;
			}
			if(input_buffer[j] == ' ' && space_counter != 0)
				space_index2 = j; // save where the second space is.
			if(input_buffer[j] == '\n')
				newline_index = j; // save where the new line is
			j++;
		}
		
		
		memcpy(data_xfers[i].nodeFrom,&input_buffer[2],space_index1-2); // memcpy node1
		memcpy(data_xfers[i].nodeTo,&input_buffer[space_index1+1], space_index2-space_index1-1); // memcpy node2
		data_xfers[i].data = realloc(data_xfers[i].data, (newline_index-space_index2) * sizeof(char));
		memcpy(data_xfers[i].data,&input_buffer[space_index2+1],newline_index-space_index2-1); // memcpy from space to newline
	
	
	}
	

	
	
	
	
	
	fclose(ip_file_ptr); // done with file
	
	
	//now all accounts are created and stored, and we know how many accounts we have. [num_accounts]
	//all transfers are also stored, and we know how many transfers we have pending [num_EFT]
	max_threads = atoi(argv[2]); // command line passed argument for how many threads we can run, convert string to int
	pthread_t threads[max_threads]; // this holds the unique thread identities
	int s; // error check variable
	
	//GUI
	boldwhite();
	printf("\tFile Processing:");
	yellow();
	printf("\n--------------------------------------\n");
	printf("Topology file successfuly read.\n");
	red();
	printf("Number of threads requested for execution: %d \n",max_threads);
	printf("Maximum loss factor allowed based on user input : %.2f\n",input_loss_max);
	yellow();
	printf("Initializing and starting simulation. . .");
	printf("\n--------------------------------------\n");
	reset();
	
	//debug
	debug_data_structures();
	
  	////sleep(50000);
  	printf("\nCreating binary semaphore for each link/node. . .\n");
	if(sem_init(&read_data_ptr, 0, 1) == -1) // initialize to 1 to use as a "lock"
		exit(3); // if it returns -1, semaphore was not able to be initialized
		
	//one semaphore per LINK. all LINK are mutually exclusive.
	for(int i=0;i<num_links;i++)
		if(sem_init(&links_sem[i],0,1) == -1)
			exit(3); // if it returns -1, semaphore wasnt able to be initialized.

		
	//create max threads
	////sleep(75000);
	printf("\nCreating %d threads. . .\n",max_threads);
	printf("\nThread creation successful! Launching threads. . .\n\n");
	reset();
	printf("\nnote: Threads don't have //sleeps like the rest of the GUI. they'll be fast. Let'err'rip!\n\n");
	//sleep(2);
	

	for(int i=0;i<max_threads;i++)	
	  	s = pthread_create(&threads[i],NULL,transfer_thread,(void*)(intptr_t)i);	
		if(s != 0)
	  		exit(1); // error check
	
	
	
	//wait for all our threads
	for(int i=0;i<max_threads;i++)
		s = pthread_join(threads[i],NULL); // wait for the threads to finish executing
	  	if(s != 0)
	  		exit(2); // error check
  		
	printf("\nSimulation successfully executed.\n\n\n");
	
	
	//average loss factor
	float avg_loss = 0;
	for(int i=0;i<num_links;i++)
		avg_loss += links[i].loss;
	avg_loss /= num_links;
	printf("average loss : %.4f%", avg_loss*100);
	
	
	blue();
	printf("\nAverage throughput with loss factor cap of ");
	red();
	printf("%.2f ", input_loss_max);
	reset();
	
	float average_throughput = 0;
	for(int i=0;i<num_transfers;i++)
		average_throughput += throughput_arr[i];
	average_throughput /= num_transfers; // get the average
	
	printf(": %.4f%", average_throughput*100);
	
	
	blue();
	printf("\nAverage slowdown with loss factor cap of ");
	red();
	printf("%.2f ", input_loss_max);
	reset();
	
	float average_slowdown = 0;
	for(int i=0;i<num_transfers;i++)
		average_slowdown += slowdown_arr[i];
	average_slowdown /= num_transfers; // get the average
	
	printf(": %.4f%\n", average_slowdown*100);
	
	
	
	return 0; //main thread returns 0
}



void* transfer_thread(void* threadNum)
{
	//can do int x = (int)threadNum; now
	int x = (int)threadNum;
	reset();
	printf("Entering thread %d...\n",x);
	int account_from_index, account_to_index, xfer_request;
	while(1)
	{
		//save value of buffer pointer. Since other threads can change it, mutex
		sem_wait(&read_data_ptr);
		xfer_request = num_xfer_processed; // Get a task.
		num_xfer_processed++; // increment task pointer so another thread can get next task [current task will be done]
		sem_post(&read_data_ptr);
	
		if(xfer_request>=num_transfers) //no more tasks to do, we can leave.
		{
			printf("No transfers remaining!\n");
			break; //we're about to leave the loop, since there is no more data to process
		}
			
			
		sem_wait(&links_sem[x]);
		
		
		yellow();
		printf("\nThread %d designated to process transfer # %d\n", x, xfer_request);
		reset();
		find_path(data_xfers[xfer_request].nodeFrom, data_xfers[xfer_request].nodeTo); // update path pipe
		
		
		
		
		
		sem_wait(&read_data_ptr);
		boldcyan();
		printf("Thread %d simulating data transfer over %d hops...\n", x, num_hops);
		reset();
		
		printf("\t\tTransferring data <%s> from <%s> to <%s>\n", data_xfers[xfer_request].data, data_xfers[xfer_request].nodeFrom, 				data_xfers[xfer_request].nodeTo);
			
		//begin simulating transfer
		char dataToTransfer[100];
		char originalMessage[100];
		memcpy(dataToTransfer, data_xfers[xfer_request].data, 100);
		memcpy(originalMessage, data_xfers[xfer_request].data, 100);
		int errorflag=0;
		int total_transfer_time = 0;
		int theoretical_transfer_time = 0;
		int extra_hops = 0;
		int this_loops_hops = 0;
		for(int i=0;i<num_hops;i++)
		{
			this_loops_hops = 0;
			blue();
			printf("\n\tHop %d\t",i+1);
			reset();
			
			boldwhite();
			printf("\tCopying %s from %s to %s's ramdisk...\n", dataToTransfer, links[links_with_node_present[i]].node1, links[links_with_node_present[i]].node2);
			reset();
			//add corruption
			printf("\t\t\tLoss Factor of Link : %.2f\n", links[links_with_node_present[i]].loss);
			LOSS_LABEL: 
			
			
			printf("\t\t\tMessage <%s> modified to", dataToTransfer);
			if((float) rand()/RAND_MAX < links[links_with_node_present[i]].loss)
			{
				dataToTransfer[rand()%20] = 'A' + (rand() % 26);
				errorflag=1;
			}
			
			
			
			printf(" <%s> after loss incorporated\n", dataToTransfer);
			if(errorflag)
			{
				red();
				printf("\t\t\t\tERRLOG : Packet loss detected in hop %d via <%s>\n\t\t\t\tRe-sending Packet. . .\n",i,links[links_with_node_present[i]].linkName);
				reset();
				errorflag=0;
				total_transfer_time += links[links_with_node_present[i]].delay;
				extra_hops++;
				this_loops_hops++;
				goto LOSS_LABEL;
			}
			else 
			{	
				green();
				printf("\t\t\t\tERRLOG : No loss detected!\n");
				reset();
			}
			
			cyan();
			printf("\n\t\t\tHop %d attempted %d times\n",i+1, 1+this_loops_hops);
			reset();
			printf("\t\t\tTransfer time : %d ms\n", links[links_with_node_present[i]].delay);
			green_thin();
			printf("\t\t\tTheoretical Data Rate : %d bps\n", links[links_with_node_present[i]].dataRate);
			yellow_thin();
			printf("\t\t\tActual Data Rate : %.2f bps\n", (float) links[links_with_node_present[i]].dataRate * (float) num_hops/(num_hops+extra_hops));
			reset();
			total_transfer_time += links[links_with_node_present[i]].delay;
			theoretical_transfer_time += links[links_with_node_present[i]].delay;
			
		}
		//add delay
		purple();
		printf("\n\tTransfer Results\n");
		boldwhite();
		printf("\tTotal theoretical transfer time : %d ms\n", theoretical_transfer_time);
		printf("\tActual transfer time : %d ms\n", total_transfer_time);
		printf("\n\tDelay induced by loss : %d ms", total_transfer_time-theoretical_transfer_time);
		printf("\n\tNumber of hops including re-attempted hops : %d\n\n", num_hops+extra_hops);
		yellow();
		printf("\n\tThroughput of links after considering dropped packets : %.2f\n\n", (float) num_hops/(num_hops+extra_hops));
		throughput_arr[xfer_request] = (float) num_hops/(num_hops+extra_hops);
		slowdown_arr[xfer_request] = (float) 1 - (theoretical_transfer_time / (total_transfer_time));
		
		reset();
			
		sem_post(&read_data_ptr);
		sem_post(&links_sem[x]);
		
		
		
	}
	
	printf("Exiting thread %d...\n",x);
	//pthread exit implicitly called here.

}

void debug_data_structures(void)
{
	//sleep(1);
	//test writing to buffers.
	boldwhite();
	printf("\n\n\tError Diagnostics:");
	green();
	printf("\n--------------------------------------\n");
	printf("Testing dynamic memory of nodes...\n");
	//sleep(1);
	reset();
	char datagram[100];
	sprintf(datagram, "message");
	hosts[0].dataBuffer = malloc(sizeof(datagram) * sizeof(char)); // allocate the memory
	printf("\n\t");
	memcpy(hosts[0].dataBuffer, &datagram, sizeof(datagram));
	printf(hosts[0].dataBuffer);
	printf("\n");
	
	//try a new datagram
	sprintf(datagram, "longermessage");
	hosts[0].dataBuffer = realloc(hosts[0].dataBuffer, sizeof(datagram) * sizeof(char)); // reallocate memory
	printf("\n\t");
	memcpy(hosts[0].dataBuffer, &datagram, sizeof(datagram));
	printf(hosts[0].dataBuffer);
	printf("\n");
	green();
	printf("\ndynamic memory passed");
	////sleep(150000);
	printf("\n--------------------------------------\n");
	reset();
	
	
	//DEBUG testing
	cyan();
	printf("\nDISPLAYING HOSTS\n\t");
	////sleep(250000);
	reset();
	for(int i=0;i<num_hosts;i++)
	{
		printf(hosts[i].hostName);
		printf(", ");
	}
	cyan();
	printf("\n\nDISPLAYING SWITCHES\n\t");
	////sleep(250000);
	reset();
	for(int i=0;i<num_switches;i++)
	{	
		printf(switches[i].switchName);
		printf(", ");
	}
	yellow();
	printf("\n\nGENERATING LINKS\n");
	reset();
	for(int i=0;i<num_links;i++)
	{
		////sleep(100000); // 500ms or 500k usec
		blue();
		printf("\tlink name: ");
		reset();
		printf(links[i].linkName);
		printf("\n");
		
		green();
		printf("\tnode1: ");
		reset();
		printf(links[i].node1);
		printf("\n");
		
		green();
		printf("\tnode2: ");
		reset();
		printf(links[i].node2);
		printf("\n");
		
		purple();
		printf("\tdata rate: ");
		reset();
		printf("%d\n", links[i].dataRate);
		
		purple();
		printf("\tloss factor:");
		reset();
		printf(" %.2f\n", links[i].loss);
		
		purple();
		printf("\taverage delay(ms) :");
		reset();
		printf(" %d\n", links[i].delay);
		printf("\n\n");
	}
	
	boldwhite();
	printf("PACKET FORWARDING TRANSFERS\n");
	reset();
	for(int i=0;i<num_transfers;i++)
	{
		////sleep(50000);
		printf("\tTransfer %d : \n", i);
		
		printf("\tnodeFrom: ");
		printf(data_xfers[i].nodeFrom);
		printf("\n");
		
		printf("\tnodeTo: ");
		printf(data_xfers[i].nodeTo);
		printf("\n");
		
		printf("\tmessage: %s\n", data_xfers[i].data);
		printf("\n\n");
	}
	
	printf("Total Transfers to complete: %d\n",num_transfers);
	
	
	yellow();
	printf("\n\n\n\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
	printf("Beginning simulations. Creating threads...\n");
	printf("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
	//TODO REMOVE
	/*printf("TODO REMOVE%d\n",rand()%100);
	
	char testnodeA[10] = "host1";
	char testnodeB[10] = "switch2";
	
	
	find_path(testnodeA, testnodeB);
	*/
}





//packet forward
void find_path(char nodeA[10], char nodeB[10])
{
	//we have an array of links links[i]
	//each link[i] has nodeA, and nodeB.
	//we want to find all i such that nodeA has a full path to nodeB.
	//and update an array of all i. (links_with_node_present)
	
	num_hops = 0; // needs 0 hops to find a path.
	found_path = 0; // path not found yet 
	printf("\nFinding path between %s and %s...\n", nodeA, nodeB);
	
	for(int i=0;i<num_links;i++)
	{
		//check for direct connection
		
		if( (strcmp(nodeA, links[i].node1) == 0 && strcmp(nodeB, links[i].node2) == 0) || 
			strcmp(nodeB, links[i].node1) == 0 && strcmp(nodeA, links[i].node2) == 0 )
		{
			num_hops=0;
			links_with_node_present[num_hops++] = i;
			found_path = 1;
			printf("FOUND DIRECT PATH\n");
			break;
		}
		
		
		else if(found_path == 0)
		{
		//check if passed in nodeA is in the link
		if(strcmp(nodeA, links[i].node1) == 0) //they are the same
			links_with_node_present[num_hops++] = i;
			
		else if(strcmp(nodeA, links[i].node2) == 0)
			links_with_node_present[num_hops++] = i;
		
		
		//check if passed in nodeB is in the link.
		if(strcmp(nodeB, links[i].node1) == 0) //they are the same
			links_with_node_present[num_hops++] = i;
		
		else if(strcmp(nodeB, links[i].node2) == 0)
			links_with_node_present[num_hops++] = i;
		
		}
		
			
	}
	
	printf("CONNECTION FOUND\n");
	printf("num hops: %d\n", num_hops);
	/*for(int i=0;i<num_hops;i++)
		printf("%d\t", links_with_node_present[i]);
		*/ // DEBUG : VIEW ROUTING TABLE
	printf("\n");

}


