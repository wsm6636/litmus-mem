#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/version.h>
#include <generated/uapi/linux/version.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/smp.h> /* IPI calls */
#include <linux/irq_work.h>
#include <linux/hardirq.h>
#include <linux/perf_event.h>
#include <linux/delay.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <asm/atomic.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/uaccess.h>
#include <linux/notifier.h>
#include <linux/kthread.h>
#include <linux/printk.h>
#include <linux/interrupt.h>
#if LINUX_VERSION_CODE > KERNEL_VERSION(3, 8, 0)
#  include <linux/sched/rt.h>
#endif
#include <linux/cpu.h>
#include <asm/idle.h>
#include <linux/sched.h>

#define MAX_NCPUS 64
#define CACHE_LINE_SIZE 64

struct memguard_info{
	int master;
	ktime_t period_in_ktime;
	int budget;              /* reclaimed budget */
	long period_cnt;
	spinlock_t lock;
	int max_budget;          /* \sum(cinfo->budget) */
	cpumask_var_t active_mask;
	cpumask_var_t throttle_mask;
	struct hrtimer hr_timer;
};

struct core_info {
	/* user configurations */
	int budget;              /* assigned budget */
	int limit;
	/* for control logic */

	volatile struct task_struct * throttled_task;
	ktime_t throttled_time;  /* absolute time when throttled */

	u64 old_val;             /* hold previous counter value */
	int prev_throttle_error; /* check whether there was throttle error in 
				    the previous period */
	struct irq_work	pending; /* delayed work for NMIs */
	struct perf_event *event;/* performance counter i/f */

	struct task_struct *throttle_thread;  /* forced throttle idle thread */
	wait_queue_head_t throttle_evt; /* throttle wait queue */
	u64 throttled_error;
	/* statistics */
	long period_cnt;         /* active periods count */
};


static struct memguard_info memguard_info;
static struct core_info __percpu *core_info;

static int g_period_us=1000;
static int g_budget_pct[MAX_NCPUS];
static int g_budget_max_bw=1000;

static struct dentry *memguard_dir;

static void __reset_stats(void *info);
enum hrtimer_restart period_timer_callback_master(struct hrtimer *timer);
static void period_timer_callback_slave(void *info);
static void memguard_process_overflow(struct irq_work *entry);
static int throttle_thread(void *arg);

