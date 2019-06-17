#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include<time.h>
#include<sys/socket.h>
#include"parse_metafile.h"
#include"bitfield.h"
#include"peer.h"
#include"policy.h"
#include"data.h"
#include"message.h"

/*
 ①：message.c文件会去做：构造消息体，将其存放在对应的peer的out_msg的缓存区中
 ②：客户端自己本身也是一个peer，所以每次在构建自己的消息的时候，都是要将自己的Peer *peer传进去
 ③：每次在处理消息的时候，都会有一个buff==NULL的判断，那是因为在peer结构体中，buff就是用来接收
	别的peer传过来的消息。如果==NULL的话，就是没有分配内存，直接退出该函数即可
 ④：在创建消息的这些函数中，去看bt协议定义的消息的时候就会比较清楚，这个语句为什么这么写
 ⑤：消息处理函数中，有的说的是状态，有的说到是消息类型，尤其是握手消息类型(HANDSHAKE)
	和握手状态(HALFSHAKED)很容易弄混。状态在peer.h中定义。
 ⑥：消息处理函数中，他们的参数的含义：
 *	
*/


#define HANDSHAKE				-2		//握手消息
#define KEEP_ALIVE				-1		//keep_alive消息
#define CHOCKE					0		//chocke消息
#define UNCHOCKE				1		//unchocke消息
#define INTERESTED				2		//interested消息
#define UNINTERESTED			3		//uninterested消息
#define HAVE					4		//have消息
#define BITFIELD				5		//bitfield消息
#define REQUEST					6		//request消息
#define PIECE					7		//piece消息
#define CANCEL					8		//cancel消息
#defin  PORT					9		//port消息

//如果45秒没有给某peer发送消息，则发送Keep_alive消息
#define KEEP_ALIVE_TIME			45		

extern Bitmap *bitmap;					//自己的位图
extern char info_hash[20];				//存放Info_hash
extern char peer_id[20];				//存放peer_id
extern int have_piece_index[64];		//存放下载的piece的index
extern Peer *peer_head;					//指向peer链表


/*
 * 功能：获取i的各个字节，并保存到字符数组c中
 * 说明：
*/

int int_to_char(int i,unsigned char c[4])
{
	c[3]=i%256;
	c[2]=(i-c[3])/256%256;
	c[1]=(i-c[3]-c[2]*256)/256/256%256;
	c[0]=(i-c[3]-c[2]*256-c[1]*256*256)/256/256/256%256;
	
	return 0;
}

/*
 * 功能：将字符数组中的字符转化为一个整型
 * 返回：将转化后的数据返回
*/

int char_to_int(unsigned char c[4])
{
	int i=0;
	i=c[0]*256*256*256+c[1]*256*256+c[2]*256+c[3];
	return i;
}

/*
 * 功能：创建握手消息
 * 返回：成功返回0，失败返回-1
*/

int create_handshake_msg(char *info_hash,char *peer_id,Peer *peer)
{
	int i=0;
	unsigned char keyword[20]="BitTorrent protocol",c=0x00;
	
	/*
	 * 将生成的握手消息存放在peer节点的发送缓冲区（msg_out）中
	 * msg_out[0]~msg_out[msg_len-1]已经存放了其他消息。初始情况下，msg_len值为0
	 */
	unsigned char *buffer=peer->out_msg+peer->msg_len;
	
	//len表示缓冲区还有多少空闲
	int len=MSG_SIZE-peer->msg_len;
	
	//握手消息固定为68字节
	if(len<68)
	{
		return -1;
	}
	
	/*
	 * 握手消息格式:
	 * <pstrlen><pstr><reserved><info_hash><peer_id>
	 */
	
	//<pstrlen>数据设置
	buffer[0]=19;
	
	//<pstr>数据设置，一般就是"BitTorrent protocol"
	for(i=0;i<19;i++)
	{
		buffer[i+1]=keyword[i];
	}
	
	//<reserved>额外补充的协议，一般不用理会
	for(i=0;i<8;i++)
	{
		buffer[i+20]=c;
	}
	
	/*
	 * <info_hash>自己与Tracker交互的info_hash
	 * 在书本上面13.2.5 与Tracker交互这一节中了解到，info_hash是使用Shal算法计算出来的hash值
	*/
	for(i=0;i<20;i++)
	{
		buffer[i+28]=info_hash[i];
	}
	for(i=0;i<20;i++)
	{
		buffer[i+48]=peer_id[i];
	}
	
	peer->msg_len+=68;
	return 0;
}

/*
 * 功能：创建keep_alive消息
 * 说明：keep_alive消息没有消息编号和负载，所以这里的buffer就没有赋值，仅仅初始化了而已
*/

