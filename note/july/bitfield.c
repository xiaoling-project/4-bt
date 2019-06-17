#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <malloc.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "parse_metafile.h"
#include "bitfield.h"

extern int  pieces_length; //该长度为种子文件中hash字符串的长度，每个piece所占字符数为20
                            //将pieces_lenght/20可得文件的piece数
extern char *file_name;  

Bitmap      *bitmap = NULL;         // 指向位图
int         download_piece_num = 0; // 当前已下载的piece数 

// 如果存在一个位图文件,则读位图文件并把获取的内容保存到bitmap
// 如此一来,就可以实现断点续传,即上次下载的内容不至于丢失
int create_bitfield()
{
	bitmap = (Bitmap *)malloc(sizeof(Bitmap));
	if(bitmap == NULL) { 
		printf("allocate memory for bitmap fiailed\n"); 
		return -1;
	}

	//每一个unsigned char 类型的数据在计算机中占据1个字节，8个位,故每个unsigned char能记录8个piece的下载情况
	bitmap->valid_length = pieces_length / 20;
	bitmap->bitfield_length = pieces_length / 20 / 8;   //piece数不一定是8的倍数,故下面要进行取余运算，如果不是8的整倍数,再分配多一个unsigned char 类型的变量
	if( (pieces_length/20) % 8 != 0 )  bitmap->bitfield_length++;  

	bitmap->bitfield = (unsigned char *)malloc(bitmap->bitfield_length);
	if(bitmap->bitfield == NULL)  { 
		printf("allocate memory for bitmap->bitfield fiailed\n"); 
		if(bitmap != NULL)  free(bitmap);
		return -1;
	}

	char bitmapfile[64];
	sprintf(bitmapfile,"%dbitmap",pieces_length);  //保存位图时，便是以pieces_length+"bitmap"以文件名来保存的
													//故打开该文件时也是以这个为文件名来尝试打开该文件
	
	int  i;
	FILE *fp = fopen(bitmapfile,"rb");
	if(fp == NULL) {  // 若打开文件失败,说明开始的是一个全新的下载
		memset(bitmap->bitfield, 0, bitmap->bitfield_length);
	} else {
		fseek(fp,0,SEEK_SET); //可以添加具体用法
		for(i = 0; i < bitmap->bitfield_length; i++)
			(bitmap->bitfield)[i] = fgetc(fp);
		fclose(fp); 
		// 给download_piece_num赋新的初值
		download_piece_num = get_download_piece_num();
	}
	
	return 0;
}

int get_bit_value(Bitmap *bitmap,int index)  
{
	int           ret;
	int           byte_index;
	unsigned char byte_value;
	unsigned char inner_byte_index;

	if(index >= bitmap->valid_length)  return -1; //这里要记得写出错的处理方式

	byte_index = index / 8;  //获取具体的某一个字符
	byte_value = bitmap->bitfield[byte_index];
 	inner_byte_index = index % 8;   //获得该字符的具体的某一位

	byte_value = byte_value >> (7 - inner_byte_index);  //将所需要的那一位移动到最后一位,通过取余2来判断最后一位是0还是1
	if(byte_value % 2 == 0) ret = 0;
	else                    ret = 1;

	return ret;
}

int set_bit_value(Bitmap *bitmap,int index,unsigned char v)
{
	int           byte_index;
	unsigned char inner_byte_index;

	if(index >= bitmap->valid_length)  return -1;
	if((v != 0) && (v != 1))   return -1;  

	byte_index = index / 8;
	inner_byte_index = index % 8;

	v = v << (7 - inner_byte_index);    
	bitmap->bitfield[byte_index] = bitmap->bitfield[byte_index] | v;

	return 0;
}

int all_zero(Bitmap *bitmap)
{
	if(bitmap->bitfield == NULL)  return -1;
	memset(bitmap->bitfield,0,bitmap->bitfield_length);
	return 0;
}
 
int all_set(Bitmap *bitmap)
{
	if(bitmap->bitfield == NULL)  return -1;
	memset(bitmap->bitfield,0xff,bitmap->bitfield_length);
	return 0;	
}

