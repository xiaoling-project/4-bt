#include<stdio.h>
#include<unistd.h>
#include<string.h>
#include<stdlib.h>
#include<fcntl.h>
#include<sys/types.h>
#include<sys/stat.h>
#include"parse_metafile.h"
#include"bitfield.h"


/*
pieces_length就是parse_metafile.c文件中每个pieces的长度
*/
extern int pieces_length;		
extern char *file_name;

Bitmap *bitmap=NULL;			//指向自己的位图，Peer的位图存放在Peer结构体中
int download_piece_num=0;		//当前已经下载的piece数

/*
 *功能：创建待下载的文件位图
 *返回：成功返回0，失败返回-1
*/

int create_bitfield()
{
	bitmap=(Bitmap*)malloc(sizeof(Bitmap));
	if(bitmap==NULL)
	{
		printf("%d：内存分配失败\n",_LINE_);
		return -1;
	}
	
	/*
	 *pieces_length/20即为总的piece数
	 *因为pieces_length是以字节为单位，所以，这里会算出总的字节数
	 */
	bitmap->valid_length=pieces_length/20;
	
	/*
	 *bitmap->valid_length/8 就是总的位数
	 *1个字节8位，后面将会使用这种bit位的方式将其映射出是否下载
	 */
	bitmap->bitfield_length=pieces_length/20/8;
	
	/*
	 *因为前面每一个pieces_length/20 %8肯定为0，但是最后一个piece的长度可能不是256KB
	 *所以，就可能导致(pieces_length/20)%8!=0
	 *这点在书本上get_pieces()这个函数的上面一句话有提到
	 */
	if((pieces_length/20)%8!=0)
	{
		bitmap->bitfield_length++;
	}
	
	bitmap->bitfield=(unsigned char*)malloc(bitmap->bitfield_length);
	if(bitmap->bitfield==NULL)
	{
		printf("%d：内存分配失败\n",_LINE_);
		if(bitmap!=NULL)
		{
			free(bitmap);
		}
		return -1;
	}
	
	//这里是获取一个bitfile的字符串
	char bitmapfile[64];
	sprintf(bitmapfile,"%dbitmap",pieces_length);
	
	
	int i;
	FILE *fp=fopen(bitmapfile,"rb");
	if(fp==NULL)
	{
		memset(bitmap->bitfield,0,bitmap->bitfield_length);
	}
	else
	{
		fseek(fp,0,SEEK_SET);
		for(i=0;i<bitmap->bitfield_length;i++)
		{
			(bitmap->bitfield)[i]=fgetc(fp);
		}
		fclose(fp);
		download_piece_num=get_download_piece_num();
	}
	
	return 0;
	
}

/*
 *功能：获取位图中某一位的值
 *返回：下载返回1，没有下载返回0，失败返回-1
*/

int get_bit_value(Bitmap *bitmap,int index)
{
	int ret;
	int byte_index;
	unsigned char byte_value;
	unsigned char inner_byte_index;
	
	//位图没有指向 或者index输入错误
	if(bitmap==NULL || index>=bitmap->valid_length)
	{
		return -1;
	}
	
	byte_index=index/8;
	byte_value=bitmap->bitfield[byte_index];
	inner_byte_index=index%8;
	
	byte_value=byte_value>>(7-inner_byte_index);
	if(byte_value%2==0)
	{
		ret=0;
	}
	else
	{
		ret=1;
	}
		
	
	return ret;
}


/*
 *功能：设置位图中某一位的值
 *返回：成功返回0，失败返回-1
*/

int set_bit_value(Bitmap* bitmap,int index,unsigned char value)
{
	int byte_index;
	unsigned charinner_byte_index;
	
	if(bitmap==NULL)
	{
		return -1;
	}
	
	if((value!=0) && (value!=1))
	{
		return -1;
	}
	
	byte_index=index/8;
	
	inner_byte_index=index%8;
	value=value<<(7-inner_byte_index);
	bitmap->bitfield[byte_index]=bitmap->bitfield[byte_index] | value;
	
	return 0;
}


/*
 *功能：设置位图所有位清零
 *返回：成功返回0，失败返回-1
*/
int all_zero(Bitmap *bitmap)
{
	if(bitmap->bitfield==NULL)
	{
		return -1;
	}
	memset(bitmap->bitfield,0,bitmap->bitfield_length);
	return 0;
}


/*
 *功能：设置位图所有位置1
 *返回：成功返回0，失败返回-1
*/

