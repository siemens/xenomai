/**
 * @file
 * Analogy for Linux, NI - M calibration program
 *
 * @note Copyright (C) 2014 Jorge A. Ramirez-Ortiz <jro@xenomai.org>
 *
 * from original code from the Comedi project
 *
 * Xenomai is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Xenomai is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Xenomai; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <gsl/gsl_statistics_double.h>
#include <gsl/gsl_multifit.h>
#include <gsl/gsl_matrix.h>
#include <gsl/gsl_vector.h>
#include <rtdm/analogy.h>
#include <math.h>

#include "calibration_ni_m.h"

struct list ai_calibration_list;
struct list ao_calibration_list;

static struct references references;
static struct subdevice mem_subd;
static struct subdevice cal_subd;
static struct subdevice ao_subd;
static struct subdevice ai_subd;
static struct subdev_ops ops;
static struct eeprom eeprom;
static struct gnumath math;

/*
 * generate the calibration file
 */
static void
write_calibration(struct list *calibration_list, struct subdevice *subd)
{
	struct subdevice_calibration_node *e, *t;
	int i, j = 0;

	if (list_empty(calibration_list))
		return;

	push_to_cal_file("\n[%s] \n", subd->name);
	push_to_cal_file("index: %d \n", subd->idx);
	list_for_each_entry_safe(e, t, calibration_list, node)
	j++;
	push_to_cal_file("elements: %d \n", j);

	j = 0;
	list_for_each_entry_safe(e, t, calibration_list, node) {
		push_to_cal_file("[%s_%d] \n", subd->name, j);
		push_to_cal_file("channel: %d \n", e->channel);
		push_to_cal_file("range: %d \n", e->range);
		push_to_cal_file("expansion_origin: %g \n",
				 e->polynomial->expansion_origin);
		push_to_cal_file("nbcoeff: %d \n",
				 e->polynomial->nb_coefficients);
		push_to_cal_file("coefficients: ");

		for (i = 0;;) {
			push_to_cal_file("%g", e->polynomial->coefficients[i]);
			i++;
			if (i == e->polynomial->nb_coefficients) {
				push_to_cal_file(" \n");
				break;
			}
			push_to_cal_file(", ");
		}
		j++;
	}

	return;
}


/*
 *  eeprom
 */
static int
eeprom_read_byte(unsigned address, unsigned *val)
{
	ops.data.read(val, &mem_subd, address, 0, 0);
	if (*val > 0xff)
		error(EXIT, 0, "failed to read byte from EEPROM %d > 0xff", *val);

	return 0;
}

static int
eeprom_read_uint16(unsigned address, unsigned *val)
{
	unsigned a = 0, b = 0;
	int err;

	err = eeprom_read_byte(address, &a);
	if (err)
		error(EXIT, 0, "failed to read byte from EEPROM");
	a = a << 8;

	err = eeprom_read_byte(address + 1, &b);
	if (err)
		error(EXIT, 0, "failed to read byte from EEPROM");

	*val = a | b;

	return 0;
}

static int
eeprom_get_calibration_base_address(unsigned *address)
{
	eeprom_read_uint16(24, address);

	return 0;
}

static int
eeprom_read_float(unsigned address, float *val)
{
	union float_converter {
		unsigned u;
		float f;
	} converter;

	unsigned a = 0, b = 0, c = 0, d = 0;

	if (sizeof(float) != sizeof(uint32_t))
		error(EXIT, 0, "eeprom_read_float");

	eeprom_read_byte(address++, &a);
	a = a << 24;
	eeprom_read_byte(address++, &b);
	b = b << 16;
	eeprom_read_byte(address++, &c);
	c = c << 8;
	eeprom_read_byte(address++, &d);

	converter.u = a | b | c | d;
	*val = converter.f;

	return 0;
}

static int
eeprom_read_reference_voltage(float *val)
{
	unsigned address;

	eeprom_get_calibration_base_address(&address);
	eeprom_read_float(address + eeprom.voltage_ref_offset, val);

	return 0;
}

/*
 * subdevice operations
 */
static int
data_read_hint(struct subdevice *s, int channel, int range, int aref,
	       unsigned int delay)
{
	sampl_t dummy_data;
	a4l_insn_t insn;
	int err;

	memset(&insn, 0, sizeof(insn));
	insn.chan_desc = PACK(channel, range, aref);
	insn.idx_subd = s->idx;
	insn.type = A4L_INSN_READ;
	insn.data = &dummy_data;
	insn.data_size = 0;

	err = a4l_snd_insn(&descriptor, &insn);
	if (err < 0)
		error(EXIT, 0, "a4l_snd_insn (%d)", err);

	return 0;
}

static int
data_read(unsigned *data, struct subdevice *s, int channel, int range, int aref)
{
	a4l_insn_t insn;
	int err;

	memset(&insn, 0, sizeof(insn));
	insn.chan_desc = PACK(channel, range, aref);
	insn.idx_subd = s->idx;
	insn.type = A4L_INSN_READ;
	insn.data = data;
	insn.data_size = 1;

	err = a4l_snd_insn(&descriptor, &insn);
	if (err < 0)
		error(EXIT, 0, "a4l_snd_insn (%d)", err);

	return 0;
}

static int
data_write(long int *data, struct subdevice *s, int channel, int range, int aref)
{
	a4l_insn_t insn;
	int err;

	memset(&insn, 0, sizeof(insn));
	insn.chan_desc = PACK(channel, range, aref);
	insn.idx_subd = s->idx;
	insn.type = A4L_INSN_WRITE;
	insn.data = data;
	insn.data_size = sizeof(*data);

	err = a4l_snd_insn(&descriptor, &insn);
	if (err < 0)
		error(EXIT, 0, "a4l_snd_insn (%d)", err);

	return 0;
}

