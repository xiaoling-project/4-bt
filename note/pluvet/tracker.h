#ifndef  TRACKER_H

#define  TRACKER_H

#include 

#include "parse_metafile.h"


typedef struct _Peer_addr {
    char            ip[16];
    unsigned short    port;
    struct _Peer_addr *next;
} Peer_addr;


// 用于将info_hash和peer_id转换为HTTP编码格式
int http_encode(unsigned char *in,int len1,char *out,int len2);
// 从种子文件中存储的Tracker的URL获取Tracker主机名
int get_tracker_name(Announce_list *node,char *name,int len);
// 从种子文件中存储的Tracker的URL获取Tracker端口号
int get_tracker_port(Announce_list *node,unsigned short *port);

// 构造发送到Tracker服务器的HTTP GET请求
int create_request(char *request, int len,Announce_list *node,

               unsigned short port,long long down,long long up,

               long long left,int numwant);
int prepare_connect_tracker(int *max_sockfd);  // 以非阻塞的方式连接Tracker
int prepare_connect_peer(int *max_sockfd);     // 以非阻塞的方式连接peer

// 获取Tracker返回的消息类型

int get_response_type(char *buffer,int len,int *total_length);

// 解析第一种Tracker返回的消息

int parse_tracker_response1(char *buffer,int ret,char *redirection,int len);

// 解析第二种Tracker返回的消息

int parse_tracker_response2(char *buffer,int ret);

// 为已建立连接的peer创建peer结点并加入到peer链表中

int add_peer_node_to_peerlist(int *sock,struct sockaddr_in saptr);

// 释放peer_addr指向的链表

void free_peer_addr_head();    

void release_memory_in_tracker();

#endif
