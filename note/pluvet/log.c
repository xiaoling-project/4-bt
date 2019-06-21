//log.c
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include "log.h"

//日志文件的描述符
int logfile_fd = -1;

/*
*功能：在命令行上打印一条日志
*传入参数：fmt ...
*传出参数：
*返回值：
*/
void logcmd(char *fmt,...)
{
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap); //标准化输出
	va_end(ap);
}

/*
*功能：打开记录日志的文件
*传入参数：filename
*传出参数：
*返回值：
*	0 打开成功
*	-1 未打开
*/
int init_logfile(char *filename)
{
	logfile_fd = open(filename,O_RDWR|O_CREAT|O_APPEND,0666);
	if(logfile_fd < 0) 
	{
		printf("open logfile failed\n");
		return -1;
	}

	return 0;	
}

/*
*功能：将一条日志写入日志文件
*传入参数：file line msg
*传出参数：
*返回值：
*	0 成功
*/

int logfile(char *file,int line,char *msg)
{
	char buff[256];

	if(logfile_fd < 0)  return -1;

	snprintf(buff,256,"%s:%d %s\n",file,line,msg);
	write(logfile_fd,buff,strlen(buff));
	
	return 0;
}