static int
data_read_async(void *dst, struct subdevice *s, unsigned int nb_samples,
		int speriod, int irange)
{
	int i, len, err;
	a4l_cmd_t cmd;
	unsigned int chan_descs[] = {
		PACK(CR_ALT_SOURCE|CR_ALT_FILTER, irange, AREF_DIFF)
	};

	memset(&cmd, 0, sizeof(cmd));
	cmd.scan_begin_src = TRIG_TIMER;
	cmd.scan_end_src = TRIG_COUNT;
	cmd.convert_src = TRIG_TIMER;
	cmd.stop_src = TRIG_COUNT;
	cmd.start_src = TRIG_NOW;
	cmd.scan_end_arg = 1;
	cmd.convert_arg = 0;
	cmd.nb_chan = 1;
	cmd.scan_begin_arg = speriod;
	cmd.chan_descs = chan_descs;
	cmd.idx_subd = s->idx;
	cmd.stop_arg = nb_samples;
	cmd.flags = A4L_CMD_SIMUL;
	SET_BIT(3, &cmd.valid_simul_stages);

	/* get driver specific info into the command structure */
	for (i = 0; i< 4; i++)
		a4l_snd_command(&descriptor, &cmd);

	/* send the real command */
	cmd.flags = 0;
	err = a4l_snd_command(&descriptor, &cmd);
	if (err)
		error(EXIT, 0, "a4l_snd_command (%d)", err);

	len = nb_samples * ai_subd.slen;
	for (;;) {
		err = a4l_async_read(&descriptor, dst, len, A4L_INFINITE);
		if (err <0)
			error(EXIT, 0, "a4l_async_read (%d)", err);
		if (err < len) {
			dst = dst + err;
			len = len - err;
		} else
			break;
	}
	a4l_snd_cancel(&descriptor, ai_subd.idx);

	return 0;
}

/*
 *
 * math: uses the gnu statistic library and the math library
 *
 */
static int
statistics_standard_deviation_of_mean(double *dst, double src[], int len,
				      double mean)
{
	double a;

	a = gsl_stats_variance_m(src, 1, len, mean);
	a = sqrt(a/len);
	*dst = a;

	return 0;
}

static int
statistics_standard_deviation(double *dst, double src[], int len, double mean)
{
	double a;

	a = gsl_stats_variance_m(src, 1, len, mean);
	a = sqrt(a);
	*dst = a;

	return 0;
}

static int
statistics_mean(double *dst, double src[], int len)
{
	*dst = gsl_stats_mean(src, 1, len);

	return 0;
}

static int
polynomial_fit(struct polynomial *dst, struct codes_info *src)
{
	gsl_multifit_linear_workspace *work;
	const int nb_coeff = dst->order + 1;
	gsl_matrix *covariance, *m;
	gsl_vector_view b, result;
	double a, *tmp, chisq;
	int i, j, len;

	work = gsl_multifit_linear_alloc(src->nb_codes, nb_coeff);
	covariance = gsl_matrix_alloc(nb_coeff, nb_coeff);
	m = gsl_matrix_alloc(src->nb_codes, nb_coeff);

	for (i = 0; i < src->nb_codes; i++) {
		gsl_matrix_set(m, i, 0, 1.0);
		for (j = 1; j < nb_coeff; j++) {
			a = gsl_matrix_get(m, i, j - 1);
			a = a * (src->codes[i].nominal - dst->expansion_origin);
			gsl_matrix_set(m, i, j, a);
		}
	}

	len = src->nb_codes * sizeof(double);
	tmp = malloc(len);
	if (!tmp)
		error(EXIT, 0, "malloc (%d)", len);

	for (i = 0; i < src->nb_codes; i++)
		tmp[i] = src->codes[i].measured;

	b = gsl_vector_view_array(tmp, src->nb_codes);

	dst->nb_coefficients = nb_coeff;
	len = dst->nb_coefficients * sizeof(double);
	dst->coefficients = malloc(len);
	if (!dst->coefficients)
		error(EXIT, 0, "malloc (%d)", len);

	result = gsl_vector_view_array(dst->coefficients, nb_coeff);
	gsl_multifit_linear(m, &b.vector, &result.vector, covariance, &chisq,
			    work);
	gsl_matrix_free(m);
	gsl_matrix_free(covariance);
	gsl_multifit_linear_free(work);

	free(tmp);

	return 0;
}

static int
polynomial_linearize(double *dst, struct polynomial *p, double val)
{
	double a = 0.0, b = 1.0;
	int i;

	for (i = 0; i < p->nb_coefficients; i++) {
		a = a + p->coefficients[i] * b;
		b = b * (val - p->expansion_origin);
	}

	*dst = a;

	return 0;
}

/*
 *
 * reference
 *
 */
static int
reference_get_min_sampling_period(int *val)
{
	unsigned int chan_descs[] = { 0 };
	a4l_cmd_t cmd;
	int err;

	memset(&cmd, 0, sizeof(cmd));
	cmd.scan_begin_src = TRIG_TIMER;
	cmd.scan_end_src = TRIG_COUNT;
	cmd.convert_src = TRIG_TIMER;
	cmd.stop_src = TRIG_COUNT;
	cmd.start_src = TRIG_NOW;
	cmd.scan_begin_arg = 0;
	cmd.convert_arg = 0;
	cmd.stop_arg = 1;
	cmd.nb_chan = 1;
	cmd.scan_end_arg = ai_subd.info->nb_chan;
	cmd.chan_descs = chan_descs;
	cmd.idx_subd = ai_subd.idx;
	cmd.flags = A4L_CMD_SIMUL;
	SET_BIT(3, &cmd.valid_simul_stages);

	err = a4l_snd_command(&descriptor, &cmd);
	if (err)
		error(EXIT, 0, "a4l_snd_command (%d)", err);

	*val = cmd.scan_begin_arg;

	return 0;
}

