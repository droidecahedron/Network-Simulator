#include <sys/wait.h>
#include "unistd.h"
#include <stdlib.h>

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <pthread.h>
#include <semaphore.h>

int main(int argc, char *argv[]) 
{

	//open the file
	FILE *file1_ptr = fopen(argv[1],"r");
	FILE *file2_ptr = fopen(argv[2],"r");
	char c1 = getc(file1_ptr);
	char c2 = getc(file2_ptr);
	int line = 1;
	int mismatch_count = 0;
	
	while(c1 != EOF && c2 != EOF)
	{
		if(c1 != c2)
		{
			printf("Mismatch at line %d",line);
			mismatch_count++;
		}
		if(c1 == '\n' && c2 == '\n')
			line++;
			
		c1 = getc(file1_ptr);
		c2 = getc(file2_ptr);
	}
	printf("No mismatches found\n");
}
