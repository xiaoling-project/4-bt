#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include "signal_handler.h"
#include "parse_metafile.h"
#include "data.h"
#include "bitfield.h"
#include "peer.h"
#include "tracker.h"
#include "torrent.h"

extern int  download_piece_num;
extern int  *fds;
extern int  fds_len;
extern Peer *peer_head;

// 进行清理工作
void do_clear_work()
{
	// �ر�����peer��socket
	Peer *p = peer_head;
	while(p != NULL) 
	{
		if(p->state != CLOSING)  
			close(p->socket);
		p = p->next;
	}

	// ����λͼ
	if(download_piece_num > 0) 
	{
		restore_bitmap();
	}

	// �ر��ļ�������
	int i;
	for(i = 0; i < fds_len; i++) 
	{
		close(fds[i]);
	}
	
	// �ͷŶ�̬������ڴ�
	release_memory_in_parse_metafile();
	release_memory_in_bitfield();
	release_memory_in_btcache();
	release_memory_in_peer();
	release_memory_in_torrent();

	exit(0);
}

// 实际上这里处理的是程序关闭的收尾工作
void process_signal(int signo)
{
	printf("Please wait for clear operations\n");
	do_clear_work();
}
	
/*
 * 设置信号处理函数（具体的函数是 process_signal）
 * 注意：handler 在 linux 信号编程中就是回调函数
 * 什么是信号：信号是
 * 返回：成功返回0，失败返回-1
 * 如果不理解信号，可以看我的笔记：
 * https://www.pluvet.com/archives/signal-and-signal-function.html
*/
int set_signal_handler()
{
	// SIG_IGN 表示忽视
	if(signal(SIGPIPE,SIG_IGN) == SIG_ERR) 
	{
		perror("can not catch signal:sigpipe\n");
		return -1;
	}
	// (Signal Interrupt) 中断信号，如 ctrl-C，通常由用户生成。
	if(signal(SIGINT,process_signal)  == SIG_ERR) 
	{
		perror("can not catch signal:sigint\n");
		return -1;
	}
	// (Signal Terminate) 发送给本程序的终止请求信号。
	if(signal(SIGTERM,process_signal) == SIG_ERR) 
	{
		perror("can not catch signal:sigterm\n");
		return -1;
	}


	return 0;
}