/* 

   Fiberhome Technology RCU845 platform driver.  Allows a user space
   process to play with the cpld misc function.

   Copyright (c) 2010,2011 Liu Yonggang <ygliu@fiberhome.com.cn> */

#include <linux/device.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <linux/interrupt.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/dmapool.h>
#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/kthread.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/moduleparam.h>

#define DRVNAME "dma_control0_channel0"
#define DMA_VERSION "v1.0"

MODULE_AUTHOR("lmyan <lmyan@fiberhome.com.cn>");
MODULE_DESCRIPTION(" dma control0 channel0 driver");
MODULE_LICENSE("GPL");

/* 用于表示调试打印*/
static int s_debug = 0;
module_param(s_debug, int, 0);
#define print(fmt, args...) do { \
    if (s_debug) \
    { printk(fmt, ## args); } \
    } while(0);

/* 用于表示中断*/
static dev_t s_devid;

/* 用于表示设备*/
static struct cdev s_dma_cdev;

/* 用于表示平台设备*/  
static struct platform_device *s_pdev;
static struct class *s_dma_class;

static int s_major = 0;           /* default to dynamic s_major */

#define MAX_LENGTH 0xa000
#define MAX_BLOCK 10

#define EOS_SET_DMA_ARG 0
#define EOS_START_DMA_TRANSFER 1
#define EOS_GET_DMA_ADDR 2
#define EOS_READ_BUFFER_DATA 3

/* dma中断号*/
#define DMA_IRQ 28

/* 用于表示中断*/
static int s_dma_soft_irq = NO_IRQ;

/* 用于dma传输的缓存*/
static unsigned char *s_dma_buf = NULL;

/* dma的内存的物理地址*/
static dma_addr_t s_dma_addr;

/* dma的物理地址*/
static unsigned long  s_dma_phy;

/* dma传输出错标志*/
static unsigned int s_dma_transfer_error = 0;

/* dma编程出错标志*/
static unsigned int s_dma_programming_error = 0;


struct dma_arg
{	
	unsigned long long addr_size[MAX_BLOCK][2];
	unsigned int num;	
};

struct fsldma_regs
{
	unsigned int mr;              
	unsigned int sr;
	unsigned int eclndar;
	unsigned int clndar;
	unsigned int satr;
	unsigned int sar;
	unsigned int datr;
	unsigned int dar;
	unsigned int bcr;
};


struct pic_regs
{
	unsigned int iivpr; //MPIC_IIVPR28
	unsigned int res[3]; //MPIC_IIDR28 MPIC_IILR28 MPIC_IIVPR29
	unsigned int iidr; //MPIC_IIDR29
};


/*dma 控制器寄存器*/
static struct fsldma_regs *s_dma_control_regs;

/*中断控制器寄存器*/
static struct pic_regs *s_dma_control_pic;

/* 用于检测一直忙状态时，1表示忙，0表示空闲*/
static unsigned int s_dma_is_running = 0;

/* 用于检测一直忙状态时，长度的统计*/
static unsigned int s_dma_check_length;

/*用于检测一直忙状态时，地址的统计*/
static unsigned int s_dma_check_sraddress; 

/*用于处于一直busy状态的统计*/
static unsigned int s_dma_busy_times;

/*用于两次读操作，上次操作仍未完成的统计*/ 
static unsigned int s_dma_timeout_num;

/*dma 描述符*/
struct dma_link_descriptor 
{
	u64 src_addr;
	u64 dst_addr;
	u64 next_ln_addr;
	u32 count;
	u32 reserve;
} __attribute__((aligned(32)));

/*dma描述符pool*/
static struct dma_pool *s_dma_link_pool;

/*dma描述符数组*/
static struct dma_link_descriptor *s_dma_link_descriptor[MAX_BLOCK];

/*dma描述符物理地址数组*/
static dma_addr_t s_dma_link_descriptor_phy[MAX_BLOCK];


struct eos_print_dma_unit
{
	unsigned int off;
	unsigned int data;
};

#if 1

/*xbluo add 20161008*/

#define EOS_DMA_MAX_CHANNEL (2)


struct dma_arg_channel
{
	unsigned long long addr_size[MAX_BLOCK][2];
	unsigned int num;
	unsigned int ch_num;
};

/*dma传输数据的结构体*/
static struct dma_arg_channel s_args_channel[EOS_DMA_MAX_CHANNEL];

/*dma传输数据的结构体*/
static struct dma_arg_channel s_args_channel_tmp;

/*dma传输地址*/
static unsigned char *s_dma_buf_channel[EOS_DMA_MAX_CHANNEL] = {0};

/*dma传输地址*/
static dma_addr_t s_dma_addr_channel[EOS_DMA_MAX_CHANNEL];

/*dma传输地址*/
static unsigned long s_dma_phy_channel[EOS_DMA_MAX_CHANNEL];

/*dma准备发起传输的通道*/
static unsigned int s_dma_is_running_channel[EOS_DMA_MAX_CHANNEL] = {0};

/*dma正在传输的通道*/
static unsigned int s_tran_channel = EOS_DMA_MAX_CHANNEL + 1;

/*dma的spin锁*/
static spinlock_t s_dma_lock;



#endif



static int dma_open(struct inode *inode,struct file *file);
static int dma_release(struct inode*,struct file*);

static ssize_t dma_read(struct file*,char __user*,size_t,loff_t*);
static ssize_t dma_write(struct file*,const char __user*,size_t,loff_t*);
static int dma_ioctl(struct inode *inode,struct file*,unsigned int,unsigned long);

static int __devinit dma_probe(struct platform_device *dev);
static int __init dma_init(void);


/*************************************************************************
* 函数名  ： dma_open
* 负责人  ：张淞钦
* 创建日期：20160621
* 函数功能：dma驱动open函数
* 输入参数：inode:节点，file，文件                             
* 输出参数：
* 返回值：	0: 成功      
* 调用关系：                            
*************************************************************************/
static int dma_open(struct inode *inode, struct file *file)
{

	return 0;
}


/*************************************************************************
* 函数名  ： dma_release
* 负责人  ：张淞钦
* 创建日期：20160621
* 函数功能：dma驱动release函数
* 输入参数：inode:节点，file，文件                             
* 输出参数：
* 返回值：	0: 成功      
* 调用关系：                            
*************************************************************************/
static int dma_release(struct inode *inode, struct file *file)
{
/*	int i = 0;
	for(i=0;i<MAX_BLOCK;i++)
	{
		dma_pool_free(s_dma_link_pool,s_dma_link_descriptor[i],s_dma_link_descriptor_phy[i]);
	}
	dma_pool_destroy(s_dma_link_pool);

	dma_free_coherent(&s_pdev->dev,MAX_LENGTH*4,s_dma_buf,s_dma_addr);
	
	free_irq(s_dma_soft_irq,NULL);
	iounmap(s_dma_control_regs);
	iounmap(s_dma_control_pic);
	
	cdev_del(&s_dma_cdev);   
	device_destroy(s_dma_class,s_devid);
	class_destroy(s_dma_class); 
	unregister_chrdev_region(s_devid, 1);
*/

        return 0;
}

/*************************************************************************
* 函数名  ： dma_write
* 负责人  ：张淞钦
* 创建日期：20160621
* 函数功能：dma驱动write函数
* 输入参数：file，文件buf:指针， count:数量，ppos:偏移         
* 输出参数：
* 返回值：	0: 成功      
* 调用关系：                            
*************************************************************************/
static ssize_t dma_write(struct file *file, const char __user *buf,size_t count, loff_t *ppos)
{
	return 0;
}


/*************************************************************************
* 函数名  ： dma_read
* 负责人  ：张淞钦
* 创建日期：20160621
* 函数功能：dma驱动read函数
* 输入参数：file，文件buf:指针， count:数量，ppos:偏移         
* 输出参数：
* 返回值：	0: 成功      
* 调用关系：                            
*************************************************************************/
static ssize_t dma_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
#if 1

/*xbluo add 20161013*/
	int channel = -1;

	copy_from_user(&channel, (void *)buf, 4);

	if ((channel < 0) || (channel >= EOS_DMA_MAX_CHANNEL))
	{
		printk("copy_to_user error err channel %d\n", channel);
		return -1;
	}	
	
#endif

	if (copy_to_user(buf, &(s_dma_is_running_channel[channel]), 4))
	{
		printk("copy_to_user error\n");
		return -1;
	}
	return 0;
}