int all_set(Bitmap *bitmap)
{
	if(bitmap->bitfield==NULL)
	{
		return -1;
	}
	memset(bitmap->bitfield,0xff,bitmap->bitfield_length);
	return 0;
}



/*
 *功能：释放本模块申请的动态内存
 *返回：无
*/

void release_memory_in_bitfield()
{
	if(bitmap->bitfield!=NULL)
	{
		free(bitmap->bitfield);
		bitmap->bitfield=NULL;		//书本上面没有这句，但是我觉得应该加上去
	}
	if(bitmap!=NULL)
	{
		free(bitmap);
		bitmap=NULL;				//书本上面没有这句，但是我觉得应该加上去
	}
}


/*
 *功能：打印位图，用于调试程序
 *返回：成功返回0，失败返回-1
*/

int print_bitfield(Bitmap* bitmap)
{
	 int i;
     
     for(i = 0; i < bitmap->bitfield_length; i++) 
	 {
          printf("%.2X ",bitmap->bitfield[i]); // 以16进制的方式打印每个位图中的字节
          if( (i+1) % 16 == 0)  
		  {
			  printf("\n");  // 每行打印16个字节,位一个slice
		  }
     }
     printf("\n");
     
     return 0;

}



/*
 *功能：保存位图，用于断点续传
 *返回：成功返回0，失败返回-1
*/

int restore_bitmap()
{
	int fd;
	char bitmapfile[64];
	
	if(bitmap==NULL || (file_name==NULL))
	{
		return -1;
	}
	
	sprintf(bitmapfile,"%dbitmap",pieces_length);
	
	fd=open(bitmapfile,O_RDWR|O_CREAT|O_TRUNC,06666);
	if(fd<0)
	{
		return -1;
	}
	write(fd,bitmap->bitfield,bitmap->bitfield_length);
	close(fd);
	
	return 0;
}



/*
 *功能：判断具有src位图的peer对具有dst位图的peer是否感兴趣
 *返回：感兴趣返回1 不感兴趣返回0 错误返回-1
*/

int is_intersted(Bitmap *dst,Bitmap* src)
{
	unsigned char const_char[8]={0x80,0x40,0x20,0x10,0x08,0x04,0x02,0x01};
	unsigned char c1,c2;
	int i,j;
	
	if(dst==NULL || src==NULL)
	{
		return -1;
	}
	
	if(dst->bitfield==NULL || src->bitfield==NULL)
	{
		return -1;
	}
	
	if(dst->bitfield_length!=src->bitfield_length ||
	   dst->valid_length!=src->valid_length)
	   {
		   return -1;
	   }
	   
	/*
		如果dst中某位为1，而src中对应位0，则说明src对dst感兴趣
		这里的for的条件是：i<dst->bitfield_length-1
		因为没有在这里考虑位图最后一个字节
	*/
	for(i=0;i<dst->bitfield_length-1;i++)
	{
		for(j=0;j<8;j++)
		{
			c1=(dst->bitfield)[i] & const_char[j];
			c2=(src->bitfield)[i] & const_char[i];
			
			if(c1>0 && c2==0)
			{
				return 1;
			}
		}
	}
	
	j=dst->valid_length%8;
	c1=dst->bitfield[dst->bitfield_length-1];
	c2=src->bitfield[src->bitfield_length-1];
	
	for(i=0;i<j;i++)
	{
		if((c1&const_char[i])>0 && (c2&const_char[i])==0)
		{
			return 1;
		}
	}
	
	return 0;
}


/*
 *功能：获取当前已经下载的piece数
 *返回：当前已经下载的piece数
*/
int get_download_piece_num()
{
	unsigned char const_char[8]={0x80,0x40,0x20,0x10,0x08,0x04,0x02,0x01};
	int i,j;
	
	if(bitmap==NULL || bitmap->bitfield==NULL)
	{
		return 0;
	}
	
	download_piece_num=0;
	
	for(i=0;i<bitmap->bitfield_length-1;i++)
	{
		for(j=0;j<8;j++)
		{
			if(((bitmap->bitfield)[i] & const_char[i])!=0)
			{
				download_piece_num++;
			}
		}
	}
	
	//c存放最后一个字节的值
	unsigned char c=(bitmap->bitfield)[i];
	//j存放位图最后一个字节的有效位数
	j=bitmap->valid_length % 8;
	
	for(i=0;i<j;i++)
	{
		if((c & const_char[i]) !=0)
		{
			download_piece_num++;
		}
	}
	
	return download_piece_num;
}