static int
reference_set_bits(unsigned int bits)
{
	unsigned int data[2] = { A4L_INSN_CONFIG_ALT_SOURCE, bits };
	a4l_insn_t insn;
	int err;

	insn.data_size = sizeof(data);
	insn.type = A4L_INSN_CONFIG;
	insn.idx_subd = ai_subd.idx;
	insn.chan_desc = 0;
	insn.data = data;

	err = a4l_snd_insn(&descriptor, &insn);
	if (err)
		error(EXIT, 0, "a4l_snd_insn (%d)", err);

	return 0;
}

static int
reference_set_pwm(struct subdevice *s, unsigned int h, unsigned int d,
		  unsigned int *rh, unsigned int *rd)
{
	unsigned int data[5] = {
		[0] = A4L_INSN_CONFIG_PWM_OUTPUT,
		[1] = TRIG_ROUND_NEAREST,
		[2] = h,
		[3] = TRIG_ROUND_NEAREST,
		[4] = d
	};
	a4l_insn_t insn;
	int err;

	insn.data_size = sizeof(data);
	insn.idx_subd = s->idx;
	insn.type = A4L_INSN_CONFIG;
	insn.chan_desc = 0;
	insn.data = data;

	err = a4l_snd_insn(&descriptor, &insn);
	if (err)
		error(EXIT, 0, "a4l_snd_insn (%d)", err);

	*rh = data[2];
	*rd = data[4];

	return 0;
}

static int
reference_read_doubles(double dst[], unsigned int nb_samples,
		       int speriod, int irange, unsigned int settle_time)
{
	int i, err = 0;
	sampl_t *p;

	p = malloc(nb_samples * ai_subd.slen);
	if (!p)
		error(EXIT, 0, "malloc");

	err = references.read_samples(p, nb_samples, speriod, irange,
				      settle_time);
	if (err) {
		free(p);
		error(EXIT, 0, "read_samples");
	}

	for (i = 0; i < nb_samples; i++)
		dst[i] = p[i];

	free(p);

	return 0;
}

static int
reference_read_samples(void *dst, unsigned int nb_samples, int speriod,
		       int irange, unsigned int settle_time)
{
	int err;

	if (settle_time > 1000000000)
		error(EXIT, 0, "invalid argument (%d)", settle_time);

	if (!nb_samples)
		error(EXIT, 0, "invalid nb samples (%d)", nb_samples);

	err = ops.data.read_hint(&ai_subd, CR_ALT_SOURCE|CR_ALT_FILTER,
				 irange, AREF_DIFF, settle_time);
	if (err)
		error(EXIT, 0, "read_hint (%d)", err);

	err = ops.data.read_async(dst, &ai_subd, nb_samples, speriod, irange);
	if (err)
		error(EXIT, 0, "read_async (%d)", err);

	return 0;
}

/*
 *
 * calibrator
 *
 *
 */
const char *ni_m_boards[] = {
	"pci-6220", "pci-6221", "pci-6221_37pin", "pci-6224", "pci-6225",
	"pci-6229", "pci-6250", "pci-6251", "pci-6254", "pci-6259", "pcie-6259",
	"pci-6280", "pci-6281", "pxi-6281", "pci-6284", "pci-6289"};

const int nr_ni_m_boards = ARRAY_LEN(ni_m_boards);

static inline
int pwm_period_ticks(void)
{
	int min_speriod, speriod_ticks, ticks;
	int err;

	err = references.get_min_speriod(&min_speriod);
	if (err || !min_speriod)
		error(EXIT, 0, "couldn't retrieve the sampling period");

	speriod_ticks = min_speriod / NI_M_MASTER_CLOCK_PERIOD;
	ticks = (NI_M_TARGET_PWM_PERIOD_TICKS + speriod_ticks - 1) /
		speriod_ticks;
	ticks = ticks * speriod_ticks;

	return ++ticks;
}

static inline
int pwm_rounded_nsamples(void)
{
	int pwm_period, total_speriod, min_speriod;
	int err;

	err = references.get_min_speriod(&min_speriod);
	if (err || !min_speriod)
		error(EXIT, 0, "couldn't retrieve the sampling period");

	pwm_period = pwm_period_ticks() * NI_M_MASTER_CLOCK_PERIOD;
	total_speriod = (NI_M_NR_SAMPLES * min_speriod + pwm_period / 2) /
			pwm_period;
	total_speriod = total_speriod * pwm_period;

	return total_speriod / min_speriod;
}

static int
check_buf_size(int slen)
{
	unsigned long blen, req_blen;
	int err;

	err = a4l_get_bufsize(&descriptor, ai_subd.idx, &blen);
	if (err)
		error(EXIT, 0, "a4l_get_bufsize (%d)", err);

	req_blen = slen * pwm_rounded_nsamples();
	if (blen < req_blen)
		error(EXIT, 0, "blen (%ld) < req_blen (%ld)", blen, req_blen);

	return 0;
}

static int
set_pwm_up_ticks(int t)
{
	unsigned int up_p, down_p, real_up_p, real_down_p;
	int  err;

	up_p = t * NI_M_MASTER_CLOCK_PERIOD;
	down_p = (pwm_period_ticks() - t) * NI_M_MASTER_CLOCK_PERIOD;
	err = references.set_pwm(&cal_subd, up_p, down_p, &real_up_p,
				 &real_down_p);
	if (err)
		error(EXIT, 0, "reference_set_pwm");

	return 0;
}

