#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<string.h>
#include<sys/types.h>
#include<sys/stat.h>
#include<fcntl.h>
#include"parse_metafile.h"
#include"bitfield.h"
#include"message.h"
#include"shal.h"
#include"data.h"



/*
 * 缓冲管理模块就是为了提高读写速度而出现的。每次将下载的数据先放到缓冲区中，在缓冲区达到一定
   的数值后在讲数据写入硬盘。
   
 * 每一个Btcache节点就是一个slice
   
 * 缓冲区还有一个功能，就是每一次peer请求数据(请求都是一个slice)的时候，先将该slice所在的整个piece
   读入到缓冲区中，下次Peer再次请求该piece的其他slice的数据的时候，只需要在缓冲区获取，避免了频发
   读取硬盘


*/

extern char *file_name;				//待下载的文件名
extern Files *files_head;			//多于多文件有效，存放各个文件的路径和长度
extern int file_length;				//待下载文件的总长度（大小）
extern int piece_length;			//每个piece的长度（大小）
extern char *pieces;				//存放所有piece的hash值
extern int pieces_length;			//缓冲区pieces的长度

extern Bitmap *bitmap;				//指向己方位图
extern int download_piece_num;		//记录已经下载了多少个piece
extern Peer *peer_head;				//指向peer链表

#define btcache_len 1024			//缓冲区总共有多少个Btcache节点
Btcache *btcache_head=NULL;			//指向一个大小为16MB的缓冲区
Btcache *last_piece=NULL;			//指向下载文件的最后一个piece
int last_piece_index=0;				//最后一个piece的索引，他的值为总piece-1
int last_piece_count=0;				//针对最后一个peice，记录已经下载了多少个slice
int last_slice_len=0;				//最后一个piece的最后一个slice的长度


int *fds=NULL;						//存放文件描述符
int fds_len=0;						//指针fds所指向的数组的长度
int have_piece_index[64];			//存放刚刚下载的piece的索引
int end_mode=0;						//是否进入终端模式，终端模式的含义参考BT协议


/*
 * 功能：创建Btcache节点，并分配内存空间
 * 返回：成功返回创建的指针，反之返回NULL
*/

Btcache* initialize_btcache_node()
{
	Btcache *node;
	node=(Btcache*)malloc(sizeof(Btcache));
	if(node==NULL)
	{
		return NULL;
	}
	node->buff=(unsigned char *)malloc(16*1024);
	if(node->buff==NULL)
	{
		if(node!=NULL)
		{
			free(node);
			node=NULL;
		}
		return NULL;
	}
	//下面都是Btcache成员的初始化值
	node->index=-1;
	node->length=-1;
	node->begin=-1;
	node->in_use=0;
	node->read_write=-1;
	node->is_full=0;
	node->is_writed=0;
	node->access_count=0;
	node->next=NULL;
	
	return node;

}

/*
 * 功能：创建总大小为16K*1024bit的缓冲区
*/

int create_btcache()
{
	int i;
	//node指向刚刚创建的节点，last指向缓冲区的最后一个节点
	Btcache *node,*last;	
	for(i=0;i<btcache_len;i++)
	{
		node=initialize_btcache_node();
		if(node==NULL)
		{
			printf("%s:%d create_btcache error\n",_FILE_,_LINE_);
			release_memory_in_btcache();
			return -1;
		}
		if(btcache_head==NULL)
		{
			//将ba=tcache_head指向创建好的节点的头部
			btcache_head=node;
			last=node;
		}
		else
		{
			last->next=node;
			last=node;
		}
	}
	
	//为最后一个piece申请空间
	int count=file_length % piece_length / (16*1024);
	if(file_length % piece_length %(16*1024) !=0)
	{
		count++;
	}
	//count为最后一个piece所含的slice数
	last_piece_count=count;	
	last_slice_len=file_length%piece_length%(16*1024);
	if(last_slice_len==0)
	{
		last_slice_len=16*1024;
	}
	//最后一个piece的index值
	last_piece_index=piece_length/20 -1;
	
	while(count>0)
	{
		node=initialize_btcache_node();
		if(node==NULL)
		{
			printf("%s:%d create_btcache error\n",_FILE_,_LINE_);
			release_memory_in_btcache();
			return -1;
		}
		if(last_piece==NULL)
		{
			last_piece=node;
			last=node;
		}
		else
		{
			last->next=node;
			last=node;
		}
		count--;
	}
	
	for(i=0;i<64;i++)
	{
		have_piece_index[i]=-1;
	}
	return 0;
}


