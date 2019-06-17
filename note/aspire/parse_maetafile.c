#include<stdio.h>
#include<ctype.h>
#include<stdlib.h>
#include<string.h>
#include<time.h>
#include"parse_metafile.h"
#include"shal.h"

/*
piece:我们待下载的文件（共享文件）在逻辑上被划分为大小相同的块(通常是256KB)，称为piece
piece的hash值：BT协议使用Sha1算法对每个Piece生成20字节的hash值，用来校验客户端下载的piece是否完整
slice:每个piece被划分为大小相同的slice，固定为16KB
pieces：对应的值是一个字符串，存放的是各个piece的hash值.一定是20的倍数

*/


//设置好以下全局变量
char *metafile_connect=NULL;		//保存种子文件的内容
long filesize;						//保存文件的长度

int piece_length=0;					//每个piece的长度，通常是256KB，即262144字节
char *pieces=NULL;					//保存每个pieces的hash值，每个hash值为20字节
int pieces_length=0;				//缓冲区pieces的长度，以字节为单位

int multi_file=0;					//指明是单文件还是多文件
char *file_name=NULL;				//对于单文件，存放文件名，多余多文件，存放目录名
long long file_length=0;			//存放待下载文件的总长度
Files  *files_head=NULL;			//只对多文件有效，存放多个文件的路径和长度

unsigned char info_hash[20];		//保存info_hash的值，连接tracker和peer时使用
unsigned char peer_id[20];			//保存perr_id的值，连接peer时使用
Announce_list *announce_list_head=NULL;	//用于保存所有tracker服务器的URL


/*
 *功能：解析种子文件
 *参数：metafile_name 是种子文件名
 *返回：处理成功返回0，否则返回-1
 *Note: 将种子文件的内容读到全局变量metafile_content所指的缓冲区中以方便处理。
*/

int read_matafile(char *metafile_name)
{
	long i;
	
	/*以二进制，只读的方式打开文件
	 *因为以文本形式打开可能导致文件提前结束。因为文本中可能会有0x00字符。
	 */
	FILE *fp=fopen(metafile_name,"rb");
	if(fp==NULL)
	{
		//_FILE_,_LINE_均为系统自定义宏，一个代表该文件名，另一个是错误发生行号
		printf("%s:%d can not open file\n",_FILE_,_LINE_);
		return -1;
	}
	
	//获取种子文件的长度，filesize为全局变量，在parse_metafile.c头部定义
	fseek(fp,0,SEEK_END);
	//ftell用于得到文件位置指针当前位置相对于文件首的偏移字节数,所以filesize是一个字节数
	filesize=ftell(fp);
	if(filesize==-1)
	{
		printf("%s:%d fseek failed\n",_FILE_,_LINE_);
		return -1;
	}
	
	//这里多分配一个1个字节就是为了存放'\0'
	metafile_content=(char*)malloc(filesize+1);
	
	//书本上面没有做出判断，我觉得有必要判断一下
	if(metafile_connect==NULL)
	{
		printf("%s:%d malloc failed\n",_FILE_,_LLINE_);
		return -1;
	}
	
	//读取种子文件内容到缓冲区中
	
	//SEEK_SET 代表文件头，代表把文件指针移到文件的开头，偏移0个字节
	fseek(fp,0,SEEK_SET);
	for(i=0;i<filesize;i++)
	{
		/*
			问题：如果是一个一个字符获取的话，那么代表字符长度的数字例如13，不会被分解为1，3吗
			解答：因为如果要计算数据的话（就像后面的函数get_pieces中计算length），
				  都是依靠一个':'来判断是否到了这个数字的结尾。也就是说，
				  后面的计算都是把这个当作字符来计算的。
		*/
		metafile_connect[i]=fgetc(fp);
	}
	metafile_connect[i]='\0';
	
	/*
	//我觉得这里没有必要使用这样的方式来读取数据，直接使用fwrite就好了
	if(fwrite(metafile_connect,filesize,1,fp)!=1)
	{
		printf("%s:%d,fwrite execute failed\n",_FILE_,_LINE);
	}
	metafile_connect[filesize]='\0';
	*/
	fclose(fp);
	
	#ifdef DEBUG
		printf("metafile size is :%ld\n",filesize);
	#endif
	
	return 0;
}


