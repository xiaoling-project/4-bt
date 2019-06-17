#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netdb.h>
#include <errno.h>
#include "torrent.h"
#include "message.h"
#include "tracker.h"
#include "peer.h"
#include "policy.h"
#include "data.h"
#include "bitfield.h"
#include "parse_metafile.h"

// 接收缓冲区中的数据达到threshold时,需要立即进行处理,否则缓冲区可能会溢出
// 18*1024即18K是接收缓冲区的大小,1500是以太网等局域网一个数据包的最大长度
#define  threshold (18*1024-1500)

extern Announce_list *announce_list_head;//该变量用于连接tracker服务器
extern char          *file_name; 
extern long long     file_length;
extern int           piece_length;
extern char          *pieces;
extern int           pieces_length;
extern Peer          *peer_head;

extern long long     total_down,total_up;
extern float         total_down_rate,total_up_rate;
extern int           total_peers;
extern int           download_piece_num;
extern Peer_addr     *peer_addr_head; //存储了从服务器返回的所有Peer的ip地址和端口号

int                  *sock    = NULL;  //存储与tracker连接时的套接字
struct sockaddr_in   *tracker = NULL; //存储服务器的ip地址和端口号,建立连接时强制转换为sockaddr类型的变量
int                  *valid   = NULL; //此时的连接状态
int                  tracker_count  = 0;

int                  response_len   = 0;//服务器
int                  response_index = 0;
char                 *tracker_response = NULL;

int                  *peer_sock  = NULL; //存储与Peer连接的套接字
struct sockaddr_in   *peer_addr  = NULL;
int                  *peer_valid = NULL;
int                  peer_count  = 0;

