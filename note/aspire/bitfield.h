#ifndef BITFIELD_H
#define BITFIELD_H

/*
 * BT协议采用的是用位图来映射该客户端pieces是否被下载
 * 一个bit代表一个pieces，1就是代表下载了，0就是代表没有下载
 * 对于最后一个pieces而言，是不一样的，可能使用一个字节来表示。但是也仅仅使用了最高位，后面7位没有用
 * 因为数据只能以一个字节一个字节的出现，所以对于pieces的最后一个字节总是特别"对待"的
 * 因为假设每一个pieces是256KB,但是共享文件的大小不会总是256KB的整数倍
 */

typedef struct _Bitmap
{
	unsigned char *bitfield;		//保存位图
	int bitfield_length;			//位图所占字节数
	int valid_length;				//位图有效总位数，每一位代表一个piece
}Bitmap;

int create_bitfield();				//创建位图，分配内存并初始化
int get_bit_value(Bitmap *bitmap,int index);	//获取每一位的值
int set_bit_value(Bitmap *bitmap,int index,unsigned char value);	//设置某一位的值
int all_zero(Bitmap *bitmap);		//全部清零
int all_set(Bitmap *bitmap);		//全部置1
void release_memory_in_bitfield();		//释放bitfiled.c中动态分配的内存
int print_bitfield(Bitmap *bitmap);		//打印位图，用于调试
int restore_bitmap();		//将位图存贮在文件中
int is_intersted(Bitmap *dst,Bitmap *src);		//拥有位图src的peer是否位拥有位图dst的peer感兴趣
int get_download_piece_num();			//获取当前已下载的总piece数

#endif