/*
 * 功能：释放data.c文件中动态分配的内存
*/

void release_memory_in_btcache()
{
	Btcache *p=btcache_head;
	while(p!=NULL)
	{
		btcache_head=p->next;
		//注意，这里是释放了p->buff和p  少释放一个的话就会导致内存泄漏
		if(p->buff!=NULL)
		{
			free(p->buff);
			p->buff=NULL;
		}
		free(p);
		p=btcache_head;
	}
	release_last_piece();
	if(fds!=NULL)
	{
		free(fds);
	}
}

/*
 * 功能：释放为存储最后一个piece而申请的空间
*/

void release_last_piece()
{
	Btcache *p=last_piece;
	while(p!=NULL)
	{
		last_piece=p->next;
		if(p->buff!=NULL)
		{
			free(p->buff);
			p->buff=NULL;
		}
		free(p);
		p=last_piece;
	}
}


/*
 * 功能：判断中种子文件中待下载的文件个数
 * 返回：种子文件中要下载的文件的个数
*/

int get_files_count()
{
	int count=0;
	
	if(is_multi_files()==0)
	{
		return 1;
	}
	Files *p=files_head;
	while(p!=NULL)
	{
		count++;
		p=p->next;
	}
	return count;
}


/*
 * 功能：根据种子文件中的信息创建保存下载数据的文件
		 通过lseek和write两个函数来实现物理存储空间的分配
*/

int create_files()
{
	int ret,i;
	char buff[1]={0x0};
	
	fds_len=get_files_count();
	if(fds_len<0)
	{
		return -1;
	}
	//fds_len为带下载的文件的数量，那么fds就是很灵活的
	fds=(int *)malloc(fds_len * sizeof(int));
	if(fds==NULL)
	{
		return -1;
	}
	
	//带下的为单文件
	if(is_multi_files()==0)
	{
		*fds=open(file_name,O_REWR|O_CREAT,0777);
		if(fds<0)
		{
			printf("%s:%d error",_FILE_,_LINE_);
			return -1;
		}
		//改变文件读写的偏移量。是从当前偏移量开始读和写的
		ret=lseek(*fds,file_length-1,SEEK_SET);
		if(ret<0)
		{
			printf("%s:%d error",_FILE_,_LINE_);
			return -1;
		}
		//向该文件末尾写了一个
		ret=write(*fds,buff,1);
		if(ret!=1)
		{
			printf("%s:%d error",_FILE_,_LINE_);
			return -1;
		}
	}
	
	//待下载的是多文件
	else
	{
		//chdir改变当前工作目录
		ret=chdir(file_name);
		//改变目录失败，说明该目录还没有创建
		if(ret<0)
		{
			ret=mkdir(file_name,0777);
			if(ret<0)
			{
				printf("%s:%d error",_FILE_,_LINE_);
				return -1;
			}
			ret=chdir(file_name);
			if(ret<0)
			{
				printf("%s:%d error",_FILE_,_LINE_);
				return -1;
			}
		}
		Files *p=files_head;
		i=0;
		while(p!=NULL)
		{
			fds[i]=open(p->path,O_RDWR|O_CREART,07777);
			if(fds[i]<0)
			{
				printf("%s:%d error",_FILE_,_LINE_);
				return -1;
			}
			ret=lseek(fds[i],p->length-1,SEEK_SET);
			if(ret<0)
			{
				printf("%s:%d error",_FILE_,_LINE_);
				return -1;
			}
			ret=write(fds[i],buff,1);
			if(ret!=1)
			{
				printf("%s:%d error",_FILE_,_LINE_);
				return -1;
			}
			p=p->next;
			i++;
		}//while循环结束
	}//end else
		
	return 0;
}


