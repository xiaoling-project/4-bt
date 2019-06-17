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


#define  threshold (18*1024-1500)

extern Announce_list *announce_list_head;
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
extern Peer_addr     *peer_addr_head;

int                  *sock    = NULL;
struct sockaddr_in   *tracker = NULL;
int                  *valid   = NULL;
int                  tracker_count  = 0;

int                  response_len   = 0;
int                  response_index = 0;
char                 *tracker_response = NULL;

int                  *peer_sock  = NULL;
struct sockaddr_in   *peer_addr  = NULL;
int                  *peer_valid = NULL;
int                  peer_count  = 0;
int download_upload_with_peers(){

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
	connect_tracker    = 1;    // 是否需要连接tracker
	connecting_tracker = 0;    // 是否正在连接tracker
	connect_peer       = 0;    // 是否需要连接peer 
	connecting_peer    = 0;    // 是否正在连接peer
	for(;;)
	{
			//一开始便判断现在需不需要连接服务器，不考虑其他
			if((now_time-last_time[2] >= 300 || connect_tracker == 1) && 
					connecting_tracker != 1 && connect_peer != 1 && connecting_peer != 1) {
					// 由tracker的URL获取tracker的IP地址和端口号
					ret = prepare_connect_tracker(&max_sockfd);
				if(ret < 0)  { printf("prepare_connect_tracker\n"); return -1; }
	
				connect_tracker       = 0;
				connecting_tracker    = 1;
				start_connect_tracker = now_time;
			}
			// 创建套接字,向peer发出连接请求
			if(connect_peer == 1) {
			
			ret = prepare_connect_peer(&max_sockfd);
			if(ret < 0)  { printf("prepare_connect_peer\n"); return -1; }

			connect_peer       = 0;
			connecting_peer    = 1;
			start_connect_peer = now_time;
			}
			FD_ZERO(&rset);
			FD_ZERO(&wset);
			//当connect_tracker连接结束之后，便需要将connecting_tracker设置为1,表明此时正在连接tracker,接收信息,将耗费时间10s
			//需要连接服务器，此时需要将进行连接的服务器进行监听
			if(connecting_tracker == 1) {
			int flag = 1;
			// 如果连接tracker超过10秒,则终止连接tracker
			if(now_time-start_connect_tracker > 10) {
				for(i = 0; i < tracker_count; i++)
					if(valid[i] != 0)  close(sock[i]);
			} else {
				for(i = 0; i < tracker_count; i++) {
					if(valid[i] != 0 && sock[i] > max_sockfd)
						max_sockfd = sock[i];  // valid[i]值为-1、1、2时要监视
					if(valid[i] == -1) { 
						FD_SET(sock[i],&rset); 
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
			//现在这个就会开始连接这个connecing_peer的内容了
			if(connecting_peer == 1) {
			int flag = 1;
			// 如果连接peer超过10秒,则终止连接peer		
			if(now_time-start_connect_peer > 10) {
				for(i = 0; i < peer_count; i++) {
					if(peer_valid[i] != 1) close(peer_sock[i]);  //不为1说明连接失败
				}
			} else {
				for(i = 0; i < peer_count; i++) {
					if(peer_valid[i] == -1) {
						if(peer_sock[i] > max_sockfd)
							max_sockfd = peer_sock[i];
						FD_SET(peer_sock[i],&rset); 
						FD_SET(peer_sock[i],&wset);
						if(flag == 1)  flag = 0;
					}
				}
			}

			if(flag == 1) {
				connecting_peer = 0;
				clear_connect_peer();
				if(peer_head == NULL)  connect_tracker = 1; 
				continue;
			}
		}
			//这个就是连接服务器时间耗完之后所需要进行的操作
			if(flag == 1) {
				connecting_tracker = 0;
				last_time[2] = now_time;
				clear_connect_tracker();
				clear_tracker_response();
				if(peer_addr_head != NULL) { 
					connect_tracker = 0;
					connect_peer    = 1;
				} else { 
					connect_tracker = 1;
				}
				continue;
			}
		}
	
	
	}
	
		tmval.tv_sec  = 2;
		tmval.tv_usec = 0;
		ret = select(max_sockfd+1,&rset,&wset,NULL,&tmval);
		if(ret < 0)  { 
			printf("%s:%d  error\n",__FILE__,__LINE__);
			perror("select error"); 
			break;
		}
		if(ret == 0)  continue;
	//现在已经将所有的端口放到select中非阻塞式的连接了,接下来就要对这些socket是否可读可写进行判断了
	//如果可读的话便发送信息，可写的话便读取信息
		if(connecting_tracker == 1) {
			for(i = 0; i < tracker_count; i++) {
				if(valid[i] == -1) {
					// 如果某个套接字可写且未发生错误,说明连接建立成功
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
				//这个不是很理解，到底是为什么呢?这个端口号是要用来干什么的
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
					create_request(request,1024,anouce,listen_port,down,up,left,200);
					write(sock[i], request, strlen(request));
					valid[i] = 2;
				}
				if(valid[i] == 2 && FD_ISSET(sock[i],&rset)) {
					char  buffer[2048];
					char  redirection[128];
					ret = read(sock[i], buffer, sizeof(buffer));
					if(ret > 0)  {
						if(response_len != 0) {
							memcpy(tracker_response+response_index,buffer,ret);
							response_index += ret;
							if(response_index == response_len) {
								parse_tracker_response2(tracker_response,response_len);
								clear_tracker_response();
								valid[i] = 0;
								close(sock[i]);
								last_time[2] = time(NULL);
							}
						} else if(get_response_type(buffer,ret,&response_len) == 1) {
							tracker_response = (char *)malloc(response_len);
							if(tracker_response == NULL) printf("malloc error\n");
							memcpy(tracker_response,buffer,ret);
							response_index = ret;
						} else {
							ret = parse_tracker_response1(buffer,ret,redirection,128);
							if(ret == 1) add_an_announce(redirection);
							valid[i] = 0;
							close(sock[i]);
							last_time[2] = time(NULL);
						}
					}
				} 
			}
		}
		//接收完tracker的信息之后解析该信息，并且将信息放置到Peer_addr链表中,此时所有的Peer
		//当服务器连接完成并且结束之后，应该进入是连接Peer的状态，即connect_Peer = 1，而下面因为此时还是初始状态
	    //其他的操作根本不会进行，接下来便会进行connectint_Peer状态下的代码

}