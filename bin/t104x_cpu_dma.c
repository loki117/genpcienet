/**/
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <memory.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <sys/mman.h>
#include <time.h>  

#include <linux/types.h>


#define EOS_DMA_MAX_LEN 0xa000
#define EOS_DMA_MAX_BLOCK 10

#if 0 //t104x use this
#define EOS_SET_DMA_ARG 0
#define EOS_START_DMA_TRANSFER 1
#define EOS_GET_DMA_ADDR 4
#define EOS_READ_BUFFER_DATA 8
#else //p204x use this
#define EOS_SET_DMA_ARG 0
#define EOS_START_DMA_TRANSFER 1
#define EOS_GET_DMA_ADDR 2
#define EOS_READ_BUFFER_DATA 3

#endif




struct eos_dma_arg
{
	unsigned long long addr_size[EOS_DMA_MAX_BLOCK][2];
	unsigned int num;
};

struct eos_print_dma_unit
{
	unsigned int off;
	unsigned int data;
};


/*************************************************************************
* 函数名  ： eos_open_dma
* 负责人  ：罗雄豹
* 创建日期：20160621
* 函数功能：dma打开函数
* 输入参数：devname，设备名称        
* 输出参数：
* 返回值：	OK: 成功      
* 调用关系：                            
*************************************************************************/
int eos_open_dma(const char *devname)
{
	int fd = 0;
	fd = open(devname, O_RDWR);
	if (fd == -1)
	{
		printf("can not open dma device\n");
		return -1;
	}
	return fd;
}



/*************************************************************************
* 函数名  ： eos_close_dma
* 负责人  ：罗雄豹
* 创建日期：20160621
* 函数功能：dma关闭函数
* 输入参数：fd，设备号        
* 输出参数：
* 返回值：	OK: 成功      
* 调用关系：                            
*************************************************************************/
int eos_close_dma(int fd)
{
	close(fd);
	return 0;
}


/*************************************************************************
* 函数名  ： eos_init_dma_arg
* 负责人  ：罗雄豹
* 创建日期：20160621
* 函数功能：dma初始化函数
* 输入参数：fd，设备号        
* 输出参数：
* 返回值：	OK: 成功      
* 调用关系：                            
*************************************************************************/
int eos_init_dma_arg(unsigned int fd, unsigned long long (*p)[2], unsigned int num)
{
	int i = 0;
	int ret = 0;
	unsigned int all_length = 0;
	struct eos_dma_arg dma_arg;
	if (num > EOS_DMA_MAX_BLOCK)
	{
		printf("arg num too large\n");
		return -1;
	}
	for (i = 0; i < num; i++)
	{
		dma_arg.addr_size[i][0] = p[i][0];
		dma_arg.addr_size[i][1] = p[i][1];
	}
	dma_arg.num = num;
	for (i = 0; i < dma_arg.num; i++)
	{
		all_length = all_length + dma_arg.addr_size[i][1];
	}
	if (all_length > EOS_DMA_MAX_LEN)
	{
		printf("size too large\n");
		return -1;
	}
	ret = ioctl(fd,EOS_SET_DMA_ARG, &dma_arg);
	if (ret == -1)
	{
		printf("set dma arg error\n");
		return -1;
	}
	return 0;
}


/*************************************************************************
* 函数名  ： eos_start_dma_transfer
* 负责人  ：罗雄豹
* 创建日期：20160621
* 函数功能：dma开始函数
* 输入参数：fd，设备号        
* 输出参数：
* 返回值：	OK: 成功      
* 调用关系：                            
*************************************************************************/
int eos_start_dma_transfer(unsigned int fd)
{
	int ret = 0;
	ret = ioctl(fd,EOS_START_DMA_TRANSFER,NULL);
	if(ret == -1)
	{
		printf("start dma transfer error\n");
		return -1;
	}
	return 0;
}

