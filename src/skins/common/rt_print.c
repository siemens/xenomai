/*
 * Copyright (C) 2007 Jan Kiszka <jan.kiszka@web.de>.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>
#include <signal.h>

#include <rtdk.h>
#include <nucleus/types.h>	/* For BITS_PER_LONG */
#include <asm/xenomai/system.h>
#include <asm/xenomai/atomic.h>	/* For atomic_cmpxchg */
#include <asm-generic/stack.h>

#define RT_PRINT_BUFFER_ENV		"RT_PRINT_BUFFER"
#define RT_PRINT_DEFAULT_BUFFER		16*1024

#define RT_PRINT_PERIOD_ENV		"RT_PRINT_PERIOD"
#define RT_PRINT_DEFAULT_PERIOD		100 /* ms */

#define RT_PRINT_BUFFERS_COUNT_ENV      "RT_PRINT_BUFFERS_COUNT"
#define RT_PRINT_DEFAULT_BUFFERS_COUNT  4

#define RT_PRINT_LINE_BREAK		256

#define RT_PRINT_SYSLOG_STREAM		NULL

#define RT_PRINT_MODE_FORMAT		0
#define RT_PRINT_MODE_FWRITE		1

struct entry_head {
	FILE *dest;
	uint32_t seq_no;
	int priority;
	size_t len;
	char data[0];
} __attribute__((packed));

struct print_buffer {
	off_t write_pos;

	struct print_buffer *next, *prev;

	void *ring;
	size_t size;

	char name[32];

	/*
	 * Keep read_pos separated from write_pos to optimise write
	 * caching on SMP.
	 */
	off_t read_pos;
};

static struct print_buffer *first_buffer;
static int buffers;
static uint32_t seq_no;
static size_t default_buffer_size;
static struct timespec print_period;
static int auto_init;
static pthread_mutex_t buffer_lock;
static pthread_cond_t printer_wakeup;
static pthread_key_t buffer_key;
static pthread_t printer_thread;
#ifdef CONFIG_XENO_FASTSYNCH
static xnarch_atomic_t *pool_bitmap;
static unsigned pool_bitmap_len;
static unsigned pool_buf_size;
static unsigned long pool_start, pool_len;
#endif /* CONFIG_XENO_FASTSYNCH */

static pthread_once_t init_once = PTHREAD_ONCE_INIT;

static void cleanup_buffer(struct print_buffer *buffer);
static void print_buffers(void);
static void spawn_printer_thread(void);

/* *** rt_print API *** */

static int vprint_to_buffer(FILE *stream, int priority, unsigned int mode,
			    size_t sz, const char *format, va_list args)
{
	struct print_buffer *buffer = pthread_getspecific(buffer_key);
	off_t write_pos, read_pos;
	struct entry_head *head;
	int len, str_len;
	int res = 0;

	if (!buffer) {
		res = 0;
		if (auto_init)
			res = rt_print_init(0, NULL);
		else
			res = EIO;

		if (res) {
			errno = res;
			return -1;
		}
		buffer = pthread_getspecific(buffer_key);
	}

	/* Take a snapshot of the ring buffer state */
	write_pos = buffer->write_pos;
	read_pos = buffer->read_pos;
	xnarch_read_memory_barrier();

	/* Is our write limit the end of the ring buffer? */
	if (write_pos >= read_pos) {
		/* Keep a safety margin to the end for at least an empty entry */
		len = buffer->size - write_pos - sizeof(struct entry_head);

		/* Special case: We were stuck at the end of the ring buffer
		   with space left there only for one empty entry. Now
		   read_pos was moved forward and we can wrap around. */
		if (len == 0 && read_pos > sizeof(struct entry_head)) {
			/* Write out empty entry */
			head = buffer->ring + write_pos;
			head->seq_no = seq_no;
			head->priority = 0;
			head->len = 0;

			/* Forward to the ring buffer start */
			write_pos = 0;
			len = read_pos - 1;
		}
	} else {
		/* Our limit is the read_pos ahead of our write_pos. One byte
		   margin is required to detect a full ring. */
		len = read_pos - write_pos - 1;
	}

	/* Account for head length */
	len -= sizeof(struct entry_head);
	if (len < 0)
		len = 0;

	head = buffer->ring + write_pos;

	if (mode == RT_PRINT_MODE_FORMAT) {
		if (stream != RT_PRINT_SYSLOG_STREAM) {
			/* We do not need the terminating \0 */
			res = vsnprintf(head->data, len, format, args);

			if (res < len) {
				/* Text was written completely, res contains its
				   length */
				len = res;
			} else {
				/* Text was truncated */
				res = len;
			}
		} else {
			/* We DO need the terminating \0 */
			res = vsnprintf(head->data, len, format, args);

			if (res < len) {
				/* Text was written completely, res contains its
				   length */
				len = res + 1;
			} else {
				/* Text was truncated */
				res = len;
			}
		}
	} else if (len >= 1) {
		str_len = sz;
		len = (str_len < len) ? str_len : len;
		memcpy(head->data, format, len);
	} else
		len = 0;

	/* If we were able to write some text, finalise the entry */
	if (len > 0) {
		head->seq_no = ++seq_no;
		head->priority = priority;
		head->dest = stream;
		head->len = len;

		/* Move forward by text and head length */
		write_pos += len + sizeof(struct entry_head);
	}

	/* Wrap around early if there is more space on the other side */
	if (write_pos >= buffer->size - RT_PRINT_LINE_BREAK &&
	    read_pos <= write_pos && read_pos > buffer->size - write_pos) {
		/* An empty entry marks the wrap-around */
		head = buffer->ring + write_pos;
		head->seq_no = seq_no;
		head->priority = priority;
		head->len = 0;

		write_pos = 0;
	}

	/* All entry data must be written before we can update write_pos */
	xnarch_write_memory_barrier();

	buffer->write_pos = write_pos;

	return res;
}

