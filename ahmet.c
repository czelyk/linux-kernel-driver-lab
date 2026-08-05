#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include  <linux/ioctl.h>

#define BUFFER_SIZE 1024
#define AHMET_IOCTL_MAGIC 'A'
#define AHMET_CLEAR_BUFFER \
        _IO(AHMET_IOCTL_MAGIC, 1)
#define AHMET_GET_BUFFER_SIZE \
        _IOR(AHMET_IOCTL_MAGIC, 2, size_t)

static dev_t dev_num;
static struct cdev ahmet_cdev;
static struct class *ahmet_class;
static struct device *ahmet_device;

//static char buffer[BUFFER_SIZE];


//static char *buffer;
//static size_t buffer_size = 0;
//static size_t buffer_capacity = 0;

//static struct mutex ahmet_mutex;

struct ahmet_device
{
char *buffer;
size_t buffer_size;
size_t buffer_capacity;

struct mutex ahmet_mutex;
};

static struct ahmet_device *ahmet_dev;

static int ahmet_open(struct inode *inode, struct file *file)
{
    file->private_data = ahmet_dev;
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
    struct ahmet_device *dev = file->private_data;

    size_t bytes_to_read;
    mutex_lock(&dev->ahmet_mutex);

    if (*offset >= dev->buffer_size)
    {
        mutex_unlock(&dev->ahmet_mutex);
        return 0;
    }

    bytes_to_read = min(len, dev->buffer_size - *offset);

    if (copy_to_user(buf,
                     dev->buffer + *offset,
                     bytes_to_read))
    {
        mutex_unlock(&dev->ahmet_mutex);
        return -EFAULT;
    }

    *offset += bytes_to_read;

    pr_info("Device read: %zu bytes\n", bytes_to_read);

    mutex_unlock(&dev->ahmet_mutex);

    return bytes_to_read;
}

static ssize_t ahmet_write(struct file *file,
                           const char __user *buf,
                           size_t len,
                           loff_t *offset)
{
    struct ahmet_device *dev = file->private_data;

    char *new_buffer;
    mutex_lock(&dev->ahmet_mutex);

    if (len > dev->buffer_capacity)
        {//len = buffer_capacity;
        new_buffer = krealloc(dev->buffer, len, GFP_KERNEL);

        if(!new_buffer)
        {
            mutex_unlock(&dev->ahmet_mutex);
            return -ENOMEM;
        }
        dev->buffer = new_buffer;
        dev->buffer_capacity = len;
        }

    if (copy_from_user(dev->buffer, buf, len))
    {
        mutex_unlock(&dev->ahmet_mutex);
        return -EFAULT;
    }

    dev->buffer_size = len;

    mutex_unlock(&dev->ahmet_mutex);

    pr_info("Device write: %zu bytes\n", len);

    return len;
}


static long ahmet_ioctl(struct file *file,
                        unsigned int cmd,
                        unsigned long arg)
{
    struct ahmet_device *dev = file->private_data;

    pr_info("ioctl received cmd=%u\n", cmd);
    
    switch (cmd)
    {
        case AHMET_CLEAR_BUFFER: 
            mutex_lock(&dev->ahmet_mutex);
            memset(dev->buffer, 0, dev->buffer_capacity);
            dev->buffer_size = 0;
            mutex_unlock(&dev->ahmet_mutex);
            pr_info("Device buffer cleared\n");
            return 0;

        case AHMET_GET_BUFFER_SIZE:
            mutex_lock(&dev->ahmet_mutex);

            if (copy_to_user((
                    void __user *)arg,
                    &dev->buffer_size,
                    sizeof(size_t)))
            {
                mutex_unlock(&dev->ahmet_mutex);
                return  -EFAULT;
            }

            mutex_unlock(&dev->ahmet_mutex);

            pr_info("Returned buffer size\n");

            return 0;
        

        default:
            return -EINVAL;
    }

}

static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = ahmet_open,
    .release = ahmet_release,
    .read = ahmet_read,
    .write = ahmet_write,
    .unlocked_ioctl = ahmet_ioctl,
};

