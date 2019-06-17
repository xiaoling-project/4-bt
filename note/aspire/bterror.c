#include<stdio.h>
#include<unistd.h>
#include<stdlib.h>
#include"bterror.h"

void btexit(int errno,char *file,int line)
{
	printf("exit at %s:%d with error number:%d\n",line,errno);
	exit(errno);
}