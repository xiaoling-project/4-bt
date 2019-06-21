// 本 note 基于前面两位同学的注释，感谢他们！！我修正了一些小的错误，并补充了一些内容。
// pluvet 2019年6月19日

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <malloc.h>
#include "data.h"
#include "tracker.h"
#include "bitfield.h"
#include "torrent.h"
#include "parse_metafile.h"
#include "signal_handler.h"
#include "policy.h"
#include "log.h"

#define  DEBUG

int main(int argc, char *argv[])
{
    int ret;

    if(argc != 2) {
		// 使用格式： 程序 种子文件
        printf("usage:%s metafile\n",argv[0]);
        exit(-1);
    }

    // 设置信号回调（处理）函数
    if(set_signal_handler() != 0)  {
        printf("%s:%d error\n",__FILE__,__LINE__);
        return -1;
    }

    // 解析种子文件，失败则退出。
    if( parse_metafile(argv[1]) != 0)  {
        printf("%s:%d error\n",__FILE__,__LINE__);
        return -1;
    }

    // 初始化未阻塞端
    // choke(拒绝/阻塞)、unchoke(接受)
    // 可参考的资料：https://blog.csdn.net/hgy413/article/details/49077675
    init_unchoke_peers();

    // 创建文件，以保存下载数据
    if( create_files() != 0)  {
        printf("%s:%d error\n",__FILE__,__LINE__);
        return -1;
    }

    // 创建位图
    if(create_bitfield() != 0)  {
        printf("%s:%d error\n",__FILE__,__LINE__);
        return -1;
    }

    // 创建缓冲
    if(create_btcache() != 0)  {
        printf("%s:%d error\n",__FILE__,__LINE__);
        return -1;
    }

    // 进行数据传输
    download_upload_with_peers();

    // 收尾工作
    do_clear_work();

    return 0;
}