/*
 *功能：从种子文件中查找某个关键字
 *参数：keyword为要查找的关键字，position用于返回关键字的第一个字符的所在下标
 *返回：成功执行并找到关键字为1，没有找到关键字为0，执行失败为-1
 *用处：可以用来查找Tracker的地址
 */
 
 int find_keyword(char *keyword,long *position)
 {
	 long i;
	 *position=-1;
	 if(keyword==NULL)
		 return 0;
	 for(i=0;i<filesize-strlen(keyword);i++)
	 {
		 if(memcmp(&metafile_connect[i],keyword,strlen(keyword))==0)
		 {
			 *position=i;
			 retrun 1;
		 }
	 }
	 return 0;
 }
 
 
 /*
  *功能：获取Tracker的地址，并将获取的地址保存到全局变量announce_list_head指向的链表中
  *返回：成功返回0，否则返回-1
  *理解这里的函数得需要一些数据结构链表的知识，不过c语言最后几章应该也是讲的这个	
 */
 
 int read_announce_list()
 {
	 Announce_list *node=NULL;
	 Announce_list *p=NULL;
	 int len=0;
	 long i;
	 
	 //例如：d8:announce35:http://tr.bangumi.moe:6969/announcee
	 if(find_keyword("13:announce_list",&i)==0)
	 {
		 if(find_keyword("8:announce",&i)==1)
		 {
			 i=i+strlen("8:announce");
			 while(isdigit(metafile_connect[i]))
			 {
				 len=len*10+(metafile[i]-'0');
				 i++;
			 }
			 
			 i++;		//跳过字符 ':'
			 
			 node=(Announce_list*)malloc(sizeof(Announce_list));
			 if(node==NULL)
			 {
				printf("%s:%d can not assign memory\n",_FILE_,_LINE_);
				return -1;
			 }
			 strncpy(node->announce,&metafile_connect[i],len);
			 node->announce[len]='\0';
			 node->next=NULL;
			 announce_list_head=node;
		 }
	 }
	 /*
	  *如果有13:announce_list关键字，就不用处理8:announce关键字了
	  *因为含有13的关键字的URL一定会含有8关键字里面的URL
	 
	  *例如：d8:announce35:http://tr.bangumi.moe:6969/announce
			 13:announce-listll35:http://tr.bangumi.moe:6969/announcee
			 l33:http://t.nyaatracker.com/announceeee
	 */
	 else
	 {
		 i=i+strlen("13:announce_list");		//按照我上面给出的例子来看，此时metafile_connect[i]='l';(是"...listll35:.."中的第二个l)
		 i++;								//跳过字符'l',这里的跳过字符'l'，那么指向的是"...listll35:.."中的第3个'l'
		 while(metafile_connect[i]!='e')
		 {
			 i++;							//第一次进入循环的时候指向"...listll35:.."中的3
			 //判断是否为阿拉伯数字，是返回True，否则返回0
			 while(isdigit(metafile_connect[i]))		//这个while循环就会计算出URL的字符个数
			 {
				 len=len*10+(metafile_connect[i]-'0');
				 i++;
			 }
			 
			 //计算出URL的字符个数后，应该紧跟着的是':',如果不是的，说明种子文件内容有问题
			 if(metafile_connect[i]==':')
				 i++;
			 else
				 return -1;
			 
			 /*
			  *只处理以http开头的tracker地址，不处理以udp开头的地址
			  *第一次进入的时候指向的应该是:metafile_connect[i]='h'
			  *这个if语句就是在首先一个链表的构建，学过数据结构（或则c语言后面的链表）应该不难理解
			  *如果没有学过的话，的需要画图来方便理解
			 */
			 if(memcmp(&metafile_connect[i],"http",4)==0)
			 {
				 node=(Announce_list*)malloc(sizeof(Announce_list));
				 if(node==NULL)
				 {
					 printf("%d：内存分配失败\n",_LINE_);
					 return -1;
				 }
				 strcpy(node->announce,&metafile_content[i],len);
				 node->announce[len]='\0';
				 node->next=NULL;
				 
				 //announce_list_head是我定义的全局变量，可以利用这个找到的所有URL的地址
				 if(announce_list_head==NULL)
				 {
					 announce_list_head=node;
				 }
				 
				 /*
				  *采用尾插法的思想，将每一次获得的URL(当然，会将其封装为一个结构体)
				  *插入到整个URL链式的尾部
				  */
				 else
				 {
					 p=announce_list_head;
					 while(p->next!=NULL)
						 p=p->next;			//使p指针指向最后一个节点
					 p->next=node;			//node成为tracker列表的最后一个节点
				 }
			 }
			 
			 /*第一次的进入时举例：此时执行了i=i+len后
			  *metafile_content[i]是"6969/announceel33:"中的第二个'e'
			  *然后执行一次i++,执向后面的字符'l'
			 */
			 i=i+len;
			 len=0;
			 i++;
			 if(i>=filesize)
				 return -1;
		 }//while循环结束
	 }//else结束
	 
	#ifdef DEBUG 
		p=announce_list_head;
		while(p!=NULL)
		{
			printf("%s\n",p->announce);
			p=p->next;
		}
	#endif
	return 0;
 }
 
 
 /*
  *功能：连接某些Tracker时会返回一个重定向的URL,需要连接该URL才能获取Peer。
  *返回：成功添加返回1；原来有该url，就没有添加,返回0；添加失败返回-1
 */
 
 int add_an_announce(char *url)
 {
	 Announce_list *p=announce_list_head,*q;
	 
	 //如果指定的URL在Tracker中已经存在，则无需添加
	 while(p!=NULL)
	 {
		 if(strcmp(p->announce,url)==0)
			 break;
		 p=p->next;
	 }
	 
	 //如果执行if语句，则证明原来的Tracker列表中有该url
	 if(p!=NULL)
		 return 0;
	 
	 //否则证明没有该url，则需要添加在其中
	 q=(Announce_list*)malloc(sizeof(Announce_list));
	 if(q==NULL)
	 {
		 printf("%d：内存分配失败\n",_LINE_);
		 return -1;
	 }
	 strcpy(q->announce,url);
	 q->next=NULL;
	 
	 p=announce_list_head;		//因为在之前的处理中，p不再为announce_list_head了
	 
	 //可能announce_list_head一直就没有URL(说实话，这点我在写程序的时候没有想到过)
	 if(p=NULL)
	 {
		 announce_list_head=q;
		 return 1;
	 }
	 
	 while(p->next!=NULL)
		 p=p->next;
	 p->next=q;
	 return 1;
 }
 
 