/*
 * 功能：判断一个Btcache节点（即一个slice）的数据要写到那个文件及具体的位置，并写入硬盘
 * 说明：也就是说，现在又有一个slice下载好了，所以将其封装为了一个Btcache。现在就是得将
		 下载到的slice写入硬盘。但是对于多文件的种子来说，是要把对应的slice写入到对应的
		 文件夹中的，所以才有了后面的判断。
*/

int write_btcache_node_to_harddisk(Btcache *node)
{
	long long     line_position;
	Files         *p;
	int           i;

	if((node == NULL) || (fds == NULL))  
	{
		return -1;
	}

	// 无论是否下载多文件，将要下载的所有数据看成一个线性字节流
	// line_position指示要写入硬盘的线性位置
	// piece_length为每个piece长度，它被定义在parse_metafile.c中
	line_position = node->index * piece_length + node->begin;

	// 如果下载的是单个文件
	if( is_multi_files() == 0 ) 
	{  
		lseek(*fds,line_position,SEEK_SET);
		write(*fds,node->buff,node->length);
		return 0;
	}

	// 下载的是多个文件
	//在给p赋值之前还判断一下，很谨慎。值得学习
	if(files_head == NULL) 
	{ 
		printf("%s:%d file_head is NULL",__FILE__,__LINE__);
		return -1;
	}
	p = files_head;
	i = 0;
	while(p != NULL) 
	{
		// 待写入的数据属于同一个文件
		if((line_position < p->length) && (line_position+node->length < p->length)) 
		{
			lseek(fds[i],line_position,SEEK_SET);
			write(fds[i],node->buff,node->length);
			break;
		} 
		
		// 待写入的数据跨越了两个文件或两个以上的文件
		else if((line_position < p->length) && (line_position+node->length >= p->length)) 
		{
			int offset = 0;             // buff内的偏移,也是已写的字节数
			int left   = node->length;  // 剩余要写的字节数
			
			lseek(fds[i],line_position,SEEK_SET);
			//这里只写自己文件能够装下的数据
			write(fds[i],node->buff,p->length - line_position);
			offset = p->length - line_position;        // offset存放已写的字节数
			left = left - (p->length - line_position); // 还需写在字节数
			p = p->next;                               // 用于获取下一个文件的长度
			i++;                                       // 获取下一个文件描述符
			
			while(left > 0)
			{
				// 当前文件的长度大于等于要写的字节数 
				if(p->length >= left) 
				{  
					lseek(fds[i],0,SEEK_SET);
					write(fds[i],node->buff+offset,left); // 写入剩余要写的字节数
					left = 0;
				} 
				// 当前文件的长度小于要写的字节数
				else if(p->length < left)
				{ 
					lseek(fds[i],0,SEEK_SET);
					write(fds[i],node->buff+offset,p->length); // 写满当前文件
					offset = offset + p->length;
					left = left - p->length;
					i++;
					p = p->next;
				}
				else
				{
					break;
				}
			}
		} 
		else 
		{
			// 待写入的数据不应写入当前文件
			line_position = line_position - p->length;
			i++;
			p = p->next;
		}
	}
	return 0;
}


/*
 * 功能：从硬盘读取一个slice数据，存放到缓冲区中，在peer需要时发送给peer
*/