void release_memory_in_bitfield()
{
	if(bitmap->bitfield != NULL) free(bitmap->bitfield);
	if(bitmap != NULL)  free(bitmap);
}

int print_bitfield(Bitmap *bitmap)
{
	int i;

	for(i = 0; i < bitmap->bitfield_length; i++) {
		printf("%.2X ",bitmap->bitfield[i]);  //.2X是以什么形式展示出来的,16进制形式展示出啦
		if( (i+1) % 16 == 0)  printf("\n");
	}
	printf("\n");

	return 0;
}

int restore_bitmap()
{
	int  fd;
	char bitmapfile[64];
	
	if( (bitmap == NULL) || (file_name == NULL) )  return -1;
	
	sprintf(bitmapfile,"%dbitmap",pieces_length);
	fd = open(bitmapfile,O_RDWR|O_CREAT|O_TRUNC,0666);//有两种打开文件的方式，fopen和open，这两者有何不同,这里的O_RDWR|O_CREST|O_TRUNC是用来干什么的
	if(fd < 0)  return -1;
	
	write(fd,bitmap->bitfield,bitmap->bitfield_length);
	close(fd);
	
	return 0;
}

int is_interested(Bitmap *dst,Bitmap *src)
{
	unsigned char const_char[8] = { 0x80,0x40,0x20,0x10,0x08,0x04,0x02,0x01};//在计算机中该16进制数以2进制形式表示，只有相应的某一位为1
	unsigned char c1, c2;
	int           i, j;
	
	if( dst==NULL || src==NULL )  return -1;
	if( dst->bitfield==NULL || src->bitfield==NULL )  return -1;
	if( dst->bitfield_length!=src->bitfield_length ||
		dst->valid_length!=src->valid_length )
		return -1;
	
	for(i = 0; i < dst->bitfield_length-1; i++) {
		for(j = 0; j < 8; j++) {
			c1 = (dst->bitfield)[i] & const_char[j];
			c2 = (src->bitfield)[i] & const_char[j];
			if(c1>0 && c2==0) return 1;
		}
	}
	
	j  = dst->valid_length % 8;
	c1 = dst->bitfield[dst->bitfield_length-1];
	c2 = src->bitfield[src->bitfield_length-1];
	for(i = 0; i < j; i++) {
		if( (c1&const_char[i])>0 && (c2&const_char[i])==0 )
			return 1;
	}
	
	return 0;
}
/*  
    以上函数的功能测试代码如下：
	测试时可以交换map1.bitfield和map2.bitfield的值或赋其他值

	Bitmap map1, map2;
	unsigned char bf1[2] = { 0xa0, 0xa0 };
	unsigned char bf2[2] = { 0xe0, 0xe0 };
  
	map1.bitfield        = bf1;
	map1.bitfield_length = 2;
	map1.valid_length    = 11;
	map2.bitfield        = bf2;
	map2.bitfield_length = 2;
	map2.valid_length    = 11;
	  
    int ret = is_interested(&map1,&map2);	
	printf("%d\n",ret);
 */

// 获取当前已下载到的总的piece数
int get_download_piece_num()
{
	unsigned char const_char[8] = { 0x80,0x40,0x20,0x10,0x08,0x04,0x02,0x01};//通过判断各位的值的大小，从而确定这个有无下载
	int           i, j;
	
	if(bitmap==NULL || bitmap->bitfield==NULL)  return 0;
	
	download_piece_num =0;

	for(i = 0; i < bitmap->bitfield_length-1; i++) {
		for(j = 0; j < 8; j++) {
			if( ((bitmap->bitfield)[i] & const_char[j]) != 0) 
				download_piece_num++;
		}
	}

	unsigned char c = (bitmap->bitfield)[i]; // c存放位图最后一个字节
	j = bitmap->valid_length % 8;            // j是位图最后一个字节的有效位数
	for(i = 0; i < j; i++) {
		if( (c & const_char[i]) !=0 ) download_piece_num++;
	}
		
	return download_piece_num;
}