int create_keep_alive_msg(Peer *peer)
{
	unsigned char *buffer=peer->out_msg+peer->msg_len;
	int len=MSG_SIZE-peer->msg_len;
	
	if(len<4)
	{
		return -1;
	}
	memset(buffer,0,4);
	peer->msg_len+=4;
	return 0;
}


/*
 * 功能：创建chock消息
 * 说明：chocke只有长度和消息编号，没有负载
*/

int create_chock_interested_msg(int type,Peer *peer)
{
	unsigned char *buffer=peer->out_msg+peer->msg_len;
	int len=MSG_SIZE-peer->msg_len;
	
	//chocke,unchocke,interested,uninterested消息的长度固定是5
	if(len<5)
	{
		return -1;
	}
	memset(buffer,0,5);
	//因为消息长度为0001，所以，buffer[0~2]没有赋值了，因为memset设置好了
	buffer[3]=1;
	buffer[4]=type;
	
	peer->msg_len+=5;
	return 0;
}


/*
 * 功能：创建have消息
 * 说明：我觉得这个应该之后会被循环调用，因为这个每一次仅仅只能传入一个index。也就是说，每调用一次
		 就将自己有的piece的inedx作为hava消息的一部分发送出去
*/

int create_have_mag(int index,Peer *peer)
{
	unsigned char *buffer=peer->out_msg+peer->msg_len;
	int len=MSG_SIZE-peer->msg_len;
	
	unsigned char c[4];
	
	if(len<9)
	{
		return -1;
	}
	memset(buffer,0,9);
	//消息长度固定为0005
	buffer[3]=5;
	//消息id为4
	buffer[4]=4;
	//index为piece的下标
	int_to_char(index,c);
	//存放已经下载的Pieces的下标
	buffer[5]=c[0];
	buffer[6]=c[1];
	buffer[7]=c[3];
	buffer[8]=c[3];
	
	peer->msg_len+=9;
	return 0;
}

/*
 * 功能：创建bitfield消息
 * 返回：成功返回0，失败返回-1
 * 解释：这个相互交换位图之后，就可以检查是否有自己感兴趣的piece
		 如果有的话，就会发送interested消息
*/

int create_bitfield_msg(char *bitfield,int bitfield_len,Peer *peer)
{
	int i=0;
	unsigned char c[4];
	unsigned char *buffer=peer->out_msg+peer->msg_len;
	int len=MSG_SIZE-peer->msg_len;
	
	if(len<bitfield_len+5)
	{
		printf("%s:%d buffer too small\n",_FILE_,_LINE_);
		return -1;
	}
	//位图消息的负载长度为位图长度+1
	int_to_char(bitfield_len+1,c);
	//bitfield的长度是0001+X，其中X是bitfield的长度
	for(i=0;i<4;i++)
	{
		buffer[i]=c[i];
	}
	buffer[4]=5;
	//这里存放的是位图的信息
	for(i=0;i<bitfield_len;i++)
	{
		buffer[i+5]=bitfield[i];
	}
	peer->msg_len+=bitfield_len+5;
	return 0;
}


/*
 * 功能：创建数据请求消息
 * 返回：成功返回0，失败返回-1
 * 参数说明：index为请求的pieces的下标	begin为piece内的偏移量	length为请求数据的长度
*/

int create_request_msg(int index,int begin,int length,Peer *peer)
{
	int i;
	unsigned char c[4];
	unsigned char *buffer=peer->out_msg+peer->msg_len;
	int len=MSG_SIZE-peer->msg_len;
	
	if(len<17)
	{
		return -1;
	}
	memset(buffer,0,17);
	buffer[3]=13;
	buffer[4]=6;
	int_to_char(index,c);
	for(i=0;i<4;i++)
	{
		buffer[i+5]=c[i];
	}
	int_to_char(begin,c);
	for(i=0;i<4;i++)
	{
		buffer[i+9]=c[i];
	}
	int_to_char(length,c);
	for(i=0;i<4;i++)
	{
		buffer[i+13]=c[i];
	}
	peer->msg_len+=17;
	return 0;
}

/*
 *功能：创建piece消息
 *返回：成功返回0，失败返回-1
 *参数说明：index,begin就是request消息中的index,begin	
			block指向待发送的数据	b_len为block所指向的数据的长度,就是request消息中的length
			
 *解释：这个函数用于，当客户端收到某个peer的request的消息的时候，如果该peer没有被客户端阻塞，而且
		peer请求的sliece客户端有的话，就会把piece消息发送给该peer。
*/

