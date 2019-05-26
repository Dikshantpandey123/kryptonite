#define DEBUG
#include <linux/module.h>
#include <linux/kfifo.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>

#include "pc_drv.h"

#define MAX_BUFSIZE (4 * 1024)		/* Size of buffer used for FIFO */


static LIST_HEAD(dev_list);		/* Head node of the device list */
static unsigned int ndevices = 1;	/* Module parameter for number of devices */
static dev_t pc_devno;			/* Variable to store the first number in the allocated device range */


/* Define ioctl commands */
#define PCDEV_IOC_MAGIC 'a'
//as per conventions, we must generate ioctl commands, using certain 
//sw techniques, for generating unique commands, not hard coded commands 
#define PCDEV_IOC_RESET _IO(PCDEV_IOC_MAGIC, 0)

//#define PCDEV_IOC_RESET 3  //this hard coded ioctl command no. is 
                             //unacceptable, as per conventions 

#define PCDEV_IOC_READLEN _IOR(PCDEV_IOC_MAGIC, 1, int)

#define PCDEV_IOC_MAX	2


/* open() method implementation for the driver */
static int pc_drv_open(struct inode *inode, struct file *file)
{
	dump_stack();
	pr_debug("In pc_drv_open()\n");

	/* Store a pointer to the private data object for other methods */
	file->private_data = container_of(inode->i_cdev, PC_DEV, cdev);

	return 0;
}


/* release() method implementation for the driver */
static int pc_drv_release(struct inode *inode,struct file *file)
{
	dump_stack();
	pr_debug("In pc_drv_release()\n");

	return 0;
}


/* write() method implementation for the driver */
static ssize_t pc_drv_write(struct file *file, const char __user *buf, size_t count, loff_t *pos)
{
	PC_DEV *dev = file->private_data;
	ssize_t bytes;
	char *tmpbuf;

	dump_stack();
	pr_debug("In pc_drv_write()\n");

	/* Check whether the user space buffer pointer is valid */
	if (access_ok(VERIFY_READ, (void __user *)buf, (unsigned long)count))
	{
		/* Check the available space in the fifo */
		if ((bytes = kfifo_avail(&dev->kfifo)) == 0)
		{
			/* If buffer is full and file is opened is non-blocking mode, return with error */
			/* Else, wait till there is space available in the buffer */
			if (file->f_flags & O_NONBLOCK)
				return -EAGAIN;
			else
				wait_event_interruptible(dev->wqueue, (kfifo_avail(&dev->kfifo) != 0));
		}

		/* Allocate a temporary buffer for copying data into kfifo */
		if ((tmpbuf = kmalloc(count, GFP_KERNEL)) == NULL)
		{
			pr_alert("Error while allocating buffer for write\n");
			return -EFAULT;
		}

		/* Copy the data from user space buffer to temporary buffer */
		__copy_from_user(tmpbuf, buf, count);

		/* Write the data to the kfifo */
		bytes = kfifo_in_spinlocked(&dev->kfifo, (void *)tmpbuf, count, &dev->slock);
		kfree(tmpbuf);

		/* Wake up a process waiting in the read queue */
		wake_up_interruptible(&dev->rqueue);
		return bytes;
	}
	else
		return -EFAULT;
}


/* read() method implementation for the driver */
static ssize_t pc_drv_read(struct file *file, char __user *buf, size_t count, loff_t *pos)
{
	PC_DEV *dev = file->private_data;
	ssize_t bytes;
	char *tmpbuf;

	dump_stack();
	pr_debug("In pc_drv_read()\n");

	/* Check whether the user space buffer pointer is valid */
	if (access_ok(VERIFY_WRITE, (void __user *)buf, (unsigned long)count))
	{
		/* Check for data availability in the fifo */
		if ((bytes = kfifo_len(&dev->kfifo)) == 0)
		{
			/* If buffer is empty and file is opened is non-blocking mode, return with error */
			/* Else, wait till there is data available in the buffer */
			if (file->f_flags & O_NONBLOCK)
				return -EAGAIN;
			else
				wait_event_interruptible(dev->rqueue, (kfifo_len(&dev->kfifo) != 0));
		}

		/* Allocate a temporary buffer for copying data from kfifo */
		if ((tmpbuf = kmalloc(count, GFP_KERNEL)) == NULL)
		{
			pr_alert("Error while allocating buffer for read\n");
			return -EFAULT;
		}

		/* Read the data from the kfifo */
		bytes = kfifo_out_spinlocked(&dev->kfifo, (void *)tmpbuf, count, &dev->slock);

		/* Copy the data from temporary buffer to user space buffer */
		__copy_to_user(buf, tmpbuf, count);
		kfree(tmpbuf);

		/* Wake up a process waiting in the read queue */
		wake_up_interruptible(&dev->wqueue);
		return bytes;
	}
	else
		return -EFAULT;
}


