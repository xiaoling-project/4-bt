#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "parse_metafile.h"
#include "peer.h"
#include "data.h"
#include "message.h"
#include "policy.h"

Unchoke_peers  unchoke_peers;
long long      total_down = 0L, total_up = 0L;
float          total_down_rate = 0.0F, total_up_rate = 0.0F;
int            total_peers = 0;

extern int	   end_mode;
extern Bitmap  *bitmap;
extern Peer    *peer_head;
extern int     pieces_length;
extern int     piece_length;

extern Btcache *btcache_head;
extern int     last_piece_index;
extern int     last_piece_count;
extern int     last_slice_len;
extern int     download_piece_num;

// 初始化全局变量unchoke_peers
void init_unchoke_peers()
{
	int i;

	for(i = 0; i < UNCHOKE_COUNT; i++) {
		*(unchoke_peers.unchkpeer + i) = NULL;	//非阻塞Peer	
	}

	unchoke_peers.count = 0;
	unchoke_peers.optunchkpeer = NULL; //优化非阻塞peer
}

// 判断一个peer是否已经存在于unchoke_peers
int is_in_unchoke_peers(Peer *node)
{
	int i;
	//通过循环Unchoke_peers结构体中的unchkpeer指针数组
	for(i = 0; i < unchoke_peers.count; i++) {
		if( node == (unchoke_peers.unchkpeer)[i] )  return 1;
	}

	return 0;
}

// 从array数组中获取下载速度最慢的peer的索引
int get_last_index(Peer **array,int len)
{
	int i, j = -1;

	if(len <= 0) return j;
	else j = 0;

	for(i = 0; i < len; i++)
		if( array[i]->down_rate < array[j]->down_rate )  j = i;

	return j;
}