/*************************************************************************
* 函数名  ： eos_start_dma_transfer
* 负责人  ：罗雄豹
* 创建日期：20160621
* 函数功能：dma开始函数
* 输入参数：fd，设备号        
* 输出参数：
* 返回值：	OK: 成功      
* 调用关系：                            
*************************************************************************/
int  eos_get_dma_buffer_addr(unsigned int fd,unsigned long long *addr)
{
	unsigned long long dma_phy_addr = 0;
	int mem_fd = 0;
	int ret = 0;
	unsigned char *mmap_addr;
	ret = ioctl(fd,EOS_GET_DMA_ADDR,&dma_phy_addr);
	if(ret == -1)
	{
		printf("get dma phy addr error\n");
		return -1;
	}
	mem_fd = open("/dev/mem",O_RDWR);
	if(mem_fd == -1)
	{
		printf("open device /dev/mem fail\n");
		return -1;
	}
	mmap_addr = (unsigned char *)mmap64(NULL, EOS_DMA_MAX_LEN * 4, PROT_READ | 0,
		MAP_SHARED, mem_fd, dma_phy_addr);

        printf("dma_phy_addr is %llx\n",dma_phy_addr);


	if(mmap_addr == MAP_FAILED)
	{
		printf("mmap /dev/mem fail\n");
		close(mem_fd);
		return -1;
	}
	*addr = (unsigned long long )mmap_addr;
	
	//close((mem_fd);
	return 0;
}	

/*************************************************************************
* 函数名  ： eos_get_dma_status
* 负责人  ：罗雄豹
* 创建日期：20160621
* 函数功能：dma获取状态函数
* 输入参数：fd，设备号        
* 输出参数：
* 返回值：	OK: 成功      
* 调用关系：                            
*************************************************************************/
int eos_get_dma_status(unsigned int fd)
{
 	int status;
	read(fd,&status,4);
	return status;
}

/*************************************************************************
* 函数名  ： eos_print_dma_data
* 负责人  ：罗雄豹
* 创建日期：20160621
* 函数功能：dma打印数据函数
* 输入参数：fd，设备号        
* 输出参数：
* 返回值：	OK: 成功      
* 调用关系：                            
*************************************************************************/
int eos_print_dma_data(unsigned int fd,unsigned long long addr)
{
        struct eos_print_dma_unit dma_data;
        //int fd = 0;
        //memset(&dma_data,0,sizeof(struct eos_print_dma_unit));
       
        dma_data.off = addr;
        /*fd = open(devname,O_RDWR);
        if(-1 == fd)
        {
                printf("open device pciedev fail\n");
                return -1;
        }*/
        ioctl(fd,EOS_READ_BUFFER_DATA,&dma_data);
        printf("addr =  0x%x,data = 0x%x\n",dma_data.off,dma_data.data);
        close(fd);
        return 0;

}


/*************************************************************************
* 函数名  ： diff_data
* 负责人  ：罗雄豹
* 创建日期：20160621
* 函数功能：比较数据函数
* 输入参数：len，数据长度，rd_buf， wr_buf比较的数组       
* 输出参数：
* 返回值：	OK: 成功      
* 调用关系：                            
*************************************************************************/
int diff_data(int len, int *rd_buf, int *wr_buf)
{
     int i;
	 int diff = 0;
	 for (i = 0; i < len; i++)
	 {
	     if(rd_buf[i] != wr_buf[i])
	     {
	         diff = 1;
			 return diff;
	     }
	 }
	 return diff;
}


/*************************************************************************
* 函数名  ： str_to_hex
* 负责人  ：罗雄豹
* 创建日期：20150520
* 函数功能：字符串转换为16进制数据
* 输入参数：
       				
       			str-字符串buffer
* 输出参数：无
* 返回值：	无
       			介绍函数功能
*************************************************************************/
 unsigned long long str_to_hex(char *str)
{
    char buf[16];
	char off[16];
	char *pstr;
    unsigned long long  sum = 0;
    int len;
	int i;
    strcpy(buf, str);
    if (buf[1] == 'x' || buf[1] == 'X')
	{
        pstr = &buf[2];
        strcpy(off, pstr);
        strcpy(buf, off);
    }
    len = strlen(buf);
    for (i = 0; i<len; i++)
	{
        if (buf[i] >= '0' && buf[i] <= '9')
    	{
            buf[i] -= '0';
    	}
        else if (buf[i] >= 'A' && buf[i] <= 'F')
    	{
            buf[i] = buf[i] - '0' - 7;
    	}
        else
    	{
            buf[i] = buf[i] - '0' - 39;
    	}
    }
    for (i = 0; i < len; i++)
	{
        sum = sum * 16 + buf[i];
    }
    return sum;
}

#if 1

/*xbluo add 20161025*/
#if 1
typedef unsigned long u32_dma_addr;
#else
typedef unsigned long long u32_dma_addr;

#endif
#define EOS_DMA_MAX_CHANNEL (2)

/*
channel号不能大于2
如channel 0、1
*/

struct eos_dma_arg_channel
{
	unsigned long long addr_size[EOS_DMA_MAX_BLOCK][2];
	unsigned int num;
	unsigned int ch_num;

};


