
#include <linux/module.h>
#include <linux/kernel.h>
extern int get_master;
extern int get_curbudget;
int __init init(void){
//	get_edfbudget(1,200);
	pr_info("test2 is start\n");
	pr_info("master==%d,curbudget==%d\n",get_master,get_curbudget);
//	pr_info("test2 is start\n");
	return 0;
}
module_init(init);
MODULE_LICENSE("GPL");
