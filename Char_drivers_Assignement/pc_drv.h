typedef struct pc_dev
{
        struct list_head list;
        struct cdev cdev;
        unsigned char *buf;
        struct kobject kobj;
        struct kfifo kfifo;
        spinlock_t slock;
        wait_queue_head_t rqueue;
        wait_queue_head_t wqueue;
        dev_t dev;
} PC_DEV;


void reset_dev(PC_DEV *dev);
int init_pc_kobj(struct kobject *kobj, const int n);
int create_pc_kset(void);
void destroy_pc_kobj(struct kobject *kobj);
void destroy_pc_kset(void);