module_param(g_budget_max_bw, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
MODULE_PARM_DESC(g_budget_max_bw, "maximum memory bandwidth (MB/s)");

static inline u64 convert_mb_to_events(int mb)
{
	return div64_u64((u64)mb*1024*1024,
			 CACHE_LINE_SIZE* (1000000/g_period_us));
}

static inline int convert_events_to_mb(u64 events)
{
	int divisor = 1024*1024;
	int mb = div64_u64(events*CACHE_LINE_SIZE*1000 + (divisor-1), divisor);
	return mb;
}

static inline u64 perf_event_count(struct perf_event *event){
	return (local64_read(&event->count)+atomic64_read(&event->child_count));
}

static void print_core_info(int cpu, struct core_info *cinfo)
{
	pr_info("CPU%d: budget: %d, period: %ld\n", 
	       cpu, cinfo->budget, cinfo->period_cnt);
}
static inline u64 memguard_event_used(struct core_info *cinfo)
{
	trace_printk("perf_event_count(cinfo->event)=%llu\n",perf_event_count(cinfo->event));
	return perf_event_count(cinfo->event) - cinfo->old_val;
}

static void __start_throttle(void *info){
         struct core_info *cinfo = (struct core_info *)info;
         ktime_t start=ktime_get();
	
         trace_printk("throttle at %lld\n",start.tv64);

         cinfo->throttled_task=current;
  
         WARN_ON_ONCE(!strncmp(current->comm, "swapper", 7));
  
         smp_mb();
         wake_up_interruptible(&cinfo->throttle_evt);
}


static void event_overflow_callback(struct perf_event *event,
		struct perf_sample_data *data,struct pt_regs *regs){
	struct core_info *cinfo =this_cpu_ptr(core_info);
	BUG_ON(!cinfo);
	trace_printk("overflow callback\n");
	irq_work_queue(&cinfo->pending);
}

static void memguard_process_overflow(struct irq_work *entry){
	struct core_info *cinfo=this_cpu_ptr(core_info);
	struct memguard_info *global=&memguard_info;
	
	s64 budget_used;

	BUG_ON(in_nmi()||!in_irq());
	WARN_ON_ONCE(cinfo->budget > global->max_budget);
	trace_printk("overflow at %d\n",smp_processor_id());

	spin_lock(&global->lock);
	if(!cpumask_test_cpu(smp_processor_id(),global->active_mask)){
		spin_unlock(&global->lock);
		trace_printk("ERR:not active\n");
		return;
	}else if(global->period_cnt!=cinfo->period_cnt){
		trace_printk("ERR:global(%ld)!=local(%ld)period mismatch\n",global->period_cnt,cinfo->period_cnt);
		spin_unlock(&global->lock);
		return;
	}
	spin_unlock(&global->lock);

	budget_used = memguard_event_used(cinfo);

	if(budget_used < cinfo->budget){
		trace_printk("ERR:overflow in timer . used%lld < budget%d .ignore\n",budget_used,cinfo->budget);
		return;
	}

	local64_set(&cinfo->event->hw.period_left,0xfffffff);
	
	if(budget_used < cinfo->budget){
		trace_printk("ERR:throttling error\n");
		cinfo->prev_throttle_error=1;
	}
	
	if(cinfo->prev_throttle_error){
		trace_printk("throttle_error=%d\n",cinfo->prev_throttle_error);
		return;
	}

	cpumask_set_cpu(smp_processor_id(), global->throttle_mask);
	if(cpumask_test_cpu(global->master,global->throttle_mask)){
		cpumask_clear_cpu(global->master,global->throttle_mask);
	}
	smp_mb();
	on_each_cpu_mask(global->throttle_mask,__start_throttle,(void *)cinfo,0);

}

void update_statistics(struct core_info *cinfo){
	s64 new;
	int used;
	new=perf_event_count(cinfo->event);
	used=(int)(new-cinfo->old_val);
	trace_printk("count==%d,old_val==%d,used==%d\n",new,cinfo->old_val,used);
	cinfo->old_val=new;

}

static void period_timer_callback_slave(void *info){
	struct core_info *cinfo=this_cpu_ptr(core_info);
	struct memguard_info *global=&memguard_info;

	long new_period =(long)info;
	int cpu=smp_processor_id();
	
	trace_printk("slave at %d\n",cpu);
	BUG_ON(!irqs_disabled());
	WARN_ON_ONCE(!in_irq());

	if (new_period <= cinfo->period_cnt) {
		trace_printk("ERR: new_period(%ld) <= cinfo->period_cnt(%ld)\n",
			     new_period, cinfo->period_cnt);
		return;
	}
	cinfo->period_cnt=new_period;
	cinfo->event->pmu->stop(cinfo->event,PERF_EF_UPDATE);
	
	spin_lock(&global->lock);
	cpumask_clear_cpu(cpu, global->throttle_mask);
	cpumask_set_cpu(cpu,global->active_mask);
	spin_unlock(&global->lock);

	if(cinfo->throttled_task!=NULL){
	trace_printk("%p|New period %ld.\n",
		cinfo->throttled_task,cinfo->period_cnt);
	}
	update_statistics(cinfo);
	
	spin_lock(&global->lock);

	if(cinfo->limit>0){
		trace_printk("cinfo->limit==%d,cinfo->budget==%d",cinfo->limit,cinfo->budget);
		cinfo->budget=cinfo->limit;
	}

	if(cinfo->budget > global->max_budget){
		trace_printk("ERR:c->budget(%d) > g->max_budget(%d)\n",
				cinfo->budget,global->max_budget);
	}
	spin_unlock(&global->lock);

	if(cinfo->event->hw.sample_period != cinfo->budget){
		trace_printk("MSG: new budget %d is assigned\n",
				cinfo->budget);
		cinfo->event->hw.sample_period=cinfo->budget;
	}	

	cinfo->throttled_task=NULL;

	local64_set(&cinfo->event->hw.period_left,cinfo->budget);
	smp_mb();
	cinfo->event->pmu->start(cinfo->event,PERF_EF_RELOAD);
}


enum hrtimer_restart period_timer_callback_master(struct hrtimer *timer){
	struct memguard_info *global=&memguard_info;
	int orun;
	long new_period;
	ktime_t now;
	cpumask_var_t active_mask;
	zalloc_cpumask_var(&active_mask,GFP_NOWAIT);
	
	now=timer->base->get_time();
	
	trace_printk("master begin\n");
	
	BUG_ON(smp_processor_id()!=global->master);
	orun=hrtimer_forward(timer,now,global->period_in_ktime);
	trace_printk("orun==%d\n",orun);	
	if(orun==0)
		return HRTIMER_RESTART;
	
	spin_lock(&global->lock);	
	global->period_cnt += orun;

	new_period=global->period_cnt;
	
	smp_mb();
	cpumask_copy(active_mask,global->active_mask);
	smp_mb();
	
	spin_unlock(&global->lock);
	
	if (orun > 1){
		trace_printk("ERR: timer overrun %d at period %ld\n",orun, new_period);}

	on_each_cpu_mask(active_mask,period_timer_callback_slave,(void*)new_period,0);	
	
	smp_mb();
	trace_printk("master end\n");
	return HRTIMER_RESTART;

}

static void __update_budget(void *info){
	struct core_info *cinfo=this_cpu_ptr(core_info);
	cinfo->limit=(unsigned long)info;
	smp_mb();
	trace_printk("MSG: new budget of Core%d is %d \n",smp_processor_id(),cinfo->budget);
}

static ssize_t memguard_limit_write(struct file *filp,const char __user *ubuf,size_t cnt,loff_t *ppos)
{
	char buf[256];
	char *p =buf;
	int i;
	int max_budget=0;
	int use_mb=0;
	struct memguard_info *global =&memguard_info;

	if(copy_from_user(&buf,ubuf,(cnt>256)?256:cnt)!=0)
		return 0;
	if(!strncmp(p,"mb ",3)){
		use_mb=1;
		p+=3;
	}
	get_online_cpus();
	for_each_online_cpu(i){

		int input;
		unsigned long events;
		sscanf(p,"%d",&input);
		if(!use_mb)
			input=g_budget_max_bw*100/input;
		events=(unsigned long)convert_mb_to_events(input);
		max_budget+=events;
		pr_info("CPU%d:New budget=%ld (%d %s)\n",i,events,input,(use_mb)?"MB/s":"pct");
	
		smp_call_function_single(i,__update_budget,(void *)events,0);

		p=strchr(p,' ');
		if(!p)break;
		p++;
	}	
	global->max_budget=max_budget;
	g_budget_max_bw=convert_events_to_mb(max_budget);
	smp_mb();
	put_online_cpus();
	return cnt;
}

static int memguard_limit_show(struct seq_file *m,void *v){
	int i,cpu;
	struct memguard_info *global=&memguard_info;
	cpu=get_cpu();

	smp_mb();
	seq_printf(m,"cpu |budget (MB/s,pct)\n");
	seq_printf(m,"-----------------------------\n");

	for_each_online_cpu(i){
		struct core_info *cinfo=per_cpu_ptr(core_info,i);
		int budget=0,pct;
		if(cinfo->limit>0)
			budget=cinfo->limit;
		WARN_ON_ONCE(budget==0);

		pct=div64_u64((u64)budget * 100 +(global->max_budget-1),(global->max_budget)?global->max_budget:1);
		seq_printf(m,"CPU%d: %d (%dMB/s,%d pct)\n",i,budget,convert_events_to_mb(budget),pct);
	}
	seq_printf(m,"g_budget_max_bw: %d MB/s,(%d)\n",g_budget_max_bw,global->max_budget);
	put_cpu();
	return 0;
}

static int memguard_limit_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, memguard_limit_show, NULL);
}