int create_piece_msg(int index,int begin,char *block,int b_len,Peer *peer)
{
	int i;
	unsigned char c[4];
	unsigned char *buffer=peer->out_msg+peer->msg_len;
	int len=MSG_SIZE-peer->msg_len;
	
	if(len<b_len+3)
	{
		printf("%s:%d buffer too small\n",_FIEL_,_LINE_);
		return -1;
	}
	
	int_to_char(b_len+9,c);
	for(i=0;i<4;i++)
	{
		buffer[i]=c[i];
	}
	buffer[4]=7;
	int_to_char(indx,c);
	for(i=0;i<4;i++)
	{
		buffer[i+5]=c[i];
	}
	int_to_char(begin,c);
	for(i=0;i<4;i++)
	{
		buffer[i+9]=c[i];
	}
	for(i=0;i<b_len;i++)
	{
		buffer[i+13]=block[i];
	}
	
	peer->msg_len+=b_len+13;
	return 0;
}


/*
 * 功能：创建cancel消息
 * 返回：成功返回0，失败返回-1
*/

int create_cancel_msg(int index,int begin,int length,Peer *peer)
{
	int i;
	unsigned char c[4];
	unsigned char *buffer=peer->out_msg+peer->msg_len;
	int len=MSG_SIZE-peer->msg_len;
	
	if(len<17)
	{
		return -1;
	}
	memset(buffer,0,17);
	buffer[3]=13;
	buffer[4]=8;
	
	int_to_char(index,c);
	for(i=0;i<4;i++)
	{
		buffer[i+5]=c[i];
	}
	int_to_char(begin,c);
	for(i=0;i<4;i++)
	{
		buffer[i+9]=c[i];
	}
	int_to_char(length,c);
	for(i=0;i<4;i++)
	{
		buffer[i+13]=c[i];
	}
	p->msg_len+=17;
	return 0;
	
}

/*
 * 功能：创建port消息
 * 说明：因为根据BT协议，该消息是给那些以DHT的方式获取Peer地址的应用程序准备的。所以该程序没有使用
*/

int create_port_msg(int port,Peer *peer)
{
	return 0;
}


/*
 *功能：判断缓冲区中是否存放了一条完整的消息
 *返回：成功返回0，失败返回-1.消息都长度由ok_len带回
 *参数说明：buff指向存放消息的缓冲区	len为缓冲区的长度	ok_len用于返回完整消息长度
			buff[0]~buff[len-1]可能存放着一条或者多条完整的消息
 *实现原理：根据消息的类型，然后去知道消息的的长度，然后计算消息长度和缓存区的消息长度是否一致
*/

int is_complete_message(unsigned char *buff,unsigned int len,int *ok_len)
{
	unsigned int i;
	char btkeyword[20];
	unsigned char keep_alive[4]={0x0,0x0,0x0,0x0};
	unsigned char chocke[5]={0x0,0x0,0x0,0x1,0x0};
	unsigned char unchocke[5]={0x0,0x0,0x0,0x1,0x1};
	unsigned char interested[5]={0x0,0x0,0x0,0x1,0x2};
	unsigned char uninterested[5]={0x0,0x0,0x0,0x1,0x3};
	unsigned char have[5]={0x0,0x0,0x0,0x5,0x4};
	unsigned char request[5]={0x0,0x0,0x0,0xd,0x6};
	unsigned char cancel[5]={0x0,0x0,0x0,0xd,0x8};
	unsigned char port[5]={0x0,0x0,0x0,0x3,0x9};
	
	//传参出错的情况
	if(buff==NULL || len<0 || ok_len==NULL)
	{
		return -1;
	}
	
	btkeyword[0]=19;
	memcpy(&btkeyword[1],"BitTorrent protocol",19);
	
	unsigned char c[4];
	unsigned int length;
	for(i=0;i<len;)
	{
		//握手消息，chocke,have等消息的长度都是固定的
		if(i+68<=len && memcpy(&buff[i],btkeyword,20)==0)
		{
			i+=68;
		}
		else if(i+4<=len && memcpy(&buff[i],keep_alive,4)==0)
		{
			i+=4;
		}
		else if(i+5<=len && memcpy(&buff[i],chocke,5)==0)
		{
			i+=5;
		}
		else if(i+5<=len && memcpy(&buff[i],unchocke,5)==0)
		{
			i+=5;
		}
		else if(i+5<=len && memcpy(&buff[i],interested,5)==0)
		{
			i+=5;
		}
		else if(i+5<=len && memcpy(&buff[i],uninterested,5)==0)
		{
			i+=5;
		}
		else if(i+9<=len && memcpy(&buff[i],have,5)==0)
		{
			i+=9;
		}
		else if(i+17<=len && memcpy(&buff[i],request,5)==0)
		{
			i+=17;
		}
		else if(i+17<=len && memcpy(&buff[i],cancel,5)==0)
		{
			i+=17;
		}
		else if(i+7<=len && memcpy(&buff[i],port,5)==0)
		{
			i+=7;
		}
		//bitfield消息长度使变化的
		else if(i+5<=len && buff[i+4]==5)
		{
			c[0]=buff[i];
			c[1]=buff[i+1];
			c[2]=buff[i+2];
			c[3]=buff[i+3];
			length=char_to_int(c);
			//消息长度本身占length4个字节
			if(i+4+length<=len)
			{
				i+=4+length;
			}
			else
			{
				*ok_len=i;
				return -1;
			}
		}
		//piece消息长度也是变化的
		else if(i+5<=len && buff[i+4]==7)
		{
			c[0]=buff[i];
			c[1]=buff[i+1];
			c[2]=buff[i+2];
			c[3]=buff[i+3];
			length=char_to_int(c);
			//消息长度本身占length4个字节
			if(i+4+length<=len)
			{
				i+=4+length;
			}
			else
			{
				*ok_len=i;
				return -1;
			}
		}
		//处理未知的消息类型，
		else
		{
			if(i+4<=len)
			{
				c[0]=buff[i];
				c[1]=buff[i+1];
				c[2]=buff[i+2];
				c[3]=buff[i+3];
				length=char_to_int(c);
				//消息长度本身占length4个字节
				if(i+4+length<=len)
				{
					i+=4+length;
				}
				else
				{
					*ok_len=i;
					return -1;
				}
			}
			//如果不是未知类型，则认为现在是一个不完整的消息
			else
			{
				*ok_len=i;
				return -1;
			}
		}
	}//for循环结束
	
	*ok_len=i；
	return 1;
}


