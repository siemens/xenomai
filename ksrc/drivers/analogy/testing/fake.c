#include <linux/module.h>
#include <analogy/analogy_driver.h>

#define AI_TASK_PERIOD 1000000

#define AI_SUBD 0
#define DIO_SUBD 1

/* --- Driver related structures --- */

struct fake_priv {
	/* Configuration parameters */
	unsigned long amplitude_div;
	unsigned long quanta_cnt;
};

struct ai_priv {

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
	int timer_running;

};

struct dio_priv {
	/* Bits status */
	uint16_t bits_values;
};

/* --- Channels / ranges part --- */

/* Channels descriptor */
static a4l_chdesc_t ai_chandesc = {
	.mode = A4L_CHAN_GLOBAL_CHANDESC,
	.length = 8,
	.chans = {
		{A4L_CHAN_AREF_GROUND, 16},
	},
};

static a4l_chdesc_t dio_chandesc = {
	.mode = A4L_CHAN_GLOBAL_CHANDESC,
	.length = 16,
	.chans = {
		{A4L_CHAN_AREF_GROUND, 1},
	},
};

/* Ranges tab */
static a4l_rngtab_t ai_rngtab = {
	.length = 2,
	.rngs = {
		RANGE_V(-5,5),
		RANGE_V(-10,10),
	},
};
/* Ranges descriptor */
static a4l_rngdesc_t ai_rngdesc = RNG_GLOBAL(ai_rngtab);

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

