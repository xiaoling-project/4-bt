#ifndef PARSE_METAFILE
#define PARSE_METAFILE

/*保存从种子文件中获取的tracker的URL
 *这种将其封装为一个结构体的思想很好
 *不仅利于管理，而且也十分适合之后的操作
*/
typedef struct _Announce_list
{
	char announce[128];
	struct _Announce_list *next;
}Announce_list;

//保存各个待下载文件的路径和长度
typedef struct _Files
{
	char path[256];
	long length;
	struct _Files *next;
}Files;

int read_metafile(char *metafile_name);		//读取种子文件
int find_kelutword(char *keyword,long *position);		//在种子文件中查找某个关键字
int read_announce_list();		//获取各个tracker服务器的地址
int add_an_announce(char *url);		//向tracker列表添加一个URL

int get_piece_length();		//获取每个piece的长度，一般为256KB
int get_pieces();		//读取每个piece的哈希值

int is_multi_files();		//判断下载是单个文件还是多个文件
int get_file_name();		//获取文件名，对于多文件，获取的是目录名
int get_file_length();		//获取待下载文件的总长度
int get_files_length_path();		//获取文件的路径和长度，针对多文件种子有效

int get_info_hash();		//由Info关键字对应计算info_hash
int get_peer_id();		//生成一个peer_id,每个peer都有一个20字节的perr_id

void release_memory_in_parse_metafile();		//释放parse_metafile.c中动态分配的内存
int parse_metafile(char *metafile);			//调用本文件中的定义的函数，完成种子的解析

#endif



