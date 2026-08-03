#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>


static dev_t dev_num;
static struct cdev ahmet_cdev;




                           

static int ahmet_open(struct inode *inode, struct file *file)
{
    pr_info("Device opened\n");
    return 0;
}


static int ahmet_release(struct inode *inode, struct file *file)
{
    pr_info("Device closed\n");
    return 0;
}

static ssize_t ahmet_read(struct file *file,
                          char __user *buf,
                          size_t len,
                          loff_t *offset)
{
    pr_info("Device read called\n");
    return 0;
}


static ssize_t ahmet_write(struct file *file,
                           const char __user *buf,
                           size_t len,
                           loff_t *offset)
{
    pr_info("Device write called\n");
    return len;
}

static const struct file_operations fops = {
    .read = ahmet_read,
    .write = ahmet_write,
    .owner = THIS_MODULE,
    .open = ahmet_open,
    .release = ahmet_release,
};


static int __init ahmet_init(void)
{
    int ret;

    ret = alloc_chrdev_region(&dev_num, 0, 1, "ahmet");

    if (ret < 0)
    {
        pr_err("Failed to allocate device number\n");
        return ret;
    }

    pr_info("Major: %d Minor: %d\n",
            MAJOR(dev_num),
            MINOR(dev_num));


    cdev_init(&ahmet_cdev, &fops);

    ret = cdev_add(&ahmet_cdev, dev_num, 1);

    if (ret < 0)
    {
        pr_err("Failed to add cdev\n");

        unregister_chrdev_region(dev_num, 1);

        return ret;
    }


    pr_info("Ahmet character device added\n");

    return 0;
}

static void __exit ahmet_exit(void)
{
    cdev_del(&ahmet_cdev);
    unregister_chrdev_region(dev_num, 1);
    pr_info("Ahmet module unloaded!\n");
    //printk(KERN_INFO "Ahmet module unloaded!\n");
}

module_init(ahmet_init);
module_exit(ahmet_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ahmet Hasan Celik");
MODULE_DESCRIPTION("My first Linux kernel module");