/*************************************************************************
* 函数名  ： dma_cpy
* 负责人  ：罗雄豹
* 创建日期：20161013
* 函数功能：dma复制函数
* 输入参数：dma_arg_channel，传输的地址和长度结构体       
* 输出参数：
* 返回值：	0: 成功      
* 调用关系：                            
*************************************************************************/
void dma_cpy(struct dma_arg_channel *dma_arg)
{
	unsigned int addr_off;
	unsigned int arg_loop;
	addr_off = 0;				

	for(arg_loop = 0; arg_loop < dma_arg->num; arg_loop++)
	{
		s_dma_link_descriptor[arg_loop]->src_addr = ((u64)0x00050000 << 32) | dma_arg->addr_size[arg_loop][0];
		s_dma_link_descriptor[arg_loop]->dst_addr = ((u64)0x00050000 << 32) | (s_dma_addr_channel[dma_arg->ch_num] + addr_off);
		s_dma_link_descriptor[arg_loop]->count = dma_arg->addr_size[arg_loop][1] * 4;
		if(arg_loop == (dma_arg->num - 1))
		{
			s_dma_link_descriptor[arg_loop]->next_ln_addr = 0x1;
		}
		else
		{
			s_dma_link_descriptor[arg_loop]->next_ln_addr = s_dma_link_descriptor_phy[arg_loop + 1];
		}
		addr_off += dma_arg->addr_size[arg_loop][1] * 4;
	}
}