int read_slice_from_harddisk(Btcache *node)
{
	unsigned int  line_position;
	Files         *p;
	int           i;
	
	if( (node == NULL) || (fds == NULL) ) 
	{
		return -1;
	}
	
	if( (node->index >= pieces_length/20) || (node->begin >= piece_length) ||(node->length > 16*1024) )
	{
			
		return -1;
	}

	// 计算线性偏移量
	line_position = node->index * piece_length + node->begin;
	
	// 如果下载的是单个文件
	if( is_multi_files() == 0 ) 
	{  
		lseek(*fds,line_position,SEEK_SET);
		read(*fds,node->buff,node->length);
		return 0;
	}
	
	// 如果下载的是多个文件
	if(files_head == NULL)  
	{
		get_files_length_path();
	}
	p = files_head;
	i = 0;
	while(p != NULL) 
	{
		if((line_position < p->length) && (line_position+node->length < p->length)) 
		{
			// 待读出的数据属于同一个文件
			lseek(fds[i],line_position,SEEK_SET);
			read(fds[i],node->buff,node->length);
			break;
		} 
		else if((line_position < p->length) && (line_position+node->length >= p->length))
		{
			// 待读出的数据跨越了两个文件或两个以上的文件
			int offset = 0;             // buff内的偏移,也是已读的字节数
			int left   = node->length;  // 剩余要读的字节数

			lseek(fds[i],line_position,SEEK_SET);
			read(fds[i],node->buff,p->length - line_position);
			offset = p->length - line_position;        // offset存放已读的字节数
			left = left - (p->length - line_position); // 还需读在字节数
			p = p->next;                               // 用于获取下一个文件的长度
			i++;                                       // 获取下一个文件描述符

			while(left > 0)
				if(p->length >= left) {  // 当前文件的长度大于等于要读的字节数 
					lseek(fds[i],0,SEEK_SET);
					read(fds[i],node->buff+offset,left); // 读取剩余要读的字节数
					left = 0;
				} else {  // 当前文件的长度小于要读的字节数
					lseek(fds[i],0,SEEK_SET);
					read(fds[i],node->buff+offset,p->length); // 读取当前文件的所有内容
					offset = offset + p->length;
					left = left - p->length;
					i++;
					p = p->next;
				}

			break;
		} 
		else 
		{
			// 待读出的数据不应写入当前文件
			line_position = line_position - p->length;
			i++;
			p = p->next;
		}
	}
	return 0;
}


/*
 * 检查下载完的一个piece的数据是否正确，若正确，则写入文件
*/

int write_piece_to_harddisk(int sequence,Peer *peer)
{
	Btcache* node_ptr=btcache_head,*p;
	unsigned char piece_hash1[20],piece_hash2[20];
	//一个piece所含的slice数
	int slice_count=piece_length/(16*1024);
	int index,index_copy;
	
	if(peer==NULL)
	{
		return -1;
	}
	int i=0;
	while(i<sequence)
	{
		node_ptr=node_ptr->next;
		i++;
	}
	//p指向piece的第一个slice所在的btcache节点
	p=node_ptr;
	
	//计算刚刚下载到的这个piece的hash值
	SHA1_CTX ctx;
	SHAInit(&ctx);
	while(slice_count>0 && node_ptr!=NULL)
	{
		SHA1Update(&ctx,node_ptr->buff,16*1024);
		slice_count--;
		node_ptr=node_ptr->next;
	}
	SHA1Final(piece_hash1,&ctx);
	//从种子中获取该piece的正确的hash值
	index=p->index*20;
	//存放piece的index
	index_copy=p->index;
	for(i=0;i<20;i++)
	{
		piece_hash2[i]=piece[index+i];
	}
	//比较两者的hash值，如果一致的话，证明下载了一个正确的piece
	int ret=memcmp(piece_hash1,piece_hash2,20);
	if(ret!=0)
	{
		printf("piece hash is wrong\n");
		return -1;
	}
	//将该piece所有的slice写入文件
	node_ptr=p;
	slice_count=piece_length /(16*1024);
	while(slice_count>0)
	{
		write_btcache_node_to_harddisk(node_ptr);
		//在Peer的请求队列中删除piece的请求
		Request_piece *req_p=peer->Request_piece_head;
		Request_piece *req_q=peer->Request_piece_head;
		while(req_p!=NULL)
		{
			if(req_p->begin==node_ptr->begin && req_p->index==node_ptr->index)
			{
				if(req_p==peer->Request_piece_head)
				{
					peer->Request_piece_head=req_p-next;
				}
				else
				{
					req_q->next=req_p->next;
				}
				free(req_p);
				req_p=req_q=NULL;
				break;
			}
			req_q=req_p;
			req_p=req_p->next;
		}
		
		node_ptr->index=-1;
		node_ptr->begin=-1;
		node_ptr->length=-1;
		node_ptr->in_use=0;
		node_ptr->read_write=-1;
		node_ptr->is_full=0;
		node_ptr->is_writed=0;
		node_ptr->access_count=0;
		node_ptr=node_ptr->next;
		slice_count--;
	}
	
	//当前处于终端模式下，则在peer链表中删除所有对该piece的请求
	if(end_mode==1)
	{
		delete_request_end_mode(index_copy);
	}
	//更新位图
	set_bit_value(bitmap,index_copy,1);
	//保存piece的index,准备给所有的peer发送have消息
	for(i=0;i<64;i++)
	{
		if(have_piece_index[i]==-1)
		{
			have_piece_index[i]=index_copy;
			break;
		}
	
	}
	//更新download_piece_num，每下载10个piece就将位图写入文件
	download_piece_num++;
	if(download_piece_num % 10==0)
	{
		restore_bitmap();
	}
	//打印提示信息
	printf("%%%%%  Total piece download:%d %%%%%\n",download_piece_num);
	printf("write piece index:%d\n",index_copy);
	return 0;
}


