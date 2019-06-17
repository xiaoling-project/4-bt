#include<stdio.h>
#include<unistd.h>
#include<stdlib.h>
#include<string.h>
#include<sys/stat.h>
#include<sys/types.h>
#include<fcntl.h>
#include"log.h"

//日志文件描述符
int logfile_fd=-1;


/*
 *功能：在命令行上面打印一条日志
 *这可变参数的函数，学过数据结构中多维数组的创建的话，可能印象比较深刻
 *也可以百度查一下
 */
void logcmd(char *fmt,...)
{
	va_list ap;
	va_start(ap,fmt);
	vprintf(fmt,ap);
	va_end(ap);
}


/*
 *功能：打开日志文件
 *返回：成功返回0，失败返回-1
*/

int init_logfile(char *filename)
{
	/*
	 *O_RDWR,O_CREAT,O_APPEND均为系统定义的文件标志位
	 *O_RDWR= 00000002   O_CREAT= 00000100   O_APPEND=00002000 
	 *O_RDWR:自读打开   O_CREAT:若此文件不存在则创建它,但是得提供一个文件的访问权限，0666
	 *O_APPEND：表示追加
	*/
	logfile_fd=open(filename,O_RDWR|O_CREAT|O_APPEND,0666);
	if(logfile_fd <0)
	{
		printf("open logfile failed\n");
		return -1;
	}
	return 0;
}


/*
 *功能：将一条日志写入日志文件
 *返回：成功返回0，失败返回-1
*/

int logfile(char *file,int line,char *msg)
{
	char buff[256];
	
	if(logfile_fd<0)
	{
		return -1;
	}
	
	snprintf(buff,256,"%s:%d %s\n",file,line,msg);
	write(logfile_fd,buff,strlen(buff));
	return 0;
}