/*
 *功能：处理接收到的一条握手的消息
 *返回：成功返回0，失败返回-1
*/

int process_handshake_msg(Peer *peer,unsigned char *buff,int len)
{
	if(peer==NULL || buff==NULL)
	{
		return -1;
	}
	
	//info_hash不一致则关闭连接
	if(memcmp(info_hash,buff+28,20)!=0)
	{
		peer->state=CLOSING;
		//丢弃发送缓冲区的数据
		discard_send_buffer(peer);
		clear_btcache_before_peer_close(peer);
		close(peer->socket);
		return -1;
	}
	//保存该peer的peer_id
	memcpy(peer->id,buff+48,20);
	(peer->id)[20]='\0';
	//若当前处于Initial状态，则发送握手消息
	if(peer->state==HALFSHAKED)
	{
		peer->state=HANDSHAKED;
	}
	//记录最近收到的该peer消息的时间
	//若一定时间内，未收到来自该peer的任何消息，则连接关闭
	peer->start_timestamp=time(NULL);
	return 0;
}


/*
 * 功能：处理刚刚收到的来自peer的keep_alive的消息
 * 返回：成功返回0，失败返回-1
*/

int process_keep_alive_msg(Peer *peer,unsigned char *buff,int len)
{
	if(peer==NULL || buff==NULL)
	{
		return -1;
	}
	//记录最近收到的该peer消息的时间
	//若一定时间内，未收到来自该peer的任何消息，则连接关闭
	peer->start_timestamp=time(NULL);
	return 0;
}


/*
 * 功能：处理刚刚收到choke的消息
 * 返回：成功返回0，失败返回-1
*/

int process_keep_alive_msg(Peer *peer,unsigned char *buff,int len)
{
	if(peer==NULL || buff==NULL)
	{
		return -1;
	}
	//若原先处于unchoke状态，收到该消息后更新peer中某些变量的值
	if(peer->state!=CLOSING && peer->peer_choking==0)
	{
		peer->peer_choking=1;
		//将最近接收来自该Peer的数据的时间清零
		peer->last_down_timestamp=0;
		//将最近从该peer下载的字节数清零
		peer->down_count=0;
		//将最近从该peer下载数据的速度清零
		peer->down_rate=0;
	}
	//记录最近收到的该peer消息的时间
	//若一定时间内，未收到来自该peer的任何消息，则连接关闭
	peer->start_timestamp=time(NULL);
	return 0;
}


