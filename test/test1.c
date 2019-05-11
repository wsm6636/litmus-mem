
#include <linux/module.h>
#include <linux/kernel.h>
extern int get_edfbudget(int get_master,int get_curbudget);
int __init init(void){
	get_edfbudget(1,200);
	printk("test is start\n");
	return 0;
}
module_init(init);
MODULE_LICENSE("GPL");