/*************************************************************************
* 函数名  ： dma_ioctl
* 负责人  ：张淞钦
* 创建日期：20160621
* 函数功能：dma驱动ioctl函数
* 输入参数：file，文件buf:指针， count:数量，ppos:偏移         
* 输出参数：
* 返回值：	OK: 成功      
* 调用关系：                            
*************************************************************************/
static int dma_ioctl( struct inode *inode,struct file *file, unsigned int cmd, unsigned long arg)
{
	struct dma_arg args;
	struct eos_print_dma_unit dma_data;
	unsigned int addr_off;
	unsigned int arg_loop;

	unsigned int dma_error_check_loop = 0;
	unsigned int dma_busy_loop;

	unsigned int loop;
	unsigned int channel_num;
	unsigned int tran_start;

	
	spin_lock_irq(&s_dma_lock);


	
	switch(cmd)
	{
	case EOS_SET_DMA_ARG:
		if (copy_from_user(&s_args_channel_tmp, (void *)arg, sizeof(struct dma_arg_channel)))
		{
			printk("set dma arg copy_from_user error\n");
			spin_unlock_irq(&s_dma_lock);
			
			return -1;
		}
		channel_num = s_args_channel_tmp.ch_num;
		if ((channel_num < 0) || (channel_num >= EOS_DMA_MAX_CHANNEL))
		{
			printk("copy_to_user error err channel %d\n", channel_num);
			spin_unlock_irq(&s_dma_lock);
			return -1;
		}	
		if (copy_from_user(&(s_args_channel[channel_num]), (void *)arg, sizeof(struct dma_arg_channel)))
		{
			printk("set dma arg copy_from_user error\n");
			spin_unlock_irq(&s_dma_lock);		
			return -1;
		}
	break;
	
	case EOS_START_DMA_TRANSFER:
		print("EOS_START_DMA_TRANSFER\n");
		copy_from_user(&channel_num, (void *)arg, 4);
	
		if ((channel_num < 0) || (channel_num >= EOS_DMA_MAX_CHANNEL))
		   {
				printk("copy_to_user error err channel %d\n", channel_num);
			  spin_unlock_irq(&s_dma_lock);
			  
					return -1;
		   }	

               if((0x80 == (s_dma_control_regs->sr & 0x80)))         
                {
                       s_dma_transfer_error++;
                        s_dma_control_regs->sr = 0x80;
                        s_dma_is_running = 0;
                        s_dma_is_running_channel[channel_num] = 0;
                        printk("dma_transfer error\n");
                }
                if((0x10 == (s_dma_control_regs->sr & 0x10)))
                {
                        s_dma_programming_error++;
                        s_dma_control_regs->sr = 0x10;
                        s_dma_is_running = 0;
                        s_dma_is_running_channel[channel_num] = 0;
                         printk("dma programming error\n");
                }
                if(0x4 == (s_dma_control_regs->sr & 0x4))
                {
                        if( (s_dma_check_length == s_dma_control_regs->bcr) && 
                        	(s_dma_check_sraddress == s_dma_control_regs->sar))
                        {
                                dma_error_check_loop++;
                                
                                //add check time,avoid the last interrupt not run interrupt function
                                if(dma_error_check_loop >= 5) 
                                {
                                        s_dma_busy_times++;
                                        dma_error_check_loop = 0;
                                        
                                        //abort transfer
                                        s_dma_control_regs->mr = s_dma_control_regs->mr | (1 << 3); 
                                        
                                        printk("dma_transfer abort\n");
                                        s_dma_is_running = 0;
                                        s_dma_is_running_channel[channel_num] = 0;
                                        for(dma_busy_loop = 0 ; dma_busy_loop < 100 ; dma_busy_loop++)
                                        {
                                                        udelay(10);
                                                        if(0x4 != (s_dma_control_regs->sr & 0x4))
                                                        {
                                                                break;
                                                        }
                                        }
                                        if(dma_busy_loop >= 100)
                                        {
                                                printk("abort dma transfer fail\n");
                                                
                                                
                                                spin_unlock_irq(&s_dma_lock);
                                                return -1;
                                        }
                                }
                        }
                        else
                        {
                                s_dma_check_length = s_dma_control_regs->bcr;
                                s_dma_check_sraddress = s_dma_control_regs->sar;
                                dma_error_check_loop = 0;
                        }
                }

                /*if(s_dma_is_running == 1)
                {
                        s_dma_timeout_num += 1;
                        return 0; 
                }*/

                s_dma_check_length = 0;
                s_dma_check_sraddress = 0;
                 
		copy_from_user(&channel_num, (void *)arg, 4);
	
		if ((channel_num < 0) || (channel_num >= EOS_DMA_MAX_CHANNEL))
		{
			printk("copy_to_user error err channel %d\n", channel_num);
			spin_unlock_irq(&s_dma_lock);
			return -1;
		}	

		tran_start = 0;
		for (loop = 0; loop < EOS_DMA_MAX_CHANNEL; loop++)
		{
			if (1 == s_dma_is_running_channel[loop])
			{
				tran_start = 1;
			}
		}
		if (tran_start == 1)
		{
			print("channel tan\n");
		}
		else
		{
			  dma_cpy(&(s_args_channel[channel_num]));
			  s_dma_control_regs->clndar = s_dma_link_descriptor_phy[0];
			  s_dma_control_regs->mr = (0x7<< 24) | (0x1<<8) ;
			  s_dma_control_regs->mr = (0x7<< 24) | (0x1<<8) | 0x1;
	
			  
			  s_tran_channel = channel_num;
		}
		s_dma_is_running_channel[channel_num] = 1;	
	break;
	
	case EOS_GET_DMA_ADDR:
		copy_from_user(&channel_num, (void *)arg, 4);
	
		if ((channel_num < 0) || (channel_num >= EOS_DMA_MAX_CHANNEL))
		{
			printk("copy_to_user error err channel %d\n", channel_num);
			spin_unlock_irq(&s_dma_lock);
		
			return -1;
		}	
		if (copy_to_user((void *)arg, &(s_dma_phy_channel[channel_num]), sizeof(long)))
		{
				 printk("test_buf data copy_from_user error!\n");
				 spin_unlock_irq(&s_dma_lock);
			
				 return -1;
		}	   
	break;
	
	case EOS_READ_BUFFER_DATA:
		if (copy_from_user(&dma_data,(void *)arg,sizeof(struct eos_print_dma_unit)))
		{
			printk("set dma arg copy_from_user error\n");
			spin_unlock_irq(&s_dma_lock);
			return -1;
		}
		dma_data.data = *(unsigned int *)(s_dma_buf + dma_data.off * 4);
		printk("s_dma_buf[0x%x] = 0x%x\n",dma_data.off * 4 ,dma_data.data);
		
		for(loop = 0;loop < args.num;loop++)
		{
			printk("args.addr_size[%d][0] = 0x%llx\n",loop,args.addr_size[loop][0]);
			printk("args.addr_size[%d][1] = 0x%llx\n",loop,args.addr_size[loop][1]);
		}
		printk("s_dma_busy_times= %d\n",s_dma_busy_times);
		printk("args.num = %d\n",args.num);
		printk("s_dma_transfer_error = 0x%x\n",s_dma_transfer_error);
		printk("s_dma_programming_error = 0x%x\n",s_dma_programming_error);
		printk("s_dma_timeout_num = 0x%x\n",s_dma_timeout_num);
		printk("eos dma ver 6.0\n");
	break;
	
	default:
		printk("cmd not found\n");
		spin_unlock_irq(&s_dma_lock);
		return -1;
	break;
	
	}
	spin_unlock_irq(&s_dma_lock);
	return 0;
}