// 找出当前下载速度最快的4个peer,将其unchoke
int select_unchoke_peer()
{
	Peer*  p;
	Peer*  now_fast[UNCHOKE_COUNT];
	Peer*  force_choke[UNCHOKE_COUNT];
	int    unchoke_socket[UNCHOKE_COUNT], choke_socket[UNCHOKE_COUNT];
	int    i, j, index = 0, len = UNCHOKE_COUNT;//此时len的长度为4

	//将变量进行初始化
	for(i = 0; i < len; i++) {
		now_fast[i]       = NULL;
		force_choke[i]    = NULL;
		unchoke_socket[i] = -1;
		choke_socket[i]   = -1;
	}

	// 将已断开连接而又处于unchoke队列中的peer清除出unchoke队列
	for(i = 0, j = 0; i < unchoke_peers.count; i++) {
		p = peer_head;
		//通过对peer链表的遍历得出unchoke队列中是否有peer已近断开连接
		while(p != NULL) {
			if(p == unchoke_peers.unchkpeer[i])  break;
			p = p->next;
		}
		//若已断开连接,将unchoke_peers.unchkpeer[i] = NULL
		if(p == NULL)  { unchoke_peers.unchkpeer[i] = NULL; j++; }
	}
	
	//将已断开连接的peer覆盖掉,同时更新此时unchoke_peers.count的数目
	//j记录非阻塞peer中已经断开连接的数量
	if(j != 0) {
		unchoke_peers.count = unchoke_peers.count - j;//更新此时unchoke_peers.count的数目
		//将还没有断开连接的peer放入到force_choke数组中
		for(i = 0, j = 0; i < len; i++) {
			if(unchoke_peers.unchkpeer[i] != NULL) {
				force_choke[j] = unchoke_peers.unchkpeer[i];
				j++;
			}
		}
		//将断开连接的peer成功从unchkpeer数组删去
		for(i = 0; i < len; i++) {
			unchoke_peers.unchkpeer[i] = force_choke[i];
			force_choke[i] = NULL;
		}
	}

	//将那些在过去10秒上传速度超过20KB/S而下载速度过小的peer强行阻塞
	//注意：up_rate和down_rate的单位是B/S而不是KB/S
	//如果下载速度过小,对方随时有可能将我阻塞
	for(i = 0, j = -1; i < unchoke_peers.count; i++) {
		if( (unchoke_peers.unchkpeer)[i]->up_rate > 50*1024 &&
			(unchoke_peers.unchkpeer)[i]->down_rate < 0.1*1024 ) {
			j++;
			force_choke[j] = unchoke_peers.unchkpeer[i];
		}
	}

	//从当前所有Peer中选出下载速度最快的四个peer
	p = peer_head;
	while(p != NULL) {
		if(p->state==DATA && is_interested(bitmap,&(p->bitmap)) && is_seed(p)!=1) {
			// p不应该在force_choke数组中
			for(i = 0; i < len; i++) {
				if(p == force_choke[i]) break;
			}
			//p并没有在force_choke数组中
			if(i == len) {
				//当now_fast中元素还没有达到UNCHOKE_COUNT时
				if( index < UNCHOKE_COUNT ) {
					now_fast[index] = p; 
					index++; 
				} 
				//当now_fast中元素达到UNCHOKE_COUNT后,选择速度更快的
				else {
					j = get_last_index(now_fast,UNCHOKE_COUNT);//找到在now_fast中速度最慢的
					if(p->down_rate >= now_fast[j]->down_rate) now_fast[j] = p;
				}
			}
		}
		p = p->next;
	}

	// 假设now_fast中所有的peer都是要unchoke的
	for(i = 0; i < index; i++) {
		Peer*  q = now_fast[i];
		unchoke_socket[i] = q->socket;
	}

	// 假设unchoke_peers.unchkpeer中所有peer都是choke的
	for(i = 0; i < unchoke_peers.count; i++) {
		Peer*  q = (unchoke_peers.unchkpeer)[i];
		choke_socket[i] = q->socket;
	}

	// 如果now_fast某个元素已经存在于unchoke_peers.unchkpeer
	// 则没有必要进行choke或unckoke
	for(i = 0; i < index; i++) {
		//判断该peer是否已经存在于unchoke_peers结构体里面的数组中
		if( is_in_unchoke_peers(now_fast[i]) == 1) {
			for(j = 0; j < len; j++) {
				Peer*  q = now_fast[i];
				if(q->socket == unchoke_socket[i])  unchoke_socket[i] = -1;
				if(q->socket == choke_socket[i])    choke_socket[i]   = -1;
			}
		}
	}

	// 更新当前unchoke的peer
	for(i = 0; i < index; i++) {
		(unchoke_peers.unchkpeer)[i] = now_fast[i];
	}
	unchoke_peers.count = index;     //unchoke_peers.count此时个数为4个

	// 状态变化后,要对peer的状态值重新赋值,并且创建choke、unchoke消息
	//对于unchoke_socket数组中相对应的peer发送unchoke信息
	//对于choke_socket数组中相对应的peer发送choke信息
	p = peer_head;
	while(p != NULL) {
		for(i = 0; i < len; i++) {
			if(unchoke_socket[i]==p->socket && unchoke_socket[i]!=-1) {
				p->am_choking = 0;
				create_chock_interested_msg(1,p); //发送unchoke信息
			}
			if(choke_socket[i]==p->socket && unchoke_socket[i]!=-1) {
				p->am_choking = 1;
				cancel_requested_list(p);    //清空被请求队列
				create_chock_interested_msg(0,p); //发送choke信息
			}
		}
		p = p->next;
	}

	//for(i = 0; i < unchoke_peers.count; i++)
	//	printf("unchoke peer:%s \n",(unchoke_peers.unchkpeer)[i]->ip);

	return 0;
}

