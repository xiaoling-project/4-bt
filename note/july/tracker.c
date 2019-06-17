#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <errno.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include "parse_metafile.h"
#include "peer.h"
#include "tracker.h" c

extern unsigned char  info_hash[20];
extern unsigned char  peer_id[20];
extern Announce_list  *announce_list_head;

extern int                 *sock;
extern struct sockaddr_in  *tracker;//存储连接服务器的信息
extern int                 *valid;
extern int                  tracker_count;

extern int                 *peer_sock;
extern struct sockaddr_in  *peer_addr;
extern int                 *peer_valid;
extern int                  peer_count;

Peer_addr  *peer_addr_head = NULL;
//将http请求中的非数字和非字母均进行编码转换
int http_encode(unsigned char *in,int len1,char *out,int len2)
{
	int  i, j;
	char hex_table[16] = "0123456789abcdef"; 
	
	if( (len1 != 20) || (len2 <= 90) )  return -1;
	for(i = 0, j = 0; i < 20; i++, j++) {
		if( isalpha(in[i]) || isdigit(in[i]) )
			out[j] = in[i];
		else { 
			out[j] = '%';
			j++;
			out[j] = hex_table[in[i] >> 4];
			j++;
			out[j] = hex_table[in[i] & 0xf];
		}
	}
	out[j] = '\0';
	
#ifdef DEBUG
	//printf("http encoded:%s\n",out);
#endif
	
	return 0;
}
//通过种子文件所获取服务器地址,获取主机的名字
int get_tracker_name(Announce_list *node,char *name,int len)
{
	int i = 0, j = 0;

	if( (len < 64) || (node == NULL) )  return -1;
	if( memcmp(node->announce,"http://",7) == 0 ) 
		i = i + 7;
	while( (node->announce[i] != '/') && (node->announce[i] != ':') ) {
		name[j] = node->announce[i];
		i++;
		j++;
		if( i == strlen(node->announce) )  break;
	}
	name[j] = '\0';

#ifdef DEBUG
	printf("%s\n",node->announce);
	printf("tracker name:%s\n",name);
#endif

	return 0;
}
//获取服务器所对应的端口,将其存储于port变量中
int get_tracker_port(Announce_list *node,unsigned short *port)
{
	int i = 0;

	if( (node == NULL) || (port == NULL) )  return -1;
	if( memcmp(node->announce,"http://",7) == 0 )  i = i + 7;
	*port = 0;
	while( i < strlen(node->announce) ) {
		if( node->announce[i] != ':')   { i++; continue; }

		i++;  // skip ':'
		while( isdigit(node->announce[i]) ) { 
			*port =  *port * 10 + (node->announce[i] - '0');
			i++;
		}
		break;
	}
	if(*port == 0)  *port = 80;

#ifdef DEBUG
	printf("tracker port:%d\n",*port);
#endif

	return 0;
}
//创建请求,会将当前的下载进度告知服务器,这里同时设置了监听的端口,在该程序中并没有实现监听该端口
下面的文章可以解答一些疑惑,主要是socket编程的一些疑惑
//https://blog.csdn.net/whuslei/article/details/6667471
//https://blog.csdn.net/zhaozekun2015/article/details/81697624
//https://blog.csdn.net/russell_tao/article/details/9111769
//https://blog.csdn.net/petershina/article/details/7946615
int create_request(char *request,int len,Announce_list *node,
				   unsigned short port,long long down,long long up,
				   long long left,int numwant)
{
	char           encoded_info_hash[100];
	char           encoded_peer_id[100];
	int            key;
	char           tracker_name[128];
	unsigned short tracker_port;

	http_encode(info_hash,20,encoded_info_hash,100);
	http_encode(peer_id,20,encoded_peer_id,100);

	srand(time(NULL));
	key = rand() / 10000;

	get_tracker_name(node,tracker_name,128);//获取主机名字
	get_tracker_port(node,&tracker_port);//获取端口号

	sprintf(request,
	"GET /announce?info_hash=%s&peer_id=%s&port=%u"
	"&uploaded=%lld&downloaded=%lld&left=%lld"
	"&event=started&key=%d&compact=1&numwant=%d HTTP/1.0\r\n"
	"Host: %s\r\nUser-Agent: Bittorrent\r\nAccept: */*\r\n"
	"Accept-Encoding: gzip\r\nConnection: closed\r\n\r\n",
	encoded_info_hash,encoded_peer_id,port,up,down,left,
	key,numwant,tracker_name);

#ifdef DEBUG
	printf("request:%s\n",request);
#endif

	return 0;
}
/**
buffer存放Tracker的回应信息,len为buffer所指的数组的长度,total_length用于存放Tracker返回数据的长度
返回的数据是第一种类型,为什么直接返回0呢
total_length的含义:
*/
int get_response_type(char *buffer,int len,int *total_length)
{
	int i, content_length = 0;

	for(i = 0; i < len-7; i++) {
		if(memcmp(&buffer[i],"5:peers",7) == 0) { 
			i = i+7;
			break; 
		}
	}//找到关键字5:peers的位置
	if(i == len-7)        return -1;  // 返回的消息不含"5:peers"关键字
	if(buffer[i] != 'l')  return 0;   // 返回的消息的类型为第一种

	//返回的第二种信息的类型
	*total_length = 0;
	for(i = 0; i < len-16; i++) {
		if(memcmp(&buffer[i],"Content-Length: ",16) == 0) {
			i = i+16;
			break; 
		}
	}//找到关键字Content-Length的位置
	if(i != len-16) {
		while(isdigit(buffer[i])) {
			content_length = content_length * 10 + (buffer[i] - '0');
			i++;
		}//记录Content-Length记录的数据的长度
		for(i = 0; i < len-4; i++) {
			if(memcmp(&buffer[i],"\r\n\r\n",4) == 0)  { i = i+4; break; }
		}//寻找关键字"\r\n\r\n的关键字的位置,这个关键字又是什么
		if(i != len-4)  *total_length = content_length + i;//为什么这个total_length的值就是这个呢?
	}

	if(*total_length == 0)  return -1;
	else return 1;
}
//准备连接服务器,首先分配相对应的内存,接着获取连接所需要的服务器ip和端口号,接着通过connect与服务器实现连接
int prepare_connect_tracker(int *max_sockfd)
{
	int             i, flags, ret, count = 0;
	struct hostent  *ht;
	Announce_list   *p = announce_list_head;
	//通过遍历Announce_list列表,该列表是解析种子文件所获取的服务器地址的列表
	while(p != NULL)  { count++; p = p->next; }//计算服务器的个数
	tracker_count = count; //将该值赋予tracker_count,那么经过prepare_connect_tracker后该值便可直接使用
	sock = (int *)malloc(count * sizeof(int));
	if(sock == NULL)  goto OUT;
	tracker = (struct sockaddr_in *)malloc(count * sizeof(struct sockaddr_in));//这里有什么必要要分配这么多的结构体呢
	if(tracker == NULL)  goto OUT;
	valid = (int *)malloc(count * sizeof(int));
	if(valid == NULL)  goto OUT;
	
	p = announce_list_head;
	//为所有的服务器分配一个套接字,并且获取实现连接所需要的数据,主机ip和相对应的端口号
	for(i = 0; i < count; i++) {
		char            tracker_name[128];
		unsigned short  tracker_port = 0;
		
		sock[i] = socket(AF_INET,SOCK_STREAM,0);//为每一个服务器分配一个通信端口
		if(sock < 0) {
			printf("%s:%d socket create failed\n",__FILE__,__LINE__);
			valid[i] = 0;
			p = p->next;
			continue;
		}

		get_tracker_name(p,tracker_name,128);
		get_tracker_port(p,&tracker_port);
		
		// 从主机名获取IP地址
		ht = gethostbyname(tracker_name);
		if(ht == NULL) {
			printf("gethostbyname failed:%s\n",hstrerror(h_errno)); 
			valid[i] = 0;
		} else {
			memset(&tracker[i], 0, sizeof(struct sockaddr_in));
			memcpy(&tracker[i].sin_addr.s_addr, ht->h_addr_list[0], 4);
			//将tracker_port转化为网络字节序列
			tracker[i].sin_port = htons(tracker_port);
			tracker[i].sin_family = AF_INET;
			valid[i] = -1;
		}
		
		p = p->next;
	}
	//通过connect与服务器实现连接,实现连接之后,便可通过套接字直接进行读写
	//注意:当ret < 0 && errno == EINPROGRESS的情况下,连接不一定失败
	//具体用法于torrent.c的download_upload_with_peers()函数中
	for(i = 0; i < tracker_count; i++) {
		if(valid[i] != 0) {
			if(sock[i] > *max_sockfd) *max_sockfd = sock[i];
			// 设置套接字为非阻塞
			flags = fcntl(sock[i],F_GETFL,0);
			fcntl(sock[i],F_SETFL,flags|O_NONBLOCK);
			// 连接tracker
			ret = connect(sock[i],(struct sockaddr *)&tracker[i],
				          sizeof(struct sockaddr));
			if(ret < 0 && errno != EINPROGRESS)  valid[i] = 0;	
			// 如果返回0，说明连接已经建立
			if(ret == 0)  valid[i] = 1;  
		}
	}

	return 0;

OUT:
	if(sock != NULL)    free(sock);
	if(tracker != NULL) free(tracker);
	if(valid != NULL)   free(valid);
	return -1;
}
//准备连接peer，与上面连接tracker服务器唯一不同的是不用通过主机名获取ip地址
int prepare_connect_peer(int *max_sockfd)
{
	int       i, flags, ret, count = 0;
	Peer_addr *p;
	
	p = peer_addr_head;//使用Peer_addr中的信息与Peer进行连接
	while(p != 0)  { count++; p = p->next; }

	peer_count = count;//该变量与tracker_count相似
	peer_sock = (int *)malloc(count*sizeof(int));
	if(peer_sock == NULL)  goto OUT;
	peer_addr = (struct sockaddr_in *)malloc(count*sizeof(struct sockaddr_in));
	if(peer_addr == NULL)  goto OUT;
	peer_valid = (int *)malloc(count*sizeof(int));
	if(peer_valid == NULL) goto OUT;
	
	p = peer_addr_head;  // 此处p重新赋值,为每个Peer分配套接字
	for(i = 0; i < count && p != NULL; i++) {
		peer_sock[i] = socket(AF_INET,SOCK_STREAM,0);
		if(peer_sock[i] < 0) { 
			printf("%s:%d socket create failed\n",__FILE__,__LINE__);
			valid[i] = 0;
			p = p->next;
			continue; 
		}
		
		memset(&peer_addr[i], 0, sizeof(struct sockaddr_in));
		peer_addr[i].sin_addr.s_addr = inet_addr(p->ip);//将ip地址转化为网络字节序列
		peer_addr[i].sin_port = htons(p->port);//将port端口号转化为网络字节序列
		peer_addr[i].sin_family = AF_INET;
		peer_valid[i] = -1;
		
		p = p->next;
	}
	count = i;
	
	for(i = 0; i < count; i++) {
		if(peer_sock[i] > *max_sockfd) *max_sockfd = peer_sock[i];
		// 设置套接字为非阻塞
		flags = fcntl(peer_sock[i],F_GETFL,0);
		fcntl(peer_sock[i],F_SETFL,flags|O_NONBLOCK);
		// 连接peer
		ret = connect(peer_sock[i],(struct sockaddr *)&peer_addr[i],
			          sizeof(struct sockaddr));
		if(ret < 0 && errno != EINPROGRESS)  peer_valid[i] = 0;
		// 如果返回0，说明连接已经建立
		if(ret == 0)  peer_valid[i] = 1;
	}
	
	free_peer_addr_head();
	return 0;

OUT:
	if(peer_sock  != NULL)  free(peer_sock);
	if(peer_addr  != NULL)  free(peer_addr);
	if(peer_valid != NULL)  free(peer_valid);
	return -1;
}
//处理服务器返回的第一种消息类型
int parse_tracker_response1(char *buffer,int ret,char *redirection,int len)
{
	int           i, j, count = 0;
	unsigned char c[4];
	Peer_addr     *node, *p;

	//寻找location的地址,获取重定位的地址
	for(i = 0; i < ret - 10; i++) {
		//新增一个服务器的地址
		if(memcmp(&buffer[i],"Location: ",10) == 0) { 
			i = i + 10;
			j = 0;
			while(buffer[i]!='?' && i<ret && j<len) {
				redirection[j] = buffer[i];
				i++;
				j++;
			}
			redirection[j] = '\0';
			return 1;
		}
	}

	// 获取返回的peer数,关键词"5:peers"之后为各个Peer的IP和端口
	for(i = 0; i < ret - 7; i++) {
		if(memcmp(&buffer[i],"5:peers",7) == 0) { i = i + 7; break; }
	}
	if(i == ret - 7	) { 
		printf("%s:%d can not find keyword 5:peers \n",__FILE__,__LINE__);
		return -1; 
	}
	
	while( isdigit(buffer[i]) ) {
		count = count * 10 + (buffer[i] - '0');
		i++;
	}
	i++;  // 跳过":"
	
	//ip和端口号占据6个字节
	count = (ret - i) / 6;
		
	// 将每个peer的IP和端口保存到peer_addr_head指向的链表中
	for(; count != 0; count--) {
		node = (Peer_addr*)malloc(sizeof(Peer_addr));
		c[0] = buffer[i];   c[1] = buffer[i+1]; 
		c[2] = buffer[i+2]; c[3] = buffer[i+3];
		//将
		sprintf(node->ip,"%u.%u.%u.%u",c[0],c[1],c[2],c[3]);
		i += 4;
		node->port = ntohs(*(unsigned short*)&buffer[i]);//ntohs()作用是将一个16位数由网络字节顺序转换为主机字节顺序,故会自动占用两个字节的内容
		i += 2;
		node->next = NULL;
	
		// 判断当前peer是否已经存在于链表中
		p = peer_addr_head;
		while(p != NULL) {
			if( memcmp(node->ip,p->ip,strlen(node->ip)) == 0 ) { 
				free(node); 
				break;
			}
			p = p->next;
		}
			
		// 将当前结点添加到链表中
		if(p == NULL) {
			if(peer_addr_head == NULL)
				peer_addr_head = node;
			else {
				p = peer_addr_head;
				while(p->next != NULL) p = p->next;
				p->next = node;
			}
		}
	}
		
#ifdef DEBUG
		count = 0;
		p = peer_addr_head;
		while(p != NULL) {
			printf("+++ connecting peer %-16s:%-5d +++ \n",p->ip,p->port);
			p = p->next;
			count++;
		}
		printf("peer count is :%d \n",count);
#endif

		return 0;
}
//处理服务器发送过来的第二种类型的信息
int parse_tracker_response2(char *buffer,int ret)
{
	int        i, ip_len, port;
	Peer_addr  *node = NULL, *p = peer_addr_head;

	//为什么是这样子的呢?为什么需要将这个peer_addr_head清空呢
	if(peer_addr_head != NULL) {
		printf("Must free peer_addr_head\n");
		return -1;
	}
	
	for(i = 0; i < ret; i++) {
		if(memcmp(&buffer[i],"2:ip",4) == 0) {
			i += 4;
			ip_len = 0;
			while(isdigit(buffer[i])) {
				ip_len = ip_len * 10 + (buffer[i] - '0');
				i++;
			}
			i++;  // skip ":"
			node = (Peer_addr*)malloc(sizeof(Peer_addr));
			if(node == NULL) { 
				printf("%s:%d error",__FILE__,__LINE__); 
				continue;
			}
			memcpy(node->ip,&buffer[i],ip_len);
			(node->ip)[ip_len] = '\0';
			node->next = NULL;
		}
		if(memcmp(&buffer[i],"4:port",6) == 0) {
			i += 6;
			i++;  // skip "i"
			port = 0;
			while(isdigit(buffer[i])) {
				port = port * 10 + (buffer[i] - '0');
				i++;
			}
			if(node != NULL)  node->port = port;
			else continue;
			
			printf("+++ add a peer %-16s:%-5d +++ \n",node->ip,node->port);
			
			if(p == peer_addr_head) { peer_addr_head = node; p = node; }
			else p->next = node;
			node = NULL;
		}
	}
	
	return 0;
}
/**
在torrent.c中当peer连接成功时,便会调用该方法
在torrent.c的401行处,当没有马上连接成功的peer连接成功之后便会调用该方法
在Peer链表后面新增一个Peer节点
*/
int add_peer_node_to_peerlist(int *sock,struct sockaddr_in saptr)
{
	Peer *node;
	
	node = add_peer_node();
	if(node == NULL)  return -1;
	
	node->socket = *sock;
	node->port   = ntohs(saptr.sin_port);
	node->state  = INITIAL;
	strcpy(node->ip,inet_ntoa(saptr.sin_addr));
	node->start_timestamp = time(NULL);

	return 0;
}
//释放Peer_addr链表所占据的内存,当与Peer尝试连接之后,即可释放该链表的内容
void free_peer_addr_head()
{
	Peer_addr *p = peer_addr_head;
    while(p != NULL) {
		p = p->next;
		free(peer_addr_head);
		peer_addr_head = p;
    }
	peer_addr_head = NULL;
}
