
#include <linux/module.h>
#include <linux/kernel.h>
int get_master;
int get_curbudget;
int __init init(void){
//	get_edfbudget(1,200);
	get_master=4;
	get_curbudget=200;
	pr_info("test3 is start\n");
	pr_info("master==%d,curbudget==%d\n",get_master,get_curbudget);
//	pr_info("test2 is start\n");
	return 0;
}
EXPORT_SYMBOL(get_curbudget);
EXPORT_SYMBOL(get_master);
module_init(init);
MODULE_LICENSE("GPL");
