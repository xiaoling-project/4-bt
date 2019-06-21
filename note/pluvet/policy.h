//policy.h
#ifndef POLICY_H
#define POLICY_H
#include "peer.h"

// 计算下载速率的间隔
#define COMPUTE_RATE_TIME  10
// 未阻塞的端的数量
#define UNCHOKE_COUNT  4
// 请求切片数量
#define REQ_SLICE_NUM  5
// 未阻塞的端的集合的结构体
typedef struct _Unchoke_peers {
	Peer*  unchkpeer[UNCHOKE_COUNT];
	int    count;
	Peer*  optunchkpeer;// 可选非阻塞端指针
} Unchoke_peers;


void init_unchoke_peers();


int select_unchoke_peer();
int select_optunchoke_peer(); 


int compute_rate(); // 计算下载速率（单端）
int compute_total_rate();      // 计算下载速率总和


int is_seed(Peer *node);       // 判断指定端是否为种子端

//构造切片的请求
int create_req_slice_msg(Peer *node);  

#endif