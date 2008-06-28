#include <linux/module.h>
#include <comedi/comedi_driver.h>

#define LOOP_TASK_PERIOD 1000000
#define LOOP_NB_BITS 16

static comedi_chdesc_t loop_chandesc = {
  mode: COMEDI_CHAN_GLOBAL_CHANDESC,
  length: 8,
  chans: { 
      {COMEDI_CHAN_AREF_GROUND, LOOP_NB_BITS},
  },
};

static comedi_rngtab_t loop_rngtab = {
    length: 2,
    rngs: {
	RANGE_V(-5,5),
	RANGE_V(-10,10),
    },
};
comedi_rngdesc_t loop_rngdesc = RNG_GLOBAL(loop_rngtab);

static comedi_cmd_t loop_cmd_mask = {
  idx_subd:0,

  start_src:TRIG_NOW,
  scan_begin_src:TRIG_TIMER,
  convert_src:TRIG_NOW|TRIG_TIMER,
  scan_end_src:TRIG_COUNT,
  stop_src:TRIG_COUNT|TRIG_NONE,
};

static comedi_drv_t loop_drv;

struct loop_priv {

    /* Task descriptor */
    comedi_task_t loop_task;

    /* Misc fields */
    volatile int loop_running:1;
    sampl_t loop_insn_value;
};
typedef struct loop_priv lpprv_t;

struct loop_attach_arg {
  unsigned long period;
};
typedef struct loop_attach_arg lpattr_t;

static void loop_task_proc(void *arg);

int loop_attach(comedi_cxt_t *cxt,
		comedi_lnkdesc_t *arg)
{
  int ret=0;  
  comedi_dev_t *dev=comedi_get_dev(cxt);
  lpprv_t *priv=(lpprv_t *)dev->priv;

  priv->loop_running = 0;
  priv->loop_insn_value = 0;

  ret=comedi_task_init(&priv->loop_task, 
		       "comedi_loop task", 
		       loop_task_proc,
		       dev, COMEDI_TASK_HIGHEST_PRIORITY);

  return ret;
}

int loop_detach(comedi_cxt_t *cxt)
{
  comedi_dev_t *dev=comedi_get_dev(cxt);
  lpprv_t *priv=(lpprv_t *)dev->priv;

  comedi_task_destroy(&priv->loop_task);

  return 0;
}

int loop_cmd(comedi_cxt_t *cxt, int idx_subd)
{
  comedi_dev_t *dev=comedi_get_dev(cxt);
  lpprv_t *priv=(lpprv_t *)dev->priv;

  comedi_loginfo("loop_cmd: (subd=%d)\n",idx_subd);

  priv->loop_running = 1;
  
  return 0;
  
}

int loop_cancel(comedi_cxt_t *cxt, int idx_subd)
{
  comedi_dev_t *dev=comedi_get_dev(cxt);
  lpprv_t *priv=(lpprv_t *)dev->priv;

  comedi_loginfo("loop_cancel: (subd=%d)\n",idx_subd);

  priv->loop_running=0;

  return 0;
}

int loop_insn_read(comedi_cxt_t *cxt, comedi_kinsn_t *insn)
{
    comedi_dev_t *dev = comedi_get_dev(cxt);
    lpprv_t *priv = (lpprv_t*)dev->priv;

    /* Checks the buffer size */
    if(insn->data_size!=sizeof(sampl_t))
	return -EINVAL;

    /* Sets the memorized value */
    insn->data[0]=priv->loop_insn_value;
    
    return 0;
}

int loop_insn_write(comedi_cxt_t *cxt, comedi_kinsn_t *insn)
{
    comedi_dev_t *dev = comedi_get_dev(cxt);
    lpprv_t *priv = (lpprv_t*)dev->priv;

    /* Checks the buffer size */
    if(insn->data_size!=sizeof(sampl_t))
	return -EINVAL;

    /* Retrieves the value to memorize */
    priv->loop_insn_value=insn->data[0];
    
    return 0;
}


int loop_init_drv(void)
{
  int ret=0;
  comedi_subd_t subd;
  
  memset(&subd, 0, sizeof(comedi_subd_t));
  subd.flags |= COMEDI_SUBD_AI;
  subd.flags |= COMEDI_SUBD_CMD;
  subd.flags |= COMEDI_SUBD_MMAP;
  subd.rng_desc = &loop_rngdesc;
  subd.chan_desc = &loop_chandesc;
  subd.do_cmd = loop_cmd;
  subd.do_cmdtest = NULL;
  subd.cancel = loop_cancel;
  subd.cmd_mask = &loop_cmd_mask;
  subd.insn_read = loop_insn_read;
  subd.insn_write = loop_insn_write;
  if((ret = comedi_add_subd(&loop_drv,&subd))<0)
    return ret;
    
  subd.flags=COMEDI_SUBD_AO;
  subd.flags|=COMEDI_SUBD_CMD;
  subd.flags|=COMEDI_SUBD_MMAP;
  subd.insn_read=loop_insn_read;
  subd.insn_write=loop_insn_write;
  ret=comedi_add_subd(&loop_drv,&subd);

  return (ret < 0) ? ret : 0;
}

static int __init comedi_loop_init(void)
{
  int ret = comedi_init_drv(&loop_drv);

  if(ret!=0)
    return ret;

  loop_drv.owner=THIS_MODULE;
  loop_drv.board_name="comedi_loop";
  loop_drv.attach=loop_attach;
  loop_drv.detach=loop_detach;
  loop_drv.privdata_size=sizeof(lpprv_t);

  if((ret=loop_init_drv())!=0)
    return ret;

  return comedi_add_drv(&loop_drv);
}

static void __exit comedi_loop_cleanup(void)
{
  comedi_rm_drv(&loop_drv);
  comedi_cleanup_drv(&loop_drv);
}

MODULE_DESCRIPTION("Comedi loop driver");
MODULE_LICENSE("GPL");

module_init(comedi_loop_init);
module_exit(comedi_loop_cleanup);

static void loop_task_proc(void *arg)
{
    comedi_dev_t *dev = (comedi_dev_t*)arg;
    lpprv_t *priv = (lpprv_t *)dev->priv;
    
    while(!comedi_check_dev(dev))
	comedi_task_sleep(LOOP_TASK_PERIOD);

    while(1) {
	
	if(priv->loop_running) {
	    sampl_t value;
	    int ret=0;
	    
	    while(ret==0) {
		
		ret = comedi_buf_get(dev, &value, sizeof(sampl_t));

		if(ret == 0) {

		    comedi_loginfo("loop_task_proc: data available\n");

		    comedi_buf_evt(dev, COMEDI_BUF_GET, 0);
		    
		    ret=comedi_buf_put(dev, &value, sizeof(sampl_t));

		    if(ret==0)
			comedi_buf_evt(dev, COMEDI_BUF_PUT, 0);
		}
	    }
	}

	comedi_task_sleep(LOOP_TASK_PERIOD);
    }
}