static int __init ahmet_init(void)
{
    int ret;

    ahmet_dev = kzalloc(sizeof(*ahmet_dev), GFP_KERNEL);

    if(!ahmet_dev)
    {
        pr_err("Failed to allocate device structure\n");
        return -ENOMEM;
    }


    mutex_init(&ahmet_dev->ahmet_mutex);

    ahmet_dev->buffer = kzalloc(BUFFER_SIZE, GFP_KERNEL);

    //kzalloc alttaki kmalloc yapısının yerine geçiyor. 
    //normalde eski atıklarla dolu olan bir alan da tanımlanabilir.
    //ama bu yapı sayesinde alan direkt olarak sıfırlanıyor.
    
    //ahmet_dev->buffer = kmalloc(BUFFER_SIZE, GFP_KERNEL);
    //memset(ahmet_dev->buffer, 0, BUFFER_SIZE);

    if(!ahmet_dev->buffer)
    {
        pr_err("Failed to allocate buffer\n");
        kfree(ahmet_dev);
        ahmet_dev = NULL;        
        return -ENOMEM;
    }

    ahmet_dev->buffer_capacity = BUFFER_SIZE;
    //ahmet_dev->buffer_size = 0;
    //memset(ahmet_dev->buffer, 0, ahmet_dev->buffer_capacity);

    ret = alloc_chrdev_region(&dev_num, 0, 1, "ahmet");
    if (ret < 0)
    {
        pr_err("Failed to allocate device number\n");
        goto free_buffer;
    }

    pr_info("Major: %d Minor: %d\n",
            MAJOR(dev_num),
            MINOR(dev_num));

    cdev_init(&ahmet_cdev, &fops);

    ret = cdev_add(&ahmet_cdev, dev_num, 1);
    if (ret < 0)
    {
        pr_err("Failed to add cdev\n");
        goto unregister_chrdev;
    }

    ahmet_class = class_create("ahmet_class");
    if (IS_ERR(ahmet_class))
    {
        ret = PTR_ERR(ahmet_class);
        goto unregister_cdev;
        //cdev_del(&ahmet_cdev);
        //unregister_chrdev_region(dev_num, 1);
        //return PTR_ERR(ahmet_class);
    }

    ahmet_device = device_create(
        ahmet_class,
        NULL,
        dev_num,
        NULL,
        "ahmet"
    );

    if(IS_ERR(ahmet_device))
    {
        ret = PTR_ERR(ahmet_device);
        goto unregister_class;
        //class_destroy(ahmet_class);
        //cdev_del(&ahmet_cdev);
        //unregister_chrdev_region(dev_num, 1);
        //return PTR_ERR(ahmet_device);
    }

    pr_info("Ahmet character device added\n");

    return 0;

    unregister_class:

        class_destroy(ahmet_class);

    unregister_cdev:

        cdev_del(&ahmet_cdev);

    unregister_chrdev:
        unregister_chrdev_region(dev_num, 1);

    free_buffer:
        kfree(ahmet_dev->buffer);

        kfree(ahmet_dev);
        ahmet_dev = NULL;

        return ret;
}

static void ahmet_cleanup(void)
{
    if(ahmet_device)
        device_destroy(ahmet_class, dev_num);


    if(ahmet_class)
        class_destroy(ahmet_class);

    cdev_del(&ahmet_cdev);

    unregister_chrdev_region(dev_num, 1);


    if(ahmet_dev)
    {
        mutex_destroy(&ahmet_dev->ahmet_mutex);

        kfree(ahmet_dev->buffer);

        kfree(ahmet_dev);

        ahmet_dev = NULL;
    }
}


static void __exit ahmet_exit(void)
{
    ahmet_cleanup();

    pr_info("Ahmet module unloaded!\n");
}

module_init(ahmet_init);
module_exit(ahmet_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ahmet Hasan Celik");
MODULE_DESCRIPTION("My first Linux character driver");