static int print_to_buffer(FILE *stream, int priority, unsigned int mode,
			   size_t sz, const char *format, ...)
{
	va_list args;
	int ret;

	va_start(args, format);
	ret = vprint_to_buffer(stream, priority, mode, sz, format, args);
	va_end(args);

	return ret;
}

int rt_vfprintf(FILE *stream, const char *format, va_list args)
{
	return vprint_to_buffer(stream, 0,
				RT_PRINT_MODE_FORMAT, 0, format, args);
}

int rt_vprintf(const char *format, va_list args)
{
	return rt_vfprintf(stdout, format, args);
}

int rt_fprintf(FILE *stream, const char *format, ...)
{
	va_list args;
	int n;

	va_start(args, format);
	n = rt_vfprintf(stream, format, args);
	va_end(args);

	return n;
}

int rt_printf(const char *format, ...)
{
	va_list args;
	int n;

	va_start(args, format);
	n = rt_vfprintf(stdout, format, args);
	va_end(args);

	return n;
}

int rt_fputs(const char *s, FILE *stream)
{
	return print_to_buffer(stream, 0, RT_PRINT_MODE_FWRITE, strlen(s), s);
}

int rt_puts(const char *s)
{
	int res;

	res = rt_fputs(s, stdout);
	if (res < 0)
		return res;

	return print_to_buffer(stdout, 0, RT_PRINT_MODE_FWRITE, 1, "\n");
}

int rt_fputc(int c, FILE *stream)
{
	unsigned char uc = c;
	int rc;

	rc = print_to_buffer(stream, 0, RT_PRINT_MODE_FWRITE, 1, (char *)&uc);
	if (rc < 0)
		return EOF;

	return (int)uc;
}

int rt_putchar(int c)
{
	return rt_fputc(c, stdout);
}

size_t rt_fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream)
{
	print_to_buffer(stream, 0, RT_PRINT_MODE_FWRITE, size * nmemb, ptr);
	return nmemb;
}


void rt_syslog(int priority, const char *format, ...)
{
	va_list args;

	va_start(args, format);
	vprint_to_buffer(RT_PRINT_SYSLOG_STREAM, priority,
			 RT_PRINT_MODE_FORMAT, 0, format, args);
	va_end(args);
}

void rt_vsyslog(int priority, const char *format, va_list args)
{
	vprint_to_buffer(RT_PRINT_SYSLOG_STREAM, priority,
			 RT_PRINT_MODE_FORMAT, 0, format, args);
}

static void set_buffer_name(struct print_buffer *buffer, const char *name)
{
	int n;

	n = sprintf(buffer->name, "%08lx", (unsigned long)pthread_self());
	if (name) {
		buffer->name[n++] = ' ';
		strncpy(buffer->name+n, name, sizeof(buffer->name)-n-1);
		buffer->name[sizeof(buffer->name)-1] = 0;
	}
}