// 假设要下载的文件共有100个piece
// 以下函数的功能是将0到99这100个数的顺序以随机的方式打乱
// 从而得到一个随机的数组,该数组以随机的方式存储0～99,供片断选择算法使用
int *rand_num = NULL;
int get_rand_numbers(int length)
{
	int i, index, piece_count, *temp_num;
	
	if(length == 0)  return -1;
	piece_count = length; //待下载piece数目
	
	//分配一个长度为length的数组内存
	rand_num = (int *)malloc(piece_count * sizeof(int));
	if(rand_num == NULL)    return -1;
	//同时分配一个temp_num的数组的内存
	temp_num = (int *)malloc(piece_count * sizeof(int)); //分别为temp_num和rand_num分配内存
	if(temp_num == NULL)    return -1;
	for(i = 0; i < piece_count; i++)  temp_num[i] = i;
	
	srand(time(NULL));
	//将rend_num打乱
	for(i = 0; i < piece_count; i++) {
		
        index = (int)( (float)(piece_count-i) * rand() / (RAND_MAX+1.0) );//RAND_MAX为一个宏,rand()能返回的最大值
		rand_num[i] = temp_num[index];
        temp_num[index] = temp_num[piece_count-1-i];
    }
	
	if(temp_num != NULL)  free(temp_num);
	return 0;
}

// 从peer队列中选择一个优化非阻塞peer
int select_optunchoke_peer()
{
	int   count = 0, index, i = 0, j, ret;
	Peer  *p = peer_head; 

	// 获取peer队列中peer的总数
	while(p != NULL) {
		count++;
		p =  p->next;
	}

	// 如果peer总数太少(小于等于4),则没有必要选择优化非阻塞peer
	if(count <= UNCHOKE_COUNT)  return 0;
	ret = get_rand_numbers(count);  //生成随机数
	if(ret < 0) {
		printf("%s:%d get rand numbers error\n",__FILE__,__LINE__);
		return -1;
	}
	while(i < count) {
		// 随机选择一个数,该数在0～count-1之间
		index = rand_num[i];

		p = peer_head;
		j = 0;
		//找到所对应的peer
		while(j < index && p != NULL) {
			p = p->next;
			j++;
		}
		//该peer不存在于非阻塞队列中，并且不是种子,还不能是原先的优化非阻塞peer,并且该peer必须对客户端的peer感兴趣
		if( is_in_unchoke_peers(p) != 1 && is_seed(p) != 1 && p->state == DATA &&
			p != unchoke_peers.optunchkpeer && is_interested(bitmap,&(p->bitmap)) ) {
		
			//遍历寻找优化非阻塞
			if( (unchoke_peers.optunchkpeer) != NULL ) {
				Peer  *temp = peer_head;
				while( temp != NULL ) {
					if(temp == unchoke_peers.optunchkpeer) break;
					temp = temp->next;
				}
				//将原先的优化非阻塞peer进行阻塞
				if(temp != NULL) {
					(unchoke_peers.optunchkpeer)->am_choking = 1;
					create_chock_interested_msg(0,unchoke_peers.optunchkpeer);
				}
			}
			//设置优化非阻塞peer为p
			p->am_choking = 0;
			create_chock_interested_msg(1,p); 
			unchoke_peers.optunchkpeer = p;
			//printf("*** optunchoke:%s ***\n",p->ip);
			break;
		}

		i++;
	}

	if(rand_num != NULL) { free(rand_num); rand_num = NULL; }
	return 0;
}

// 计算最近一段时间(如10秒)每个peer的上传下载速度,计算下载速度,一般都是要重新选择unchoke_peer
int compute_rate()
{
	Peer    *p       = peer_head;
	time_t  time_now = time(NULL);
	long    t        = 0;

	while(p != NULL) {
		//计算最近的下载速度,然后将速度清零
		if(p->last_down_timestamp == 0) {
			p->down_rate  = 0.0f;
			p->down_count = 0;
		} else {
			t = time_now - p->last_down_timestamp;
			if(t == 0)  printf("%s:%d time is 0\n",__FILE__,__LINE__);
			else  p->down_rate = p->down_count / t;
			p->down_count          = 0;
			p->last_down_timestamp = 0;
		}
		//计算最近的上传速度,然后清零
		if(p->last_up_timestamp == 0) {
			p->up_rate  = 0.0f;
			p->up_count = 0;
		} else {
			t = time_now - p->last_up_timestamp;
			if(t == 0)  printf("%s:%d time is 0\n",__FILE__,__LINE__);
			else  p->up_rate = p->up_count / t;
			p->up_count          = 0;
			p->last_up_timestamp = 0;
		}

		p = p->next;
	}

	return 0;
}