/*************************************************************************
* 函数名  ：eos_open_dma_channel
* 负责人  ：
* 创建日期：
* 函数功能：
* 输入参数：
* 输出参数：
* 返回值：
* 调用关系：
* 其?   它：
*************************************************************************/
int eos_open_dma_channel(const char *devname)
{
	int fd = 0;
	fd = open(devname,O_RDWR);
	if(fd == -1)
	{
		printf("can not open dma device\n");
		return -1;
	}
	return fd;
}


/*************************************************************************
* 函数名  ：eos_close_dma_channel
* 负责人  ：
* 创建日期：
* 函数功能：
* 输入参数：
* 输出参数：
* 返回值：
* 调用关系：
* 其?   它：
*************************************************************************/
int eos_close_dma_channel(int fd)
{
	close(fd);
	return 0;
}


/*************************************************************************
* 函数名  ：eos_init_dma_arg_channel
* 负责人  ：
* 创建日期：
* 函数功能：
* 输入参数：
* 输出参数：
* 返回值：
* 调用关系：
* 其?   它：
*************************************************************************/
int eos_init_dma_arg_channel(unsigned int fd,unsigned long long (*p)[2],unsigned int num, int channel)
{
	int i = 0;
	int ret = 0;
	unsigned int all_length = 0;
	struct eos_dma_arg_channel dma_arg;
	if(num > EOS_DMA_MAX_BLOCK)
	{
		printf("arg num too large\n");
		return -1;
	}
	for(i = 0;i<num;i++)
	{
		dma_arg.addr_size[i][0] = p[i][0];
		dma_arg.addr_size[i][1] = p[i][1];
	}
	dma_arg.num = num;
	for(i = 0;i<dma_arg.num;i++)
	{
		all_length = all_length + dma_arg.addr_size[i][1];
	}
	if(all_length > EOS_DMA_MAX_LEN)
	{
		printf("size too large\n");
		return -1;
	}
	dma_arg.ch_num = (channel);
	ret = ioctl(fd,EOS_SET_DMA_ARG,&dma_arg);
	if(ret == -1)
	{
		printf("set dma arg error\n");
		return -1;
	}
	return 0;
}


/*************************************************************************
* 函数名  ：eos_get_dma_buffer_addr_channel
* 负责人  ：
* 创建日期：
* 函数功能：
* 输入参数：
* 输出参数：
* 返回值：
* 调用关系：
* 其?   它：
*************************************************************************/
int  eos_get_dma_buffer_addr_channel(unsigned int fd,u32_dma_addr *addr, int channel)
{
	unsigned int dma_phy_addr = 0;
	int mem_fd = 0;
	int ret = 0;
	unsigned char *mmap_addr;
	dma_phy_addr = channel;
	ret = ioctl(fd,EOS_GET_DMA_ADDR,&dma_phy_addr);
	printf("dma_phy_addr =0x%lx\n", dma_phy_addr);
	if(ret == -1)
	{
		printf("get dma phy addr error\n");
		return -1;
	}
	mem_fd = open("/dev/mem",O_RDWR);
	if(mem_fd == -1)
	{
		printf("open device /dev/mem fail\n");
		return -1;
	}
	mmap_addr = (unsigned char *)mmap(NULL,EOS_DMA_MAX_LEN * 4,PROT_READ,MAP_SHARED,mem_fd,dma_phy_addr);
	if(mmap_addr == MAP_FAILED)
	{
		printf("mmap /dev/mem fail\n");
		close(mem_fd);
		return -1;
	}
	printf("mmap_addr =0x%lx\n", mmap_addr);
	*addr = (unsigned long long)mmap_addr;
	printf("addr =0x%lx\n", addr);

	return 0;
}


/*************************************************************************
* 函数名  ：eos_get_dma_status_channel
* 负责人  ：
* 创建日期：
* 函数功能：
* 输入参数：
* 输出参数：
* 返回值：
* 调用关系：
* 其?   它：
*************************************************************************/
int eos_get_dma_status_channel(unsigned int fd, int channel)
{
 	int status;
	status = channel;
	read(fd,&status,4);
	return status;
}