/*
 * 功能：从硬盘上的文件中读取一个piece到p指针所指向的缓冲区中
*/

int read_peice_from_harddisk(Btcache *p,int index)
{
	
}


/*
 * 功能：将整个缓冲区中已经下载的piece写入硬盘，这样可以释放缓冲区
*/

int write_btcache_to_harddisk(Peer *peer)
{
	Btcache *p=btcache_head;
	int slice_count=piece_length /(16*1024);
	int index_count=0;
	int full_count=0;
	int first_index;
	
	while(p!=NULL)
	{
		if(index_count % slice_count==0)
		{
			full_count]=0;
			first_index=index_count;
		}
		if((p->in_use==1) && (p->read_write==1) &&
		    (p->is_full==1) && (p->is_writed==0))
			{
				full_count++;
			}
		if(full_count==slice_count)
		{
			write_piece_to_harddisk(first_index,peer);
		}
		inde_count++;
		p=p->next;
	}
	
	return 0;
}


/*
 * 功能：当缓冲区不够用的时候，释放那些从硬盘上读取的Piece
*/

int release_read_btcache_node(int base_count)
{
	Btcache *p=btcache_head;
	Btcache *q=NULL;
	int count=0;
	int used_count=0;
	int slice_count=piece_length /(16*1024);
	
	if(base_count < 0)
	{
		return -1;
	}
	while(p!=NULL)
	{
		if(count % slice_count==0)
		{
			used_count=0;
			q=p;
		}
		if(p->in_use==1 && p->read_write==0)
		{
			used_count+=p->access_count;
		}
		//找到一个空闲的piece
		if(used_count==base_count)
		{
			break;
		}
		
		count++;
		p=p->next;
		
	}
	if(p!=NULL)
	{
		p=q;
		while(slice_count > 0)
		{
			p->index=-1;
			p->begin=-1;
			p->length=-1;
			p->in_use=0;
			p->read_write=-1;
			p->is_full=0;
			p->is_writed=0;
			p->access_count=0;
			
			slice_count--;
			p=p->next;
		}
	}
	
	return 0;
}


/*
 *功能：下载完一个slice后，检查是否为piece的最后一个slice
*/

int is_a_complete_piece(int index,int *sequence)
{
	
}

/*
 * 将整个缓冲区的数据清空
*/

void clear_btcache()
{
	Btcache *node=btcache_head;
	while(node!=NULL)
	{
		p->index=-1;
		p->begin=-1;
		p->length=-1;
		p->in_use=0;
		p->read_write=-1;
		p->is_full=0;
		p->is_writed=0;
		p->access_count=0;
		node=node->next;
	}
}