static void rt_print_init_inner(struct print_buffer *buffer, size_t size)
{
	buffer->size = size;

	memset(buffer->ring, 0, size);

	buffer->read_pos  = 0;
	buffer->write_pos = 0;

	buffer->prev = NULL;

	pthread_mutex_lock(&buffer_lock);

	buffer->next = first_buffer;
	if (first_buffer)
		first_buffer->prev = buffer;
	first_buffer = buffer;

	buffers++;
	pthread_cond_signal(&printer_wakeup);

	pthread_mutex_unlock(&buffer_lock);
}

int rt_print_init(size_t buffer_size, const char *buffer_name)
{
	struct print_buffer *buffer = pthread_getspecific(buffer_key);
	size_t size = buffer_size;
	unsigned long old_bitmap;
	unsigned j;

	pthread_once(&init_once, spawn_printer_thread);

	if (!size)
		size = default_buffer_size;
	else if (size < RT_PRINT_LINE_BREAK)
		return EINVAL;

	if (buffer) {
		/* Only set name if buffer size is unchanged or default */
		if (size == buffer->size || !buffer_size) {
			set_buffer_name(buffer, buffer_name);
			return 0;
		}
		cleanup_buffer(buffer);
		buffer = NULL;
	}

#ifdef CONFIG_XENO_FASTSYNCH
	/* Find a free buffer in the pool */
	do {
		unsigned long bitmap;
		unsigned i;

		for (i = 0; i < pool_bitmap_len; i++) {
			old_bitmap = xnarch_atomic_get(&pool_bitmap[i]);
			if (old_bitmap)
				goto acquire;
		}

		goto not_found;

	  acquire:
		do {
			bitmap = old_bitmap;
			j = __builtin_ffsl(bitmap) - 1;
			old_bitmap = xnarch_atomic_cmpxchg(&pool_bitmap[i],
							   bitmap,
							   bitmap & ~(1UL << j));
		} while (old_bitmap != bitmap && old_bitmap);
		j += i * BITS_PER_LONG;
	} while (!old_bitmap);

	buffer = (struct print_buffer *)(pool_start + j * pool_buf_size);

  not_found:
#endif /* CONFIG_XENO_FASTSYNCH */

	if (!buffer) {
		assert_nrt();

		buffer = malloc(sizeof(*buffer));
		if (!buffer)
			return ENOMEM;

		buffer->ring = malloc(size);
		if (!buffer->ring) {
			free(buffer);
			return ENOMEM;
		}

		rt_print_init_inner(buffer, size);
	}

	set_buffer_name(buffer, buffer_name);

	pthread_setspecific(buffer_key, buffer);

	return 0;
}

void rt_print_auto_init(int enable)
{
	auto_init = enable;
	if (enable)
		pthread_once(&init_once, spawn_printer_thread);
}

void rt_print_cleanup(void)
{
	struct print_buffer *buffer = pthread_getspecific(buffer_key);

	if (buffer)
		cleanup_buffer(buffer);
	else {
		pthread_mutex_lock(&buffer_lock);

		print_buffers();

		pthread_mutex_unlock(&buffer_lock);
	}

	pthread_cancel(printer_thread);
	printer_thread = 0;
}

const char *rt_print_buffer_name(void)
{
	struct print_buffer *buffer = pthread_getspecific(buffer_key);

	if (!buffer) {
		int res = -1;

		if (auto_init)
			res = rt_print_init(0, NULL);

		if (res)
			return NULL;

		buffer = pthread_getspecific(buffer_key);
	}

	return buffer->name;
}

/* *** Deferred Output Management *** */
void rt_print_flush_buffers(void)
{
	assert_nrt();

	pthread_mutex_lock(&buffer_lock);

	print_buffers();

	pthread_mutex_unlock(&buffer_lock);
}