/*************************************************************************
* 函数名  ：eos_start_dma_transfer_channel
* 负责人  ：
* 创建日期：
* 函数功能：
* 输入参数：
* 输出参数：
* 返回值：
* 调用关系：
* 其?   它：
*************************************************************************/
int eos_start_dma_transfer_channel(unsigned int fd, int channel)
{
	int ret = 0;
	//unsigned long channel_tmp = 0;
	if ((channel < 0) || (channel >= EOS_DMA_MAX_CHANNEL))
       {
                printf("copy_to_user error err channel %d\n", channel);
                return -1;
       }		
	//channel_tmp = channel;
	//ret = ioctl(fd,EOS_START_DMA_TRANSFER,&channel_tmp);
	ret = ioctl(fd,EOS_START_DMA_TRANSFER,&channel);
	if(ret == -1)
	{
		printf("start dma transfer error\n");
		return -1;
	}
	return 0;
}


#endif




/*************************************************************************
* 函数名  ： main
* 负责人  ：罗雄豹
* 创建日期：20160621
* 函数功能：main函数
* 输入参数：
* 输出参数：
* 返回值：	OK: 成功      
* 调用关系：                            
*************************************************************************/
int main(int argc, char *argv[])
{
	int fd = 0;
	int i = 0;
	unsigned long addr = 0;
//	u32_dma_addr  addr;
	int len = 16;
//	unsigned long long data[4][2] = {{0x4044a0b000,252},{0x4044a06000,252},{0x4044b03000,252},{0x4044b84000,4000}};
//	unsigned long long data[4][2] = {{0x80000000,252},{0x80100000,252},{0x81000000,252},{0x82000000,4000}};
	unsigned long long data[4][2] = {{0x4846000000,252},{0x4846000000,252},{0x4846000000,252},{0x4846000000,4000}};


	struct timeval tpstart;
	struct timeval tpend;    
	float timeuse;
	float timeuse_min = 0.01;
	float timeuse_max = 0.000001;
	float timeuse_mid  = 0;
	int err_times = 0;
	int times;
	
	int ret;
	int rev_buffer[EOS_DMA_MAX_LEN];
	int wr_buffer[EOS_DMA_MAX_LEN];

	int channel = 0;

printf("%s %s\n", __DATE__,__TIME__);
	if (argc <= 1)
	{
		printf("arg:addr1  len addr2 channel times\n");
		return 0;
	}

	
	fd = eos_open_dma_channel("/dev/dma_control0_channel0");
	if(fd == -1)
	{
		return -1;
	}
	if (argc >= 2)
	{
#if 1
	    data[0][0] = (unsigned long)str_to_hex(argv[1]);
	    //data[2][0] = (unsigned long)str_to_hex(argv[1]);

#endif		//data[0][0] *= 4;
		printf("read  1 offset  0x%lx [0x%lx]\n", (unsigned long)strtoul(argv[1], NULL, 0), str_to_hex(argv[1]));
	}
	if (argc >= 3)
	{
#if 1
	    len = (unsigned int)strtoul(argv[2], NULL, 0);//atoi(argv[2]);
	    data[0][1] =  1 * (unsigned int)strtoul(argv[2], NULL, 0);//atoi(argv[2]);
	    //data[2][1] =  1 * (unsigned int)strtoul(argv[2], NULL, 0);//atoi(argv[2]);
	    data[1][1]  = data[0][1];
		//data[3][1]  = data[0][1];

		len =len *4;
#else	



		len = 0;
		for (i = 0;i < 4; i++)
		{	
			len += data[i][1];
		}
		if (len > EOS_DMA_MAX_LEN)
		{
			len = EOS_DMA_MAX_LEN;
		}	
		else
		{	
			len /=4;
		}
#endif 		printf("read len  0x%x[0x%x]\n", (unsigned int)strtoul(argv[2], NULL, 0), data[0][1]);
	}	
	if (argc > 4)
	{
	  //data[0][1] = data[0][1] ;
	 //data[1][0] = 4 * (unsigned int)strtoul(argv[4], NULL, 0);
	 //data[1][1]  = data[0][1];
#if 1
	   data[1][0] = (unsigned long)str_to_hex(argv[3]);
	   //data[3][0] = (unsigned long)str_to_hex(argv[3]);
#else
            data[0][0] = (unsigned long long)str_to_hex(argv[1]);
            data[1][0] = (unsigned long long)str_to_hex(argv[1]);
	    data[2][0] = (unsigned long long)str_to_hex(argv[2]);
            data[3][0] = (unsigned long long)str_to_hex(argv[3]);
		printf("addr1 0x%llx,addr2 0x%llx,addr3 0x%llx,addr4 0x%llx\n", data[0][0],data[1][0],data[2][0],data[3][0]);
		printf("len1  0x%llx,len2  0x%llx,len3  0x%llx,len4  0x%llx\n", data[0][1],data[1][1],data[2][1],data[3][1]);
#endif		
		//data[0][0] *= 4;
		printf("read 2 offset  0x%lx [0x%lx]\n", (unsigned long)strtoul(argv[3], NULL, 0), str_to_hex(argv[3]));	 
	}
	else
	{
	 //data[0][1] = data[0][1] ;
	 //data[1][0] = 4 * 0x14000; 
	 //data[1][1]  = data[0][1];	
	}

	if (argc > 5)
	{
	       channel = (unsigned long)str_to_hex(argv[4]);
		printf("channel= %d\n", channel);
	}
        printf("eos_init_dma_arg\n");
	eos_init_dma_arg_channel(fd, data, 2, channel);
        printf("eos_init_dma_arg\n");

	eos_get_dma_buffer_addr_channel(fd, &addr, channel);
        printf("eos_get_dma_buffer_addr\n");
        //eos_print_dma_data(fd, &addr);
        printf("addr is 0x%lx\n",addr);
	

		eos_start_dma_transfer_channel(fd, channel);
        printf("eos_start_dma_transfer\n");	
			//eos_start_dma_transfer(fd);
        //printf("eos_start_dma_transfer==2\n");	
	//memcpy(wr_buffer, (unsigned int *)addr, 4);
        printf("2\n");
        ret = eos_get_dma_status_channel(fd, channel);
         printf("3\n");
while ((ret == 1))
{
ret = eos_get_dma_status_channel(fd, channel);		    			
}		 
	if(ret == 1)
	{
		printf("this is busy = %d\n", ret);
		sleep(1);
	}
	printf("DMA get data  0:");
	memcpy(wr_buffer, (unsigned int  *)(addr), sizeof(len * 4));
	printf("DMA get data:");
	if ((len > 0) && (len < EOS_DMA_MAX_LEN))
    	{
        	for(i = 0;i < len; i++)
        	{
       	    if (i % 16 == 0)
       	    {
    			 printf("\n0x%04x :", 16 * (i / 16));
       	    }
        		
    			printf(" %08x",*((unsigned int *)(addr) + i));
    
        		
        	}
    	}
	printf("\n");



int add_times = 0;


if (argc > 4)
{
	times = (unsigned int)strtoul(argv[5], NULL, 0);
	printf("test times %d", times);
	for (i = 0; i < times; i++)
        {
//     	gettimeofday(&tpstart, NULL);	
        	eos_init_dma_arg_channel(fd,data, 2, channel);
        	eos_start_dma_transfer_channel(fd, channel);//Read_Fpga_Dma(fd,rd_buffer, offset, len);
       	//eos_start_dma_transfer(fd);
        	//eos_get_dma_buffer_addr(fd,&addr);
	       ret = eos_get_dma_status_channel(fd, channel);
		add_times = 0;
#if 1
		//while ((ret == 1) && 1 /*(100 > add_times)*/)
		while ((ret == 1) &&  (100 > add_times))
		{
		    ret = eos_get_dma_status_channel(fd, channel);
		    usleep(10);
			add_times++;
		}
		if (100 < add_times)
		printf("=%d=[%d]\n", add_times,  i);	
        	gettimeofday(&tpend, NULL); 	
        	timeuse = 1000000 * (tpend.tv_sec - tpstart.tv_sec) + tpend.tv_usec - tpstart.tv_usec;		
        	timeuse = timeuse / 1000000;   
		memset(rev_buffer, 0, sizeof(len * 4));
		memcpy(rev_buffer, (unsigned int *)(addr), sizeof(len * 4));
        	ret = diff_data(len, wr_buffer, rev_buffer);
		if (timeuse_min > timeuse)
		{
		    timeuse_min = timeuse;
		}
		if (timeuse_max < timeuse)
		{
		    timeuse_max = timeuse;
		}
		timeuse_mid +=  timeuse;
		if (ret == 1)
              err_times++;
#else	
		memset(rev_buffer, 0, sizeof(len * 4));
		memcpy(rev_buffer, (unsigned int *)(addr), sizeof(len * 4));
		ret = diff_data(len, wr_buffer, rev_buffer);
		if (ret == 1)
              err_times++;

		usleep(500);
#endif   	
        }
	printf("\n");
	 printf("err times %d  min_time =%f max_time =%f mid_time =%f \n",
	 	err_times, timeuse_min, timeuse_max,timeuse_mid / times);
	
}

close(fd);
	
}