// 计算总的下载和上传速度
int compute_total_rate()
{
	Peer *p = peer_head;

	total_peers     = 0;
	total_down      = 0;
	total_up        = 0;  
	total_down_rate = 0.0f;
	total_up_rate   = 0.0f;

	while(p != NULL) {
		total_down      += p->down_total;
		total_up        += p->up_total;
		total_down_rate += p->down_rate;
		total_up_rate   += p->up_rate;

		total_peers++;
		p = p->next;
	}

	return 0;
}
//判断该peer是不是种子
int is_seed(Peer *node)
{
	int            i;
	unsigned char  c = (unsigned char)0xFF, last_byte;
	unsigned char  cnst[8] = { 255, 254, 252, 248, 240, 224, 192, 128 };
	
	if(node->bitmap.bitfield == NULL)  return 0;
	
	for(i = 0; i < node->bitmap.bitfield_length-1; i++) {
		if( (node->bitmap.bitfield)[i] != c ) return 0;
	}
		
	// 获取位图的最后一个字节
	last_byte = node->bitmap.bitfield[i];
	// 获取最后一个字节的无效位数
	i = 8 * node->bitmap.bitfield_length - node->bitmap.valid_length; 
	// 判断最后一个是否位种子的最后一个字节
	if(last_byte >= cnst[i]) return 1;
	else return 0;
}

//生成request请求消息,实现了片断选择算法,17为一个request消息的固定长度
//如果整个piece的信息都已经被发出请求了,接下便会选择一个新的piece,这个piece此时并没有被其他的peer请求
//当收到piece信息时,将slice写入缓冲区后,会调用该函数,继续请求slice