static int
characterize_pwm(struct pwm_info *dst, int pref, unsigned range)
{
	int i, up_ticks, err, speriod, len;
	double mean, stddev, stddev_of_mean;
	double *p;

	err = references.set_bits(pref | REF_NEG_CAL_GROUND);
	if (err)
		error(EXIT, EINVAL, "reference_set_bits");

	len = pwm_rounded_nsamples() * sizeof(*p);
	p = malloc(len);
	if (!p)
		error(EXIT, 0, "malloc (%d)", len);

	for (i = 0; i < dst->nb_nodes; i++) {

		up_ticks = NI_M_MIN_PWM_PULSE_TICKS * (i + 1);
		err = set_pwm_up_ticks(up_ticks);
		if (err)
			error(EXIT, 0, "set_pwm_up_ticks");

		err = references.get_min_speriod(&speriod);
		if (err)
			error(EXIT, 0, "get_min_speriod");

		err = references.read_doubles(p, len/sizeof(*p), speriod, range,
					      NI_M_SETTLE_TIME);
		if (err)
			error(EXIT, 0, "read_doubles");

		err = math.stats.mean(&mean, p, len/sizeof(*p));
		if (err)
			error(EXIT, 0, "estimate_mean");

		err = math.stats.stddev(&stddev, p, len/sizeof(*p), mean);
		if (err)
			error(EXIT, 0, "estimate_stddev");

		err = math.stats.stddev_of_mean(&stddev_of_mean, p,
						len/sizeof(*p), mean);
		if (err)
			error(EXIT, 0, "estimate_stddev_of_mean");

		dst->node[i].up_tick = up_ticks;
		dst->node[i].mean = mean;
	}
	free(p);

	return 0;
}

static void
print_polynomial(struct polynomial *p)
{
	int i;

	__debug("Polynomial :\n");
	__debug("\torder =  %d \n", p->order);
	__debug("\texpansion origin =  %f \n", p->expansion_origin);

	for (i = 0; i < p->nb_coefficients; i++)
		__debug("\torder  %d  coefficient =  %g \n",
			i, p->coefficients[i]);
}

static int
calibrate_non_linearity(struct polynomial *poly, struct pwm_info *src)
{
	unsigned int max_data = (1 << ai_subd.slen * 8)  - 2;
	unsigned up_ticks, down_ticks, i;
	struct codes_info data;
	int len;

	data.nb_codes = src->nb_nodes;
	len = data.nb_codes * sizeof(*data.codes);
	data.codes = malloc(len);
	if (!data.codes)
		error (EXIT, 0, "malloc (%d)", len);

	for (i = 0; i < data.nb_codes; i++) {
		up_ticks = src->node[i].up_tick;
		down_ticks = pwm_period_ticks() - up_ticks;
		data.codes[i].nominal = max_data * down_ticks /
					pwm_period_ticks();
		data.codes[i].measured = src->node[i].mean;
	}

	poly->order = 3;
	poly->expansion_origin = max_data / 2;
	math.polynomial.fit(poly, &data);

	print_polynomial(poly);
	free(data.codes);

	return 0;
}

static int
calibrate_ai_gain_and_offset(struct polynomial *dst, struct polynomial *src,
			     unsigned pos_ref, float volt_ref, unsigned range)
{
	double *p;
	double measured_ground_code, linearized_ground_code;
	double measured_reference_code, linearized_reference_code;
	double gain, offset;
	int i, len, err, speriod;
	double a, b;

	len = pwm_rounded_nsamples() * sizeof(*p);
	p = malloc(len);
	if (!p)
		error(EXIT, 0, "malloc (%d)", len);

	/* ground */
	references.set_bits(REF_POS_CAL_GROUND | REF_NEG_CAL_GROUND);
	err = references.get_min_speriod(&speriod);
	if (err)
		error(EXIT, 0, "get_min_speriod");
	err = references.read_doubles(p, len/sizeof(*p), speriod, range,
				      NI_M_SETTLE_TIME);
	if (err)
		error(EXIT, 0, "read_doubles");
	math.stats.mean(&measured_ground_code, p, len/sizeof(*p));
	math.polynomial.linearize(&linearized_ground_code, src,
				  measured_ground_code);

	/* reference */
	references.set_bits(pos_ref | REF_NEG_CAL_GROUND);
	err = references.get_min_speriod(&speriod);
	if (err)
		error(EXIT, 0, "get_min_speriod");
	err = references.read_doubles(p, len/sizeof(*p), speriod, range,
				      NI_M_SETTLE_TIME);
	if (err)
		error(EXIT, 0, "read_doubles");
	math.stats.mean(&measured_reference_code, p, len/sizeof(*p));
	math.polynomial.linearize(&linearized_reference_code, src,
				  measured_reference_code);

	gain = volt_ref / (linearized_reference_code - linearized_ground_code);

	/*
	 * update output
	 */

	dst->coefficients = malloc(src->nb_coefficients * sizeof(double));
	if (!dst->coefficients)
		error(EXIT, 0, "malloc");

	dst->expansion_origin = src->expansion_origin;
	dst->nb_coefficients = src->nb_coefficients;
	dst->order = src->order;
	for (i = 0; i < dst->nb_coefficients; i++)
		dst->coefficients[i] = src->coefficients[i] * gain;

	math.polynomial.linearize(&offset, dst, measured_ground_code);
	dst->coefficients[0] = dst->coefficients[0] - offset;

	__debug("volt_ref                = %g \n", volt_ref);
	__debug("measured_ground_code    = %g, linearized_ground_code     = %g \n",
		measured_ground_code, linearized_ground_code);
	__debug("measured_reference_code = %g, linearized_reference_code  = %g \n",
		measured_reference_code, linearized_reference_code);

	math.polynomial.linearize(&a, dst, measured_ground_code);
	__debug("full_correction(measured_ground_code)    = %g \n", a);
	math.polynomial.linearize(&b, dst, measured_reference_code);
	__debug("full_correction(measured_reference_code) = %g \n", b);

	print_polynomial(dst);

	free(p);

	return 0;
}