/*
 * 功能：将从peer处获取的一个slice存贮到缓冲区中
*/

int write_slice_to_btcache(int index,int begin,int length,unsigned char *buff,int len,Peer *peer)
{
	int count=0,slice_count,unuse_count;
	//q指向每一个piece的第一个slice
	Btcache *p=btcache_head,*q=NULL;
	
	if(p==NULL)
	{
		return -1;
	}
	if(index>=piece_length/20 || begin>piece_length-16*1024)
	{
		return -1;
	}
	if(buff==NULL || peer==NULL)
	{
		return -1;
	}
	if(index==last_piece_index)
	{
		write_slice_to_last_piece(index,begin,length,buff,len,peer);
		return 0;
	}
	//处于终端模式的时候，先判断该slice所在的piece的其他数据是否存在
	//不存在说明是一个新的piece，存在说明不是一个新的piece
	slice_count=piece_length /(16*1024);
	while(p!=NULL)
	{
		if(count % slice==0)
		{
			q=p;
		}
		if(p->index=index && p->in_use==1)
		{
			break;
		}
		count++;
		p=p->next;
	}
	
	//p非空说明当前slice所在的piece的部分数据已经下载
	if(p!=NULL)
	{
		//count存放当前要存放的slice在piece中的索引值
		count=begin / (16*1024);
		p=q;
		while(count>0)
		{
			p=p->next;
			count--;
		}
		//该slice已经存在
		if(p->begin==begin && p->in_use==1 && p->read_write==1 && p->is_full==1)
		{
			return 0;
		}
		p-index=index;
		p->begin=begin;
		p->length=length;
		p->in_use=1;
		p->read_write=1;
		p->is_full=1;
		p->is_writed=0;
		p->access_count=0;
		
		memcpy(p->buff,buff,len);
		
		printf("++++ write a slice to btcache index:%-6d begin:%-6x ++++\n",index,begin);
		//如果是刚刚下载的（下载的piece不足10个），则立即写入硬盘并告知peer
		if(download_piece_num < 10)
		{
			int sequence;
			int ret;
			ret=is_a_complete_piece(index,&sequence);
			if(ret==1)
			{
				printf("##### begin write a piece to harddisk ####\n");
				write_piece_to_harddisk(sequence,peer);
				printf("##### end write a piece to harddisk ####\n");
			}
		}
		return 0;
	}
	
	//p为NULL说明当前slice是其所在的piece第一块下载到的数据
	//首先判断是否存在空的缓冲区，如不存在，则将已经下载的数据写入硬盘
	int i=4;
	while(i>0)
	{
		slice_count=piece_length /(16*1024);
		count=0;
		unuse_count=0;
		Btcache *q;
		p=btcache_head;
		while(p!=NULL)
		{
			p=q;
			count=begin /(16*1024);
			while(count>0)
			{
				p=p->next;
				count--;
			}
			p->index=index;
			p->begin=begin;
			p->length=length;
			
			p->in_use=1;
			p->read_write=1;
			p->is_full=1;
			p->is_writed=0;
			p->access_count=0;
			
			memcpy(p->buff,buff,len);
			printf("++++ write a slice to btcache index:%-6d begin:%-6x ++++",index,begin);
			return 0;
		}
		
		if(i==4)
		{
			write_btcache_to_harddisk(peer);
		}
		if(i==3)
		{
			release_read_btcache_node(16);
		}
		if(i==2)
		{
			release_read_btcache_node(8);
		}
		if(i==1)
		{
			release_read_btcache_node(0);
		}
		i--;
	}
	//如果还没有缓冲区，那么就丢弃下载到的这个slice
	printf("++++  write a slice to btcache FAILED:NO BUFFER ++++\n");
	clear_btcache();
	return 0;
}



/*
 * 功能：从缓冲区中获取一个slice，读取的slice存放到peer的发送缓冲区中。
         如果缓冲区不存在该slice，则从硬盘中读取该slice所在的piece到缓冲区中
*/