//request消息说明: 如果requeste队列为空,那么将会随机选择一个还没有被其他peer请求过piece,连续发出四个请求
//如果requeste队列不为空,那么判断最后一个请求是否为某个piece的最后一个请求,如果是,则将再次随机选择一个piece
//如果不是,将发送该piece的下一个slice数据请求
int create_req_slice_msg(Peer *node)
{
	//每一个slice的大小都是16kb
	int index, begin, length = 16*1024;
	int i, count = 0;

	if(node == NULL)  return -1;
	// 如果被peer阻塞或对peer不感兴趣,就没有必要生成request消息
	if(node->peer_choking==1 || node->am_interested==0 )  return -1;

	// 如果之前向该peer发送过请求,则根据之前的请求构造新请求
	// 遵守一条原则：同一个piece的所有slice应该从同一个peer处下载
	Request_piece *p = node->Request_piece_head, *q = NULL;
	if(p != NULL) 
	{
		while(p->next != NULL)  
		{ p = p->next; } // 定位requeste_piece链表的最后一个结点处

		// 一个piece的最后一个slice的起始下标
		int last_begin = piece_length - 16*1024; //这个piece并不是最后一个piece，piece的长度为piece_length,
		//则最后一个slice的位置自然就在piece_length - 16*1024
		
		// 如果是最后一个piece；last_piece_index记录了最后一个piece的index
		if(p->index == last_piece_index) {
			//last_piece_count记录了最后pieces有多少个slice,这里的last_begin记录了最后一个slice的开始位置
			last_begin = (last_piece_count - 1) * 16 * 1024;
		}
		
		//当前piece还有未请求的slice,则构造请求消息
		if(p->begin < last_begin) 
		{
			index = p->index;
			begin = p->begin + 16*1024;//从此处开始发送请求,而且只会请求一个slice
			count = 0;
			//这里的count证明只发送一个request
			while(begin!=piece_length && count<1) 
			{
				// 如果是最后一个piece的最后一个slice,那么长度将为last_slice_len,否则长度不变
				if(p->index == last_piece_index) {
					if( begin == (last_piece_count - 1) * 16 * 1024 )
						length = last_slice_len;
				}
				
				create_request_msg(index,begin,length,node);
				
				//为请求节点赋予相应的值
				q = (Request_piece *)malloc(sizeof(Request_piece));
				if(q == NULL) { 
					printf("%s:%d error\n",__FILE__,__LINE__);
					return -1;
				}
				q->index  = index;
				q->begin  = begin;
				q->length = length;
				q->next   = NULL;
				p->next   = q;
				p         = q;

				begin += 16*1024;
				count++;
			}
			
			return 0;  // 构造完毕,就返回
		}
	}

	// 然后去btcache_head中寻找这样的piece:它没有下载完,但它不在任何peer的
	// request消息队列中,应该优先下载这样的piece,出现这样的piece的原因是:
	// 从一个peer处下载一个piece,还没下载完,那个peer就将我们choke了或下线了

	// 但是测试结果表明, 以这种方式这种方式创建rquest请求执行效率并不高
	// 如果直接丢弃未下载完成的piece,则没有必要进行这种生成请求的方式
	// int ret = create_req_slice_msg_from_btcache(node);
	// if(ret == 0) return 0;

	
	
	// 生成随机数
	//选择一个新的piece继续请求
	//当整个piece请求完成之后,便开始一个新的pieces请求的开始
	if(get_rand_numbers(pieces_length/20) == -1) {
		printf("%s:%d error\n",__FILE__,__LINE__);
		return -1;
	}
	// 随机选择一个piece的下标,该下标所代表的piece需要没有向任何peer请求过,除非进入了终端模式
	for(i = 0; i < pieces_length/20; i++) {
		index = rand_num[i];

		// 判断将index作为下标的piece,peer是否拥有
		if( get_bit_value(&(node->bitmap),index) != 1)  continue;
		// 判断对于以index为下标的piece,是否已经下载
		if( get_bit_value(bitmap,index) == 1) continue;

		// 判断对于以index为下标的piece,是否已经请求过了
		Peer          *peer_ptr = peer_head;
		Request_piece *reqt_ptr;
		int           find = 0;
		//通过对所有的peer的Request_piece队列的循环遍历,判断是否别的peer已经发出过请求了
		while(peer_ptr != NULL) {
			reqt_ptr = peer_ptr->Request_piece_head;
			//如果真的已经被发出过请求,将find置为1,然后跳出循环
			while(reqt_ptr != NULL) {
				if(reqt_ptr->index == index)  { find = 1; break; }
				reqt_ptr = reqt_ptr->next;
			}
			if(find == 1) break;

			peer_ptr = peer_ptr->next;
		}
		
		if(find == 1) continue;//这个说明该peer已经被请求过了,需要重新进行选择

		break; // 程序若执行到此处,说明已经找到一个复合要求的index
	}
	//如果所有的piece都已经下载完成或者是被请求过了,而此时还有空闲的peer,那么此时便进入终端模式,即是最后阶段的下载
	if(i == pieces_length/20) {
		if(end_mode == 0)  end_mode = 1;
		for(i = 0; i < pieces_length/20; i++) {
			if( get_bit_value(bitmap,i) == 0 )  { index = i; break; }
		}//寻找一个还没有下载成功的piece进行下载
		
		if(i == pieces_length/20) {
			printf("Can not find an index to IP:%s\n",node->ip);
			return -1;
		}
	}

	// 构造piece请求消息
	begin = 0;
	count = 0;
	p = node->Request_piece_head;
	if(p != NULL)
		while(p->next != NULL)  p = p->next;
		
	//在一开始时发出四个request请求
	while(count < 4) {
		// 如果是构造最后一个piece的请求消息
		if(index == last_piece_index) {
			if(count+1 > last_piece_count) 
				break;
			//如果请求的是最后一个slice
			if(begin == (last_piece_count - 1) * 16 * 1024)
				length = last_slice_len;
		}
		//发出请求
		create_request_msg(index,begin,length,node);

		q = (Request_piece *)malloc(sizeof(Request_piece));
		if(q == NULL) { printf("%s:%d error\n",__FILE__,__LINE__); return -1; }
		q->index  = index;
		q->begin  = begin;
		q->length = length;
		q->next   = NULL;
		if(node->Request_piece_head == NULL) 
		{ node->Request_piece_head = q; p = q; }
		else  { p->next = q; p = q; }
		//printf("*** create request index:%-6d begin:%-6x to IP:%s ***\n",
		//	index,q->begin,node->ip);
		begin += 16*1024;
		count++;
	}
	//释放刚才调用的rand_num的内存
	if(rand_num != NULL)  { free(rand_num); rand_num = NULL; }
	return 0;
}

