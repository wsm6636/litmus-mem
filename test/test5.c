#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/syscalls.h>
#include <asm/unistd.h>
#include <asm/uaccess.h>

#define MY_FILE "/sys/kernel/debug/memguard/limit"

char buf[256];
struct file *file = NULL;

static int __init init(void)
{
        mm_segment_t old_fs;
	buf[128]="";
	int mem=400;
	int g=3;
	int j;
        if(file == NULL)
                file = filp_open(MY_FILE, O_RDWR | O_APPEND | O_CREAT, 0644);
        if (IS_ERR(file)) {
                return 0;
        }
        j=sprintf(buf,"%d%s%d", g," ",mem);
        old_fs = get_fs();
        set_fs(KERNEL_DS);
        file->f_op->write(file, (char *)buf, sizeof(buf), &file->f_pos);
        set_fs(old_fs);
        filp_close(file, NULL);  
        file = NULL;
        return 0;
}

static void __exit fini(void)
{
        if(file != NULL)
                filp_close(file, NULL);
	buf[128]="";
}

module_init(init);
module_exit(fini);
MODULE_LICENSE("GPL");


