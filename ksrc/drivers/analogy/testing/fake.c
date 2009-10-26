#include <linux/module.h>
#include <analogy/analogy_driver.h>

#define TEST_TASK_PERIOD 1000000
#define TEST_NB_BITS 16

#define TEST_INPUT_SUBD 0

/* --- Driver related structures --- */

/* Device private structure */
struct test_priv {

	/* Task descriptor */
	a4l_task_t timer_task;
  
	/* Specific timing fields */
	unsigned long scan_period_ns;
	unsigned long convert_period_ns;
	unsigned long current_ns;
	unsigned long reminder_ns;
	unsigned long long last_ns;

	/* Misc fields */
	unsigned long amplitude_div;
	unsigned long quanta_cnt;
	volatile int timer_running;

};
typedef struct test_priv tstprv_t;

/* Attach options structure */
struct test_attach_arg {
	unsigned long amplitude_div;
	unsigned long quanta_cnt;
};
typedef struct test_attach_arg tstattr_t;

/* --- Channels / ranges part --- */

/* Channels descriptor */
static a4l_chdesc_t test_chandesc = {
	.mode = A4L_CHAN_GLOBAL_CHANDESC,
	.length = 8,
	.chans = { 
		{A4L_CHAN_AREF_GROUND, TEST_NB_BITS},
	},
};

/* Ranges tab */
static a4l_rngtab_t test_rngtab = {
	.length = 2,
	.rngs = {
		RANGE_V(-5,5),
		RANGE_V(-10,10),
	},
};
/* Ranges descriptor */
a4l_rngdesc_t test_rngdesc = RNG_GLOBAL(test_rngtab);

/* Command options mask */
static a4l_cmd_t test_cmd_mask = {
	.idx_subd = 0,
	.start_src = TRIG_NOW,
	.scan_begin_src = TRIG_TIMER,
	.convert_src = TRIG_NOW|TRIG_TIMER,
	.scan_end_src = TRIG_COUNT,
	.stop_src = TRIG_COUNT|TRIG_NONE,
};

/* --- Analog input simulation --- */

static sampl_t output_tab[8] = { 
	0x0001, 0x2000, 0x4000, 0x6000, 
	0x8000, 0xa000, 0xc000, 0xffff 
};
static unsigned int output_idx;
static a4l_lock_t output_lock = A4L_LOCK_UNLOCKED;

static sampl_t test_output(tstprv_t *priv)
{
	unsigned long flags;
	unsigned int idx;
    
	a4l_lock_irqsave(&output_lock, flags);

	output_idx += priv->quanta_cnt;
	if(output_idx == 8)
		output_idx = 0; 
	idx = output_idx;

	a4l_unlock_irqrestore(&output_lock, flags);
    
	return output_tab[idx] / priv->amplitude_div;
}

/* --- Task part --- */

/* Timer task routine */
static void test_task_proc(void *arg)
{
	a4l_dev_t *dev = (a4l_dev_t*)arg;
	a4l_subd_t *subd = a4l_get_subd(dev, TEST_INPUT_SUBD);
	tstprv_t *priv = (tstprv_t *)dev->priv;
	a4l_cmd_t *cmd = NULL;
	u64 now_ns, elapsed_ns=0;

	while(!a4l_check_dev(dev))
		a4l_task_sleep(TEST_TASK_PERIOD);

	while(1) {
		if(priv->timer_running != 0)
		{
			int i = 0;

			cmd = a4l_get_cmd(subd);    

			now_ns = a4l_get_time();
			elapsed_ns += now_ns - priv->last_ns + priv->reminder_ns;
			priv->last_ns = now_ns;

			while(elapsed_ns >= priv->scan_period_ns)
			{
				int j;

				for(j = 0; j < cmd->nb_chan; j++)
				{
					sampl_t value = test_output(priv);

					a4l_buf_put(subd, &value, sizeof(sampl_t));

				}

				elapsed_ns -= priv->scan_period_ns;
				i++;

			}

			priv->current_ns += i * priv->scan_period_ns;
			priv->reminder_ns = elapsed_ns;

			a4l_buf_evt(subd, 0);
		}

		a4l_task_sleep(TEST_TASK_PERIOD);

	}
}

/* --- Analogy Callbacks --- */