// 以下这个函数实际并未调用,若要使用需先在头文件中声明
int create_req_slice_msg_from_btcache(Peer *node)
{
	// 指针b用于遍历btcache缓冲区
	// 指针b_piece_first指向每个piece第一个slice处
	// slice_count指明一个piece含有多少个slice
	// valid_count指明一个piece中已下载的slice数
	Btcache        *b = btcache_head, *b_piece_first;
	Peer           *p;
	Request_piece  *r;
	int            slice_count = piece_length / (16*1024);
	int            count = 0, num, valid_count;
	int            index = -1, length = 16*1024;
	
	while(b != NULL) {
		if(count%slice_count == 0) {
			num           = slice_count;
			b_piece_first = b;
			valid_count   = 0;
			index         = -1;
			
			// 遍历btcache中一个piece的所有slice
			while(num>0 && b!=NULL) {
				if(b->in_use==1 && b->read_write==1 && b->is_writed==0)
					valid_count++;
				if(index==-1 && b->index!=-1) index = b->index;
				num--;
				count++;
				b = b->next;
			}
			
			// 找到一个未下载完piece
			if(valid_count>0 && valid_count<slice_count) {
				// 检查该piece是否存在于某个peer的请求队列中
				p = peer_head;
				while(p != NULL) {
					r = p->Request_piece_head;
					while(r != NULL) {
						if(r->index==index && index!=-1) break;
						r = r->next;
					}
					if(r != NULL) break;
					p = p->next;
				}
				// 如果该piece没有存在于任何peer的请求队列中,那么就找到了需要的piece
				if(p==NULL && get_bit_value(&(node->bitmap),index)==1) {
					int request_count = 5;
					num = 0;
					// 将r定位到peer最后一个请求消息处
					r = node->Request_piece_head;
					if(r != NULL) {
						while(r->next != NULL) r = r->next;
					}
					while(num<slice_count && request_count>0) {
						if(b_piece_first->in_use == 0) {
							create_request_msg(index,num*length,length,node);
							
							Request_piece *q;
							q = (Request_piece *)malloc(sizeof(Request_piece));
							if(q == NULL) { 
								printf("%s:%d error\n",__FILE__,__LINE__);
								return -1;
							}
							q->index  = index;
							q->begin  = num*length;
							q->length = length;
							q->next   = NULL;
							printf("create request from btcache index:%-6d begin:%-6x\n",
								index,q->begin);
							if(r == NULL) {
								node->Request_piece_head = q;
								r = q;
							} else{
								r->next = q;
								r = q;
							}
							request_count--;
						}
						num++;
						b_piece_first = b_piece_first->next;
					}
					return 0;
				}
			}
		}
	}
	
	return -1;
}