/* Function to reset the device */
void reset_dev(PC_DEV *dev)
{
	unsigned long flags;

	/* Reset the fifo */
	spin_lock_irqsave(&dev->slock, flags);
	kfifo_reset(&dev->kfifo);
	spin_unlock_irqrestore(&dev->slock, flags);
}


/* ioctl() method implementation for driver */
static long pc_drv_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	PC_DEV *dev = file->private_data;
	long ret = 0;

	/* Validate the commands */
	if (_IOC_TYPE(cmd) != PCDEV_IOC_MAGIC)
		return -ENOTTY;

	if (_IOC_NR(cmd) >= PCDEV_IOC_MAX)
		return -ENOTTY;

	switch (cmd)
	{
		/* Reset the device */
		case PCDEV_IOC_RESET:
			reset_dev(dev); 
			break;

		/* Retrieve the length of data in the fifo */
		case PCDEV_IOC_READLEN:
			ret = put_user(kfifo_len(&dev->kfifo), (unsigned int *)arg); 
			break;

		/* Invalid command */
		default:
			ret = -ENOTTY; 
			break;
	}

	return ret;
}


/* File operations for pseudo character device */
static struct file_operations pc_drv_fops = {
	.open    = pc_drv_open,
	.read    = pc_drv_read,
	.write   = pc_drv_write,
	.release = pc_drv_release,
	.unlocked_ioctl = pc_drv_ioctl,
	.owner   = THIS_MODULE,
};


/* start() iterator method for the procfs file */
static void *pc_seq_start(struct seq_file *seq, loff_t *pos)
{
	PC_DEV *p; 
	loff_t off = 0;

	dump_stack();

	if (*pos == 0)
		seq_printf(seq, "DEVNAME\tDEVNO\n");

	/* Return the node (from the device list) located at the position specified by the pos parameter */
	list_for_each_entry(p, &dev_list, list)
	{
		if (*pos == off++)
		{
			pr_debug("In proc_seq_start, pos = %lld\n", *pos); 
			return p;
		}
	}

	return NULL;
}


/* next() iterator method for the procfs file */
static void *pc_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct list_head *n = ((PC_DEV *)v)->list.next; 

	pr_debug("In pc_seq_next()\n");

	++*pos;

	pr_debug("In pc_seq_next(), pos = %lld\n", *pos);

	/* Return the next node in the list of character devices */
	return (n != &dev_list)? list_entry(n, PC_DEV, list) : NULL;
}


/* stop() method for the procfs file */
static void pc_seq_stop(struct seq_file *seq, void *v)
{
	pr_debug("In pc_seq_stop()\n");
}


/* show() method for the procfs file */
static int pc_seq_show(struct seq_file *seq, void *v) 
{
	int ret = 0;
	const PC_DEV *p = v; 

	pr_debug("In pc_seq_show()\n");

	/* Print the device name, major and minor numbers */
	ret = seq_printf(seq, "%s\t%d,%d\n", kobject_name(&p->cdev.kobj), MAJOR(p->dev), MINOR(p->dev));

	printk(KERN_INFO "The return value of seq_printf is %d\n", ret); 

	return 0;
}


/* Iterator operations for procfs file */
static struct seq_operations pc_info_sops = {
	.start = pc_seq_start,
	.next = pc_seq_next,
	.stop = pc_seq_stop,
	.show = pc_seq_show,
};


/* open() method implementation for the procfs file */
static int pc_info_open(struct inode *inode, struct file *file)
{
	pr_debug("In pc_info_open()\n"); 
	dump_stack(); 

	return seq_open(file, &pc_info_sops);
}


/* File operations for procfs file */
static struct file_operations pc_info_fops = {
	.owner    = THIS_MODULE,
	.open     = pc_info_open, 
	.read     = seq_read,
	.llseek   = seq_lseek,
	.release  = seq_release,
};