static int
calibrate_base_range(struct polynomial *dst, struct polynomial *src)
{
	float volt_ref;
	int err;

	eeprom.ops.read_reference_voltage(&volt_ref);
	err = calibrate_ai_gain_and_offset(dst, src, REF_POS_CAL, volt_ref,
					   NI_M_BASE_RANGE);
	if (err)
		error(EXIT, 0, "calibrate_ai_gain_and_offset");

	return err;
}


static struct subdevice_calibration_node *
get_calibration_node(struct list *l, unsigned channel, unsigned range) {
	struct subdevice_calibration_node *e, *t;

	if (list_empty(l))
		return NULL;

	list_for_each_entry_safe(e, t, l, node) {
		if (e->channel == channel ||
		    e->channel == ALL_CHANNELS ||
		    channel == ALL_CHANNELS) {
			if (e->range == range ||
			    e->range == ALL_RANGES ||
			    range == ALL_RANGES) {
				return e;
			}
		}
	}

	return NULL;
}

static int
calibrate_pwm(struct polynomial *dst, struct pwm_info *pwm_info,
	      struct subdevice_calibration_node *range_calibration)
{
	double pwm_cal, adrange_cal, lsb_error;
	double aprox_volts_per_bit, a, b;
	double measured_voltages;
	struct codes_info info;
	int i;

	if (!pwm_info->nb_nodes)
		error(EXIT, 0, "no pwm nodes \n");

	info.nb_codes = pwm_info->nb_nodes;
	info.codes = malloc (info.nb_codes * sizeof(*info.codes));

	for (i = 0; i < pwm_info->nb_nodes; i++) {
		info.codes[i].nominal = pwm_info->node[i].up_tick;
		math.polynomial.linearize(&measured_voltages,
					  range_calibration->polynomial,
					  pwm_info->node[i].mean);
		info.codes[i].measured = measured_voltages;
	}

	dst->order = 1;
	dst->expansion_origin = pwm_period_ticks() / 2;
	math.polynomial.fit(dst, &info);

	math.polynomial.linearize(&a, range_calibration->polynomial, 1);
	math.polynomial.linearize(&b, range_calibration->polynomial, 0);
	aprox_volts_per_bit = a - b;

	for (i = 0; i < pwm_info->nb_nodes; i++) {
		math.polynomial.linearize(&pwm_cal, dst,
					  pwm_info->node[i].up_tick);
		math.polynomial.linearize(&adrange_cal,
					  range_calibration->polynomial,
					  pwm_info->node[i].mean);
		lsb_error = (adrange_cal - pwm_cal) / aprox_volts_per_bit;
		__debug("upTicks=%d code=%g "
			"pwm_cal=%g adrange_cal=%g lsb_error=%g \n",
			pwm_info->node[i].up_tick, pwm_info->node[i].mean,
			pwm_cal, adrange_cal, lsb_error);
	}

	return 0;
}

static int
append_calibration_node(struct list *l, struct polynomial *polynomial,
			unsigned channel, unsigned range)
{
	struct subdevice_calibration_node *q;

	q = malloc(sizeof(struct subdevice_calibration_node));
	if (!q)
		error(EXIT, 0, "malloc");

	q->polynomial = polynomial;
	q->channel = channel;
	q->range = range;
	list_append(&q->node ,l);

	return 0;
}

static int
calibrate_ai_range(struct polynomial *dst, struct polynomial *pwm_calibration,
		   struct polynomial *non_linearity_correction, unsigned pos_ref,
		   unsigned range)
{
	struct polynomial inverse_pwm_calibration;
	double reference_voltage;
	a4l_rnginfo_t *rng;
	unsigned up_ticks;
	double *p, val;
	int err;

	if (pwm_calibration->order != 1)
		error(EXIT, -1, "pwm_calibration order \n");

	inverse_pwm_calibration.expansion_origin = pwm_calibration->coefficients[0];
	p = malloc((pwm_calibration->order + 1) * sizeof(double));
	if (!p)
		error(EXIT,0,"malloc\n");

	inverse_pwm_calibration.order = pwm_calibration->order;
	inverse_pwm_calibration.nb_coefficients = pwm_calibration->order + 1;
	inverse_pwm_calibration.coefficients = p;
	inverse_pwm_calibration.coefficients[0] = pwm_calibration->expansion_origin;
	inverse_pwm_calibration.coefficients[1] = 1.0 / pwm_calibration->coefficients[1];

	err = a4l_get_rnginfo(&descriptor, ai_subd.idx, 0, range, &rng);
	if (err < 0)
		error(EXIT,0,"a4l_get_rnginfo (%d)\n", err);

	__debug("adjusted rng_max: %g \n", rng_max(rng) * 0.9);

	math.polynomial.linearize(&val, &inverse_pwm_calibration,
				  rng_max(rng) * 0.9);
	up_ticks = lrint(val);
	free(p);

	if (up_ticks > pwm_period_ticks() - NI_M_MIN_PWM_PULSE_TICKS)
		up_ticks = pwm_period_ticks() - NI_M_MIN_PWM_PULSE_TICKS;

	set_pwm_up_ticks(up_ticks);
	math.polynomial.linearize(&val, pwm_calibration, up_ticks);
	reference_voltage = val;
	calibrate_ai_gain_and_offset(dst, non_linearity_correction, pos_ref,
				     reference_voltage, range);

	return 0;
}