static const struct file_operations memguard_limit_fops = {
	.open		= memguard_limit_open,
	.write          = memguard_limit_write,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int memguard_init_debugfs(void){
	memguard_dir=debugfs_create_dir("memguard",NULL);
	BUG_ON(!memguard_dir);
	debugfs_create_file("limit",0444,memguard_dir,NULL,&memguard_limit_fops);
	return 0;
}

static void __init_per_core(void *info){
	struct core_info *cinfo=this_cpu_ptr(core_info);
	memset(cinfo,0,sizeof(struct core_info));
	smp_rmb();

	cinfo->event=(struct perf_event *)info;

	cinfo->budget=cinfo->limit=cinfo->event->hw.sample_period;

	cinfo->throttled_task=NULL;
	init_waitqueue_head(&cinfo->throttle_evt);

	__reset_stats(cinfo);
	
	print_core_info(smp_processor_id(),cinfo);
	
	smp_wmb();
	init_irq_work(&cinfo->pending,memguard_process_overflow);
}

static struct perf_event *init_counter(int cpu,int budget){
	struct perf_event *event=NULL;
	struct perf_event_attr sched_perf_hw_attr={
		.type           = PERF_TYPE_HARDWARE,
		.config         = PERF_COUNT_HW_CACHE_MISSES,
		.size		= sizeof(struct perf_event_attr),
		.pinned		= 1,
		.disabled	= 1,
		.exclude_kernel = 1,
		.pinned 	= 1,
	};

	sched_perf_hw_attr.sample_period=budget;
	event=perf_event_create_kernel_counter(&sched_perf_hw_attr,cpu,
			NULL,event_overflow_callback,NULL);
	
	if(!event)
		return NULL;
	if (IS_ERR(event)) {
		/* vary the KERN level based on the returned errno */
		if (PTR_ERR(event) == -EOPNOTSUPP)
			pr_info("cpu%d. not supported\n", cpu);
		else if (PTR_ERR(event) == -ENOENT)
			pr_info("cpu%d. not h/w event\n", cpu);
		else
			pr_err("cpu%d. unable to create perf event: %ld\n",
			       cpu, PTR_ERR(event));
		return NULL;
	}
	pr_info("cpu%d enabled counter.\n", cpu);

	smp_wmb();

	return event;
}
static void __kill_throttlethread(void *info)
{
	struct core_info *cinfo = this_cpu_ptr(core_info);
	cinfo->throttled_task = NULL;
}

static void __disable_counter(void *info){
	struct core_info *cinfo=this_cpu_ptr(core_info);
	BUG_ON(!cinfo->event);
	cinfo->event->pmu->stop(cinfo->event,PERF_EF_UPDATE);
	cinfo->event->pmu->del(cinfo->event,0);
}

static void disable_counters(void){
	on_each_cpu(__disable_counter,NULL,0);
}

static void __start_counter(void* info)
{
	pr_info("__start_counter\n");
	struct core_info *cinfo = this_cpu_ptr(core_info);
	
	cinfo->event->pmu->add(cinfo->event, PERF_EF_START);
}

static void start_counters(void)
{
	pr_info("start\n");
	on_each_cpu(__start_counter,NULL,1);
}


static void __reset_stats(void *info){
	struct core_info *cinfo=this_cpu_ptr(core_info);
	trace_printk("CPU%d\n",smp_processor_id());

	cinfo->period_cnt=0;
	cinfo->old_val=perf_event_count(cinfo->event);
	cinfo->throttled_error=0;

	smp_mb();

	trace_printk("MSG: Clear statistics of Core%d\n",
			smp_processor_id());
}

static int throttle_thread(void *arg)
{
	int cpunr = (unsigned long)arg;
	struct core_info *cinfo = per_cpu_ptr(core_info, cpunr);

	static const struct sched_param param = {
		.sched_priority = MAX_USER_RT_PRIO/2,
	};

	sched_setscheduler(current, SCHED_FIFO, &param);

	while (!kthread_should_stop() && cpu_online(cpunr)) {

		trace_printk("wait an event\n");
		wait_event_interruptible(cinfo->throttle_evt,
					 cinfo->throttled_task ||
					 kthread_should_stop());

		trace_printk("got an event\n");

		if (kthread_should_stop())
			break;

		smp_mb();
		while (cinfo->throttled_task && !kthread_should_stop())
		{
			cpu_relax();
	
			smp_mb();
		}
	}

	trace_printk("exit\n");
	return 0;
}


/*
static int memguard_idle_notifier(struct notifier_block *nb,
		unsigned long val,void *data){
	struct memguard_info *global=&memguard_info;
	unsigned long flags;

	//trace_printk("idle state update:%ld\n",val);
	spin_lock_irqsave(&global->lock,flags);
	if(val==IDLE_START){
		cpumask_clear_cpu(smp_processor_id(),global->active_mask);
		//trace_printk("cpu%d is idle\n",smp_processor_id());
	}else{
		cpumask_set_cpu(smp_processor_id(),global->active_mask);
		//trace_printk("cpu%d is not idle\n",smp_processor_id());
	}
	spin_unlock_irqrestore(&global->lock,flags);
	return 0;
}

static struct notifier_block memguard_idle_nb={
	.notifier_call	=memguard_idle_notifier,
};
*/
int init_module(void){
	int i;
	struct memguard_info *global=&memguard_info;
	memset(global,0,sizeof(struct memguard_info));
	
	zalloc_cpumask_var(&global->active_mask,GFP_NOWAIT);
	zalloc_cpumask_var(&global->throttle_mask,GFP_NOWAIT);
	
	spin_lock_init(&global->lock);
	global->period_in_ktime=ktime_set(0,g_period_us*1000);	
	global->max_budget = convert_mb_to_events(g_budget_max_bw);

	cpumask_copy(global->active_mask,cpu_online_mask);	

	core_info=alloc_percpu(struct core_info);
	smp_mb();

	get_online_cpus();
	for_each_online_cpu(i){
		struct perf_event *event;
		struct core_info *cinfo=per_cpu_ptr(core_info,i);
		int budget,mb;
		if(g_budget_pct[i]==0)
			g_budget_pct[i]=100/num_online_cpus();
		mb=div64_u64((u64)g_budget_max_bw * g_budget_pct[i],100);
		
		budget=convert_mb_to_events(mb);

		pr_info("budget[%d]=%d(%d pct,%d MB/s)\n",i,budget,g_budget_pct[i],mb);

		/* create performance counter */
		event=init_counter(i,budget);
		if(event)
			pr_info("event-----\n");
		if(!event)
			break;
		/* initialize per-core data structure */
		smp_call_function_single(i,__init_per_core,(void*)event,1);
		
		smp_mb();
		
		cinfo->throttle_thread=
			kthread_create_on_node(throttle_thread,(void*)((unsigned)i),cpu_to_node(i),"kthrottle/%d",i);
		BUG_ON(IS_ERR(cinfo->throttle_thread));
		kthread_bind(cinfo->throttle_thread,i);
		wake_up_process(cinfo->throttle_thread);
		
	}
	put_online_cpus();
	smp_mb();	
	memguard_init_debugfs();
	smp_mb();
	pr_info("S\n");
	start_counters();
	smp_mb();

	pr_info("Start period timer (period=%lld us)\n",div64_u64(global->period_in_ktime.tv64, 1000));

	get_cpu();
	smp_mb();
	global->master=smp_processor_id();
	pr_info("master=%d\n",global->master);
	hrtimer_init(&global->hr_timer,CLOCK_MONOTONIC,HRTIMER_MODE_REL_PINNED);
	global->hr_timer.function=&period_timer_callback_master;
	hrtimer_start(&global->hr_timer,global->period_in_ktime,HRTIMER_MODE_REL_PINNED);
	smp_mb();
	put_cpu();
	
//	idle_notifier_register(&memguard_idle_nb);
	return 0;
}

void cleanup_module(void){
	int i;
	struct memguard_info *global=&memguard_info;

//	idle_notifier_unregister(&memguard_idle_nb);

	get_online_cpus();
	smp_mb();
	pr_info("kill throttle threads\n");
	on_each_cpu(__kill_throttlethread,NULL,1);

	pr_info("cancel timer\n");
	hrtimer_cancel(&global->hr_timer);
	
	debugfs_remove_recursive(memguard_dir);

	disable_counters();

	for_each_online_cpu(i){
		struct core_info *cinfo=per_cpu_ptr(core_info,i);
		smp_mb();	
		pr_info("stopping kthrottle/%d\n",i);
		cinfo->throttled_task=NULL;
		kthread_stop(cinfo->throttle_thread);
		perf_event_release_kernel(cinfo->event);
	}

	smp_mb();
	
//	unregister_hotcpu_notifier(&memguard_cpu_notifier);
	free_percpu(core_info);

	smp_mb();
	put_online_cpus();
	pr_info("uninstall\n");
	return;
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("wsm");