/*
 *功能：判断下载是多个文件还是单文件
 *返回：多文件返回1，单文件返回0
*/

int is_multi_files()
{
	long i;
	
	//根据种子文件内容定义,含有5:files字样的就是多文件下载
	if(find_keyword("5:files",&i)==1)
	{
		multi_file=1;
		return 1;
	}
	
	#ifdef DEBUG
		printf("is_multi_files:%d\n",multi_file);
	#endif
	
	return 0;
}


/*
 *功能：获得piece的长度
 *返回：失败返回-1，成功返回0.具体的长度由全局变量piece_length取得
 *例子：12:piece_lengthi262144e，那么piece_length就是262144，即256KB
*/

int get_piece_length()
{
	long i;
	if(find_keyword("12:piece_length",&i)==1)
	{
		i=i+strlen("12:piece_length");		//跳过"12：piece_length"
		i++;								//跳过'i'
		while(metafile_connect[i]!='e')
		{
			piece_length=piece_length*10+(metafile_connect[i]-'0');
			i++;
		}
	}
	else
		return -1;
	
	#ifdef DEBUG
		printf("piece_length:%d\n",piece_length);
	#endif
	
	return 0;
}


/*
 *功能：获取每一个piece的hash值，并保存到pieces所指的缓存中
 *返回：成功返回0，否则返回-1
 *例子：2:piece_lengthi262144e6:pieces17100:....
 *上面例子中的...在用记事本打开的时候，就是乱码，其实是每个pieces的hash值
*/