/*
 * 功能：处理收到的choke消息
 * 返回：成功返回0，失败返回-1
*/
int process_choke_msg(Peer *peer,unsigned char *buff,int len)
{
    if(peer==NULL || buff==NULL)  return -1;
	// 若原先处于unchoke状态，收到该消息后更新peer中某些变量的值
    if( peer->state!=CLOSING && peer->peer_choking==0 ) 
	{
        peer->peer_choking = 1;
        peer->last_down_timestamp = 0;      // 将最近接收到来自该peer数据的时间清零
        peer->down_count = 0;      // 将最近从该peer处下载的字节数清零
        peer->down_rate = 0;       // 将最近从该peer下载数据的速度清零
    }
     
     peer->start_timestamp = time(NULL);
     return 0;
}



/*
 * 功能：处理收到的unchoke消息
 * 返回：成功返回0，失败返回-1
*/

int process_unchoke_msg(Peer *peer,unsigned char *buff,int len)
{
	if(peer==NULL || buff==NULL)
	{
		return -1;
	}
	//若原来处于choke状态且于该peer的连接没有关闭
	if(peer->state!=CLOSING && peer->peer_choking==1)
	{
		peer->peer_choking=0;
		//若对该peer感兴趣，则构造request消息，请求peer发送数据
		if(peer->am_interested==1)
		{
			create_req_slice_msg(peer);
		}
		else
		{
			peer->am_interested=is_interested(&(peer->bitmap),bitmap);
			if(peer->am_interested==1)
			{
				create_req_slice_msg(peer);
			}
			else
			{
				printf("Received unchoke but not interested to IP:%s\n",bitmap);
			}
		}
		
		//更新成员值
		peer->last_down_timestamp=0;
		peer->down_count=0;
		peer->down_rate=0;
	}
	
	peer->start_timestamp=time(NULL);
	return 0;
}


/*
 * 功能：处理接收到的interested消息
 * 返回：
*/

int process_interested_msg(Peer *peer,unsigned char *buff,int len)
{
	if(peer==NULL || buff==NULL)
	{
		return -1;
	}
	if(peer->state!=CLOSING && peer->state==DATA)
	{
		peer->peer_interested=is_interested(bitmap,&(peer->bitmap));
		if(peer->peer_interested==0)
		{
			return -1;
		}
		if(peer->am_choking==0)
		{
			create_choke_interested_msg(1,peer);
		}
	}
	peer->start_timestamp=time(NULL);
	return 0;
}


/*
 *功能：处理接收到的uninterested消息
 *返回：
*/

int process_uninterested_msg(Peer *peer,unsigned char *buff,int len)
{
	if(peer==NULL || buff==NULL)
	{
		return -1;
	}
	if(peer->state!=CLOSING && peer->state==DATA)
	{
		peer->peer_interested=0;
		cancel_requested_list(peer);
	}
	
	peer->start_timestamp=time(NULL);
	return 0;
}

/*
 *功能：处理接收到的have消息
 *返回：
*/

int process_have_msg(Peer *peer,unsigned char *buff,int len)
{
	int rand_num;
	unsigned cahr c[4];
	
	if(peer==NULL || buff==NULL)
	{
		return -1;
	}
	
	srand(time(NULL));
	rand_num=rand()%3;
	if(peer->state!=CLOSING && peer->state==DATA)
	{
		c[0]=buff[5];
		c[1]=buff[6];
		c[2]=buff[7];
		c[3]=buff[8];
		
		//更新该peer的位图
		if(peer->bitmap.bitfield!=NULL)
		{
			set_bit_value(&(peer->bitmap),char_to_int(c),1);
		}
		if(peer->am_interested==0)
		{
			peer->am_interested=is_interested(&(peer->bitmap),bitmap);
			//由原来不感兴趣变成感兴趣的时候，发送interested消息
			if(peer->am_interested==1)
			{
				create_chock_interested_msg(2,peer);
			}
			else
			{
				//收到3个have，则发送一个interested消息
				if(rand_num==0)
				{
					create_chock_interested_msg(2,peer);
				}
			}
		}
	}
	
	peer->start_timestamp=time(NULL);
	return 0;
}

/*
 * 功能：处理接收到的cancel消息
 * 返回：
*/

int process_cancel_msg(Peer *peer,unsigned char *buff,int len)
{
	
}

/*
 *功能：处理接收到的位图消息
 *返回：
*/