static void cleanup_buffer(struct print_buffer *buffer)
{
	struct print_buffer *prev, *next;

	assert_nrt();

	pthread_setspecific(buffer_key, NULL);

	pthread_mutex_lock(&buffer_lock);

	print_buffers();

	pthread_mutex_unlock(&buffer_lock);

#ifdef CONFIG_XENO_FASTSYNCH
	/* Return the buffer to the pool */
	{
		unsigned long old_bitmap, bitmap;
		unsigned i, j;

		if ((unsigned long)buffer - pool_start >= pool_len)
			goto dofree;

		j = ((unsigned long)buffer - pool_start) / pool_buf_size;
		i = j / BITS_PER_LONG;
		j = j % BITS_PER_LONG;

		old_bitmap = xnarch_atomic_get(&pool_bitmap[i]);

		if ((old_bitmap & (1UL << j)))
			return;

		do {
			bitmap = old_bitmap;
			old_bitmap = xnarch_atomic_cmpxchg(&pool_bitmap[i],
							   bitmap,
							   bitmap | (1UL << j));
		} while (old_bitmap != bitmap);

		return;
	}
  dofree:
#endif /* CONFIG_XENO_FASTSYNCH */

	pthread_mutex_lock(&buffer_lock);

	prev = buffer->prev;
	next = buffer->next;

	if (prev)
		prev->next = next;
	else
		first_buffer = next;
	if (next)
		next->prev = prev;

	buffers--;

	pthread_mutex_unlock(&buffer_lock);

	free(buffer->ring);
	free(buffer);
}

static inline uint32_t get_next_seq_no(struct print_buffer *buffer)
{
	struct entry_head *head = buffer->ring + buffer->read_pos;
	return head->seq_no;
}

static struct print_buffer *get_next_buffer(void)
{
	struct print_buffer *pos = first_buffer;
	struct print_buffer *buffer = NULL;
	uint32_t next_seq_no = 0; /* silence gcc... */

	while (pos) {
		if (pos->read_pos != pos->write_pos &&
		    (!buffer || get_next_seq_no(pos) < next_seq_no)) {
			buffer = pos;
			next_seq_no = get_next_seq_no(pos);
		}
		pos = pos->next;
	}

	return buffer;
}

static void print_buffers(void)
{
	struct print_buffer *buffer;
	struct entry_head *head;
	off_t read_pos;
	int len, ret;

	while (1) {
		buffer = get_next_buffer();
		if (!buffer)
			break;

		read_pos = buffer->read_pos;
		head = buffer->ring + read_pos;
		len = head->len;

		if (len) {
			/* Print out non-empty entry and proceed */
			/* Check if output goes to syslog */
			if (head->dest == RT_PRINT_SYSLOG_STREAM) {
				syslog(head->priority,
				       "%s", head->data);
			} else {
				ret = fwrite(head->data,
					     head->len, 1, head->dest);
				(void)ret;
			}

			read_pos += sizeof(*head) + len;
		} else {
			/* Emptry entries mark the wrap-around */
			read_pos = 0;
		}

		/* Make sure we have read the entry competely before
		   forwarding read_pos */
		xnarch_read_memory_barrier();
		buffer->read_pos = read_pos;

		/* Enforce the read_pos update before proceeding */
		xnarch_write_memory_barrier();
	}
}

static void unlock(void *cookie)
{
	pthread_mutex_t *mutex = (pthread_mutex_t *)cookie;
	pthread_mutex_unlock(mutex);
}

static void *printer_loop(void *arg)
{
	sigset_t mask;

	sigfillset(&mask);
	pthread_sigmask(SIG_BLOCK, &mask, NULL);

	while (1) {
		pthread_cleanup_push(unlock, &buffer_lock);
		pthread_mutex_lock(&buffer_lock);

		while (buffers == 0)
			pthread_cond_wait(&printer_wakeup, &buffer_lock);

		print_buffers();

		pthread_cleanup_pop(1);

		nanosleep(&print_period, NULL);
	}

	return NULL;
}

static void spawn_printer_thread(void)
{
	pthread_attr_t thattr;

	pthread_attr_init(&thattr);
	pthread_attr_setstacksize(&thattr, xeno_stacksize(0));
	pthread_attr_setdetachstate(&thattr, PTHREAD_CREATE_DETACHED);
	pthread_create(&printer_thread, &thattr, printer_loop, NULL);
}

