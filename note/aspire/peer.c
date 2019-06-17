#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include"peer.h"
#include"message.h"
#include"bitfield.h"

extern Bitmap *bitmap;

/*
 *指向当前与之通信的peer链表
 *因为当前客户端会有很多peer与之连接
 */
Peer *peer_head=NULL;



/*
 *功能：初始化Peer结构体
 *返回：成功返回0，失败返回-1
*/
initialize_peer(Peer *peer)
{
	if(peer=NULL)
	{
		return -1;
	}
	
	peer->socket=-1;
	memset(peer->id,0,16);
	peer->state=INITIAL;	//初始化状态
	
	peer->in_buff=NULL;
	peer->out_msg=NULL;
	peer->out_msg_copy=NULL;
	
	//动态分配内存
	peer->in_buff=(char*)malloc(MSGSIZE);
	if(peer->in_buff==NULL)
	{
		printf("%s:%d memory assigned error\n",_FILE_,_LINE_);
		return -1;
	}
	
	//动态分配内存
	peer->out_msg=(char*)malloc(MSGSIZE);
	if(peer->out_msg==NULL)
	{
		printf("%s:%d memory assigned error\n",_FILE_,_LINE_);
		//因为此时peer->in_buff的内存已经分配好了
		free(peer->in_buff);
		out->in_buff=NULL;
		return -1;
	}
	//这个算是一个比较谨慎的操作，因为malloc分配的内存空间的值是没有初始化的
	memset(peer->out_msg==NULL,0,MSG_SIZE);
	peer->msg_len=0;
	
	//动态分配内存
	peer->out_msg_copy=(char*)malloc(MSG_SIZE);
	if(peer->out_msg_copy==NULL)
	{
		printf("%s:%d memory assigned error\n",_FILE_,_LINE_);
		free(peer->in_buff);
		out->in_buff=NULL;
		free(peer->out_msg);
		out->msg=NULL;
		return -1;
	}
	memset(peer->out_msg_copy,0,MSG_SIZE);
	peer->msg_copy_len=0;
	peer->msg_copy_index=0;
	
	
	peer->am_choking=0;
	peer->am_intersted=0;
	peer->peer_choking=1;
	peer->peer_intersted=0;
	
	peer->bitmap.bitfield=NULL;
	peer->bitmap.bitfield.length=0;
	peer->bitmap.valid_length=0;
	
	peer->Request_piece_head=NULL;
	peer->Requested_piece_head=NULL;
	
	peer->down_total=0;
	peer->up_total=0;
	
	peer->start_timestamp=0;
	peer->recet_timestamp=0;
	
	peer->last_down_timestamp=0;
	peer->last_up_timestamp=0;
	peer->down_count=0;
	peer->down_count=0;
	peer->down_rate=0.0;
	peer->up_rate=0.0;
	
	peer->next=NULL;
	return 0;
	
	
}


/*
 *功能：向peer链表中添加一个节点
 *返回：成功返回头节点，失败返回NULL
*/

Peer* add_peer_node()
{
	int ret;
	Peer *node,*p;
	
	//分配内存空间
	node=(Peer*)malloc(sizeof(Peer));
	if(node==NULL)
	{
		printf("%s:%d error\n",_FILE_,_LINE_);
		return NULL;
	}
	
	//进行初始化
	ret=initialize_peer(node);
	if(ret<0)
	{
		printf("%s:%d error\n",_FILE_,_LINE_);
		//初始化失败，但是你分配的peer node节点内存还是得回收内存
		free(node);
		return NULL;
	}
	
	//将node加入到peer链表中,还是使用的尾插法
	if(peer_head==NULL)
	{
		peer_head=node;
	}
	else
	{
		p=peer_head;
		while(p->next!=NULL)
			p=p->next;
		p->next=node;
	}
	return node;
}


/*
 *功能：从peer链表中删除一个节点
 *返回：成功返回0，失败返回-1
*/

int del_peer_node(Peer *peer)
{
	Peer *p=peer_head,*q;
	
	//防止传入的是一个NULL(这个也是你之前很少考虑的事情)
	if(peer=NULL)
	{
		return -1;
	}
	
	while(p!=NULL)
	{
		//找到了peer这个节点
		if(p==peer)
		{
			//看看peer是不是头节点
			if(p==peer_head)
			{
				peer_head=p->next;
			}
			else
			{
				q->next=p->next;
			}
			free_peer_node(p);
			return 0;
		}
		else
		{
			q=p;
			p=p->next;
		}
	}
	return -1;
}


/*
 *功能：撤销当前请求队列
*/

int cancel_request_list(Peer *node)
{
	Request_piece *p=node->Request_piece_head;
	
	while(p!=NULL)
	{
		node->Request_piece_head=node->Request_piece_head->next;
		free(p);
		p=node->Request_piece_head;
	}
	
	return 0;
}


/*
 *功能：撤销当前被请求队列
*/

int cancel_requested_list(Peer *node)
{
	Request_piece *p=node->Requested_piece_head;
	while(p!=NULL)
	{
		node->Requested_piece_head=node->Requested_piece_head->next;
		free(p);
		p=node->Requested_piece_head;
	}
	
	return 0;
}


/*
 *功能：释放一个peer的内存节点
*/

void free_peer_node(Peer *node)
{
	if(node==NULL)
	{
		return;
	}
	
	/*
	 * 在初始化函数中，可以看到我写的注释，对于每一个Peer节点而言，是需要动态分配四次内存的
	 * 所以，这里也得释放四次内存
	 * 至于我这里为什么一定要使用一个变量，而不是实际上去操作，这个我是看到别人说最好是不要
	 * 直接去操作这个形参
	 */
	Peer *p=node;
	if(p->bitfield!=NULL)
	{
		free(p->bitfield.bitfield);
		p->bitfield.bitfield=NULL;
	}
	if(p->in_buff!=NULL)
	{
		free(p->in_buff);
		p->in_buff=NULL;
	}
	if(p->out_msg!=NULL)
	{
		free(p->out_msg);
		p->out_msg=NULL;
	}
	if(p->out_msg_copy)
	{
		free(p->out_msg_copy);
		p->out_msg_copy=NULL;
	}
	
	// 撤销请求队列和被请求队列
     cancel_request_list(node);
     cancel_requested_list(node);
	 
     // 释放完peer成员的内存后,再释放peer所占的内存
     free(node);
	 node=NULL;
	 p=NULL;

	
}


/*
 *功能：释放peer管理模块中动态分配的内存
*/

void release_memory_in_peer()
{
	if(peer_head==NULL)
	{
		return;
	}
	
	Peer *p=peer_head;
	while(p!=NULL)
	{
		peer_head=peer_head->next;
		free(p);
		p=peer_head;
	}
	
	
}

/*
 *功能：打印Peer节点的一些信息，用于调试程序
*/

void print_peer_data()
{
	if(peer_head==NULL)
	{
		printf("暂时没有peer节点信息\n");
		return;
	}
	Peer *p=peer_head;
	while(p!=NULL)
	{
		printf("该peer所处的状态：%d\n",p->statte);
		p=p->next;
	}
}