/*************************************************************************
* 函数名  ： dma_control0_channel1_interrupt
* 负责人  ：张淞钦
* 创建日期：20160621
* 函数功能：dma中断函数
* 输入参数：irq，中断dev_instance:数据结构       
* 输出参数：
* 返回值：	OK: 成功      
* 调用关系：                            
*************************************************************************/
static irqreturn_t dma_control0_channel1_interrupt(int irq, void *dev_instance)
{
	unsigned long flags;
	int loop;
	int tran_start = 0;

	spin_lock_irqsave(&s_dma_lock, flags);

	memset(s_dma_control_regs,0,sizeof(struct fsldma_regs));
	s_dma_control_regs->sr = 0x9b;//清各种状态位
		
	if ((s_tran_channel < 0) || (s_tran_channel >= EOS_DMA_MAX_CHANNEL))
	{
		//printk("copy_to_user error err channel %d\n", channel);
		spin_unlock_irqrestore(&s_dma_lock, flags);
		return -1;
	}
	s_dma_is_running_channel[s_tran_channel] = 0;
	memset(&(s_args_channel[s_tran_channel]), 0, sizeof(struct dma_arg_channel));
			
	for (loop = 0; loop < EOS_DMA_MAX_CHANNEL; loop++)
	{
		if (1 == s_dma_is_running_channel[loop])//1表示找到要用的channel，故置 tran_start 标志
		{
			tran_start = 1;
			break;
		}
	}
	if ((tran_start == 1) && (s_args_channel[loop].ch_num == loop))
	{
		dma_cpy(&(s_args_channel[loop]));
		s_dma_control_regs->clndar = s_dma_link_descriptor_phy[0];
		s_dma_control_regs->mr = (0x7<< 24) | (0x1<<8) ;//BWC=0111(128byte) EOLNIE=1
		s_dma_control_regs->mr = (0x7<< 24) | (0x1<<8) | 0x1;//CS=1 Start the DMA process
		
		s_dma_is_running_channel[loop] = 1; 
		s_tran_channel = loop;
	}
	spin_unlock_irqrestore(&s_dma_lock, flags);
		
    return IRQ_HANDLED;
}

