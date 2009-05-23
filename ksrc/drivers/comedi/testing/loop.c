#include <linux/module.h>
#include <comedi/comedi_driver.h>

#define LOOP_TASK_PERIOD 1000000
#define LOOP_NB_BITS 16

/* Channels descriptor */
static comedi_chdesc_t loop_chandesc = {
	.mode = COMEDI_CHAN_GLOBAL_CHANDESC,
	.length = 8,
	.chans = { 
		{COMEDI_CHAN_AREF_GROUND, LOOP_NB_BITS},
	},
};

/* Ranges tab */
static comedi_rngtab_t loop_rngtab = {
	.length =  2,
	.rngs = {
		RANGE_V(-5,5),
		RANGE_V(-10,10),
	},
};
/* Ranges descriptor */
comedi_rngdesc_t loop_rngdesc = RNG_GLOBAL(loop_rngtab);

/* Command options mask */
static comedi_cmd_t loop_cmd_mask = {
	.idx_subd = 0,
	.start_src = TRIG_NOW,
	.scan_begin_src = TRIG_TIMER,
	.convert_src = TRIG_NOW|TRIG_TIMER,
	.scan_end_src = TRIG_COUNT,
	.stop_src = TRIG_COUNT|TRIG_NONE,
};

/* Private data organization */
struct loop_priv {

	/* Task descriptor */
	comedi_task_t loop_task;

	/* Misc fields */
	volatile int loop_running:1;
	sampl_t loop_insn_value;
};
typedef struct loop_priv lpprv_t;

/* Attach arguments contents */
struct loop_attach_arg {
	unsigned long period;
};
typedef struct loop_attach_arg lpattr_t;

static void loop_task_proc(void *arg);

/* --- Task part --- */

/* Timer task routine  */
static void loop_task_proc(void *arg)
{
	comedi_dev_t *dev = (comedi_dev_t*)arg;
	lpprv_t *priv = (lpprv_t *)dev->priv;
    
	while (!comedi_check_dev(dev))
		comedi_task_sleep(LOOP_TASK_PERIOD);

	while (1) {
	
		if (priv->loop_running) {
			sampl_t value;
			int ret=0;
	    
			while (ret==0) {
		
				ret = comedi_buf_get(dev, 
						     &value, sizeof(sampl_t));

				if (ret == 0) {

					comedi_info(dev, 
						    "loop_task_proc: "
						    "data available\n");

					comedi_buf_evt(dev, COMEDI_BUF_GET, 0);
		    
					ret=comedi_buf_put(dev, 
							   &value, 
							   sizeof(sampl_t));

					if (ret==0)
						comedi_buf_evt(dev, 
							       COMEDI_BUF_PUT, 
							       0);
				}
			}
		}

		comedi_task_sleep(LOOP_TASK_PERIOD);
	}
}

/* --- Comedi Callbacks --- */

/* Command callback */
int loop_cmd(comedi_subd_t *subd, comedi_cmd_t *cmd)
{
	lpprv_t *priv = (lpprv_t *)subd->dev->priv;

	comedi_info(dev, "loop_cmd: (subd=%d)\n",idx_subd);

	priv->loop_running = 1;
  
	return 0;
  
}

/* Cancel callback */
int loop_cancel(comedi_subd_t *subd, int idx_subd)
{
	lpprv_t *priv=(lpprv_t *)subd->dev->priv;

	comedi_info(dev, "loop_cancel: (subd=%d)\n",idx_subd);

	priv->loop_running=0;

	return 0;
}

/* Read instruction callback */
int loop_insn_read(comedi_subd_t *subd, comedi_kinsn_t *insn)
{
	lpprv_t *priv = (lpprv_t*)subd->dev->priv;

	/* Checks the buffer size */
	if (insn->data_size!=sizeof(sampl_t))
		return -EINVAL;

	/* Sets the memorized value */
	insn->data[0]=priv->loop_insn_value;
    
	return 0;
}

/* Write instruction callback */
int loop_insn_write(comedi_subd_t *subd, comedi_kinsn_t *insn)
{
	lpprv_t *priv = (lpprv_t*)subd->dev->priv;

	/* Checks the buffer size */
	if (insn->data_size!=sizeof(sampl_t))
		return -EINVAL;

	/* Retrieves the value to memorize */
	priv->loop_insn_value=insn->data[0];
    
	return 0;
}

void setup_input_subd(comedi_subd_t *subd)
{
	memset(subd, 0, sizeof(comedi_subd_t));

	subd->flags |= COMEDI_SUBD_AI;
	subd->flags |= COMEDI_SUBD_CMD;
	subd->flags |= COMEDI_SUBD_MMAP;
	subd->rng_desc = &loop_rngdesc;
	subd->chan_desc = &loop_chandesc;
	subd->do_cmd = loop_cmd;
	subd->do_cmdtest = NULL;
	subd->cancel = loop_cancel;
	subd->cmd_mask = &loop_cmd_mask;
	subd->insn_read = loop_insn_read;
	subd->insn_write = loop_insn_write;
}

void setup_output_subd(comedi_subd_t *subd)
{
	memset(subd, 0, sizeof(comedi_subd_t));

	subd->flags = COMEDI_SUBD_AO;
	subd->flags |= COMEDI_SUBD_CMD;
	subd->flags |= COMEDI_SUBD_MMAP;
	subd->insn_read = loop_insn_read;
	subd->insn_write = loop_insn_write;
}

/* Attach callback */
int loop_attach(comedi_dev_t *dev,
		comedi_lnkdesc_t *arg)
{
	int ret = 0;
	comedi_subd_t *subd;
	lpprv_t *priv = (lpprv_t *)dev->priv;

	/* Add the fake input subdevice */
	subd = comedi_alloc_subd(0, setup_input_subd); 
	if (subd == NULL)
		return -ENOMEM;  

	ret = comedi_add_subd(dev, subd);
	if (ret < 0)
		/* Let Comedi free the lately allocated subdevice */
		return ret;

	/* Add the fake output subdevice */
	subd = comedi_alloc_subd(0, setup_output_subd); 
	if (subd == NULL)
		/* Let Comedi free the lately allocated subdevice */
		return -ENOMEM;  

	ret = comedi_add_subd(dev, subd);
	if (ret < 0)
		/* Let Comedi free the lately allocated subdevices */
		return ret;

	priv->loop_running = 0;
	priv->loop_insn_value = 0;

	ret = comedi_task_init(&priv->loop_task, 
			       "comedi_loop task", 
			       loop_task_proc,
			       dev, COMEDI_TASK_HIGHEST_PRIORITY);

	return ret;
}

/* Detach callback */
int loop_detach(comedi_dev_t *dev)
{
	lpprv_t *priv = (lpprv_t *)dev->priv;

	comedi_task_destroy(&priv->loop_task);

	return 0;
}

/* --- Module part --- */

static comedi_drv_t loop_drv = {
	.owner = THIS_MODULE,
	.board_name = "comedi_loop",
	.attach = loop_attach,
	.detach = loop_detach,
	.privdata_size = sizeof(lpprv_t),
};

static int __init comedi_loop_init(void)
{
	return comedi_register_drv(&loop_drv);
}

static void __exit comedi_loop_cleanup(void)
{
	comedi_unregister_drv(&loop_drv);
}

MODULE_DESCRIPTION("Comedi loop driver");
MODULE_LICENSE("GPL");

module_init(comedi_loop_init);
module_exit(comedi_loop_cleanup);
