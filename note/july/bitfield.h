#ifndef BITFIELD_H
#define BITFIELD_H

typedef struct _Bitmap {
	unsigned char *bitfield;       // 保存位图，每一个Peer都有一个bitmap来存储位图的内容,将会发送给其他的Peer,是通过一个字节来存储的
									//一个字节有8个位,为什么不用int类型或者其他的类型呢?应该是8个位代表一个字节，应该是比较适合的存储  
									//单元，其他的存储单元大小都是字节的整数倍，故用该大小来存储应该是最适合的
	int           bitfield_length; // 位图所占的总字节数
	int           valid_length;    // 位图有效的总位数,每一位代表一个piece
} Bitmap;

int  create_bitfield();                       // 创建位图,分配内存并进行初始化
int  get_bit_value(Bitmap *bitmap,int index); // 获取某一位的值
int  set_bit_value(Bitmap *bitmap,int index,
				   unsigned char value);      // 设置某一位的值
int  all_zero(Bitmap *bitmap);                // 全部清零
int  all_set(Bitmap *bitmap);                 // 全部设置为1
void release_memory_in_bitfield();            // 释放bitfield.c中动态分配的内存
int  print_bitfield(Bitmap *bitmap);          // 打印位图值,用于调试

int  restore_bitmap(); // 将位图存储到文件中 
                       // 在下次下载时,先读取该文件获取已经下载的进度
int  is_interested(Bitmap *dst,Bitmap *src);  // 拥有位图src的peer是否对拥有
                                              // dst位图的peer感兴趣
int  get_download_piece_num(); // 获取当前已下载到的总piece数

#endif
