#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "litmus.h"
#define CALL(exp) do{\
	int ret;\
	ret=exp;\
	if(ret!=0)\
		fprintf(stderr,"%s failed: %m\n", #exp);\
	else\
		fprintf(stderr,"%s ok.\n", #exp);\
}while(0)
int main (){
	struct rt_task param;
	//CALL(init_litmus());
	//CALL(task_mode(LITMUS_RT_TASK));
	CALL(get_rt_task_param(gettid(),&param));
        printf("pid==%d,budget==%d\n",gettid(),param.mem_budget_task);
	//CALL(task_mode(BACKGROUND_TASK));
	return 0;
}