// 负责与所有Peer收发数据、交换消息
int download_upload_with_peers()
{
	Peer            *p;
	int             ret, max_sockfd, i;

	int             connect_tracker, connecting_tracker;
	int             connect_peer, connecting_peer;
	time_t          last_time[3], now_time;

	time_t          start_connect_tracker;  // 开始连接tracker的时间
	time_t          start_connect_peer;     // 开始连接peer的时间
	fd_set          rset, wset;  // select要监视的描述符集合
	struct timeval  tmval;       // select函数的超时时间

	
	now_time     = time(NULL);
	last_time[0] = now_time;   // 上一次选择非阻塞peer的时间
	last_time[1] = now_time;   // 上一次选择优化非阻塞peer的时间
	last_time[2] = now_time;   // 上一次连接tracker服务器的时间
	/**
	下面是用来连接服务器的
	**/
	connect_tracker    = 1;    // 是否需要连接tracker
	connecting_tracker = 0;    // 是否正在连接tracker
	connect_peer       = 0;    // 是否需要连接peer 
	connecting_peer    = 0;    // 是否正在连接peer

	for(;;) {
		max_sockfd = 0;
		now_time = time(NULL);
		
		// 每隔10秒重新选择非阻塞peer
		if(now_time-last_time[0] >= 10) {
			//为什么要download_piece_num > 0呢
			if(download_piece_num > 0 && peer_head != NULL) {
				compute_rate();         // 计算各个peer的下载、上传速度
				select_unchoke_peer();  // 选择非阻塞的peer
				last_time[0] = now_time;
			}
		}
		
		// 每隔30秒重新选择优化非阻塞peer
		if(now_time-last_time[1] >= 30) {
			if(download_piece_num > 0 && peer_head != NULL) {
				select_optunchoke_peer();//随机选择非阻塞的peer
				last_time[1] = now_time;
			}
		}
		
		// 每隔5分钟连接一次tracker,如果当前peer数为0也连接tracker
		if((now_time-last_time[2] >= 300 || (connect_tracker == 1 && 
			connecting_tracker != 1 && connect_peer != 1 && connecting_peer != 1) {
			// 由tracker的URL获取tracker的IP地址和端口号
			ret = prepare_connect_tracker(&max_sockfd);
			if(ret < 0)  { printf("prepare_connect_tracker\n"); return -1; }

			connect_tracker       = 0;
			connecting_tracker    = 1;//获取tracker服务器的信息之后便真正开始连接服务器
			start_connect_tracker = now_time;
		}
		
		/**开始连接服务器，顺序依次是connect_tracker,connecting_tracker,connect_peer,connecting_peer
		connecting_tracker结束之后,如果Peer_addr链表还是为空,则需要重新连接服务器,因为此时并没有Peer可以进行连接
		如果Peer结构体链表的内容不为空，那么将无需重新连接tracker服务器，否则的话需要重新连接该服务器
		*/
		if(connect_peer == 1) {
			// 创建套接字,向peer发出连接请求
			ret = prepare_connect_peer(&max_sockfd);
			if(ret < 0)  { printf("prepare_connect_peer\n"); return -1; }

			connect_peer       = 0;
			connecting_peer    = 1;
			start_connect_peer = now_time;
		}
		//将两个文件描述符字清零,接下来将会对放入文件描述符字的sock进行监听，可能同时对服务器或者的正在连接的peer或者正在  
		//通信的peer进行监听
		FD_ZERO(&rset);
		FD_ZERO(&wset);//将文件描述符字清零,将两个描述符字放于select之后,当select函数退出的时候，会对每个位根据它此时的状态进行设置。
					   //因此，如果在循环中使用select,这个文件描述符集在调用它前每次都需要进行初始化

		// 将连接tracker的socket加入到待监视的集合中
		if(connecting_tracker == 1) {
			int flag = 1;
			//如果连接tracker时间超过10秒,则终止连接tracker
			if(now_time-start_connect_tracker > 10) {
				for(i = 0; i < tracker_count; i++)
					if(valid[i] != 0)  close(sock[i]);
			} else {
				for(i = 0; i < tracker_count; i++) {
					if(valid[i] != 0 && sock[i] > max_sockfd)
						max_sockfd = sock[i];  // valid[i]值为-1、1、2时要监视
					if(valid[i] == -1) { 
						FD_SET(sock[i],&rset); //当valid[i] == -1时，连接不一定失败，只是没有马上完成连接,故需要进行相应的判断
						FD_SET(sock[i],&wset);
						if(flag == 1)  flag = 0;
					} else if(valid[i] == 1) {
						FD_SET(sock[i],&wset);
						if(flag == 1)  flag = 0;
					} else if(valid[i] == 2) {
						FD_SET(sock[i],&rset);
						if(flag == 1)  flag = 0;
					}
				}
			}
			// 如果flag = 1,说明连接tracker结束,开始与peer建立连接
			if(flag == 1) {
				connecting_tracker = 0; //连接服务器正式结束
				last_time[2] = now_time;
				clear_connect_tracker();
				clear_tracker_response();
				//当连接tracker没有获取到Peer的信息时，即无法继续通过connect_peer来连接Peer，将重新连接tracker服务器
				if(peer_addr_head != NULL) { 
					connect_tracker = 0;
					connect_peer    = 1;
				} else { 
					connect_tracker = 1;  
				}
				continue;
			}
		}

		//将还没有成功连接的peer放入到描述符字中进行监听
		if(connecting_peer == 1) {
			int flag = 1;
			// 如果连接peer超过10秒,则终止连接peer		
			if(now_time-start_connect_peer > 10) {
				for(i = 0; i < peer_count; i++) {
					if(peer_valid[i] != 1) close(peer_sock[i]);  //超过10s,valid[i]不为1说明连接失败
				}
			} else {
				for(i = 0; i < peer_count; i++) {
					if(peer_valid[i] == -1) { //这里将valid[i] == -1的值放入进行监听，是为了将这些连接进行判断，是否成功连接
												//而其他已经连接成功的将通过链表循环放入文件描述符字中
						if(peer_sock[i] > max_sockfd)
							max_sockfd = peer_sock[i];
						FD_SET(peer_sock[i],&rset); 
						FD_SET(peer_sock[i],&wset);
						if(flag == 1)  flag = 0;
					}
				}
			}
			//当flag = 1时,连接Peer结束
			if(flag == 1) {
				connecting_peer = 0;
				clear_connect_peer();
				if(peer_head == NULL)  connect_tracker = 1;  //结束连接时如果Peer链表为空，重新连接服务器获取内容
				continue;
			}
		}
		
		//每隔3分钟会连接一次服务器,在3分钟之内,如果出现所有的连接都被关闭了,那么将马上重新连接服务器
	   //这里的connect_tracker置为1,便是用来判断是否需要重新连接服务器
		connect_tracker = 1; 
		
		//在这个程序中,我们并没有实现监听某个端口,我们此时需要假设已经实现了
		//在这过程中,如果有新的peer与我们建立连接,那么此时就需要给该连接分配新的套接字,而且在这个过程中,本来连接的套接字会断开连接
	    //所以每次都会将新的套接字放入描述符字中
		p = peer_head;
		while(p != NULL) {
			if(p->state != CLOSING && p->socket > 0) {
				FD_SET(p->socket,&rset); 
				FD_SET(p->socket,&wset); 
				if(p->socket > max_sockfd)  max_sockfd = p->socket; 
				connect_tracker = 0;
			}
			p = p->next;
		}
		//判断是否要连接服务器
		if(peer_head==NULL && (connecting_tracker==1 || connecting_peer==1)) 
			connect_tracker = 0; //现在如果既没有在连接peer也没有在连接tracker服务器并且Peer链表也为空的话，就需要连接服务器了
		if(connect_tracker == 1)  continue;

		tmval.tv_sec  = 2;
		tmval.tv_usec = 0;//设置等待时间
		ret = select(max_sockfd+1,&rset,&wset,NULL,&tmval);//select的第一个参数nfds，应该设置为这三个文件描述符集的最大的文件描述符加1
	    //出现错误	
	   if(ret < 0)  { 
			printf("%s:%d  error\n",__FILE__,__LINE__);
			perror("select error"); 
			break;
		}
		//在这个时间内没有任何事情发生，即所有的均不可读或写
		if(ret == 0)  continue;

		// 添加have消息,have消息要发送给每一个peer,放在此处是为了方便处理
		prepare_send_have_msg();

		// 对于每个peer,接收或发送消息,接收一条完整的消息就进行处理
		//如果当前缓冲区
		p = peer_head;
		while(p != NULL) {
			if( p->state != CLOSING && FD_ISSET(p->socket,&rset) ) {
				ret = recv(p->socket,p->in_buff+p->buff_len,MSG_SIZE-p->buff_len,0);//从该套接字接收信息，存放到p->in_buff中
				if(ret <= 0) {  // recv返回0说明对方关闭连接,返回负数说明出错
					//if(ret < 0)  perror("recv error"); 
					p->state = CLOSING; //当连接为CLOSING，程序将会将该peer从链表中删除，将不再接收或者发送信息
					
					// 通过设置套接字选项来丢弃发送缓冲区中的数据
					discard_send_buffer(p);
					clear_btcache_before_peer_close(p);
					close(p->socket); 
				} else {
					//处理完整或不完整的信息时,都会对p->buff_len的值进行设置
					int completed, ok_len;
					p->buff_len += ret;
					completed = is_complete_message(p->in_buff,p->buff_len,&ok_len);//判断在p->in_buff的数据是否为一条完整的信息
					
					if (completed == 1)  parse_response(p);
					//缓冲区的信息超过threshold,马上进行处理
					else if(p->buff_len >= threshold) {
						parse_response_uncomplete_msg(p,ok_len);//处理不完整的消息
					} else {
						p->start_timestamp = time(NULL);//记录最近一次接收peer消息的时间
					}
				}
			}
			//此时并没有关闭连接,并且此时是可写的状态
			if(p->state != CLOSING && FD_ISSET(p->socket,&wset) ) {
				//当没有要发送给peer的消息时,主动根据此时的状态生成信息
				if( p->msg_copy_len == 0) {
					// 创建待发送的消息,并把生成的消息拷贝到发送缓冲区并发送
					create_response_message(p);
					if(p->msg_len > 0) {
						memcpy(p->out_msg_copy,p->out_msg,p->msg_len);//将要发送的信息复制到缓冲区中
						p->msg_copy_len = p->msg_len;
						p->msg_len = 0; // 消息长度赋0,使p->out_msg所存消息清空
					}	
				}	
				
				if(p->msg_copy_len > 1024) {				
				//发送长度为1024的信息给peer,一般都是可以全部发送过去的,除非是网络非常差，数据无法发送到接收缓冲区,才会造成socket缓冲区无法全部装下发送的信息
					send(p->socket,p->out_msg_copy+p->msg_copy_index,1024,0);
					p->msg_copy_len   = p->msg_copy_len - 1024;//记录此时msg_copy剩余的数据的长度
					p->msg_copy_index = p->msg_copy_index + 1024;//记录下次发送数据的起始坐标
					p->recet_timestamp = time(NULL); // 记录最近一次发送消息给peer的时间
				}
				else if(p->msg_copy_len <= 1024 && p->msg_copy_len > 0 ) {
					send(p->socket,p->out_msg_copy+p->msg_copy_index,p->msg_copy_len,0);//将缓冲区的信息全部发送给相对应的peer
					p->msg_copy_len   = 0;
					p->msg_copy_index = 0;
					p->recet_timestamp = time(NULL); // 记录最近一次发送消息给peer的时间
				}
			}
			p = p->next;
		}
		/**
		这里是对正在连接的tracker的通信处理
		*/
		if(connecting_tracker == 1) {
			for(i = 0; i < tracker_count; i++) {
				if(valid[i] == -1) {
					// 如果某个套接字可写且未发生错误,说明连接建立成功
					//如果error返回的是EINPPROGRESS的话，就代表这个线程没有被阻塞
					//但是呢，这个连接也不能被马上完成。
					//可能是使用poll或者select来完成连接。调用select后,如果这个套接字是可读的话
					//接下来便调用getsockopt去读SO_ERROR来看待这个连接是否功，如果SO_ERROR = 0 的话就代表成功
					//不然的话就是失败了
					if(FD_ISSET(sock[i],&wset)) {
						int error, len;
						error = 0;
						len = sizeof(error);
						if(getsockopt(sock[i],SOL_SOCKET,SO_ERROR,&error,&len) < 0) {
							valid[i] = 0; 
							close(sock[i]);
						}
						if(error) { valid[i] = 0; close(sock[i]); } 
						else { valid[i] = 1; }
					}
				}
				//实现监听该端口,便能成功接收connect发送过来的请求,并且可以成功与发送connect请求的Peer进行通信
				//在这个程序中并没有实现监听该端口
				if(valid[i] == 1 && FD_ISSET(sock[i],&wset) ) {
					char  request[1024];
					unsigned short listen_port = 33550; // 本程序并未实现监听某端口
					unsigned long  down = total_down;
					unsigned long  up = total_up;
					unsigned long  left;
					left = (pieces_length/20-download_piece_num)*piece_length;
					
					int num = i;
					Announce_list *anouce = announce_list_head;
					while(num > 0) {
						anouce = anouce->next;
						num--;
					}
					create_request(request,1024,anouce,listen_port,down,up,left,200);//创建请求信息
					write(sock[i], request, strlen(request));//向服务器发送信息,成功发送的话，在非阻塞模式下返回值为1到strlen(request);
					valid[i] = 2;
				}
				//这部分的内容不是很理解,不明白为什么要这样子解析服务器发送过来的信息
				if(valid[i] == 2 && FD_ISSET(sock[i],&rset)) {
					char  buffer[2048];
					char  redirection[128];  //用于存储服务器重定向的url
					ret = read(sock[i], buffer, sizeof(buffer));//接收服务器发送过来的信息,成功读取，返回数据为读取的字节数
					//根据这个是否可以猜测服务器响应信息的长度不超过2048,故可以将服务器发送过来的信息全部接收,
					//如果response_len=0,分配内存,当分配的内存所能读取的数据满了之后,便将信息进行解析,并且都是第二种类型的解析
					if(ret > 0)  {
						if(response_len != 0) {
							memcpy(tracker_response+response_index,buffer,ret);
							response_index += ret;
							if(response_index == response_len) {
								parse_tracker_response2(tracker_response,response_len);
								clear_tracker_response();//将tracker_response所占用内存清零,response_len = 0,response_index = 0
								valid[i] = 0;
								close(sock[i]);//那为什么只关闭这个连接呢?
								last_time[2] = time(NULL);//设置最后一次与服务器连接的时间
							}
						}
					    /**
						接收的数据是第一种类型的数据,将其放到缓冲区里面
						*/
						else if(get_response_type(buffer,ret,&response_len) == 1) {
							tracker_response = (char *)malloc(response_len);//分配内存存储服务器发送过来的信息
							if(tracker_response == NULL)
							{
								printf("malloc error\n");
								return -1;
							}
							memcpy(tracker_response,buffer,ret);
							response_index = ret;
						} else {
							//进行第一种类型信息的解析,如果进行了第一种解析,那么便不久不能进行第二次解析了吗
							ret = parse_tracker_response1(buffer,ret,redirection,128);
							if(ret == 1) add_an_announce(redirection);//添加一个新的url
							valid[i] = 0;
							close(sock[i]);//关闭这个连接
							last_time[2] = time(NULL);
						}
					}
				} 
			}
		}
		//将还没有成功连接的peer进行检测,步骤跟上面的连接tracker时的相同
		if(connecting_peer == 1) {
			for(i = 0; i < peer_count; i++) {
				if(peer_valid[i] == -1 && FD_ISSET(peer_sock[i],&wset)) {
					int error, len;
					error = 0;
					len = sizeof(error);
					if(getsockopt(peer_sock[i],SOL_SOCKET,SO_ERROR,&error,&len) < 0) {
						peer_valid[i] = 0;
					}
					if(error == 0) {
						peer_valid[i] = 1;
						add_peer_node_to_peerlist(&peer_sock[i],peer_addr[i]);//连接成功直接将其放到Peer链表中去
					}
				} // end if
			} // end for
		} // end if
		
		// 对处于CLOSING状态的peer,将其从peer队列中删除
		// 此处应当非常小心,处理不当非常容易使程序崩溃
		p = peer_head;
		while(p != NULL) {
			if(p->state == CLOSING) {
				del_peer_node(p); 
				p = peer_head;
			} else {
				p = p->next;
			}
		}

		// 判断是否已经下载完毕
		if(download_piece_num == pieces_length/20) { 
			printf("++++++ All Files Downloaded Successfully +++++\n"); 
			break;
		}
	}

	return 0;
}

void print_process_info()
{
	char  info[256];
	float down_rate, up_rate, percent;
	
	down_rate = total_down_rate;
	up_rate   = total_up_rate;
	percent   = (float)download_piece_num / (pieces_length/20) * 100;
	if(down_rate >= 1024)  down_rate /= 1024;
	if(up_rate >= 1024)    up_rate   /= 1024;
	
	if(total_down_rate >= 1024 && total_up_rate >= 1024)
		sprintf(info,"Complete:%.2f%% Peers:%d Down:%.2fKB/s Up:%.2fKB/s \n",
				percent,total_peers,down_rate,up_rate);
	else if(total_down_rate >= 1024 && total_up_rate < 1024)
		sprintf(info,"Complete:%.2f%% Peers:%d Down:%.2fKB/s Up:%.2fB/s \n",
				percent,total_peers,down_rate,up_rate);
	else if(total_down_rate < 1024 && total_up_rate >= 1024)
		sprintf(info,"Complete:%.2f%% Peers:%d Down:%.2fB/s Up:%.2fKB/s \n",
				percent,total_peers,down_rate,up_rate);
	else if(total_down_rate < 1024 && total_up_rate < 1024)
		sprintf(info,"Complete:%.2f%% Peers:%d Down:%.2fB/s Up:%.2fB/s \n",
				percent,total_peers,down_rate,up_rate);
	
	//if(total_down_rate<1 && total_up_rate<1)  return;
	printf("%s",info);
}

int print_peer_list()
{
	Peer *p = peer_head;
	int  count = 0;
	
	while(p != NULL) {
		count++;
		printf("IP:%-16s Port:%-6d Socket:%-4d\n",p->ip,p->port,p->socket);
		p = p->next;
	}
	
	return count;
}

void release_memory_in_torrent()
{
	if(sock    != NULL)  { free(sock);    sock = NULL; }
	if(tracker != NULL)  { free(tracker); tracker = NULL; }
	if(valid   != NULL)  { free(valid);   valid = NULL; }

	if(peer_sock  != NULL)  { free(peer_sock);  peer_sock  = NULL; }
	if(peer_addr  != NULL)  { free(peer_addr);  peer_addr  = NULL; }
	if(peer_valid != NULL)  { free(peer_valid); peer_valid = NULL; }
	free_peer_addr_head();
}

void clear_connect_tracker()
{
	if(sock    != NULL)  { free(sock);    sock    = NULL; }
	if(tracker != NULL)  { free(tracker); tracker = NULL; }
	if(valid   != NULL)  { free(valid);   valid   = NULL; }
	tracker_count = 0;
}

void clear_connect_peer()
{
	if(peer_sock  != NULL) { free(peer_sock);  peer_sock  = NULL; }
	if(peer_addr  != NULL) { free(peer_addr);  peer_addr  = NULL; }
	if(peer_valid != NULL) { free(peer_valid); peer_valid = NULL; }
	peer_count = 0;
}

void clear_tracker_response()
{
	if(tracker_response != NULL) { 
		free(tracker_response);
		tracker_response = NULL;
	}
	response_len   = 0;
	response_index = 0;
}