int process_bitfield_msg(Peer *peer,unsigned char *buff,int len)
{
	unsigned char c[4];
	
	if(peer==NULL || buff==NULL)
	{
		return -1;
	}
	if(peer->state==HANDSHAKED || peer->state==SENDBITFIELD)
	{
		c[0]=buff[0];
		c[1]=buff[1];
		c[2]=buff[2];
		c[3]=buff[3];
		//若原先收到一个位图消息，则清除原来的位图
		if(peer->bitmap.bitfield!=NULL)
		{
			free(peer->bitmap.bitfield);
			peer->bitmap.bitfield=NULL;
		}
		peer->bitmap.valid_length=bitmap->vaild_length;
		//收到一个错误的位图
		if(bitmap->bitfield_length!=char_to_int(c)-1)
		{
			peer->state=CLOSING;
			//丢弃发送缓冲区的数据
			discard_send_buffer(peer);
			clear_btcache_before_peer_close(peer);
			close(peer->socket);
			return -1;
		}
		//生成该peer的位图
		peer->bitmap.bitfield_length=char_to_int(c)-1;
		peer->bitmap.bitfield=(unsigned char *)malloc(sizeof(peer->bitmap.bitfield_length));
		memcpy(peer->bitmap.bitfield,&buff[5],peer->bitmap.bitfield_length);
		
		//如果原来的状态为已握手，收到位图后应该向peer发位图
		if(peer->state==HANDSHAKED)
		{
			create_bitfield_msg(bitmap->bitfield,bitmap->bitfield_length,peer);
			peer->state=DATA;
		}
		
		//如果原来的状态为已发送位图，收到位图可以进入DATA状态准备交互数据
		if(peer->state==SENDBITFIELD)
		{
			peer->state=DATA;
		}
		
		//根据位图判断Peer是否对本客户端感兴趣
		peer->peer_interested=is_interested(bitmap,&(peer->bitmap));
		
		//判断是否对peer感兴趣，若是的话就发送interested消息
		peer->am_interested=is_interested(&(peer->bitmap),bitmap);
		if(peer->am_interested==1)
		{
			create_chock_interested_msg(2,peer);
		}
	}
	
	peer->start_timestamp=time(NULL);
	return 0;
}


/*
 * 功能：处理接收到的request消息
*/

int process_request_msg(Peer *peer,unsigned char *buff,int len)
{
	unsigned char c[4];
	int index,begin,length;
	Request_piece *request_piece,*p;
	
	if(peer==NULL || buff==NULL)
	{
		return -1;
	}
	
	if(peer->am_choking=0 && peer->peer_interested==1)
	{
		c[0]=buff[5];
		c[1]=buff[6];
		c[2]=buff[7];
		c[3]=buff[8];
		index=char_to_int(c);
		c[0]=buff[9];
		c[1]=buff[10];
		c[2]=buff[11];
		c[3]=buff[12];
		begin=char_to_int(c);
		c[0]=buff[13];
		c[1]=buff[14];
		c[2]=buff[15]
		c[3]=buff[16];
		length=char_to_int(c);
		
		//查看请求是否存在，若已经存在，则不进行处理
		p=peer->Request_piece_head;
		while(p!=NULL)
		{
			//证明这个请求已经存在
			if(p->index==index && p->begin==begin && p->length==length)
			{
				break;
			}
			p=p->next;
		}
		//请求已经存在，直接退出函数
		if(p!=NULL)
		{
			return 0;
		}
		
		//将请求加入到请求队列中去,使用的尾插法
		request_piece=(Request_piece *)malloc(sizeof(Request_piece));
		if(request_piece==NULL)
		{
			printf("%s:%d error",_FILE_,_LINE_);
			return 0;
		}
		request_piece->index=index;
		request_piece->begin=begin;
		request_piece->length=length;
		request_piece->next=NULL;
		
		//这里是存放到向peer请求数据的队列中去
		if(peer->Request_piece_head==NULL)
		{
			peer->Request_piece_head=request_piece;
		}
		else
		{
			p=peer->Request_piece_head;
			while(p->next!=NULL)
			{
				p=p->next;
			}
			p->next=request_piece;
		}
		
		//打印提示信息
		pritnf("**** add a request FROM IP：%s indx:%-6d begin:%-6dx ***\n",
											peer->ip,index,begin);
	}
	
	peer->start_timestamp=time(NULL);
	return 0;
}


/*
 * 功能：处理接收到的piece消息
*/