static int
calibrate_ranges_above_threshold(struct polynomial *pwm_calibration,
				 struct polynomial *non_linearity_correction,
				 unsigned pos_ref,
				 struct list *calibration_list,
				 struct calibrated_ranges *calibrated,
				 double max_range_threshold )
{
	struct polynomial *dst;
	a4l_rnginfo_t *rnginfo;
	int err, i;

	for (i = 0; i < calibrated->nb_ranges; i++) {
		if (calibrated->ranges[i] == 1)
			continue;

		err = a4l_get_rnginfo(&descriptor, ai_subd.idx, 0, i, &rnginfo);
		if (err < 0)
			error(EXIT,0,"a4l_get_rnginfo (%d)\n", err);

		if (rng_max(rnginfo) < max_range_threshold)
			continue;

		dst = malloc(sizeof(*dst));
		if (!dst)
			error(EXIT, 0, "malloc");

		__debug("calibrating range %d \n", i);
		calibrate_ai_range(dst, pwm_calibration, non_linearity_correction,
				   pos_ref, i);
		append_calibration_node(calibration_list, dst, ALL_CHANNELS, i);
		calibrated->ranges[i] = 1;
		__debug("done \n");
	}

	return 0;
}

static int
get_min_range_containing(struct calibrated_ranges *calibrated, double value)
{
	a4l_rnginfo_t *rnginfo, *smallest = NULL;
	unsigned smallest_range = 0;
	int err, i;

	for (i = 0; i < calibrated->nb_ranges; i++) {
		if (!calibrated->ranges[i])
			continue;

		err = a4l_get_rnginfo(&descriptor, ai_subd.idx, 0, i, &rnginfo);
		if (err < 0)
			error(EXIT,0,"a4l_get_rnginfo (%d)\n", err);

		if (rng_max(rnginfo) > value &&
		    (smallest_range == 0 || rng_max(rnginfo) < rng_max(smallest))) {
			smallest_range = i;
			smallest = rnginfo;
		}
	}
	if (!smallest)
		error(EXIT,0,"no cal range with max volt above %g V found \n", value);

	return smallest_range;
}

static int
ni_m_calibrate_ai(void)
{
	const unsigned PWM_CAL_POINTS = (NI_M_TARGET_PWM_PERIOD_TICKS / NI_M_MIN_PWM_PULSE_TICKS);
	const double MEDIUM_RANGE = 0.499;
	const double LARGE_RANGE = 1.99;
	const double SMALL_RANGE = 0.0;

	struct polynomial non_linearity_correction, full_correction;
	struct subdevice_calibration_node *node;
	struct calibrated_ranges calibrated;
	struct polynomial pwm_calibration;
	struct pwm_info pwm_info;
	a4l_chinfo_t *chan_info;
	int i, err;

	struct calibration_loop {
		const char *message;
		unsigned ref_pos;
		double threshold;
		double item;
		int range;
	} calibration_info [] = {
		[0] = {
			.message = "low gain range ",
			.ref_pos = REF_POS_CAL_PWM_10V,
			.threshold = LARGE_RANGE,
			.range = NI_M_BASE_RANGE,
			.item = - 1,
		},
		[1] = {
			.message = "medium gain range ",
			.ref_pos = REF_POS_CAL_PWM_2V,
			.threshold = MEDIUM_RANGE,
			.item = LARGE_RANGE,
			.range = -1,
		},
		[2] = {
			.message = "high gain range ",
			.ref_pos = REF_POS_CAL_PWM_500mV,
			.threshold = SMALL_RANGE,
			.item =  MEDIUM_RANGE,
			.range = -1,
		},

	};

	list_init(&ai_calibration_list);

	/*
	 * check if the buffer is big enough
	 */
	err = a4l_get_chinfo(&descriptor, ai_subd.idx, 0, &chan_info);
	if (err)
		error(EXIT, 0,"a4l_get_chinfo (%d)", err);

	calibrated.nb_ranges = chan_info->nb_rng;
	calibrated.ranges = malloc(chan_info->nb_rng * sizeof(unsigned));
	if (!calibrated.ranges)
		error(EXIT, 0,"malloc");

	memset(calibrated.ranges, 0, calibrated.nb_ranges * sizeof(unsigned));

	ai_subd.slen = a4l_sizeof_chan(chan_info);
	if (ai_subd.slen < 0)
		error (RETURN, 0, "a4l_sizeof_chan (%d)", err);

	err = check_buf_size(ai_subd.slen);
	if (err)
		error(EXIT, -1, "ni_m_check_buf_size: device buffer too small, "
		      "please re-attach a bigger buffer");

	pwm_info.nb_nodes = PWM_CAL_POINTS;
	pwm_info.node = malloc(PWM_CAL_POINTS * sizeof(*pwm_info.node));
	if (err)
		error(EXIT, -ENOMEM, "malloc error");

	/*
	 * calibrate base range
	 */
	err = characterize_pwm(&pwm_info, REF_POS_CAL_PWM_10V, NI_M_BASE_RANGE);
	if (err)
		error(EXIT, 0, "characterize_pwm");

	err = calibrate_non_linearity(&non_linearity_correction, &pwm_info);
	if (err)
		error(EXIT, 0, "calibrate_non_linearity");

	err = calibrate_base_range(&full_correction, &non_linearity_correction);
	if (err)
		error(EXIT, 0, "calibrate_ai_base_range");

	append_calibration_node(&ai_calibration_list, &full_correction,
				ALL_CHANNELS, NI_M_BASE_RANGE);
	calibrated.ranges[NI_M_BASE_RANGE] = 1;


	/*
	 * calibrate low, medium and high gain ranges
	 */
	for (i = 0; i < ARRAY_LEN(calibration_info); i++) {

		__debug("Calibrating AI: %s \n", calibration_info[i]);

		if (calibration_info[i].range >= 0)
			goto calibrate;

		calibration_info[i].range = get_min_range_containing(&calibrated,
								     calibration_info[i].item);
		if (!calibrated.ranges[calibration_info[i].range])
			error(EXIT, 0, "not calibrated yet \n" );

		err = characterize_pwm(&pwm_info, calibration_info[i].ref_pos,
				       calibration_info[i].range);
		if (err)
			error(EXIT, 0, "characterize_pwm \n");

calibrate:
		node = get_calibration_node(&ai_calibration_list, 0,
					    calibration_info[i].range);
		if (!node)
			error(EXIT, 0, "couldnt find node \n");

		err = calibrate_pwm(&pwm_calibration, &pwm_info, node);
		if (err)
			error(EXIT, 0, "calibrate_pwm \n");

		err = calibrate_ranges_above_threshold(&pwm_calibration,
						       &non_linearity_correction,
						       calibration_info[i].ref_pos,
						       &ai_calibration_list,
						       &calibrated,
						       calibration_info[i].threshold);
		if (err)
			error(EXIT, 0, "calibrate_ranges_above_threshold \n");

	}

	return 0;
}