static const struct file_operations dma_fileops = 
{
	.owner   = THIS_MODULE,
	.write   = dma_write,
	.read    = dma_read,
	.open    = dma_open,
	.ioctl   = dma_ioctl,
	.release = dma_release,
};

/*************************************************************************
* 函数名  ： dma_probe
* 负责人  ：张淞钦
* 创建日期：20160621
* 函数功能：dma驱动dma_probe函数
* 输入参数：
* 输出参数：
* 返回值：	OK: 成功      
* 调用关系：                            
*************************************************************************/
static int  dma_probe(struct platform_device *dev)
{
	int err = 0;
	int rc = 0;
	int i = 0;
	
	printk("entry %s\n",__FUNCTION__);

	if (s_major != 0) 
	{
		s_devid = MKDEV(s_major, 0);
		rc = register_chrdev_region(s_devid, 0, "dma_control0_channel1");
	} 
	else 
	{
		rc = alloc_chrdev_region(&s_devid, 0, 1, "dma_control0_channel1");
		s_major = MAJOR(s_devid);
	}
	if (rc < 0) 
	{
		dev_err(&s_pdev->dev, "dma chrdev_region err: %d\n", rc);
                goto undo_platform_del;
		return -1;
	}

	s_dma_class = class_create(THIS_MODULE, "dma_control0_channel0_class");
	if (IS_ERR(s_dma_class) != 0)
	{
		printk("Error,failed in creating class!\n");
		goto undo_register;
	}
	device_create(s_dma_class, NULL, s_devid, NULL, "%s", DRVNAME);	
	
	cdev_init(&s_dma_cdev, &dma_fileops);
	s_dma_cdev.owner = THIS_MODULE;
	s_dma_cdev.ops = &dma_fileops;
	if (cdev_add(&s_dma_cdev, s_devid, 1))
	{
		printk("Error adding dma_control0_channel0_cdev!\n");
		goto undo_device;
	}

	//MPIC 28 寄存器基地址
	s_dma_control_pic = (struct pic_regs *)ioremap(0xffe000000 + 0x50580 , sizeof(struct pic_regs));//Internal interrupt n vector/priority register (MPIC_IIVPR28)
	if(s_dma_control_pic == NULL)
	{
		goto undo_cdev;
	}
	s_dma_control_pic->iivpr |= 0x800000;//MPIC_IIVPR28[P]:All internal interrupts are active-high

	//DMA 控制器寄存器基地址
	s_dma_control_regs = (struct fsldma_regs *)ioremap(0xffe000000 + 0x100000 + 0x100, 
	sizeof(struct fsldma_regs));//DMA controller 1
	if(s_dma_control_regs == NULL)
	{
		goto undo_iounmap_pic;
	}

	//中断申请，并挂处理函数
	s_dma_soft_irq = irq_create_mapping(NULL,DMA_IRQ);
	rc = request_irq(s_dma_soft_irq, dma_control0_channel1_interrupt, 0, "dma0-channel0", NULL);
	if(rc != 0)
	{
		printk("request irq failed!\n");
		goto undo_iounmap_reg;
	}

	//申请大块DMA一致性缓冲区
	for (i = 0; i < EOS_DMA_MAX_CHANNEL; i++)
	{
		s_dma_buf_channel[i] = dma_alloc_coherent(&s_pdev->dev, 
											MAX_LENGTH * 4,
											&(s_dma_addr_channel[i]), 
											GFP_ATOMIC);
		if(NULL == (s_dma_buf_channel[i]))
		{
			goto undo_dma_pool;
		}
		/* DMA的操作是需要物理地址的，但是在linux内核中使用的都是虚拟地址，如果我们想要用DMA对一段内存进行操作，我们如何得到这一段内存的物理地址和虚拟地址的映射呢？dma_alloc_coherent这个函数实现了这种机制:
		
		A = dma_alloc_coherent(B, C, D, GFP_KERNEL);
		A: 内存的虚拟起始地址，在内核要用此地址来操作所分配的内存(代表缓冲区的内核虚拟地址) CPU-viewed address
		B: struct device指针，可以平台初始化里指定，主要是dma_mask之类，可参考framebuffer
		C: 实际分配大小，传入dma_map_size即可
		D: 返回的内存物理地址，dma就可以用。 (相关的总线地址(物理地址)) device-viewed address
		所以，A和D是一一对应的，只不过，A是虚拟地址，而D是物理地址。对
		任意一个操作都将改变缓冲区内容。当然要注意操作环境。
		
		我对此函数的理解是，调用此函数将会分配一段内存，D将返回这段内存的实际物理地址供DMA来使用，A将是D对应的
虚拟地址供操作系统调用，对A和D的的任意一个进行操作，都会改变这段内存缓冲区的内容。
		*/
		
		s_dma_phy_channel[i] = virt_to_phys(s_dma_buf_channel[i]);
		
		print("s_dma_phy is %llx \n", s_dma_phy_channel[i]);
		print("s_dma_addr is %llx \n", s_dma_addr_channel[i]);
	}

	/*申请 小块DMA一致性缓冲区*/
	// 1.为设备初始化DMA一致性内存的内存池
	s_dma_link_pool = dma_pool_create("control0_channel0_link_pool",//内存池的名字
									&s_pdev->dev,
									sizeof(struct dma_link_descriptor),
									__alignof__(struct dma_link_descriptor),
							0);
	if(s_dma_link_pool == NULL)
	{
		goto undo_irq;
	}

	//2.从内存池中分配内存
	/*A = dma_pool_alloc(B, GFP_ATOMIC, D);
	A:cpu可以使用的虚拟地址
	D是内存池设备可以使用的dma物理地址， D 给 DMA_CLNDAR 寄存器用
	*/
	for(i = 0;i < MAX_BLOCK;i++)
	{
		s_dma_link_descriptor[i] = dma_pool_alloc(s_dma_link_pool,
												GFP_ATOMIC,
												&s_dma_link_descriptor_phy[i]);
		if(!s_dma_link_descriptor[i])
		{
			printk("no memory for dma  link descriptor\n");
			return -1;
		}
	}
	spin_lock_init(&s_dma_lock);
	return 0;

undo_dma_pool:
	for (i = 0; i < EOS_DMA_MAX_CHANNEL;i++)
	{
	    dma_free_coherent(&s_pdev->dev,MAX_LENGTH*4, s_dma_buf_channel[i], s_dma_addr_channel[i]);
	}

undo_irq:
	free_irq(s_dma_soft_irq,NULL);
undo_iounmap_reg:
	iounmap(s_dma_control_regs);
undo_iounmap_pic:
	iounmap(s_dma_control_pic);	
undo_cdev:
	cdev_del(&s_dma_cdev);   
undo_device:
	device_destroy(s_dma_class, s_devid);
	class_destroy(s_dma_class); 
undo_register:
	unregister_chrdev_region(s_devid, 1);
undo_platform_del:
	platform_device_del(s_pdev);
	
	return -1;
}