static uint16_t ai_value_output(struct ai_priv *priv)
{
	static uint16_t output_tab[8] = {
		0x0001, 0x2000, 0x4000, 0x6000,
		0x8000, 0xa000, 0xc000, 0xffff
	};
	static unsigned int output_idx;
	static a4l_lock_t output_lock = A4L_LOCK_UNLOCKED;

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
static void ai_task_proc(void *arg)
{
	a4l_subd_t *subd = (a4l_subd_t *)arg;
	struct ai_priv *priv = (struct ai_priv *)subd->priv;
	a4l_cmd_t *cmd = NULL;
	uint64_t now_ns, elapsed_ns=0;

	while(1) {
		int running;

		RTDM_EXECUTE_ATOMICALLY(running = priv->timer_running);

		if(running)
		{
			int i = 0;

			cmd = a4l_get_cmd(subd);

			now_ns = a4l_get_time();
			elapsed_ns += now_ns - priv->last_ns + priv->reminder_ns;
			priv->last_ns = now_ns;

			while(elapsed_ns >= priv->scan_period_ns) {
				int j;

				for(j = 0; j < cmd->nb_chan; j++) {
					uint16_t value = ai_value_output(priv);
					a4l_buf_put(subd, &value, sizeof(uint16_t));
				}

				elapsed_ns -= priv->scan_period_ns;
				i++;

			}

			priv->current_ns += i * priv->scan_period_ns;
			priv->reminder_ns = elapsed_ns;

			if (i != 0)
				a4l_buf_evt(subd, 0);
		}

		a4l_task_sleep(AI_TASK_PERIOD);
	}
}

/* --- Asynchronous AI functions --- */

static int ai_cmd(a4l_subd_t *subd, a4l_cmd_t *cmd)
{
	struct ai_priv *priv = (struct ai_priv *)subd->priv;

	priv->scan_period_ns = cmd->scan_begin_arg;
	priv->convert_period_ns = (cmd->convert_src==TRIG_TIMER)?
		cmd->convert_arg:0;

	a4l_dbg(1, drv_dbg, subd->dev,
		"ai_cmd: scan_period=%luns convert_period=%luns\n",
		priv->scan_period_ns, priv->convert_period_ns);

	priv->last_ns = a4l_get_time();

	priv->current_ns = ((unsigned long)priv->last_ns);
	priv->reminder_ns = 0;

	RTDM_EXECUTE_ATOMICALLY(priv->timer_running = 1);

	return 0;

}

static int ai_cmdtest(a4l_subd_t *subd, a4l_cmd_t *cmd)
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

static int ai_cancel(a4l_subd_t *subd)
{
	struct ai_priv *priv = (struct ai_priv *)subd->priv;

	RTDM_EXECUTE_ATOMICALLY(priv->timer_running = 0);

	return 0;
}

static void ai_munge(a4l_subd_t *subd, void *buf, unsigned long size)
{
	int i;

	for(i = 0; i < size / sizeof(uint16_t); i++)
		((uint16_t *)buf)[i] += 1;
}

/* --- Synchronous AI functions --- */

static int ai_insn_read(a4l_subd_t *subd, a4l_kinsn_t *insn)
{
	struct ai_priv *priv = (struct ai_priv *)subd->priv;
	uint16_t *data = (uint16_t *)insn->data;
	int i;

	for(i = 0; i < insn->data_size / sizeof(uint16_t); i++)
		data[i] = ai_value_output(priv);

	return 0;
}

/* --- Synchronous DIO function --- */

static int dio_insn_bits(a4l_subd_t *subd, a4l_kinsn_t *insn)
{
	struct dio_priv *priv = (struct dio_priv *)subd->priv;
	uint16_t *data = (uint16_t *)insn->data;

	if (insn->data_size != 2 * sizeof(uint16_t))
		return -EINVAL;

	if (data[0] != 0) {
		priv->bits_values &= ~(data[0]);
		priv->bits_values |= (data[0] & data[1]);
	}

	data[1] = priv->bits_values;

	return 0;
}

/* --- Initialization functions --- */

void setup_ai_subd(a4l_subd_t *subd)
{
	/* Fill the subdevice structure */
	subd->flags |= A4L_SUBD_AI;
	subd->flags |= A4L_SUBD_CMD;
	subd->flags |= A4L_SUBD_MMAP;
	subd->rng_desc = &ai_rngdesc;
	subd->chan_desc = &ai_chandesc;
	subd->do_cmd = ai_cmd;
	subd->do_cmdtest = ai_cmdtest;
	subd->cancel = ai_cancel;
	subd->munge = ai_munge;
	subd->cmd_mask = &test_cmd_mask;
	subd->insn_read = ai_insn_read;
}

void setup_dio_subd(a4l_subd_t *subd)
{
	/* Fill the subdevice structure */
	subd->flags |= A4L_SUBD_DIO;
	subd->chan_desc = &dio_chandesc;
	subd->rng_desc = &range_digital;
	subd->insn_bits = dio_insn_bits;
}

/* --- Attach / detach functions ---  */

int test_attach(a4l_dev_t *dev, a4l_lnkdesc_t *arg)
{
	int ret = 0;
	a4l_subd_t *subd;
	struct fake_priv *priv = (struct fake_priv *)dev->priv;
	struct ai_priv * ai_priv;

	a4l_dbg(1, drv_dbg, dev, "starting attach procedure...\n");

	if (arg->opts_size < sizeof(unsigned long)) {
		priv->amplitude_div = 1;
		priv->quanta_cnt = 1;
	} else {
		unsigned long *args = (unsigned long *)arg->opts;
		priv->amplitude_div = args[0];

		if (arg->opts_size == 2 * sizeof(unsigned long))
			priv->quanta_cnt = (args[1] > 7 || args[1] == 0) ?
				1 : args[1];
	}

	a4l_dbg(1, drv_dbg, dev,
		"amplitude divisor = %lu\n", priv->amplitude_div);
	a4l_dbg(1, drv_dbg, dev,
		"quanta count = %lu\n", priv->quanta_cnt);

	/* Add the AI subdevice to the device */
	subd = a4l_alloc_subd(sizeof(struct ai_priv), setup_ai_subd);
	if(subd == NULL)
		return -ENOMEM;

	ai_priv = (struct ai_priv*)subd->priv;
	ai_priv->timer_running = 0;
	ai_priv->amplitude_div = priv->amplitude_div;
	ai_priv->quanta_cnt = priv->quanta_cnt;

	ret = a4l_task_init(&ai_priv->timer_task,
			    "Fake AI task",
			    ai_task_proc,
			    subd, A4L_TASK_HIGHEST_PRIORITY);

	ret = a4l_add_subd(dev, subd);
	if(ret != AI_SUBD)
		return (ret < 0) ? ret : -EINVAL;

	a4l_dbg(1, drv_dbg, dev, "AI subdevice registered\n");

	/* Add the DIO subdevice to the device */
	subd = a4l_alloc_subd(sizeof(struct dio_priv), setup_dio_subd);
	if(subd == NULL)
		return -ENOMEM;

	ret = a4l_add_subd(dev, subd);
	if(ret != DIO_SUBD)
		return (ret < 0) ? ret : -EINVAL;

	a4l_dbg(1, drv_dbg, dev, "DIO subdevice registered\n");

	a4l_dbg(1, drv_dbg, dev, "attach procedure complete\n");

	return 0;
}

int test_detach(a4l_dev_t *dev)
{
	a4l_subd_t *subd = a4l_get_subd(dev, AI_SUBD);
	struct ai_priv *priv = (struct ai_priv *)subd->priv;

	a4l_task_destroy(&priv->timer_task);

	a4l_dbg(1, drv_dbg, dev, "detach procedure complete\n");

	return 0;
}

/* --- Module stuff --- */

static a4l_drv_t test_drv = {
	.owner = THIS_MODULE,
	.board_name = "analogy_fake",
	.attach = test_attach,
	.detach = test_detach,
	.privdata_size = sizeof(struct fake_priv),
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
