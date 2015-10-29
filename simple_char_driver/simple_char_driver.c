#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/semaphore.h>

#include <asm/uaccess.h>

#define SCULL_QUANTUM 4000
#define SCULL_QSET 1000

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jonas");
MODULE_DESCRIPTION("Simple Character Device Driver");

static dev_t dev_nrs = 0;
static unsigned int count = 1;
static unsigned int minor = 0;
static unsigned int major;
static char driver_name[] = "scd_test";
static int result = -2;
static int reg_result = -2;

struct scull_dev {
	struct cdev cdev;
	struct scull_qset *data;
	int qset;
	int quantum;
	unsigned long size;
	unsigned int access_key;
	//struct semaphore sem;
};

struct scull_qset {
	void **data;
	struct scull_qset *next;
};

int scull_trim(struct scull_dev *dev)
{
	struct scull_qset *next, *dptr;
	int qset = dev->qset;
	int i;

	for (dptr = dev->data; dptr; dptr = next) {
		if (dptr->data) {
			for (i = 0; i < qset; i++)
				kfree(dptr->data[i]);
			kfree(dptr->data);
			dptr->data = NULL;
		}
		next = dptr->next;
		kfree(dptr);
	}
	dev->size = 0;
	dev->quantum = SCULL_QUANTUM;
	dev->qset = SCULL_QSET;
	dev->data = NULL;

	return 0;
}

struct scull_qset *scull_follow(struct scull_dev *dev, int n)
{
	struct scull_qset *qs = dev->data;

        /* Allocate first qset explicitly if need be */
	if (! qs) {
		qs = dev->data = kmalloc(sizeof(struct scull_qset), GFP_KERNEL);
		if (qs == NULL)
			return NULL;  /* Never mind */
		memset(qs, 0, sizeof(struct scull_qset));
	}

	/* Then follow the list */
	while (n--) {
		if (!qs->next) {
			qs->next = kmalloc(sizeof(struct scull_qset), GFP_KERNEL);
			if (qs->next == NULL)
				return NULL;  /* Never mind */
			memset(qs->next, 0, sizeof(struct scull_qset));
		}
		qs = qs->next;
		continue;
	}
	return qs;
}


struct scull_dev dev;
//static loff_t write_pos = 0;

static int scull_open(struct inode *inode, struct file *filp)
{
	struct scull_dev *open_dev;

	open_dev = container_of(inode->i_cdev, struct scull_dev, cdev);
	filp->private_data = open_dev;

	if ( (filp->f_flags & O_ACCMODE) == O_WRONLY) {
		scull_trim(&dev);
	}
	//pr_notice("opening\n");
	return 0;
}

static int scull_release(struct inode *inode, struct file *filp)
{
	pr_notice("size = %lu", dev.size);
	return 0;
}

static ssize_t scull_read(struct file *filp, char __user *buf, size_t count,
		loff_t *f_pos)
{
	struct scull_dev *read_dev = filp->private_data; /* the first list item */
	struct scull_qset *dptr;
	int quantum = read_dev->quantum;
	int qset = read_dev->qset;
	int itemsize = quantum * qset; /* how many bytes in the list item */
	int item, s_pos, q_pos, rest;
	ssize_t ret = 0;

	if (*f_pos >= read_dev->size)
		goto out;
	if (*f_pos + count > read_dev->size)
		count = read_dev->size - *f_pos;

	/* find list item, qset index, and offset in the quantum */
	item = (long)*f_pos / itemsize;
	rest = (long)*f_pos % itemsize;
	s_pos = rest / quantum;
	q_pos = rest % quantum;

	/* follow the list up to the right position (defined elsewhere) */
	dptr = scull_follow(read_dev, item);

	if (!dptr->data)
		goto out; /* don't fill holes */
	if (!dptr->data[s_pos])
		goto out;

	/* read only up to the end of this quantum */
	if (count > quantum - q_pos)
		count = quantum - q_pos;

	if (copy_to_user(buf, dptr->data[s_pos] + q_pos, count)) {
		ret = -EFAULT;
		goto out;
	}
	*f_pos += count;
	ret = count;

	out:
	return ret;
}

static ssize_t scull_write(struct file *filp, char __user *buf, size_t count,
		loff_t *f_pos)
{
	struct scull_dev *write_dev = filp->private_data;
	struct scull_qset *dptr;
	int quantum = write_dev->quantum, qset = write_dev->qset;
	int itemsize = quantum * qset;
	int item, s_pos, q_pos, rest;
	ssize_t retval = -ENOMEM; /* value used in "goto out" statements */
	//if (*f_pos == 0)
	//	*f_pos += write_pos;

	/* find listitem, qset index and offset in the quantum */
	item = (long)*f_pos / itemsize;
	rest = (long)*f_pos % itemsize;
	s_pos = rest / quantum; q_pos = rest % quantum;

	/* follow the list up to the right position */
	dptr = scull_follow(write_dev, item);
	if (dptr == NULL)
		goto out;
	if (!dptr->data) {
		dptr->data = kmalloc(qset * sizeof(char *), GFP_KERNEL);
		if (!dptr->data)
			goto out;
		memset(dptr->data, 0, qset * sizeof(char *));
	}
	if (!dptr->data[s_pos]) {
		dptr->data[s_pos] = kmalloc(quantum, GFP_KERNEL);
		if (!dptr->data[s_pos])
			goto out;
	}
	/* write only up to the end of this quantum */
	if (count > quantum - q_pos)
		count = quantum - q_pos;

	if (copy_from_user(dptr->data[s_pos]+q_pos, buf, count)) {
		retval = -EFAULT;
		goto out;
	}
	*f_pos += count;
	//write_pos += count;
	retval = count;

        /* update the size */
	if (write_dev->size < *f_pos)
		write_dev->size = *f_pos;

	out:
	return retval;
}

static struct file_operations fops = {
	.owner = THIS_MODULE,
	//.llseek = scull_llseek,
	.read = scull_read,
	.write = scull_write,
	//.ioctl = scull_ioctl,
	.open = scull_open,
	.release = scull_release,
};

static int register_device(void)
{
	result = alloc_chrdev_region(&dev_nrs, minor, count, driver_name);

	cdev_init(&dev.cdev, &fops);	/* might be a problem with non embedded struct cdev? */
	dev.cdev.ops = &fops;
	dev.cdev.owner = THIS_MODULE;

	reg_result = cdev_add(&dev.cdev, dev_nrs, count);

	major = MAJOR(dev_nrs);

	return reg_result;
}

static int __init hello_init(void)
{
	dev.quantum = SCULL_QUANTUM;
	dev.qset = SCULL_QSET;

	reg_result = register_device();	

	pr_notice("result = %d\n", result);
	pr_notice("major = %d\n", major);
	pr_notice("reg_result = %d\n", reg_result);
	
	return 0;
}

static void __exit hello_cleanup(void)
{
	if (result == 0)
		unregister_chrdev_region(dev_nrs, count);
	cdev_del(&dev.cdev);
	scull_trim(&dev);

	pr_notice("_____________________________________________________\n");
}

module_init(hello_init);
module_exit(hello_cleanup);