static unsigned
find_ai_range_for_ao(unsigned ao_range)
{
	a4l_rnginfo_t *ao_rng_info, *ai_rng_info, *rng_info = NULL;
	a4l_chinfo_t *ai_chan_info;
	unsigned range = 0xFFFF;
	double max_ao_voltage;
	int num_ai_ranges;
	int i, err;

	err = a4l_get_chinfo(&descriptor, ai_subd.idx, 0, &ai_chan_info);
	if (err)
		error(EXIT, 0,"a4l_get_chinfo (%d)", err);

	num_ai_ranges = ai_chan_info->nb_rng;

	err = a4l_get_rnginfo(&descriptor, ao_subd.idx, 0, ao_range, &ao_rng_info);
	if (err)
		error(EXIT, 0, "a4l_get_rng_info (%d)", err);

	max_ao_voltage = rng_max(ao_rng_info);

	for (i = 0; i < num_ai_ranges; i++) {
		err = a4l_get_rnginfo(&descriptor, ai_subd.idx, 0, i, &ai_rng_info);
		if (err)
			error(EXIT, 0, "a4l_get_rng_info (%d)", err);

		if (rng_info == NULL ||
		    (rng_max(ai_rng_info) > max_ao_voltage &&
		     rng_max(ai_rng_info) < rng_max(rng_info)) ||
		    (rng_max(rng_info) < max_ao_voltage &&
		     rng_max(ai_rng_info) > rng_max(rng_info))) {

			range = i;
			rng_info = ai_rng_info;
		}
	}

	if (rng_info == NULL)
		error(EXIT, 0, "cant find range");

	return range;
}

static long int
get_high_code(unsigned ai_rng, unsigned ao_rng)
{
	unsigned int ao_max_data = (1 << ao_subd.slen * 8)  - 2;
	a4l_rnginfo_t *ai, *ao;
	double fractional_code;

	a4l_get_rnginfo(&descriptor, ai_subd.idx, 0, ai_rng, &ai);
	a4l_get_rnginfo(&descriptor, ao_subd.idx, 0, ai_rng, &ao);

	if (rng_max(ai) > rng_max(ao))
		return lrint(ao_max_data * 0.9);

	fractional_code = (0.9 * rng_max(ai) - rng_min(ao)) / (rng_max(ao) - rng_min(ao));
	if (fractional_code < 0.0 || fractional_code > 1.0)
		error(EXIT, 0, "error looking for high code");

	return lrint(ao_max_data * fractional_code);
}