/* Accept module parameter for number of devices */
module_param(ndevices, int, S_IRUGO);


/* Function called when loading module */
static int __init pc_drv_init(void)
{
	int i, ret = 0;
	PC_DEV *p_cdev, *p, *n;
	struct proc_dir_entry *entry = NULL;

	dump_stack();
	pr_debug("In pc_drv_init()\n");

	/* Allocate a range of character device numbers */
	if (alloc_chrdev_region(&pc_devno, 0, ndevices, "pc_driver"))
	{
		pr_alert("Error when setting up char device(s)\n");
		return -EBUSY;
	}

	/* Create a kset for the character devices and add it to sysfs */
	if ((ret = create_pc_kset()))
	{
		pr_alert("Error when creating kset\n");
		goto handle_err;
	}

	for (i = 0; i < ndevices; i++)
	{
		/* Allocate memory for the pseudo device and add it to the device list */
		if ((p_cdev = kmalloc(sizeof(PC_DEV), GFP_KERNEL)) == NULL)
		{
			ret = -ENOMEM;
			goto handle_err;
		}

		list_add_tail(&p_cdev->list, &dev_list);

		/* Allocate a buffer for the fifo */
		if ((p_cdev->buf = kmalloc(MAX_BUFSIZE, GFP_KERNEL)) == NULL)
		{
			pr_alert("Error when allocating buffer memory for device: %d\n", i);
			ret = -ENOMEM;
			goto handle_err;
		}

		/* Initialize the read and write wait queues and spinlock for fifo operations */
		init_waitqueue_head(&p_cdev->wqueue);
		init_waitqueue_head(&p_cdev->rqueue);
		spin_lock_init(&p_cdev->slock);

		/* Initialize the fifo */
		if ((ret = (kfifo_init(&p_cdev->kfifo, p_cdev->buf, MAX_BUFSIZE))))
		{
			pr_alert("Error when initializing kfifo for device: %d\n", i);
			goto handle_err;
		}

		/* Initialize the cdev structure */
		cdev_init(&p_cdev->cdev, &pc_drv_fops);
		kobject_set_name(&(p_cdev->cdev.kobj), "device%d", i);
		p_cdev->cdev.ops = &pc_drv_fops;
		p_cdev->cdev.owner = THIS_MODULE;

		/* Set the device number */
		p_cdev->dev = pc_devno + i;

		/* Add the character device to the system */
		if ((ret = (cdev_add(&p_cdev->cdev, pc_devno + i, 1))))
		{
			pr_alert("Error while adding device: %d\n", i);
			goto handle_err;
		}

		/* Initialize the kobject */
		if ((ret = init_pc_kobj(&p_cdev->kobj, i)))
		{
			pr_alert("Error while initializing kobject\n");
			goto handle_err;
		}
	}

	/* Create an entry in the proc filesystem */
	if ((entry = proc_create("pc_devinfo", S_IRUSR, NULL)) == NULL)
	{
		pr_alert("Error while creating proc entry for driver\n");
		ret = -EINVAL;
		goto handle_err;
	}

	entry->proc_fops = &pc_info_fops;
	return ret;

handle_err:
	/* Error handling */
	list_for_each_entry_safe(p, n, &dev_list, list)
	{
		cdev_del(&p->cdev);
		destroy_pc_kobj(&p->kobj);
		kfree(p->buf);
		kfree(p);
	}
	destroy_pc_kset();
	unregister_chrdev_region(pc_devno, ndevices);
	return ret;
}


/* Function called when unloading module */
static void __exit pc_drv_exit(void)
{
	PC_DEV *p, *n;

	dump_stack();
	pr_debug("In pc_drv_exit()\n");

	/* Release resources allocated during module initialization */
	remove_proc_entry("pc_devinfo", NULL);
	list_for_each_entry_safe(p, n, &dev_list, list)
	{
		cdev_del(&p->cdev);
		destroy_pc_kobj(&p->kobj);
		kfree(p->buf);
		kfree(p);
	}
	destroy_pc_kset();
	unregister_chrdev_region(pc_devno, ndevices);
}


module_init(pc_drv_init);
module_exit(pc_drv_exit);


MODULE_DESCRIPTION("Pseudo Char Device Driver");
MODULE_ALIAS("pseudo char driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("0:1.0");
