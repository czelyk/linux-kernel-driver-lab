#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include  <linux/ioctl.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/kfifo.h>

#define BUFFER_SIZE PAGE_SIZE

#define AHMET_IOCTL_MAGIC 'A'

#define AHMET_CLEAR_BUFFER \
        _IO(AHMET_IOCTL_MAGIC, 1)

#define AHMET_GET_BUFFER_SIZE \
        _IOR(AHMET_IOCTL_MAGIC, 2, size_t)

#define AHMET_GET_BUFFER_CAPACITY \
        _IOR(AHMET_IOCTL_MAGIC, 3, size_t)

#define AHMET_FILL_BUFFER \
        _IOW(AHMET_IOCTL_MAGIC, 4, char)

#define AHMET_GET_DEVICE_ID \
        _IOR(AHMET_IOCTL_MAGIC, 5, int)

#define DEVICE_COUNT 4


        

static dev_t dev_num;
static struct class *ahmet_class;
static struct device *ahmet_device[DEVICE_COUNT];

//static char buffer[BUFFER_SIZE];


//static char *buffer;
//static size_t buffer_size = 0;
//static size_t buffer_capacity = 0;

//static struct mutex ahmet_mutex;

struct ahmet_device
{
struct cdev cdev;
struct kfifo fifo;
int device_id;

struct mutex ahmet_mutex;

struct fasync_struct *async_queue;

wait_queue_head_t read_queue;
wait_queue_head_t write_queue;
};

//static struct ahmet_device *ahmet_dev;

static struct ahmet_device ahmet_devices[DEVICE_COUNT];

static int ahmet_open(struct inode *inode, struct file *file)
{
    struct ahmet_device *dev;

    dev = container_of(inode->i_cdev,
                        struct ahmet_device,
                        cdev);

    file->private_data = dev;

    pr_info("Device opened\n");
    return 0;
}

static int ahmet_release(struct inode *inode, struct file *file)
{
    struct ahmet_device *dev = file->private_data;

    (void) inode;
    
    fasync_helper(-1,
              file,
              0,
              &dev->async_queue);
    pr_info("Device closed\n");
    return 0;
}

static ssize_t ahmet_read(struct file *file,
                          char __user *buf,
                          size_t len,
                          loff_t *offset)
{   
    (void) offset;

    struct ahmet_device *dev = file->private_data;

    unsigned int copied = 0;
    int ret;

    if(len == 0)
        return 0;

    if((file->f_flags & O_NONBLOCK) &&
        kfifo_is_empty(&dev->fifo))
    {
        return -EAGAIN;
    }

    if (wait_event_interruptible(dev->read_queue,
                                !kfifo_is_empty(&dev->fifo)))
    {
        return -ERESTARTSYS;
    }

    mutex_lock(&dev->ahmet_mutex);

    ret = kfifo_to_user(&dev->fifo,
                        buf,
                        len,
                        &copied);

    mutex_unlock(&dev->ahmet_mutex);
    
    if(ret)
        return ret;

    wake_up_interruptible(&dev->write_queue);

    pr_info("FIFO read: %u bytes\n", copied);

    return copied;
}

static ssize_t ahmet_write(struct file *file,
                           const char __user *buf,
                           size_t len,
                           loff_t *offset)
{
    (void) offset;

    struct ahmet_device *dev = file->private_data;

    unsigned int copied = 0;
    int ret;
    //char *new_buffer;
    if (len> kfifo_size(&dev->fifo))
    {
        return -ENOSPC;
    }

    if ((file->f_flags & O_NONBLOCK) &&
    kfifo_avail(&dev->fifo) < len)
    {
    return -EAGAIN;
    }

    if(wait_event_interruptible(dev->write_queue,
                                kfifo_avail(&dev->fifo) >= len))
    {
        return -ERESTARTSYS;
    }

    mutex_lock(&dev->ahmet_mutex);

    ret = kfifo_from_user(&dev->fifo,
                            buf,
                            len,
                            &copied);
                            

    mutex_unlock(&dev->ahmet_mutex);

    if(ret)
        return ret;

    wake_up_interruptible(&dev->read_queue);

    if (dev->async_queue)
        kill_fasync(&dev->async_queue,
                    SIGIO,
                    POLL_IN);

    pr_info("Device write: %u bytes\n", copied);

    return copied;
}