int get_pieces()
{
	long i;
	if(find_keyword("6:pieces",&i)==1)
	{
		i=i+8;
		//利用':'来判断是否到了这个数字的结尾
		while(metafile_connect[i]!=':')
		{
			pieces_length=pieces_length*10+(metafile_connect[i]-'0');
			i++;
		}
		i++;			//跳过字符':'
		pieces=(char*)malloc(pieces_length+1);			//在上面举例子中:pieces_length=17100
		printf(pieces==NULL)
		{
			printf("%d,内存分配失败\n",_LINE_);
		
		memcpy(pieces,&metafile_content[i],pieces_length);	//然后就把所有的hash值拷贝到pieces中了
		pieces[pieces_length]='\0';
	}
	else
	{
		return -1;
	}
	
	#ifdef DEBUG
		printf("get_pieces ok\n");
	#endif
	
	return 0;
}


/*
 *功能：获取带下载的文件的文件名，如果下载的是多个文件，则获取的是目录名
 *返回：成功返回0，否则返回-1
 *例子：4:name62:[学园奶爸][Gakuen Babysitters][12][简日][1080P][END].mp4
*/

int get_file_name()
{
	long i;
	int count=0;
	
	if(find_keyword("4:name",&i)==1)
	{
		i=i+6;
		while(metafile_connect[i]!=':')
		{
			count=count*10+(metafile_connect[i]-'0');
			i++;
		}
		i++;
		file_name=(char*)malloc(count+1);
		printf(file_name==NULL)
		{
			printf("%d,内存分配失败\n",_LINE_);
		}
		memcpy(file_name,metafile_connect[i],count);
		file_name[count]='\0';
	}
	else
	{
		return -1;
	}
	
	#ifdef DEBUG
		printf("file_name :%d\n",file_name);
	#endif
	
	return 0;
}


/*
 *功能：获取带下载的文件的长度
 *返回：失败返回-1，成功返回0
*/

int get_file_length()
{
	
	long i;
	
	//如果是多文件，则采用这样的方式计算要下载的文件的长度
	if(is_multi_files()==1)
	{
		if(files_head==NULL)
		{
			//该函数是为了获得各个要下载的文件的路径和长度，保存在全局变量files_head中
			get_files_length_path();
			/*
			  这里的files_head是一个全局变量，记录所有文件路径和长度
			  Files是一个结构体变量，用于存放多文件的路径和长度
			*/
			Files *p=files_head;		
			while(p!=NULL)
			{
				file_length+=p->length;
				p=p->next;
			}
		}
		
	//如果是单一文件，则采用这样的方式计算下载长度
		else
		{
			if(find_keyword("6:length",&i)==1)
			{
				i=i+8;
				while(metafile_connect[i]!='e')
				{
					file_length=file_length*10+(metafile_connect[i]-'0');
					i++;
				}
			}
		}
		
		#ifdef DEBUG
			printf("file_length:%lld\n",file_length);
		#endif
		
		return 0;
	}
}


/*
 *功能：对于多文件，获取各文件的路径以及长度
 *返回：失败返回-1，成功返回0
*/

int get_files_length_path()
{
	long i;
	int length;
	int count;
	Files *node=NULL;
	Files *p=NULL;
	
	if(is_multi_files()!=1)
	{
		return 0;
	}
	
	/*
	 *可以看到，这里其中有两个地方其实都是在寻找字符串"6:length"和字符串"4:path"
	 *但是为什么不使用函数find_keyword()
	 *因为我们写的函数仅仅只能找到第一个与目标字符串匹配的位置
	 *因为这里是多文件，必然字符串"6:length"和字符串"4:path"会出现很多次
	 *所以使用for循环搭配memcmp
	 *for循环中i<filesize-8是因为"6:length"是8个字符串。因为"6:length"与"4:path"会成对出现
	 *在第filesize-9的时候就都没有必要去匹配了
	*/
	for(i=0;i<filesize-8;i++)
	{
		if(memcmp(&metafile_connect[i],"6:length",8)==0)
		{
			i=i+8;
			i++;
			length=0;
			
			//这里是为了计算要下载的文件的长度
			
			while(metafile_connect[i]!='e')
			{
				length=length*10+(metafile_connect[i]-'0');
				i++;
			}
			node=(Files*)malloc(sizeof(Files));
			if(node==NULL)
			{
				printf("%d:内存分配失败\n",_LINE_);
				return -1;
			}
			
			node->length=length;
			
			/*
			 *因为是多文件的话，会产生几个node
			 *为了方便管理，使用files_head作为头元素，形成一个链表
			 *这里使用的尾插法
			*/
			
			if(files_head==NULL)
			{
				files_head=node;
			}
			else
			{
				p=files_head;
				while(p->next!=NULL)
					p=p->next;
				p->next=node;
			}
		}
		
		if(memcmp(&metafile_connect[i],"4:path",6)==0)
		{
			i=i+6;
			i++;
			count=0;
			
			//这里是为了计算要下载的文件的路径
			
			while(metafile_connect[i]!=':')
			{
				count=count*10+(metafile_connect[i]-'0');
				i++;
			}
			
			//这里也是使用尾插法将之前插入的node上面的路径补全
			p=files_head;
			while(p->next!=NULL)
				p=p->next;
			memcpy(p->path,&metafile_content[i],count);
			*(p->path+count)='\0';
		}
	}
	
	return 0;
}


/*
 *功能：计算info_hash的值
 *返回：失败返回-1，成功返回0
*/

int get_info_hash()
{
	int push_pop=0;
	long i,begin,end;
	
	if(metafile_connect==NULL)
	{
		return -1;
	}
	
	//begin的值是表示关键字"4:info"对应值的起始下标
	if(find_keyword("4:info",&i)==1)
		begin=i+6;
	else
		return -1;
	
	i=i+6;
	for(;i<filesize;)
	{
		if(metafile_connect[i]=='d')
		{
			push_pop++;
			i++;
		}
		else if(metafile_connect[i]=='l')
		{
			push_pop++;
			i++;
		}
		else if(metafile_connect[i]=='i')
		{
			i++;
			if(i==filesize)
					return -1;
			while(metafile_connect[i]!='e')
			{
				if((i+1)==filesize)
					return -1;
				else
					i++;
			}
			i++;
			
		}
		else if((metafile_connect[i]>='0') && (metafile_connect[i]<='9'))
		{
			push_pop--;
			if(push_pop==0)
			{
				end=i;
				break;
			}
			else
			{
				i++;
			}
		}
		else
		{
			return -1;
		}
		
	}
	
	if(i==filesize)
	{
		return -1;
	}
	
	SHA1_CTX context;
	SHA1Init(&context);
	SHA1Update(&context,&metafile_connect[begin],end-begin+1);
	SHA1Final(info_hash,&context);
	
	#ifdef DEBUG
		printf("info_hash:");
		for(i=0;i<20;i++)
		{
			printf("%.2x",info_hash[i]);
		}
		printf("\n");
	#endif
	
	return 0;
}


/*
 *功能：生成一个唯一的peer id
 *返回：失败返回-1，成功返回0
*/

int get_peer_id()
{
	//设置产生随机数的种子
	srand(time(NULL));
	//使用rand函数来随机产生一个数，并用该数来构造peer_id
	//peer_id前8位固定为-TT1000-
	sprintf(peer_id,"-TT1000-%12d",rand());
	
	#ifdef DEBUG
		printf("peer_id:%s\n",peer_id);
	#endif
	
	return 0;
}


/*
 *功能：释放之前动态申请的空间
 *这个函数是在是太重要了。你之前做的项目从来没有考虑这一点，以后记得这个函数一定的有
 *还有，free之前的每一个判断都得去做，不然free是会出错的
*/

void release_memory_in_parse_metafile()
{
	Announce_list *p;
	Files *q;
	
	if(metafile_connect!=NULL)
	{
		free(metafile_connect);
	}
	if(file_name!=NULL)
	{
		free(file_name);
	}
	if(pieces!=NULL)
	{
		free(pieces);
	}
	
	while(announce_list_head!=NULL)
	{
		p=announce_list_head;
		announce_list_head=announce_list_head->next;
		free(p);
	}
	
	while(files_head!=NULL)
	{
		q=files_head;
		files_head=files_head->next;
		free(q);
	}
}


/*
 *功能：调用parse_metafile.c中定义的函数，完成解析种子文件。该函数由main.c调用
 *返回：解析成功返回0，否则返回-1
*/

int parse_metafile(char *metafile)
{
	int ret;
	
	//读取种子文件
	ret=read_matafile(metafile);
	if(ret<0)
	{
		printf("%s:%d wrong\n",_FILE_,_LINE_);
		return -1;
	}
	
	//从种子文件中获取tracker服务器地址
	ret=read_announce_list();
	if(ret<0)
	{
		printf("%s:%d wrong\n",_FILE_,_LINE_);
		return -1;
	}
	
	//判断是否为多文件
	ret=is_multi_files();
	if(ret<0)
	{
		printf("%s:%d wrong\n",_FILE_,_LINE_);
		return -1;
	}
	
	//获取每个pieces的长度，一般为256KB
	ret=get_piece_length();
	if(ret<0)
	{
		printf("%s:%d wrong\n",_FILE_,_LINE_);
		return -1;
	}
	
	//读取各个piece的hash值
	ret=get_pieces();
	if(ret<0)
	{
		printf("%s:%d wrong\n",_FILE_,_LINE_);
		return -1;
	}
	
	//获取要下载的文件名，对于多文件的种子，获取的是目录名
	ret=get_file_name();
	if(ret<0)
	{
		printf("%s:%d wrong\n",_FILE_,_LINE_);
		return -1;
	}
	
	//对于多文件，获取各文件的路径以及长度
	ret=get_files_length_path();
	if(ret<0)
	{
		printf("%s:%d wrong\n",_FILE_,_LINE_);
		return -1;
	}
	
	//获取带下载的文件的总长度
	ret=get_file_length();
	if(ret<0)
	{
		printf("%s:%d wrong\n",_FILE_,_LINE_);
		return -1;
	}
	
	
	//获得info_hash，生成peer_id
	ret=get_info_hash();
	if(ret<0)
	{
		printf("%s:%d wrong\n",_FILE_,_LINE_);
		return -1;
	}
	ret=get_peer_id();
	if(ret<0)
	{
		printf("%s:%d wrong\n",_FILE_,_LINE_);
		return -1;
	}
	
	return 0;
}