int process_piece_msg(Peer *peer,unsigned char *buff,int len)
{
	unsigned char c[4];
	int index,begin,length;
	Request_piece *p;
	if(peer==NULL || buff==NULL)
	{
		return -1;
	}
	if(peer->peer_choking==0)
	{
		c[0]=buff[0];
		c[1]=buff[1];
		c[2]=buff[2];
		c[3]=buff[3];
		length=char_to_int(c)-9;
		c[0]=buff[5];
		c[1]=buff[6];
		c[2]=buff[7];
		c[3]=buff[8];
		index=char_to_int(c);
		c[0]=buff[9];
		c[1]=buff[10];
		c[2]=buff[11]
		c[3]=buff[12];
		begin=char_to_int(c);
		
		//判断收到的piece是否被请求过
		p=peer->Request_piece_head;
		while(p!=NULL)
		{
			if(p->index==index && p->begin==begin && p->length==length)
			{
				break;
			}
			p=p->next;
		}
		if(p==NULL)
		{
			printf("did not found matched request\n");
			return -1;
		}
		
		//开始计时，并且累计接收到的字节数
		if(peer->last_down_timestamp==0)
		{
			peer->last_down_timestamp=time(NULL);
		}
		peer->down_count+=length;
		peer->down_total+=length;
		//将收到的数据写入缓冲区
		write_slice_to_btcache(index,begin,length,buff+13,length,peer);
		//生成请求数据的消息，要求继续发送数据
		create_req_slice_msg(peer);
	}
	
	peer->start_timestamp=time(NULL);
	return 0;
}



/*
 * 功能：处理接受到的消息
 * 说明：这个函数就是用来调用上面所写的那些消息处理函数的
*/

int parse_response(Peer *peer)
{
	unsigned char btkeyword[20];
	unsigned char keep_alive[4]={0x0,0x0,0x0,0x0};
	int index;
	//接收缓冲区(没有直接操作peer->in_buff缓冲区)
	unsigned char *buff=peer->in_buff;
	
	//接收缓冲区的有效数据长度
	int len=peer->buff_len;
	
	if(buff==NULL || peer==NULL)
	{
		return -1;
	}
	btkeyword[0]=19;
	memcpy(&btkeyword[1],"BitTorrent protocol",19);
	
	//分别处理12种消息(peer的接收缓冲区中可能存放着多条消息,所以，使用了for)
	for(index=0;index<len;)
	{
		if((len-index>=68) && (memcmp(&buff[index],btkeyword,20)==0))
		{
			process_handshake_msg(peer,buff+index,68);
			index+=4;
		}
		else if((len-index>=4) && (memcmp(&buff[index],keep_alive,4)==0))
		{
			process_keep_alive_msg(peer,buff+index,4);
			index+=4;
		}
		else if((len-index>=5) && (buff[index+4]==CHOKE))
		{
			process_choke_msg(peer,buff+index,5);
			index+=5;
		}
		else if((len-index>=5) && (buff[index+4]==UNCHOKE))
		{
			process_unchoke_msg(peer,buff+index,5);
			index+=5;
		}
		else if((len-index>=5) && (buff[index+4]==INTERESTED))
		{
			process_interested_msg(peer,buff+index,5);
			index+=5;
		}
		else if((len-index>=5) && (buff[index+4]==UNINTERESTED))
		{
			process_uninterested_msg(peer,buff+index,5);
			index+=5;
		}
		else if((len-index>=9) && (buff[index+4]==HAVE))
		{
			process_have_msg(peer,buff+index,9);
			index+=9;
		}
		else if((len-index>=5) && (buff[index+4]==BITFIELD))
		{
			process_bitfield_msg(peer,buff+index,peer->bitmap.bitfield_length+5);
			index+=peer->bitmap.bitfield_length+5;
		}
		else if((len-index>=17) && (buff[index+4]==REQUEST))
		{
			process_bitfield_msg(peer,buff+index,17);
			index+=17;
		}
		else if((len-index>=13) && (buff[index+4]==PIECE))
		{
			unsigned char c[4];
			int length;
			
			c[0]=buff[index];
			c[1]=buff[index+1];
			c[2]=buff[index+2];
			c[3]=buff[index+3];
			length=char_to_int(c)-9;
			
			process_piece_msg(peer,buff+index,length+13);
			index+=length+13;
		}
		else if((len-index>=17) &&(buff[index+4]==CANCEL))
		{
			process_cancel_msg(peer,buff+index,17);
			index+=17;
		}
		else if((len-index>=7) && (buff[index+4]==PORT))
		{
			index+=7;
		}
		//如果是未知消息类型，就不予处理
		else
		{
			unsigned char c[4];
			int length;
			if(index+4<=len)
			{
				c[0]=buff[index];
				c[1]=buff[index+1];
				c[2]=buff[index+2];
				c[3]=buff[index+3];
			}
			//如果是一条错误的消息，清空接收缓冲区
			peer->buff_len=0;
			return -1;
		}
	}//for循环结束
	
	//接收缓冲区的消息处理完毕后，清空接收缓冲区
	peer->buff_len=0;
	return 0;
}


/*
 *功能：处理收到的消息
*/