static __poll_t ahmet_poll(struct file *file,
                            struct poll_table_struct *wait)
{
    struct ahmet_device *dev = file->private_data;
    __poll_t mask = 0;

    poll_wait(file, &dev->read_queue, wait);
    poll_wait(file, &dev->write_queue, wait);

    if(!kfifo_is_empty(&dev->fifo))
    mask |= EPOLLIN | EPOLLRDNORM;


    if(!kfifo_is_full(&dev->fifo))
    {
        mask |= EPOLLOUT | EPOLLWRNORM;
    }

    return mask;
}

static int ahmet_fasync(int fd,
                        struct file *file,
                        int on)
{
    struct ahmet_device *dev = file->private_data;

    return fasync_helper(fd,
                        file,
                        on,
                        &dev->async_queue);
}


static long ahmet_ioctl(struct file *file,
                        unsigned int cmd,
                        unsigned long arg)
{
    struct ahmet_device *dev = file->private_data;
    size_t fifo_length;
    size_t fifo_capacity;

    pr_info("ioctl received cmd=%u\n", cmd);
    
    switch (cmd)
    {
        case AHMET_CLEAR_BUFFER: 
            mutex_lock(&dev->ahmet_mutex);
            
            kfifo_reset(&dev->fifo);
            
            mutex_unlock(&dev->ahmet_mutex);
            
            wake_up_interruptible(&dev->write_queue);

            pr_info("FIFO cleared\n");
            
            return 0;

        case AHMET_GET_BUFFER_SIZE:
            mutex_lock(&dev->ahmet_mutex);

            fifo_length =kfifo_len(&dev->fifo);

            mutex_unlock(&dev->ahmet_mutex);
            
            if (copy_to_user((
                    void __user *)arg,
                    &fifo_length,
                    sizeof(fifo_length)))
            {
                return  -EFAULT;
            }

            pr_info("Returned FIFO length: %zu\n", fifo_length);

            return 0;


        case AHMET_GET_BUFFER_CAPACITY:
          
        mutex_lock(&dev->ahmet_mutex);

        fifo_capacity =kfifo_size(&dev->fifo);

        mutex_unlock(&dev->ahmet_mutex);

            if (copy_to_user((void __user *)arg,
                            &fifo_capacity,
                            sizeof(fifo_capacity)))
            {
                return -EFAULT;
            }

            pr_info("Returned FIFO capacity\n");
            
            return 0;

        case AHMET_FILL_BUFFER:
        {
            
            char fill_char;
            unsigned int i;
            unsigned int inserted;
            
            if(copy_from_user(&fill_char,
                                (void __user *)arg,
                                sizeof(char)))
            {
                return -EFAULT;
            }

            mutex_lock(&dev->ahmet_mutex);
            
            kfifo_reset(&dev->fifo);
            
            for (i= 0; i < kfifo_size(&dev->fifo); i++)
            {
                inserted = kfifo_in(&dev->fifo,
                                    &fill_char,
                                    1);

                if(inserted != 1)
                    break;
            }

            mutex_unlock(&dev->ahmet_mutex);

            wake_up_interruptible(&dev->read_queue);

            if(dev->async_queue)
            {
                kill_fasync(&dev->async_queue,
                            SIGIO,
                            POLL_IN);
            }

            pr_info("Buffer filled with '%c'\n", fill_char);

            return 0;
        }

        case AHMET_GET_DEVICE_ID:
            if(copy_to_user((void __user *)arg,
                            &dev->device_id,
                            sizeof(int)))
            {
                return -EFAULT;
            }

            pr_info("Returned device id: %d\n",
                    dev->device_id);
        
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
    .poll = ahmet_poll,
    .fasync = ahmet_fasync,
};

static int __init ahmet_init(void)
{
    int ret;
    int i;

    for(i=0;i < DEVICE_COUNT;i++)
    {
        ahmet_devices[i].device_id = i;
        mutex_init(&ahmet_devices[i].ahmet_mutex);
        
        init_waitqueue_head(&ahmet_devices[i].read_queue);
        init_waitqueue_head(&ahmet_devices[i].write_queue);

        ret = kfifo_alloc(&ahmet_devices[i].fifo,
                            BUFFER_SIZE,
                            GFP_KERNEL);

        if(ret)
        {
            pr_err("Failed to allocate FIFO for device %d\n", i);

            mutex_destroy(&ahmet_devices[i].ahmet_mutex);

            goto free_device_buffers;
        }
    }

    ret = alloc_chrdev_region(&dev_num, 0, DEVICE_COUNT, "ahmet");
    if (ret < 0)
    {
        pr_err("Failed to allocate device number\n");
        goto free_device_buffers;
    }

    pr_info("Major: %d Minor: %d\n",
            MAJOR(dev_num),
            MINOR(dev_num));


    for(i=0; i< DEVICE_COUNT; i++)
    {
        dev_t current_dev_num;

        current_dev_num = MKDEV(MAJOR(dev_num),
                                MINOR(dev_num)+ i);
        
        cdev_init(&ahmet_devices[i].cdev, &fops);

        ret= cdev_add(&ahmet_devices[i].cdev,
                                        current_dev_num,
                                        1);

        if (ret<0)
        {
            pr_err("Failed to add cdev for device %d\n", i);
            goto unregister_cdevs;
        }
    }

    ahmet_class = class_create("ahmet_class");
    if (IS_ERR(ahmet_class))
    {
        ret = PTR_ERR(ahmet_class);
        goto unregister_cdevs;
    }

    for(i = 0;i < DEVICE_COUNT; i++)
    {
        dev_t current_dev_num;

        current_dev_num = MKDEV(MAJOR(dev_num),
                                MINOR(dev_num) + i);
        
        ahmet_device[i] = device_create(
        ahmet_class,
        NULL,
        current_dev_num,
        NULL,
        "ahmet%d",
        i
        );

        if(IS_ERR(ahmet_device[i]))
        {
            ret = PTR_ERR(ahmet_device[i]);
            pr_err("Failed to create device %d\n", i);
            goto destroy_devices;
        }
    }


    pr_info("Ahmet character device added\n");

    return 0;

    destroy_devices:
        while (--i >=0)
        {
            dev_t current_dev_num;

            current_dev_num = MKDEV(MAJOR(dev_num),
                                    MINOR(dev_num) + i);

            device_destroy(ahmet_class, current_dev_num);
            ahmet_device[i] = NULL;
        }

        i = DEVICE_COUNT;



        class_destroy(ahmet_class);

    unregister_cdevs:
        while(--i >=0)
            cdev_del(&ahmet_devices[i].cdev);


        unregister_chrdev_region(dev_num, DEVICE_COUNT);
        i= DEVICE_COUNT;

    free_device_buffers:
        while(--i >= 0)
        {
            mutex_destroy(&ahmet_devices[i].ahmet_mutex);
            
            kfifo_free(&ahmet_devices[i].fifo);
        }
        
        return ret;
}

static void ahmet_cleanup(void)
{
    int i;


    for(i = 0; i < DEVICE_COUNT; i++)
    {
        dev_t current_dev_num;

        current_dev_num = MKDEV(MAJOR(dev_num),
                                MINOR(dev_num) + i);

        device_destroy(ahmet_class, current_dev_num);
        ahmet_device[i] = NULL;
    }


    if(ahmet_class)
        class_destroy(ahmet_class);


    for(i =0;i < DEVICE_COUNT;i++)
    {
        cdev_del(&ahmet_devices[i].cdev);
    }

    unregister_chrdev_region(dev_num, DEVICE_COUNT);


    for(i=0; i< DEVICE_COUNT;i++)
    {
        mutex_destroy(&ahmet_devices[i].ahmet_mutex);

        kfifo_free(&ahmet_devices[i].fifo);
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