static struct platform_driver dma_driver = {
	.probe = dma_probe,
	.driver = {
		.name	= "dma_control0_channel0",
		.owner	= THIS_MODULE,
	},
};

/*************************************************************************
* 函数名  ： dma_init
* 负责人  ：张淞钦
* 创建日期：20160621
* 函数功能：dma驱动dma_init函数
* 输入参数：
* 输出参数：
* 返回值：	OK: 成功      
* 调用关系：                            
*************************************************************************/
static int __init dma_init(void)
{
	int rc;
	printk("dma  driver version %s\n", DMA_VERSION);

	s_pdev = platform_device_alloc(DRVNAME, 0);
	if (s_pdev == NULL)
	{
                printk("alloc platform device fail\n");
                return -1;
	}
	rc = platform_device_add(s_pdev);
	if (rc != 0)
	{
                goto undo_platform_alloc;
	}
	
	rc = platform_driver_register(&dma_driver);
	if(rc != 0)
	{
		printk("platform_driver_register fail\n");
		goto undo_cdev_del;
	}

	return 0; /* succeed */

undo_cdev_del:
	platform_device_del(&s_dma_cdev);
undo_platform_alloc:
	platform_device_put(s_pdev);	
        return -1;
}


/*************************************************************************
* 函数名  ： dma_cleanup
* 负责人  ：张淞钦
* 创建日期：20160621
* 函数功能：dma驱动cleanup函数
* 输入参数：
* 输出参数：
* 返回值：	OK: 成功      
* 调用关系：                            
*************************************************************************/
static void __exit dma_cleanup(void)
{


         	int i = 0;
	printk("clean dma control0 channel0  module!\n");
		
	for(i = 0;i < MAX_BLOCK;i++)
	{
		dma_pool_free(s_dma_link_pool,s_dma_link_descriptor[i],s_dma_link_descriptor_phy[i]);
	}
	dma_pool_destroy(s_dma_link_pool);

#if 0
	dma_free_coherent(&s_pdev->dev,MAX_LENGTH * 4,s_dma_buf,s_dma_addr);
#else
	for (i = 0; i < EOS_DMA_MAX_CHANNEL; i++)
	{
	    dma_free_coherent(&s_pdev->dev,MAX_LENGTH * 4, s_dma_buf_channel[i], s_dma_addr_channel[i]);
	}
#endif		
	free_irq(s_dma_soft_irq,NULL);
	iounmap(s_dma_control_regs);
	iounmap(s_dma_control_pic);
	
	cdev_del(&s_dma_cdev);   
	device_destroy(s_dma_class,s_devid);
	class_destroy(s_dma_class); 
	
	unregister_chrdev_region(s_devid, 1);
	
	platform_driver_unregister(&dma_driver);
	platform_device_unregister(s_pdev);
       
}	

module_init(dma_init);
module_exit(dma_cleanup);