int parse_response_uncomplete_msg(Peer *peer,int ok_len)
{
	char *tmp_buff;
	int tmp_buff_len;
	
	//分配存贮空间，并保存接收缓冲区种不完整的消息
	tmp_buff_len=p->buff_len-ok_len;
	if(tmp_buff_len<=0)
	{
		return -1;
	}
	tmp_buff=(char *)malloc(tmp_buff_len);
	if(tmp_buff==NULL)
	{
		printf("%s:%d error\n",_FILE_,_LINE_);
		return -1;
	}
	memcpy(tmp_buff,p->buff+ok_len,tmp_buff_len);
	//处理接收缓冲区中前面完整的消息
	p->buff_len=ok_len;
	parse_response(p);
	//将不完整消息拷贝到接收缓冲区的开始处
	memcpy(p->in_buff,tmp_buff,tmp_buff_len);
	p->buff_len=tmp_buff_len;
	if(tmp_buff!=NULL)
	{
		free(tmp_buff);
		tmp_buff=NULL;
	}
	return 0;
}


/*
 * 功能：发送一个have消息做准备
 * 说明：在下载玩一个piece的时候，应该向所有的peer发送have消息
*/

int prepare_send_have_msg()
{
	Peer *p=peer_head;
	int i;
	
	if(peer_head==NULL)
	{
		return -1;
	}
	if(have_piece_index[0]==-1)
	{
		return -1;
	}
	
	while(p!=NULL)
	{
		for(i=0;i<64;i++)
		{
			if(have_piece_index[i]!=-1)
			{
				create_have_msg(have_piece_index[i],p);
			}
			else
			{
				break;
			}
		}
		p=p->next;
	}
	for(i=0;i<64;i++)
	{
		if(have_piece_index[i]==-1)
		{
			break;
		}
		else
		{
			have_piece_index[i]=-1;
		}
	}
	
	return 0;
}


/*
 * 功能：主动创建发送给peer的消息，而不是等收到某个消息后才创建响应的消息
*/

int create_response_message(Peer *peer)
{
	if(peer==NULL)
	{
		return -1;
	}
	//处于Intial状态时，主动发送握手消息
	if(peer->state==INITIAL)
	{
		create_handshake_msg(info_hash,peer_id,peer);
		peer->state=HALFSHAKED;
		return 0;
	}
	//处于已握手状态，主动发送位图消息
	if(peer->state==HANDSHAKED)
	{
		//没有位图
		if(bitmap==NULL)
		{
			return -1；
		}
		create_bitfield_msg(bitmap->bitfield,bitmap->bitfield_length,peer);
		//修改状态
		peer->state=SENDBITFIELD;
		return 0;
	}
	//如果没有将该peer阻塞，且peer发送过请求消息，则主动发送Piece消息
	if(peer->am_choking==0 && peer->Requested_piece_head!=NULL)
	{
		Request_piece *req_p=peer->Requested_piece_head;
		int ret=read_slice_for_send(req_p->index,req_p->begin,req_p->length,peer);
		if(ret<0)
		{
			printf("read_slice_for_send error\n");
		
		}
		else
		{
			if(peer->last_up_timestamp==0)
			{
				peer->last_up_timestamp=time(NULL);
			}
			peer->up_count+=req_p->length;
			peer->up_total+=req_p->length;
			peer->Request_piece_head=req_p->next;
			
			free(req_p);
			req_p=NULL;
			return 0;
		}
	}
	
	//如果3分钟没有收到任何消息，则关闭连接
	
	//获取当前时间
	time_t now=time(NULL);
	long intervall=now-peer->state_timestamp;
	if(intervall>180)
	{
		peer->state=CLOSING;
		//丢弃发送缓冲区的数据
		discard_send_buffer(peer);
		//将从该peer处下载的不足一个piece的数据删除
		clear_btcache_before_peer_close(peer);
		close(peer->socker);
	}
	
	//如果45秒没有给某peer发送消息，则发送Keep_alive消息
	long interval2=now - peer->recet_timestamp;
	if(intervall>45 && interval2>45 && peer->msg_len==0)
	{
		create_keep_alive_msg(peer);
	}
	
	return 0;
	
}


/*
 * 功能：即将与peer断开时，丢弃发送缓冲区的内容
*/

void discard_send_buffer(Peer *peer)
{
	struct linger lin;
	int in_len;
	
	lin.l_onoff=1;
	lin.l_linger=0;
	lin_len=sizeof(lin);
	
	//通过设置套接字来丢弃未发送的数据
	if(peer->socket > 0)
	{
		setsockopt(peer->socket,SOL_SOCKET,SO_LINGER,(char*)&lin,lin_len);
	}
}