/* Command callback */
int test_cmd(a4l_subd_t *subd, a4l_cmd_t *cmd)
{
	a4l_dev_t *dev = subd->dev;
	tstprv_t *priv = (tstprv_t *)dev->priv;

	a4l_info(dev, "test_cmd: begin (subd=%d)\n", subd->idx);
  
	priv->scan_period_ns=cmd->scan_begin_arg;
	priv->convert_period_ns=(cmd->convert_src==TRIG_TIMER)?
		cmd->convert_arg:0;
  
	a4l_info(dev, 
		 "test_cmd: scan_period=%luns convert_period=%luns\n",
		 priv->scan_period_ns, priv->convert_period_ns);

	priv->last_ns = a4l_get_time();

	priv->current_ns = ((unsigned long)priv->last_ns);
	priv->reminder_ns = 0;
  
	priv->timer_running = 1;
  
	return 0;
  
}

/* Test command callback */
int test_cmdtest(a4l_subd_t *subd, a4l_cmd_t *cmd)
{
	if(cmd->scan_begin_src == TRIG_TIMER)
	{
		if (cmd->scan_begin_arg < 1000)
			return -EINVAL;

		if (cmd->convert_src == TRIG_TIMER &&
		    cmd->scan_begin_arg < (cmd->convert_arg * cmd->nb_chan))
			return -EINVAL;
	}

	return 0;
}

/* Cancel callback */
int test_cancel(a4l_subd_t *subd)
{
	tstprv_t *priv = (tstprv_t *)subd->dev->priv;

	priv->timer_running = 0;

	return 0;
}

/* Read instruction callback */
int test_ai_insn_read(a4l_subd_t *subd, a4l_kinsn_t *insn)
{
	tstprv_t *priv = (tstprv_t *)subd->dev->priv;
	int i;

	for(i = 0; i < insn->data_size / sizeof(sampl_t); i++)
		((sampl_t*)insn->data)[i] = test_output(priv);

	return 0;
}

/* Munge callback */
void test_ai_munge(a4l_subd_t *subd, void *buf, unsigned long size)
{
	int i;

	for(i = 0; i < size / sizeof(sampl_t); i++)
		((sampl_t*)buf)[i] += 1;
}

void setup_test_subd(a4l_subd_t *subd)
{
	/* Initialize the subdevice structure */
	memset(subd, 0, sizeof(a4l_subd_t));
	
	/* Fill the subdevice structure */
	subd->flags |= A4L_SUBD_AI;
	subd->flags |= A4L_SUBD_CMD;
	subd->flags |= A4L_SUBD_MMAP;
	subd->rng_desc = &test_rngdesc;
	subd->chan_desc = &test_chandesc;
	subd->do_cmd = test_cmd;
	subd->do_cmdtest = test_cmdtest;
	subd->cancel = test_cancel;
	subd->munge = test_ai_munge;
	subd->cmd_mask = &test_cmd_mask;
	subd->insn_read = test_ai_insn_read;
}

/* Attach callback */
int test_attach(a4l_dev_t *dev, a4l_lnkdesc_t *arg)
{
	int ret = 0;  
	a4l_subd_t *subd;
	tstprv_t *priv = (tstprv_t *)dev->priv;

	if(arg->opts!=NULL) {
		tstattr_t *attr = (tstattr_t*) arg->opts;

		priv->amplitude_div = attr->amplitude_div;
		priv->quanta_cnt = 
			(attr->quanta_cnt > 7 || attr->quanta_cnt == 0) ? 
			1 : attr->quanta_cnt;
	}
	else {
		priv->amplitude_div = 1;
		priv->quanta_cnt = 1;
	}

	/* Adds the subdevice to the device */
	subd = a4l_alloc_subd(0, setup_test_subd);
	if(subd == NULL)
		return -ENOMEM;

	ret = a4l_add_subd(dev, subd);
	if(ret != TEST_INPUT_SUBD)
		return (ret < 0) ? ret : -EINVAL;

	priv->timer_running = 0;

	ret = a4l_task_init(&priv->timer_task, 
			    "a4l_test task", 
			    test_task_proc, 
			    dev, A4L_TASK_HIGHEST_PRIORITY);

	return ret;
}

/* Detach callback */
int test_detach(a4l_dev_t *dev)
{
	tstprv_t *priv = (tstprv_t *)dev->priv;

	a4l_task_destroy(&priv->timer_task);

	return 0;
}

/* --- Module part --- */

static a4l_drv_t test_drv = {
	.owner = THIS_MODULE,
	.board_name = "a4l_fake",
	.attach = test_attach,
	.detach = test_detach,
	.privdata_size = sizeof(tstprv_t),
};

static int __init a4l_fake_init(void)
{
	return a4l_register_drv(&test_drv);
}

static void __exit a4l_fake_cleanup(void)
{
	a4l_unregister_drv(&test_drv);
}

MODULE_DESCRIPTION("Analogy fake driver");
MODULE_LICENSE("GPL");

module_init(a4l_fake_init);
module_exit(a4l_fake_cleanup);
