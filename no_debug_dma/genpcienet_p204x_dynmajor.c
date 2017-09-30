#include <linux/device.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <linux/mii.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/delay.h>
#include <linux/ethtool.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/interrupt.h>

#define MAX_LENGTH				0x4000  /*16k*/

#define SET_CHANNEL0_ARG		0
#define SET_CHANNEL1_ARG		1
#define READ_CONTROL0_CHANNEL1	2
#define READ_BUFFER_DATA		3

dma_addr_t control0_dma_addr;
unsigned char *control0_dma_buf = NULL;
unsigned int control0_buf[MAX_LENGTH * 2];
unsigned int control0_is_running = 0;
unsigned int control0_is_error = 0;

dma_addr_t control0_channel1_dma_addr;
unsigned char *control0_channel1_dma_buf = NULL;
unsigned int control0_channel1_buf[MAX_LENGTH];

dma_addr_t control1_dma_addr;
unsigned char *control1_dma_buf = NULL;
unsigned int control1_buf[MAX_LENGTH * 2];
unsigned int control1_is_running = 0;
unsigned int control1_is_error = 0;

int control0_soft_irq = NO_IRQ;
int control0_channel1_soft_irq = NO_IRQ;
int control1_soft_irq = NO_IRQ;

unsigned int control0_buffer_select = 0;
unsigned int control1_buffer_select = 0;

unsigned int control0_loop_len = 0;
unsigned int control0_loop = 0;

unsigned int control1_loop_len = 0;
unsigned int control1_loop = 0;

static unsigned int control0_timeout_num = 0;
static unsigned int control1_timeout_num = 0;

/*for dma control0_channel0 control1_channel1 check dma is ok.
every 5ms check dma source address register,status register,count register
check 5 times,if every time those register is the same valu.then think dma control as error.must abort.

*/


static unsigned int control0_channel0_error_check_loop = 0;
static unsigned int control1_channel0_error_check_loop = 0;

static unsigned int control0_channel0_check_sraddress = 0;
static unsigned int control1_channel0_check_sraddress = 0;

static unsigned int control0_channel0_check_length = 0;
static unsigned int control1_channel0_check_length = 0;

static unsigned int control0_channle0_busy_times = 0;
static unsigned int control1_channle0_busy_times = 0;

static unsigned int control0_channel0_transfer_error = 0;
static unsigned int control1_channel0_transfer_error = 0;

static unsigned int control0_channel0_programming_error = 0;
static unsigned int control1_channel0_programming_error = 0;

int transfer_ok = 0;
int probe_ok = 0;

unsigned long long device_base = 0;


struct dma_arg
{	
	unsigned long long addr_size[10][2];
	unsigned int num;
	unsigned int nsecond;
	int fd;
	unsigned int channel;
};

struct dma_arg control0_arg;
struct dma_arg control1_arg;


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
	unsigned int iivpr;
	unsigned int iidr;
};

struct fsldma_regs *control0_regs;
struct pic_regs *control0_pic;

struct fsldma_regs *control0_channel1_regs;
struct pic_regs *control0_channel1_pic;

struct fsldma_regs *control1_regs;
struct pic_regs *control1_pic;


struct dma_read_unit
{
	unsigned int channel;
	int error;
	unsigned int dma_data[MAX_LENGTH];
};
struct dma_read_unit control0_data;
struct dma_read_unit control1_data;

struct test_buf_unit
{
	unsigned int channel;
	unsigned int off;
	unsigned int data;
};