static int
calibrate_ao_channel_and_range(unsigned ai_rng, unsigned ao_channel, unsigned ao_rng)
{
	unsigned int ao_max_data = (1 << ao_subd.slen * 8)  - 2;
	double measured_low_code, measured_high_code, tmp;
	long int low_code = lrint(ao_max_data * 0.1);
	struct subdevice_calibration_node *node;
	struct codes_info data;
	struct polynomial poly;
	long int high_code;
	double *readings;
	int speriod;
	int i;

	node = get_calibration_node(&ai_calibration_list, 0, ai_rng);
	if (!node)
		error(EXIT, 0, "couldnt find node \n");

	data.nb_codes = 2;
	data.codes = malloc (data.nb_codes * sizeof(*data.codes));
	readings = malloc(NI_M_NR_SAMPLES * sizeof(*readings));
	if (data.codes == NULL || readings == NULL)
		error(EXIT,0, "malloc");

	if ((ao_channel & 0xf) != ao_channel)
		error(EXIT,0, "wrong ao channel (%d)", ao_channel);

	references.set_bits(REF_POS_CAL_AO |  REF_NEG_CAL_GROUND | ao_channel << 15);

	/* low nominals */
	data.codes[0].nominal = low_code;
	ops.data.write(&low_code, &ao_subd, ao_channel, ao_rng, AREF_GROUND);
	references.get_min_speriod(&speriod);
	references.read_doubles(readings, NI_M_NR_SAMPLES, speriod, ai_rng,
				NI_M_SETTLE_TIME);
	math.stats.mean(&measured_low_code, readings, NI_M_NR_SAMPLES);
	math.polynomial.linearize(&data.codes[0].measured, node->polynomial,
				  measured_low_code);

	/* high nominals */
	high_code = get_high_code(ai_rng, ao_rng);
	data.codes[1].nominal = (1.0) * (double) high_code;
	ops.data.write(&high_code, &ao_subd, ao_channel, ao_rng, AREF_GROUND);
	references.get_min_speriod(&speriod);
	references.read_doubles(readings, NI_M_NR_SAMPLES, speriod, ai_rng,
				NI_M_SETTLE_TIME);
	math.stats.mean(&measured_high_code, readings, NI_M_NR_SAMPLES);
	math.polynomial.linearize(&data.codes[1].measured, node->polynomial,
				  measured_high_code);

	poly.expansion_origin = 0.0;
	poly.order = data.nb_codes - 1;
	__debug("AO calibration for channel %d, range %d \n", ao_channel, ao_rng);

	for (i = 0; i < data.nb_codes ; i++)
		__debug("set ao to %g, measured %g \n", data.codes[i].nominal,
			data.codes[i].measured);

	/*----------------------------------------------------------------------
	 * the comedi calibration seems to invert the nominal and measured
	 * values (I suppose they know about this) so I will have to hack it
	 */
	for (i = 0; i < data.nb_codes; i++) {
		tmp = data.codes[i].measured ;
		data.codes[i].measured = data.codes[i].nominal;
		data.codes[i].nominal = tmp;
	}
	/*--------------------------------------------------------------------*/
	math.polynomial.fit(&poly, &data);

	append_calibration_node(&ao_calibration_list, &poly, ao_channel, ao_rng);

	print_polynomial(&poly);
	free(data.codes);

	return 0;
}

static int
ni_m_calibrate_ao(void)
{
	a4l_rnginfo_t *range_info;
	a4l_chinfo_t *chan_info;
	unsigned channel, range;
	unsigned ai_range;
	int err;

	list_init(&ao_calibration_list);

	err = a4l_get_chinfo(&descriptor, ao_subd.idx, 0, &chan_info);
	if (err)
		error(EXIT, 0,"a4l_get_chinfo (%d)", err);

	ao_subd.slen = a4l_sizeof_chan(chan_info);
	if (ao_subd.slen < 0)
		error (RETURN, 0, "a4l_sizeof_chan (%d)", err);

	for (channel = 0; channel < ao_subd.info->nb_chan; channel++) {
		for (range = 0 ; range < chan_info->nb_rng; range++) {

			err = a4l_get_rnginfo(&descriptor, ao_subd.idx, 0, range,
					      &range_info);
			if (err)
				error(EXIT, 0, "a4l_get_rng_info (%d)", err);

			if (A4L_RNG_UNIT(range_info->flags) !=  A4L_RNG_VOLT_UNIT)
				continue;

			ai_range = find_ai_range_for_ao(range);

			err = calibrate_ao_channel_and_range(ai_range, channel,
							     range);
			if (err)
				error(EXIT, 0, "calibrate_ao");
		}
	}

	return 0;
}

/*
 * main entry
 */
int ni_m_software_calibrate(void)
{
	a4l_sbinfo_t *sbinfo;
	int i, err;

	__debug("calibrating device: %s \n", descriptor.board_name);

	descriptor.sbdata = malloc(descriptor.sbsize);
	if (descriptor.sbdata == NULL)
		error(EXIT, 0, "malloc ENOMEM (requested %d)", descriptor.sbsize);

	err = a4l_fill_desc(&descriptor);
	if (err)
		error(EXIT, 0, "a4l_fill_desc (%d)", err);

	for (i = 0; i < descriptor.nb_subd; i++) {

		err = a4l_get_subdinfo(&descriptor, i, &sbinfo);
		if (err < 0)
			error(EXIT, 0, "a4l_get_subdinfo (%d)", err);

		switch (sbinfo->flags & A4L_SUBD_TYPES) {
		case A4L_SUBD_CALIB:
			SET_SUBD(cal, i, sbinfo, "calibration");
			break;
		case A4L_SUBD_AI:
			SET_SUBD(ai, i, sbinfo, "analog_input");
			break;
		case A4L_SUBD_AO:
			SET_SUBD(ao, i, sbinfo, "analog_output");
			break;
		case A4L_SUBD_MEMORY:
			SET_SUBD(mem, i, sbinfo, "memory");
			break;
		}
	}

	if (cal_subd.idx < 0 || ai_subd.idx < 0 || mem_subd.idx < 0)
		error(EXIT, 0, "can't find subdevice");

	err = ni_m_calibrate_ai();
	if (err)
		error(EXIT, 0, "ai calibration error (%d)", err);

	write_calibration(&ai_calibration_list, &ai_subd);

	/* only calibrate the analog output subdevice if present */
	if (ao_subd.idx < 0) {
		__debug("analog output not present \n");
		return 0;
	}

	err = ni_m_calibrate_ao();
	if (err)
		error(EXIT, 0, "ao calibration error (%d)", err);

	write_calibration(&ao_calibration_list, &ao_subd);

	return 0;
}

static void __attribute__ ((constructor)) __ni_m_calibrate_init(void)
{
	init_interface(references, REFERENCES);
	init_interface(eeprom, EEPROM);
	init_interface(ops,SUBDEV_OPS);
	init_interface(mem_subd, SUBD);
	init_interface(cal_subd, SUBD);
	init_interface(math, GNU_MATH);
	init_interface(ao_subd, SUBD);
	init_interface(ai_subd, SUBD);
}

