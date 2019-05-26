#define DEBUG
#include <linux/module.h>
#include <linux/kobject.h>
#include <linux/kfifo.h>
#include <linux/cdev.h>
#include <linux/sysfs.h>
#include "pc_drv.h"


static struct kset *pc_drv_kset = NULL;


/* Custom attribute structure for PC_DEV */
struct pc_attr
{
	struct attribute attr;
	ssize_t (*show)(PC_DEV *dev, struct pc_attr *attr, char *buf);
	ssize_t (*store)(PC_DEV *dev, struct pc_attr *attr, const char *buf, size_t count);
};


/* store() method implementation for the reset attribute */
static ssize_t reset_store(PC_DEV *dev, struct pc_attr *attr, const char *buf, size_t count)
{
	int i = 0;
	
        dump_stack();
	sscanf(buf, "%d", &i);

	if (i == 1)
		reset_dev(dev); /* Reset the device */

        return count;
}


/* show() method implementation for the nbytes attribute */
static ssize_t nbytes_show(PC_DEV *pdev, struct pc_attr *attr, char *buf)
{
        dump_stack();

	/* Print the number of data bytes available in the device */
	return sprintf(buf, "%d\n", kfifo_len(&pdev->kfifo));
}


/* show() method implementation for the devno attribute */
static ssize_t devno_show(PC_DEV *pdev, struct pc_attr *attr, char *buf)
{
        dump_stack();

	/* Print the major and minor number for the device */
	return sprintf(buf, "%d,%d\n", MAJOR(pdev->dev), MINOR(pdev->dev));
}


/* Attributes for the pseudo character device */
static struct pc_attr reset_attr = __ATTR(reset, 0222, NULL, reset_store);
static struct pc_attr nbytes_attr = __ATTR(nbytes, 0444, nbytes_show, NULL);
static struct pc_attr devno_attr = __ATTR(devno, 0444, devno_show, NULL);


/* Default show() function to be passed to sysfs */
static ssize_t pc_attr_show(struct kobject *kobj, struct attribute *attr, char *buf)
{
	struct pc_attr *pc_attr;
	PC_DEV *dev;

	dump_stack();

	/* Retrieve pointers to the attribute and device objects and pass them to the corresponding show() function */
	pc_attr = container_of(attr, struct pc_attr, attr);
	dev = container_of(kobj, PC_DEV, kobj);

	if (!pc_attr->show)
		return -EIO;

	return pc_attr->show(dev, pc_attr, buf);
}


/* Default store() function to be passed to sysfs */
static ssize_t pc_attr_store(struct kobject *kobj, struct attribute *attr, const char *buf, size_t len)
{
	struct pc_attr *pc_attr;
	PC_DEV *dev;

	dump_stack();

	/* Retrieve pointers to the attribute and device objects and pass them to the corresponding store() function */
	pc_attr = container_of(attr, struct pc_attr, attr);
	dev = container_of(kobj, PC_DEV, kobj);

	if (!pc_attr->store)
		return -EIO;

	return pc_attr->store(dev, pc_attr, buf, len);
}


/* release() method implementation for the kobject */
static void pc_release(struct kobject *kobj)
{
}


/* Default methods for kobject attributes */
static struct sysfs_ops pc_sysfs_ops = {
	.show = pc_attr_show,
	.store = pc_attr_store,
};


/* Set of default attributes for the kobject */
static struct attribute *pc_def_attrs[] = {
	&reset_attr.attr,
	&nbytes_attr.attr,
	&devno_attr.attr,
	NULL,
};


/* ktype for the kobject */
static struct kobj_type pc_ktype = {
	.sysfs_ops = &pc_sysfs_ops,
	.release = pc_release,
	.default_attrs = pc_def_attrs,
};


/* Initialize the kobject instance */
int init_pc_kobj(struct kobject *kobj, const int n)
{
	int ret = 0;
	dump_stack();

	memset(kobj, 0, sizeof(struct kobject));
	kobj->kset = pc_drv_kset;

	ret = kobject_init_and_add(kobj, &pc_ktype, NULL, "device%d", n);
	return ret;
}


/* Decrement the kobject instance reference count */
void destroy_pc_kobj(struct kobject *kobj)
{
	kobject_put(kobj);
}


/* Function to create a kset for the character device and add it to sysfs */
int create_pc_kset(void)
{
	/* Create and add the kset to /sys/kernel */
	if ((pc_drv_kset = kset_create_and_add("kset_pc_devs", NULL, kernel_kobj)) == NULL)
		return -ENOMEM;

	return 0;
}


/* Remove the kset from sysfs */
void destroy_pc_kset(void)
{
	kset_unregister(pc_drv_kset);
}