/* 用于表示调试打印*/
static int s_debug = 0;
module_param(s_debug, int, 0);
#define print(fmt, args...) do { \
    if (s_debug) \
    { printk(fmt, ## args); } \
    } while(0);


wait_queue_head_t control0_channel1_queue;
struct mutex control0_channel1_read_lock;
struct mutex dma_write_lock;
struct mutex dma_read_lock;

static int pcie_open(struct inode *inode,struct file *file);
static int pcie_release(struct inode*,struct file*);
static int pcie_mmap(struct file *, struct vm_area_struct *vma);
static ssize_t pcie_read(struct file*,char __user*,size_t,loff_t*);
static ssize_t pcie_write(struct file*,const char __user*,size_t,loff_t*);
static int pcie_ioctl(struct inode *inode,struct file*,unsigned int,unsigned long);
static irqreturn_t dma_control0_channel0_interrupt(int irq,void *dev_instance);
static irqreturn_t dma_control0_channel1_interrupt(int irq,void *dev_instance);
static irqreturn_t dma_control1_channel0_interrupt(int irq,void *dev_instance);
static int __init pciedev_probe(struct pci_dev *pci_dev,const struct pci_device_id *pci_id);

static int pcie_open(struct inode *inode,struct file *file)
{
	control0_timeout_num = 0;
	control1_timeout_num = 0;
	print("ferry,@ %s\n",__FUNCTION__);
	return 0;
}

int pcie_release(struct inode *inode,struct file *file)
{
	print("ferry,@ %s\n",__FUNCTION__);
	return 0;
}

ssize_t pcie_read(struct file *file,char __user *buf,size_t count,loff_t * offset)
{
	unsigned int channel = 0;
	int i = 0;
	print("entry %s\n",__FUNCTION__);
	if(probe_ok != 1)
	{
		printk("no device\n");
		return -1;
	}
	if(count > MAX_LENGTH)
	{
		printk("pcie_read count too large!\n");
		return -1;
	}
	//mutex_lock(&dma_read_lock);
	if (copy_from_user(&channel, buf, sizeof(unsigned int))) /*只获取通道号*/
	{
		printk("copy_from_user error!n");
		//mutex_unlock(&dma_read_lock);
		return -1;
	}
	print("channel = %d\n",channel);
	print("count = %d\n",count);
	if(channel == 0)
	{
		control0_data.channel = channel;
		control0_data.error = control0_is_error;
		if(control0_buffer_select ==0)
		{
			memcpy(control0_data.dma_data,&control0_buf[MAX_LENGTH], count * 4);
			#if 0
			for(i=0;i<0x80;i++)
			{
				printk("control0_buf[MAX_LENGTH + 0x80*14 + i] = 0x%x\n",control0_buf[MAX_LENGTH + 0x80*14 +i]);
				if(i%4 == 0)
				{
					printk("\n");
				}
			}
			#endif
			print("control0_buf[MAX_LENGTH]= 0x%x\n",control0_buf[MAX_LENGTH]);
			print("control0_buf[MAX_LENGTH + 1]= 0x%x\n",control0_buf[MAX_LENGTH + 1]);
		}
		else if(control0_buffer_select ==1)
		{
			memcpy(control0_data.dma_data,&control0_buf[0], count * 4);
			#if 0
			for(i=0;i<0x80;i++)
			{
				printk("control0_buf[ 0x80*14 + i] = 0x%x\n",control0_buf[0x80*14 +i]);
				if(i%4 == 0)
				{
					printk("\n");
				}
			}
			#endif
			print("control0_buf[0]= 0x%x\n",control0_buf[0]);
			print("control0_buf[1]= 0x%x\n",control0_buf[1]);
		}
		if (copy_to_user(buf, &control0_data, (count + 2) * 4))
		{
			printk("copy_from_user error!\n");
			return -1;
		}	
	}
	else if(channel == 1)
	{
		control1_data.channel = 1;
		control1_data.error = control1_is_error;
		if(control1_buffer_select ==0)
		{
			memcpy(control1_data.dma_data,&control1_buf[MAX_LENGTH], count * 4);
		}
		else if(control1_buffer_select ==1)
		{
			memcpy(control1_data.dma_data,&control1_buf[0], count * 4);
		}
		if (copy_to_user(buf, &control1_data, (count + 2) * 4))
		{
			printk("copy_from_user error!\n");
			return -1;
		}	
	}
//	mutex_unlock(&dma_read_lock);
	return 0;
}

ssize_t pcie_write(struct file *file,const char __user *buf,size_t count,loff_t *offset)
{
	unsigned int channel = 0;
	unsigned int sa_high =0;
	unsigned int sa_low = 0;
	unsigned int sa_len = 0;
	unsigned int channel0_loop = 0;
	unsigned int channel1_loop = 0;
	print("entry %s\n",__FUNCTION__);
//	mutex_lock(&dma_write_lock);
	if(probe_ok != 1)
	{
		printk("no device\n");
		return -1;
	}
	if (copy_from_user(&channel, buf, sizeof(unsigned int)))
	{
		printk("copy_from_user error!n");
		//mutex_unlock(&dma_write_lock);
		return -1;
	}
	print("pcie_write channel = %d\n", channel);/*只获取通道号*/
	if(channel == 0)
	{
		/*如果仍然在进行上一次传输，则中止，设错误标志，再进行传输*/
		/*dma control maybe error,is busy.should abort*/

		if((0x80 == (control0_regs->sr & 0x80)))//Transfer error
		{
			control0_channel0_transfer_error++;
			control0_regs->sr = 0x80;
			control0_is_running = 0;
		}

		if((0x10 == (control0_regs->sr & 0x10)))//Programming error
		{
			control0_channel0_programming_error++;
			control0_regs->sr = 0x10;
			control0_is_running = 0;
		}
	 
	 	if(0x4 == (control0_regs->sr & 0x4))//Channel busy
	 	{
			if( (control0_channel0_check_length == control0_regs->bcr) && (control0_channel0_check_sraddress == control0_regs->sar))
			{
				control0_channel0_error_check_loop++;
				if(control0_channel0_error_check_loop >= 5) //add check time,avoid the last interrupt not run interrupt function
				{
					control0_channle0_busy_times++;
					control0_channel0_error_check_loop = 0;				
					control0_regs->mr = control0_regs->mr | (1 << 3);
					control0_is_running = 0;

					for(channel0_loop = 0;channel0_loop <100;channel0_loop++)
					{
						udelay(10);
						if(0x4 != (control0_regs->sr & 0x4))
						{
							break;
						}
					}
					if(channel0_loop >= 100)
					{
						printk("abort channel0 transfer fail\n");
						return -1;
					}
				}
			}
			else
			{
				control0_channel0_check_length = control0_regs->bcr;
				control0_channel0_check_sraddress = control0_regs->sar;
				control0_channel0_error_check_loop = 0;
			}
		}

		if(control0_is_running == 1)
		{
			#if 0
			control0_regs->mr = 0x8;
			control0_regs->sr = 0xbf;
			#endif
			control0_is_error = 1;
			control0_timeout_num += 1;
			return 0; 
		}
		control0_loop = 0;
		control0_loop_len =0;

		control0_channel0_check_length = 0;
		control0_channel0_check_sraddress = 0;
		
		sa_high = control0_arg.addr_size[control0_loop][0] >> 32;
		sa_low = control0_arg.addr_size[control0_loop][0];
		sa_len =  control0_arg.addr_size[control0_loop][1];
		
		control0_regs->mr = (0x7<< 24) | (0x1<<9) | 0x04;//BWC=0111(Bandwidth=128byte) End-of-segments interrupt enable=1  Channel transfer mode=1 Channel continue=1
		control0_regs->satr = (0x5 << 16) |(sa_high);//SREADTTYPE=5(Read, snoop local processor)
		control0_regs->sar = sa_low;
		control0_regs->datr = (0x5 << 16)  | (control0_dma_addr >> 32);//DWRITETTYPE=5(Write, snoop local processor)
		control0_regs->dar = control0_dma_addr;
		control0_regs->bcr = sa_len * 4;	
		control0_regs->mr = (0x7<< 24) | (0x1<<9) | 0x05;//Channel start
		control0_is_running = 1;		
	}
	else if(channel ==1)
	{
	
		if((0x80 == (control1_regs->sr & 0x80) ))
		{
			control1_channel0_transfer_error++;
			control1_regs->sr = 0x80;
			control1_is_running = 0;
		}

		if((0x10 == (control1_regs->sr & 0x10)))
		{
			control1_channel0_programming_error++;
			control1_regs->sr = 0x10;
			control1_is_running = 0;
		}
		/*check dma channel is busy,if 5 times is busy and length ,source address is equal.then think dma error is not working.abort channel*/
	 	if(0x4 == (control1_regs->sr & 0x4))
	 	{
			if( (control1_channel0_check_length == control1_regs->bcr) && (control1_channel0_check_sraddress == control1_regs->sar))
			{
				control1_channel0_error_check_loop++;
				if(control1_channel0_error_check_loop >= 5)    //add check time to 100
				{
					control1_channle0_busy_times++;
					control1_channel0_error_check_loop = 0;
					control1_regs->mr = control1_regs->mr | (1 << 3);
					control1_is_running = 0;
					for(channel1_loop = 0;channel1_loop <100;channel1_loop++)
					{
						udelay(10);
						if(0x4 != (control1_regs->sr & 0x4))
						{
							break;
						}
					}
					if(channel1_loop >= 100)
					{
						printk("abort channel1 transfer fail\n");
						return -1;
					}
				}
			}
			else
			{
				control1_channel0_check_length = control1_regs->bcr;
				control1_channel0_check_sraddress = control1_regs->sar;
				control1_channel0_error_check_loop = 0;
			}
		}
		
		if(control1_is_running == 1)
		{
			#if 0
			control1_regs->mr = 0x8;
			control1_regs->sr = 0xbf;
			#endif
			control1_is_error = 1;
			control1_timeout_num += 1;
			return 0;
			//printk("dma control1 transfer timeout\n");
			//printk("control1_timeout_num = %d\n",control1_timeout_num);
		}
		print("control1_regs.mr = 0x%x\n", control1_regs->mr);
		print("control1_regs.sr = 0x%x\n", control1_regs->sr);
		print("control1_regs.eclndar = 0x%x\n", control1_regs->eclndar);
		print("control1_regs.clndar = 0x%x\n", control1_regs->clndar);
		print("control1_regs.satr= 0x%x\n", control1_regs->satr);
		print("control1_regs.sar = 0x%x\n", control1_regs->sar);
		print("control1_regs.datr = 0x%x\n", control1_regs->datr);	
		print("control1_regs.dar = 0x%x\n", control1_regs->dar);	
		print("control1_regs.bcr = 0x%x\n", control1_regs->bcr);
		control1_loop = 0;
		control1_loop_len =0;

		control1_channel0_check_length = 0;
		control1_channel0_check_sraddress = 0;
		
		sa_high = control1_arg.addr_size[control1_loop][0] >> 32;
		sa_low = control1_arg.addr_size[control1_loop][0];
		sa_len =  control1_arg.addr_size[control1_loop][1];

		print("control1_arg.addr_size[0][0] = 0x%llx\n",control1_arg.addr_size[0][0]);
		print("control1_arg.addr_size[0][1] = 0x%llx\n",control1_arg.addr_size[0][1]);
		print("control1_arg.addr_size[1][0] = 0x%llx\n",control1_arg.addr_size[1][0]);
		print("control1_arg.addr_size[1][1] = 0x%llx\n",control1_arg.addr_size[1][1]);
		print("control1_arg.channel = %d\n",control1_arg.channel);
		print("control1_arg.nsecond = %d\n",control1_arg.nsecond);
		print("control1_arg.num = %d\n",control1_arg.num);	

		control1_regs->mr = (0x7<< 24) | (0x1<<9) | 0x04;
		control1_regs->satr = (0x5 << 16) |(sa_high);
		control1_regs->sar = sa_low;
		control1_regs->datr = (0x5 << 16)  | (control1_dma_addr >> 32);
		control1_regs->dar = control1_dma_addr;
		control1_regs->bcr = sa_len*4;	
		control1_regs->mr = (0x7<< 24) | (0x1<<9) | 0x05;
		control1_is_running = 1;

	}
	//mutex_unlock(&dma_write_lock);
	return 0;
}

int pcie_ioctl(struct inode *inode,struct file *file,unsigned int cmd,unsigned long arg)
{	
	int ret = 0;
	struct test_buf_unit test_buf_data;
	unsigned int control0_channel1_arg[3];
	unsigned int timeout = 0;
	unsigned int control0_channel1_haddr = 0;
	unsigned int control0_channel1_laddr = 0;
	unsigned int count = 0;
	unsigned int loop = 0;
	if(probe_ok != 1)
	{
		printk("no device\n");
		return -1;
	}
	switch(cmd)
	{
		case SET_CHANNEL0_ARG: 
			if (copy_from_user(&control0_arg,(void *)arg,sizeof(struct dma_arg)))
			{
				ret = -EFAULT;
				return ret;
			}
			print("control0_arg.addr_size[0][0] = 0x%llx\n",control0_arg.addr_size[0][0]);
			print("control0_arg.addr_size[0][1] = 0x%llx\n",control0_arg.addr_size[0][1]);
			print("control0_arg.addr_size[1][0] = 0x%llx\n",control0_arg.addr_size[1][0]);
			print("control0_arg.addr_size[1][1] = 0x%llx\n",control0_arg.addr_size[1][1]);
			print("control0_arg.channel = %d\n",control0_arg.channel);
			print("control0_arg.nsecond = %d\n",control0_arg.nsecond);
			print("control0_arg.num = %d\n",control0_arg.num);	
           	break;
		case SET_CHANNEL1_ARG: 
	       if (copy_from_user(&control1_arg,(void *)arg,sizeof(struct dma_arg)))
			{
				ret = -EFAULT;
				return ret;
			}
			print("control1_arg.addr_size[0][0] = 0x%llx\n",control1_arg.addr_size[0][0]);
			print("control1_arg.addr_size[0][1] = 0x%llx\n",control1_arg.addr_size[0][1]);
			print("control1_arg.addr_size[1][0] = 0x%llx\n",control1_arg.addr_size[1][0]);
			print("control1_arg.addr_size[1][1] = 0x%llx\n",control1_arg.addr_size[1][1]);
			print("control1_arg.channel = %d\n",control1_arg.channel);
			print("control1_arg.nsecond = %d\n",control1_arg.nsecond);
			print("control1_arg.num = %d\n",control1_arg.num);	
			break;
		case READ_CONTROL0_CHANNEL1:
			mutex_lock(&control0_channel1_read_lock);
			if (copy_from_user(control0_channel1_arg,(void *)arg,sizeof(unsigned int)*3))
			{
				mutex_unlock(&control0_channel1_read_lock);
				printk("copy_from_user error\n");
				return -1;
			}
			control0_channel1_haddr = control0_channel1_arg[0];
			control0_channel1_laddr = control0_channel1_arg[1];
			count = control0_channel1_arg[2];
			print("control0_channel1_haddr = 0x%x\n",control0_channel1_haddr);
			print("control0_channel1_laddr = 0x%x\n",control0_channel1_laddr);
			print("count = 0x%x\n",count);
			if(count > MAX_LENGTH)
			{
				mutex_unlock(&control0_channel1_read_lock);
				printk("count too large\n");
				return -1;
			}
			control0_channel1_regs->mr = 0x8;
			control0_channel1_regs->sr = 0xbf;
			control0_channel1_regs->mr = (0x7 << 24) | (0x1 << 9) | 0x04;
			control0_channel1_regs->satr = (0x5 << 16) |control0_channel1_haddr;
			control0_channel1_regs->sar = control0_channel1_laddr;
			control0_channel1_regs->datr = (0x5 << 16)  | (control0_channel1_dma_addr >> 32);
			control0_channel1_regs->dar = control0_channel1_dma_addr;
			control0_channel1_regs->bcr = count * 4;	
			control0_channel1_regs->mr = (0x7 << 24) | (0x1 << 9) | 0x05;
			timeout = 1 * HZ;
			transfer_ok = 0;
			wait_event_timeout(control0_channel1_queue,transfer_ok==1,timeout);
			if(transfer_ok == 0)
			{
				printk("not get interrupt,timeout\n");
				mutex_unlock(&control0_channel1_read_lock);
				return -1;
			}
			memcpy(control0_channel1_buf,control0_channel1_dma_buf,count * 4);
			print("control0_channel1_buf[0] = 0x%x\n",control0_channel1_buf[0]);
			print("control0_channel1_buf[1] = 0x%x\n",control0_channel1_buf[1]);
			print("control0_channel1_buf[2] = 0x%x\n",control0_channel1_buf[2]);
			if (copy_to_user((void *)arg, control0_channel1_buf, count * 4))
			{
				mutex_unlock(&control0_channel1_read_lock);
				printk("copy_to_user error\n");
				return -1;
			}
			mutex_unlock(&control0_channel1_read_lock);
			break;
		case READ_BUFFER_DATA:
			memset(&test_buf_data,0,sizeof(struct test_buf_unit));
			if (copy_from_user(&test_buf_data,(void *)arg,sizeof(struct test_buf_unit)))
			{
				printk("test buf data copy_from_user error\n");
				return -1;
			}
			if(test_buf_data.channel == 0)
			{
				printk("control0_is_running = %d\n",control0_is_running);
				for(loop = 0;loop < control0_arg.num;loop++)
				{
					printk("control0_arg.addr_size[%d][0] = 0x%llx\n",loop,control0_arg.addr_size[loop][0]);
					printk("control0_arg.addr_size[%d][1] = 0x%llx\n",loop,control0_arg.addr_size[loop][1]);
				}
				printk("control0_channle0_busy_times = %d\n",control0_channle0_busy_times);
				printk("control0_arg.channel = %d\n",control0_arg.channel);
				printk("control0_arg.nsecond = %d\n",control0_arg.nsecond);
				printk("control0_arg.num = %d\n",control0_arg.num);
				printk("control0_channel0_transfer_error = 0x%x\n",control0_channel0_transfer_error);
				printk("control0_channel0_programming_error = 0x%x\n",control0_channel0_programming_error);
				printk("ver 4.0\n");
				if(control0_buffer_select ==0)
				{
					test_buf_data.data = control0_buf[MAX_LENGTH + test_buf_data.off];
					printk("control0_buf[MAX_LENGTH + 0x%x]= 0x%x\n",test_buf_data.off , control0_buf[MAX_LENGTH + test_buf_data.off]);
				}
				else if(control0_buffer_select ==1)
				{
					test_buf_data.data = control0_buf[ test_buf_data.off];
					printk("control0_buf[0x%x]= 0x%x\n",test_buf_data.off , control0_buf[ test_buf_data.off]);
				}
			}
			else if(test_buf_data.channel == 1)
			{
				printk("control1_is_running = %d\n",control1_is_running);
				for(loop = 0;loop < control1_arg.num;loop++)
				{
					printk("control1_arg.addr_size[%d][0] = 0x%llx\n",loop,control1_arg.addr_size[loop][0]);
					printk("control1_arg.addr_size[%d][1] = 0x%llx\n",loop,control1_arg.addr_size[loop][1]);
				}
				printk("control1_channle0_busy_times= %d\n",control1_channle0_busy_times);
				printk("control1_arg.channel = %d\n",control1_arg.channel);
				printk("control1_arg.nsecond = %d\n",control1_arg.nsecond);
				printk("control1_arg.num = %d\n",control1_arg.num);
				printk("control1_channel0_transfer_error = 0x%x\n",control1_channel0_transfer_error);
				printk("control1_channel0_programming_error = 0x%x\n",control1_channel0_programming_error);
				printk("ver 4.0\n");
				if(control1_buffer_select ==0)
				{
					test_buf_data.data = control1_buf[MAX_LENGTH + test_buf_data.off];
					printk("control1_buf[MAX_LENGTH + 0x%x]= 0x%x\n",test_buf_data.off , control1_buf[MAX_LENGTH + test_buf_data.off]);
				}
				else if(control1_buffer_select ==1)
				{
					test_buf_data.data = control1_buf[ test_buf_data.off];
					printk("control1_buf[0x%x]= 0x%x\n",test_buf_data.off , control1_buf[ test_buf_data.off]);
				}
			}	
			if (copy_to_user((void *)arg, &test_buf_data, sizeof(struct test_buf_unit)))
			{
				printk("test_buf data copy_from_user error!\n");
				return -1;
			}	
			break;
        default:
            printk("cmd not found\n");
            return -1;
	}
	return 0;
}

static int pcie_mmap(struct file *file, struct vm_area_struct *vma)
{
   	unsigned long vsz = 0;
	unsigned long psz = 0;
	unsigned long off = 0;
	int error = 0;

	print("VMA start\t%lx", vma->vm_start);
	print("VMA end\t%lx", vma->vm_end);
	print("VMA pgoff\t%lx", vma->vm_pgoff);
	if(probe_ok != 1)
	{
		return -1;
	}
 #if 0
    if (vma->vm_pgoff != 0)
    {
        print("For now you have to use offset 0, not %lx", vma->vm_pgoff);
        return (-EINVAL);
    }
#endif

	vsz = vma->vm_end - vma->vm_start;
	off = vma->vm_pgoff << PAGE_SHIFT;
  #if 0
    if (vsz != 0x8000)
    {
        print("You wanted to mmap() %ld bytes, but only 2M is" "allowed", vsz);
        return (-EINVAL);
    }
#endif
	vma->vm_flags |= VM_IO;
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	error = remap_pfn_range(vma, vma->vm_start, device_base >> PAGE_SHIFT, vsz, vma->vm_page_prot);
	if (error != 0)
	{
		print("Couldn't get memory remapped, error = %d", error);
		return -EAGAIN;
	}
	return (0);
}

static irqreturn_t dma_control0_channel0_interrupt(int irq,void *dev_instance)
{
	int i;
	unsigned int sa_high = 0;
	unsigned int sa_low = 0;
	unsigned int sa_len = 0;

	print("ferry,@ %s\n",__FUNCTION__);
	
	memset(control0_regs, 0, sizeof(struct fsldma_regs));
	control0_regs->sr = 0x9b;//清各种状态位
	if(control0_buffer_select == 0)
	{
		memcpy(&control0_buf[control0_loop_len], control0_dma_buf, control0_arg.addr_size[control0_loop][1]*4);
		print("control0_loop_len = %d\n",control0_loop_len);
		print("control0_loop = %d\n",control0_loop);
		#if 0
		for(i=0;i<6;i++)
		{
			printk("control0_buf[%d] = 0x%x\n",i,control0_buf[i]);
		}
		#endif
	}
	if(control0_buffer_select == 1)
	{
		print("control0_loop_len = %d\n",control0_loop_len);
		print("control0_loop = %d\n",control0_loop);
		memcpy(&control0_buf[MAX_LENGTH + control0_loop_len], control0_dma_buf, control0_arg.addr_size[control0_loop][1]*4);
		#if 0
		for(i=0;i<6;i++)
		{
			printk("control0_buf[MAX_LENGTH + %d] = 0x%x\n",i,control0_buf[MAX_LENGTH +i]);
		}
		#endif
	}
	
	control0_loop_len = control0_loop_len + control0_arg.addr_size[control0_loop][1];
	control0_loop = control0_loop + 1;
	if(control0_loop < control0_arg.num)
	{
		sa_high = control0_arg.addr_size[control0_loop][0] >> 32;
		sa_low = control0_arg.addr_size[control0_loop][0];
		sa_len =  control0_arg.addr_size[control0_loop][1];
		
		control0_regs->mr = (0x7<< 24) | (0x1<<9) | 0x04;
		control0_regs->satr = (0x5 << 16) |(sa_high);
		control0_regs->sar = sa_low;
		control0_regs->datr = (0x5 << 16)  | (control0_dma_addr >> 32);
		control0_regs->dar = control0_dma_addr;
		control0_regs->bcr = sa_len * 4;	
		control0_regs->mr = (0x7<< 24) | (0x1<<9) | 0x05;
		return IRQ_HANDLED;
	}
	control0_is_running = 0;
	if(control0_buffer_select == 0)
	{
		control0_buffer_select = 1;
	}
	else
	{
		control0_buffer_select = 0;
	}
	control0_is_error = 0;
	return IRQ_HANDLED;
}

static irqreturn_t dma_control0_channel1_interrupt(int irq,void *dev_instance)
{
	print("entry %s\n",__FUNCTION__);
	memset(control0_channel1_regs,0,sizeof(struct fsldma_regs));
	control0_channel1_regs->sr = 0x9b;
	transfer_ok = 1;
	wake_up(&control0_channel1_queue);
	return IRQ_HANDLED;
}

static irqreturn_t dma_control1_channel0_interrupt(int irq,void *dev_instance)
{
	int i;
	unsigned int sa_high =0;
	unsigned int sa_low = 0;
	unsigned int sa_len = 0;

	print("ferry,@ %s\n",__FUNCTION__);
	memset(control1_regs,0,sizeof(struct fsldma_regs));
	control1_regs->sr = 0x9b;
	if(control1_buffer_select == 0)
	{
		memcpy(&control1_buf[control1_loop_len],control1_dma_buf,control1_arg.addr_size[control1_loop][1]*4);
		print("control1_loop_len = %d\n",control1_loop_len);
		print("control1_loop = %d\n",control1_loop);
		#if 0
		for(i=0;i<8;i++)
		{
			printk("control1_buf[%d] = 0x%x",control1_loop_len + i,control1_buf[control1_loop_len + i]);
			printk("\n");
		}
		#endif
	}
	if(control1_buffer_select == 1 )
	{
		memcpy(&control1_buf[MAX_LENGTH + control1_loop_len],control1_dma_buf,control1_arg.addr_size[control1_loop][1]*4);
		print("control1_loop_len = %d\n",control1_loop_len);
		print("control1_loop = %d\n",control1_loop);
		#if 0
		for(i=0;i<8;i++)
		{
			printk("control1_buf[%d] = 0x%x\n",MAX_LENGTH + control1_loop_len + i,control1_buf[MAX_LENGTH + control1_loop_len + i]);
			printk("\n");
		}
		#endif
	}
	
	control1_loop_len = control1_loop_len + control1_arg.addr_size[control1_loop][1];
	control1_loop = control1_loop + 1;
	if(control1_loop < control1_arg.num)
	{
		sa_high = control1_arg.addr_size[control1_loop][0] >> 32;
		sa_low = control1_arg.addr_size[control1_loop][0];
		sa_len =  control1_arg.addr_size[control1_loop][1];
		
		control1_regs->mr = (0x7<< 24) | (0x1<<9) | 0x04;
		control1_regs->satr = (0x5 << 16) |(sa_high);
		control1_regs->sar = sa_low;
		control1_regs->datr = (0x5 << 16)  | (control1_dma_addr >> 32);
		control1_regs->dar = control1_dma_addr;
		control1_regs->bcr = sa_len*4;	
		control1_regs->mr = (0x7<< 24) | (0x1<<9) | 0x05;
		return IRQ_HANDLED;
	}
	control1_is_running = 0;
	if(control1_buffer_select == 0)
		control1_buffer_select = 1;
	else
		control1_buffer_select = 0;
	control1_is_error = 0;
	return IRQ_HANDLED;
}

struct pciedev_info {
    unsigned int		mem_virt_addr[6];
    unsigned int		mem_virt_size[6];
    struct list_head	node;
    unsigned char		busnum;
    unsigned char		devnum;
    unsigned char		funcnum;
    unsigned int		barsize[6];	
    unsigned long long	barbase[6];
    struct pci_dev		*pcidev;
    int					irq;
};

//地址信息
#define CCSRBASE_ADDR	0xffe000000//p2020:0xfffe00000 p204x:0xffe000000

#define DMA0_CHANNEL0_IRQ_ADDR	0x50380//p2020(IIVPR4):0x50280  p204x(IIVPR12):0x50380/(IIVPR28)0x50580
#define DMA0_CHANNEL1_IRQ_ADDR	0x503a0//p2020(IIVPR5):0x502a0  p204x(IIVPR13):0x503a0/(IIVPR29)0x505a0
#define DMA1_CHANNEL0_IRQ_ADDR	0x50400//p2020(IIVPR60):0x50980 p204x(IIVPR16):0x50400/(IIVPR32)0x50600

#define DMA0_MR0_ADDR	0x100100//p2020:0x21100 p204x:0x100100
#define DMA0_MR1_ADDR	0x100180//p2020:0x21180 p204x:0x100180
#define DMA1_MR0_ADDR	0x101100//p2020:0x0c100 p204x:0x101100

//DMA channel的中断信息
#define dma_control0_irq			28//p2020(4+16=20)    p204x(12+16=28)
#define dma_control0_channel1_irq	29//p2020(5+16=21)    p204x(13+16=29)
#define dma_control1_irq			32//p2020(60+16=76)   p204x(16+16=32)

static unsigned int busnum = 0x07;//这里注意要和硬件的实际情况一致
module_param(busnum, uint, S_IRUGO);

static unsigned int devnum = 0;
module_param(devnum, uint, S_IRUGO);

static unsigned int funcnum = 0;
module_param(funcnum, uint, S_IRUGO);

static LIST_HEAD(device_list);

static int __init pciedev_probe(struct pci_dev *pci_dev, const struct pci_device_id *pci_id)
{
	int ret = 0;
	int i = 0;
	int val = 0;
	struct pciedev_info *tmp_pcidev = NULL;
	
	tmp_pcidev = kmalloc(sizeof(struct pciedev_info), GFP_KERNEL);
	if (tmp_pcidev == NULL)
	{
		printk("tmp_pcidev kmalloc pciedev_info failed\n");
		return -ENOMEM;
	}
	memset(tmp_pcidev, 0, sizeof(struct pciedev_info));

	tmp_pcidev->busnum = pci_dev->bus->number;
	tmp_pcidev->devnum = PCI_SLOT(pci_dev->devfn);
	tmp_pcidev->funcnum = PCI_FUNC(pci_dev->devfn);
	print("busno:%d, devnum:%d, funcnum:%d\n", tmp_pcidev->busnum, 
											tmp_pcidev->devnum, 
											tmp_pcidev->funcnum);
    
	//这里需要和预设的全部都对上才能往下走，注意p204x平台上可能有不同
	if ((tmp_pcidev->busnum == busnum) && 
		(tmp_pcidev->devnum == devnum) && 
		(tmp_pcidev->funcnum == funcnum))
	{
		if(pci_enable_device(pci_dev))
		{
			printk("pci_enable_device error!\n");
			return -EIO;
		}

		mutex_init(&control0_channel1_read_lock);
		mutex_init(&dma_write_lock);
		mutex_init(&dma_read_lock);
		
		init_waitqueue_head(&control0_channel1_queue);
		
		tmp_pcidev->pcidev = pci_dev;
		tmp_pcidev->irq = pci_dev->irq;

		for (i = 0; i < 6; i++)
		{
			tmp_pcidev->barbase[i] = pci_resource_start(pci_dev,i);
			tmp_pcidev->barsize[i] = pci_resource_len(pci_dev,i);
			printk("memory [%d] base address:%llx, barsize:%x, irq:%d\n", 
										i, 
										tmp_pcidev->barbase[i],
										tmp_pcidev->barsize[i],
										pci_dev->irq);	
		}
		device_base = tmp_pcidev->barbase[0];
		
		//p2020:DMA0_channel0_irq=4(20) p204x:DMA0_channel0_irq=12(28)
		control0_pic = (struct pic_regs *)ioremap(CCSRBASE_ADDR + DMA0_CHANNEL0_IRQ_ADDR, 8);
		if(control0_pic == NULL)
		{
			pci_disable_device(pci_dev);
			return -1;
		}
		control0_pic->iivpr |= 0x800000;
		
		//DMA1_MR0 地址
		control0_regs=(struct fsldma_regs *)ioremap(CCSRBASE_ADDR + DMA0_MR0_ADDR, 0x24);
		if(control0_regs == NULL)
		{
			iounmap(control0_pic);
			pci_disable_device(pci_dev);
			return -1;
		}

		//p2020:DMA0_channel1_irq=5(21) p204x:DMA0_channel1_irq=13(29)
		control0_channel1_pic =(struct pic_regs *)ioremap(CCSRBASE_ADDR + DMA0_CHANNEL1_IRQ_ADDR, 8);
		if(control0_channel1_pic == NULL)
		{
			iounmap(control0_regs);
			iounmap(control0_pic);
			pci_disable_device(pci_dev);
			return -1;
		}
		control0_channel1_pic->iivpr |= 0x800000;
		
		//DMA1_MR1 地址
		control0_channel1_regs=(struct fsldma_regs *)ioremap(CCSRBASE_ADDR + DMA0_MR1_ADDR, 0x24);
		if(control0_channel1_regs == NULL)
		{
			iounmap(control0_channel1_pic);
			iounmap(control0_regs);
			iounmap(control0_pic);
			pci_disable_device(pci_dev);
			return -1;
		}

		//p2020:DMA1_channel0_irq=60(76) p204x:DMA1_channel0_irq=16(32)
		control1_pic =(struct pic_regs *)ioremap(CCSRBASE_ADDR + DMA1_CHANNEL0_IRQ_ADDR, 8);
		if(control1_pic == NULL)
		{
			iounmap(control0_channel1_regs);
			iounmap(control0_channel1_pic);
			iounmap(control0_regs);
			iounmap(control0_pic);
			pci_disable_device(pci_dev);
			return -1;
		}
		control1_pic->iivpr |= 0x800000;
		
		//DMA2_MR0
		control1_regs=(struct fsldma_regs *)ioremap(CCSRBASE_ADDR + DMA1_MR0_ADDR, 0x24);
		if(control1_regs == NULL)
		{
			iounmap(control1_pic);
			iounmap(control0_channel1_regs);
			iounmap(control0_channel1_pic);
			iounmap(control0_regs);
			iounmap(control0_pic);
			pci_disable_device(pci_dev);
			return -1;
		}
		
		//DMA0_channel0_irq中断申请，并挂处理函数
		control0_soft_irq = irq_create_mapping(NULL, dma_control0_irq);
		if(control0_soft_irq == NO_IRQ)
		{	
			iounmap(control1_regs);
			iounmap(control1_pic);
			iounmap(control0_channel1_regs);
			iounmap(control0_channel1_pic);
			iounmap(control0_regs);
			iounmap(control0_pic);
			pci_disable_device(pci_dev);
			kfree(tmp_pcidev);
			printk("irq mapping failed!\n");
			return -1;
		}		
		ret = request_irq(control0_soft_irq, dma_control0_channel0_interrupt, 0, "dma0-channel0", NULL);
		if(ret != 0)
		{
			iounmap(control1_regs);
			iounmap(control1_pic);
			iounmap(control0_channel1_regs);
			iounmap(control0_channel1_pic);
			iounmap(control0_regs);
			iounmap(control0_pic);
			pci_disable_device(pci_dev);
			kfree(tmp_pcidev);
			printk("request irq failed!\n");
			return -1;
		}
		else
		{
			print("control0 request irq success!\n");
		}

		//DMA0_channel1_irq中断申请，并挂处理函数
		control0_channel1_soft_irq = irq_create_mapping(NULL, dma_control0_channel1_irq);
		if(control0_channel1_soft_irq == NO_IRQ)
		{	
			free_irq(control0_soft_irq,NULL);
			iounmap(control1_regs);
			iounmap(control1_pic);
			iounmap(control0_channel1_regs);
			iounmap(control0_channel1_pic);
			iounmap(control0_regs);
			iounmap(control0_pic);
			pci_disable_device(pci_dev);
			kfree(tmp_pcidev);
			printk("irq mapping failed!\n");
			return -1;
		}		
		ret = request_irq( control0_channel1_soft_irq, dma_control0_channel1_interrupt, 0, "dma0-channel1", NULL);
		if(ret != 0)
		{
			free_irq(control0_soft_irq,NULL);
			iounmap(control1_regs);
			iounmap(control1_pic);
			iounmap(control0_channel1_regs);
			iounmap(control0_channel1_pic);
			iounmap(control0_regs);
			iounmap(control0_pic);
			pci_disable_device(pci_dev);
			kfree(tmp_pcidev);
			printk("request irq failed!\n");
			return -1;
		}
		else
		{
			print("control0 channel1 request irq success!\n");
		}

		//DMA1_channel0_irq中断申请，并挂处理函数
		control1_soft_irq = irq_create_mapping(NULL, dma_control1_irq);
		if(control1_soft_irq == NO_IRQ)
		{	
			free_irq(control0_channel1_soft_irq, NULL);
			free_irq(control0_soft_irq, NULL);
			iounmap(control1_regs);
			iounmap(control1_pic);
			iounmap(control0_channel1_regs);
			iounmap(control0_channel1_pic);
			iounmap(control0_regs);
			iounmap(control0_pic);
			pci_disable_device(pci_dev);
			kfree(tmp_pcidev);
			printk("irq mapping failed!\n");
			return -1;
		}		
		ret = request_irq( control1_soft_irq, dma_control1_channel0_interrupt, 0, "dma1-channel0", NULL);
		if(ret != 0)
		{
			free_irq(control0_channel1_soft_irq, NULL);
			free_irq(control0_soft_irq, NULL);
			iounmap(control1_regs);
			iounmap(control1_pic);
			iounmap(control0_channel1_regs);
			iounmap(control0_channel1_pic);
			iounmap(control0_regs);
			iounmap(control0_pic);
			pci_disable_device(pci_dev);
			kfree(tmp_pcidev);
			printk("request irq failed!\n");
			return -1;
		}
		else
		{
			print("control1 request irq success!\n");
		}

		//PCI设备申请DMA一致性缓冲区
		control0_dma_buf = pci_alloc_consistent(pci_dev, MAX_LENGTH * 4, &control0_dma_addr);
       	if(!control0_dma_buf)
       	{
       	  	free_irq(control0_channel1_soft_irq, NULL);
       		free_irq(control1_soft_irq, NULL);
			free_irq(control0_soft_irq, NULL);
			iounmap(control1_regs);
			iounmap(control1_pic);
			iounmap(control0_channel1_regs);
			iounmap(control0_channel1_pic);
			iounmap(control0_regs);
			iounmap(control0_pic);
       		pci_disable_device(pci_dev);
			kfree(tmp_pcidev);
			printk("could not allocate coherent DMA buffer.\n");
			return -1;
	 	}

		control0_channel1_dma_buf = pci_alloc_consistent(pci_dev,MAX_LENGTH * 4, &control0_channel1_dma_addr);
       	if(!control0_channel1_dma_buf)
       	{
       		pci_free_consistent(pci_dev,MAX_LENGTH * 4,control0_dma_buf,control0_dma_addr);
       	  	free_irq(control0_channel1_soft_irq, NULL);
       		free_irq(control1_soft_irq, NULL);
			free_irq(control0_soft_irq, NULL);
			iounmap(control1_regs);
			iounmap(control1_pic);
			iounmap(control0_channel1_regs);
			iounmap(control0_channel1_pic);
			iounmap(control0_regs);
			iounmap(control0_pic);
       		pci_disable_device(pci_dev);
			kfree(tmp_pcidev);
			printk("could not allocate coherent DMA buffer.\n");
			return -1;
	 	}

		control1_dma_buf = pci_alloc_consistent(pci_dev,MAX_LENGTH * 4,&control1_dma_addr);
       	if(!control1_dma_buf)
       	{
			pci_free_consistent(pci_dev,MAX_LENGTH * 4,control0_channel1_dma_buf,control0_channel1_dma_addr);
       		pci_free_consistent(pci_dev,MAX_LENGTH * 4,control0_dma_buf,control0_dma_addr);
			free_irq(control0_channel1_soft_irq, NULL);
       		free_irq(control1_soft_irq, NULL);
			free_irq(control0_soft_irq, NULL);
			iounmap(control1_regs);
			iounmap(control1_pic);
			iounmap(control0_channel1_regs);
			iounmap(control0_channel1_pic);
			iounmap(control0_regs);
			iounmap(control0_pic);
       		pci_disable_device(pci_dev);
			kfree(tmp_pcidev);
			printk("could not allocate coherent DMA buffer.\n");
			return -1;
	 	}
		
		print("control0_dma_buf = 0x%x\n",control0_dma_buf);
		print("control0_channel1_dma_buf = 0x%x\n",control0_channel1_dma_buf);
		print("control1_dma_buf = 0x%x\n",control1_dma_buf);
		pci_set_master(pci_dev);
		list_add(&tmp_pcidev->node, &device_list);
		probe_ok = 1;
		return 0;
	}
	else
	{
		if(tmp_pcidev)
			kfree(tmp_pcidev);
		return -1;
	}	
}

struct pciedev_general_struct {
	unsigned int		*mem_virt_addr;
	struct pci_dev		*pci_dev;
	spinlock_t			dev_lock;
	struct list_head	device_entry;
	struct cdev			pciedev_cdev; 
	struct pciedev_general_struct	*next;
};

static struct pciedev_general_struct	*pciedev;
static int pciedev_major = 0;//240;  /* default to dynamic major */
static int pciedev_minor = 0;
#define PCIE_MODULE_NAME 	"pciedev"
#define PCIE_CLASS_NAME		"pcie_dev_class"
static rwlock_t	rcu_rwlock;
static struct class	*pcie_dev_class;
int insmodok_flag = 0;

static struct file_operations pciedev_fops={
	.owner		=	THIS_MODULE,
	.read		=	pcie_read,
	.write		=	pcie_write,
	.open		=	pcie_open,
	.release	=	pcie_release,
	.ioctl		=	pcie_ioctl,
	.mmap		=	pcie_mmap,
};

static struct pci_device_id pciedev_tbl[] __initdata = {
{0x1172, 0x0004, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
{0,}};

static struct pci_driver general_pci_driver={
	.name		=	PCIE_MODULE_NAME,
	.id_table	=	pciedev_tbl,
	.probe		=	pciedev_probe,
};
MODULE_DEVICE_TABLE(pci, pciedev_tbl);

static int __init pciedev_init_module(void)
{
    int ret = 0;
	dev_t devno;
	print("yptian-ferry,@ %s\n",__FUNCTION__);

	if((pciedev = kmalloc(sizeof(struct pciedev_general_struct), GFP_KERNEL)) == NULL)
	{
		printk("kmalloc pciedev_general_struct fail!\n");
		return -ENOMEM;
	}
	memset(pciedev, 0, sizeof(struct pciedev_general_struct));

	if (pciedev_major != 0) 
	{
		devno = MKDEV(pciedev_major, pciedev_minor);
		ret = register_chrdev_region(devno, 1, PCIE_MODULE_NAME);
	} 
	else 
	{
		ret = alloc_chrdev_region(&devno, 0, 1, PCIE_MODULE_NAME);
		pciedev_major = MAJOR(devno);
		pciedev_minor = MINOR(devno);
	}
	if (ret < 0) 
	{
		printk("register major number failed with err=%d\n",ret);
		return ret;
	}
	
	rwlock_init(&rcu_rwlock);
	
	INIT_LIST_HEAD(&pciedev->device_entry);
	
	cdev_init(&pciedev->pciedev_cdev, &pciedev_fops);
	pciedev->pciedev_cdev.owner = THIS_MODULE;
	pciedev->pciedev_cdev.ops = &pciedev_fops;
	if (cdev_add(&pciedev->pciedev_cdev, devno, 1))
	{
		printk("Error adding rcu_fpga_cdev!\n");
		return -1;
	}

	pcie_dev_class = class_create(THIS_MODULE, PCIE_CLASS_NAME);
	if (IS_ERR(pcie_dev_class))
	{
		printk("Error,failed in creating class!\n");
		return -1;
	}

	device_create(pcie_dev_class, NULL, devno, NULL, "%s", PCIE_MODULE_NAME);	
	insmodok_flag = 1;
	
	if(pci_register_driver(&general_pci_driver)!=0)
	{
		pci_unregister_driver(&general_pci_driver);
		printk("%s pci_register_driver error!\n",PCIE_MODULE_NAME);
		return -ENODEV;
	}
	print("init ok!\n");
	return 0;
}
module_init(pciedev_init_module);

static void __exit pciedev_cleanup_module(void)
{
	struct pciedev_info *tmp_pcidev = NULL;
	int i = 0;
	struct list_head *lh;
	print("step 2\n");	
	if (insmodok_flag)
	{
		dev_t devno = MKDEV(pciedev_major, pciedev_minor);
		cdev_del(&pciedev->pciedev_cdev);
		device_destroy(pcie_dev_class, devno);
		class_destroy(pcie_dev_class); 
		unregister_chrdev_region(devno, 1); 
	} 
	print("step 3\n");	
	while (!list_empty(&device_list)) 
	{
		tmp_pcidev = list_entry(device_list.next, struct pciedev_info, node);
		print("step 4\n");
		if((tmp_pcidev->busnum == busnum) && 
			(tmp_pcidev->devnum == devnum) && 
			(tmp_pcidev->funcnum == funcnum))
		{
			pci_clear_master(tmp_pcidev->pcidev);
			pci_free_consistent(tmp_pcidev->pcidev, MAX_LENGTH * 4, control1_dma_buf, control1_dma_addr);
			pci_free_consistent(tmp_pcidev->pcidev, MAX_LENGTH * 4, control0_channel1_dma_buf, control0_channel1_dma_addr);
			pci_free_consistent(tmp_pcidev->pcidev,MAX_LENGTH * 4, control0_dma_buf, control0_dma_addr);
			free_irq(control1_soft_irq, NULL);
			free_irq(control0_channel1_soft_irq, NULL);
			free_irq(control0_soft_irq, NULL);
			iounmap(control1_regs);
			iounmap(control1_pic);
			iounmap(control0_channel1_regs);
			iounmap(control0_channel1_pic);
			iounmap(control0_regs);
			iounmap(control0_pic);
			pci_disable_device(tmp_pcidev->pcidev);
			print("step 6\n");
			list_del(&tmp_pcidev->node);
			if(tmp_pcidev)
				kfree(tmp_pcidev);  
		}
		kfree(pciedev);
		pci_unregister_driver(&general_pci_driver);  
	}
        
}
module_exit(pciedev_cleanup_module);

MODULE_AUTHOR("yptian <ygliu@fiberhome.com.cn>");
MODULE_DESCRIPTION("Fiberhome/EOS Platform processor Driver");
MODULE_LICENSE("GPL");