static void forked_child_init(void)
{
	struct print_buffer *my_buffer = pthread_getspecific(buffer_key);
	struct print_buffer **pbuffer = &first_buffer;

	if (my_buffer) {
		/* Any content of my_buffer should be printed by our parent,
		   not us. */
		memset(my_buffer->ring, 0, my_buffer->size);

		my_buffer->read_pos  = 0;
		my_buffer->write_pos = 0;
	}

	/* re-init to avoid finding it locked by some parent thread */
	pthread_mutex_init(&buffer_lock, NULL);

	while (*pbuffer) {
		if (*pbuffer == my_buffer)
			pbuffer = &(*pbuffer)->next;
#ifdef CONFIG_XENO_FASTSYNCH
		else if ((unsigned long)*pbuffer - pool_start < pool_len) {
			cleanup_buffer(*pbuffer);
			pbuffer = &(*pbuffer)->next;
		}
#endif /* CONFIG_XENO_FASTSYNCH */
		else
			cleanup_buffer(*pbuffer);
	}

	if (printer_thread)
		spawn_printer_thread();
}

static __attribute__((constructor)) void __rt_print_init(void)
{
	const char *value_str;
	unsigned long long period;

	first_buffer = NULL;
	seq_no = 0;
	auto_init = 0;

	default_buffer_size = RT_PRINT_DEFAULT_BUFFER;
	value_str = getenv(RT_PRINT_BUFFER_ENV);
	if (value_str) {
		errno = 0;
		default_buffer_size = strtol(value_str, NULL, 10);
		if (errno || default_buffer_size < RT_PRINT_LINE_BREAK) {
			fprintf(stderr, "Invalid %s\n", RT_PRINT_BUFFER_ENV);
			exit(1);
		}
	}

	period = RT_PRINT_DEFAULT_PERIOD;
	value_str = getenv(RT_PRINT_PERIOD_ENV);
	if (value_str) {
		errno = 0;
		period = strtoll(value_str, NULL, 10);
		if (errno) {
			fprintf(stderr, "Invalid %s\n", RT_PRINT_PERIOD_ENV);
			exit(1);
		}
	}
	print_period.tv_sec  = period / 1000;
	print_period.tv_nsec = (period % 1000) * 1000000;

#ifdef CONFIG_XENO_FASTSYNCH
	/* Fill the buffer pool */
	{
		unsigned buffers_count, i;

		buffers_count = RT_PRINT_DEFAULT_BUFFERS_COUNT;

		value_str = getenv(RT_PRINT_BUFFERS_COUNT_ENV);
		if (value_str) {
			errno = 0;
			buffers_count = strtoul(value_str, NULL, 0);
			if (errno) {
				fprintf(stderr, "Invalid %s\n",
					RT_PRINT_BUFFERS_COUNT_ENV);
				exit(1);
			}
		}

		pool_bitmap_len = (buffers_count + BITS_PER_LONG - 1)
			/ BITS_PER_LONG;
		if (!pool_bitmap_len)
			goto done;

		pool_bitmap = malloc(pool_bitmap_len * sizeof(*pool_bitmap));
		if (!pool_bitmap) {
			fprintf(stderr, "Error allocating rt_printf "
				"buffers\n");
			exit(1);
		}

		pool_buf_size = sizeof(struct print_buffer) + default_buffer_size;
		pool_len = buffers_count * pool_buf_size;
		pool_start = (unsigned long)malloc(pool_len);
		if (!pool_start) {
			fprintf(stderr, "Error allocating rt_printf "
				"buffers\n");
			exit(1);
		}

		for (i = 0; i < buffers_count / BITS_PER_LONG; i++)
			xnarch_atomic_set(&pool_bitmap[i], ~0UL);
		if (buffers_count % BITS_PER_LONG)
			xnarch_atomic_set(&pool_bitmap[i],
					  (1UL << (buffers_count % BITS_PER_LONG))					  - 1);

		for (i = 0; i < buffers_count; i++) {
			struct print_buffer *buffer =
				(struct print_buffer *)
				(pool_start + i * pool_buf_size);

			buffer->ring = (char *)(buffer + 1);

			rt_print_init_inner(buffer, default_buffer_size);
		}
	}
  done:
#endif /* CONFIG_XENO_FASTSYNCH */

	pthread_mutex_init(&buffer_lock, NULL);
	pthread_key_create(&buffer_key, (void (*)(void*))cleanup_buffer);

	pthread_cond_init(&printer_wakeup, NULL);

	pthread_atfork(NULL, NULL, forked_child_init);
}

static __attribute__ ((destructor)) void __rt_print_exit(void)
{
	if (buffers) {
		/* Flush the buffers. Do not call print_buffers here
		 * since we do not know if our stack is big enough. */
		nanosleep(&print_period, NULL);
		nanosleep(&print_period, NULL);
	}
}
