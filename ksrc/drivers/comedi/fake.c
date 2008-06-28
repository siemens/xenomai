#include <linux/module.h>
#include <comedi/comedi_driver.h>

#define TEST_TASK_PERIOD 1000000
#define TEST_NB_BITS 16

/* --- Driver related structures --- */

/* Device private structure */
struct test_priv {

    /* Task descriptor */
    comedi_task_t timer_task;
  
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
static comedi_chdesc_t test_chandesc = {
  mode: COMEDI_CHAN_GLOBAL_CHANDESC,
  length: 8,
  chans: { 
      {COMEDI_CHAN_AREF_GROUND, TEST_NB_BITS},
  },
};

/* Ranges tab */
static comedi_rngtab_t test_rngtab = {
    length: 2,
    rngs: {
	RANGE_V(-5,5),
	RANGE_V(-10,10),
    },
};
/* Ranges descriptor */
comedi_rngdesc_t test_rngdesc = RNG_GLOBAL(test_rngtab);

/* Command options mask */
static comedi_cmd_t test_cmd_mask = {
  idx_subd:0,
  start_src:TRIG_NOW,
  scan_begin_src:TRIG_TIMER,
  convert_src:TRIG_NOW|TRIG_TIMER,
  scan_end_src:TRIG_COUNT,
  stop_src:TRIG_COUNT|TRIG_NONE,
};

/* --- Analog input simulation --- */

static sampl_t output_tab[8] = { 
    0x0001, 0x2000, 0x4000, 0x6000, 
    0x8000, 0xa000, 0xc000, 0xffff 
};
static unsigned int output_idx;
static comedi_lock_t output_lock = COMEDI_LOCK_UNLOCKED;

static sampl_t test_output(tstprv_t *priv)
{
    unsigned long flags;
    unsigned int idx;
    
    comedi_lock_irqsave(&output_lock, flags);

    output_idx += priv->quanta_cnt;
    if(output_idx == 8)
	output_idx = 0; 
    idx = output_idx;

    comedi_unlock_irqrestore(&output_lock, flags);
    
    return output_tab[idx] / priv->amplitude_div;
}

/* --- Task part --- */

/* Timer task routine */
static void test_task_proc(void *arg)
{
  comedi_dev_t *dev = (comedi_dev_t*)arg;
  tstprv_t *priv = (tstprv_t *)dev->priv;
  comedi_cmd_t *cmd = NULL;
  u64 now_ns, elapsed_ns=0;

  while(!comedi_check_dev(dev))
    comedi_task_sleep(TEST_TASK_PERIOD);

  while(1) {
    if(priv->timer_running != 0)
    {
      int i = 0;

      cmd = comedi_get_cmd(dev, COMEDI_BUF_PUT, 0);    

      now_ns = comedi_get_time();
      elapsed_ns += now_ns - priv->last_ns + priv->reminder_ns;
      priv->last_ns = now_ns;

      while(elapsed_ns >= priv->scan_period_ns)
      {
	int j;

	for(j = 0; j < cmd->nb_chan; j++)
	{
	  sampl_t value = test_output(priv);

	  comedi_buf_put(dev, &value, sizeof(sampl_t));

	}

	elapsed_ns -= priv->scan_period_ns;
	i++;

      }

      priv->current_ns += i * priv->scan_period_ns;
      priv->reminder_ns = elapsed_ns;

      comedi_buf_evt(dev, COMEDI_BUF_PUT, 0);
    }

    comedi_task_sleep(TEST_TASK_PERIOD);

  }
}

/* --- Comedi Callbacks --- */

/* Attach callback */
int test_attach(comedi_cxt_t *cxt,
		comedi_lnkdesc_t *arg)
{
    int ret=0;  
    comedi_dev_t *dev = comedi_get_dev(cxt);
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


    priv->timer_running = 0;

    ret = comedi_task_init(&priv->timer_task, 
			   "comedi_test task", 
			   test_task_proc, 
			   dev, COMEDI_TASK_HIGHEST_PRIORITY);

    return ret;
}

/* Detach callback */
int test_detach(comedi_cxt_t *cxt)
{
    comedi_dev_t *dev = comedi_get_dev(cxt);
    tstprv_t *priv = (tstprv_t *)dev->priv;

    comedi_task_destroy(&priv->timer_task);

    return 0;
}

/* Command callback */
int test_cmd(comedi_cxt_t *cxt, int idx_subd)
{
  comedi_dev_t *dev=comedi_get_dev(cxt);
  comedi_cmd_t *cmd=comedi_get_cmd(dev, 0, idx_subd);
  tstprv_t *priv=(tstprv_t *)dev->priv;

  comedi_loginfo("test_cmd: begin (subd=%d)\n",idx_subd);
  
  priv->scan_period_ns=cmd->scan_begin_arg;
  priv->convert_period_ns=(cmd->convert_src==TRIG_TIMER)?
    cmd->convert_arg:0;
  
  comedi_loginfo("test_cmd: scan_period=%luns convert_period=%luns\n",
	      priv->scan_period_ns, priv->convert_period_ns);

  priv->last_ns = comedi_get_time();

  priv->current_ns = ((unsigned long)priv->last_ns);
  priv->reminder_ns = 0;
  
  priv->timer_running = 1;
  
  return 0;
  
}

/* Test command callback */
int test_cmdtest(comedi_cxt_t *cxt, comedi_cmd_t *cmd)
{
  if(cmd->scan_begin_src==TRIG_TIMER)
  {
    if(cmd->scan_begin_arg < 1000)
      return -EINVAL;

    if(cmd->convert_src==TRIG_TIMER &&
       cmd->scan_begin_arg<(cmd->convert_arg*cmd->nb_chan))
      return -EINVAL;
  }

  return 0;
}

/* Cancel callback */
int test_cancel(comedi_cxt_t *cxt, int idx_subd)
{
  comedi_dev_t *dev = comedi_get_dev(cxt);
  tstprv_t *priv = (tstprv_t *)dev->priv;

  priv->timer_running = 0;

  return 0;
}

/* Read instruction callback */
int test_ai_insn_read(comedi_cxt_t *cxt, comedi_kinsn_t *insn)
{
    comedi_dev_t *dev = comedi_get_dev(cxt);
    tstprv_t *priv = (tstprv_t *)dev->priv;
    int i;

    for(i = 0; i < insn->data_size / sizeof(sampl_t); i++)
	((sampl_t*)insn->data)[i] = test_output(priv);

    return 0;
}

/* Munge callback */
void test_ai_munge(comedi_cxt_t *cxt, 
		  int idx_subd, void *buf, unsigned long size)
{
    int i;

    for(i = 0; i < size / sizeof(sampl_t); i++)
	((sampl_t*)buf)[i] += 1;
}

/* --- Module part --- */

static comedi_drv_t test_drv;

static int __init comedi_fake_init(void)
{
    int ret;
    comedi_subd_t subd;

    /* Initializes the driver structure */
    ret = comedi_init_drv(&test_drv);
    if(ret!=0)
	return ret;

    /* Fills the driver structure main fields */
    test_drv.owner=THIS_MODULE;
    test_drv.board_name="comedi_fake";
    test_drv.attach=test_attach;
    test_drv.detach=test_detach;
    test_drv.privdata_size=sizeof(tstprv_t);

    /* Initializes the subdevice structure */
    memset(&subd, 0, sizeof(comedi_subd_t));

    /* Fills the subdevice structure */
    subd.flags |= COMEDI_SUBD_AI;
    subd.flags |= COMEDI_SUBD_CMD;
    subd.flags |= COMEDI_SUBD_MMAP;
    subd.rng_desc = &test_rngdesc;
    subd.chan_desc = &test_chandesc;
    subd.do_cmd=test_cmd;
    subd.do_cmdtest = test_cmdtest;
    subd.cancel = test_cancel;
    subd.munge = test_ai_munge;
    subd.cmd_mask = &test_cmd_mask;
    subd.insn_read = test_ai_insn_read;

    /* Adds the subdevice to the driver */
    ret = comedi_add_subd(&test_drv,&subd);
    if(ret < 0)
	return ret;

    return comedi_add_drv(&test_drv);
}

static void __exit comedi_fake_cleanup(void)
{
    comedi_rm_drv(&test_drv);
    comedi_cleanup_drv(&test_drv);
}

MODULE_DESCRIPTION("Comedi fake driver");
MODULE_LICENSE("GPL");

module_init(comedi_fake_init);
module_exit(comedi_fake_cleanup);
