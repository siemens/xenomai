/*
 * Copyright (C) 2001,2002 IDEALX (http://www.idealx.com/).
 * Written by Julien Pinon <jpinon@idealx.com>.
 * Copyright (C) 2003 Philippe Gerum <rpm@xenomai.org>.
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

#ifndef _XENO_SKIN_VRTX_H
#define _XENO_SKIN_VRTX_H

#include <nucleus/xenomai.h>

#define VRTX_SKIN_VERSION_CODE    0x00000002
#define VRTX_SKIN_MAGIC           0x56525458

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef struct _TCB {

    int TCBSTAT;

} TCB;

#define seconds 	tv_sec
#define nanoseconds	tv_nsec

#define TBSSUSP   0x0001
#define TBSMBOX   0x0002
#define TBSPUTC   0x0008
#define TBSDELAY  0x0020
#define TBSQUEUE  0x0040
#define TBSIDLE   0x0100
#define TBSFLAG   0x0200
#define TBSSEMA   0x0400
#define TBSMUTEX  0x0800
#define TBSADELAY 0x8000

#define VRTX_VERSION 0x00000005

void sc_putc(int c);

int sc_tecreate(void (*entry)(void *),
		int tid,
		int prio,
		int mode,
		unsigned long user,
		unsigned long sys,
		char *paddr,
		unsigned long psize,
		int *perr);

int sc_tcreate(void (*entry)(void*),
	       int tid,
	       int prio,
	       int *perr);

void sc_tdelete(int tid,
		int opt,
		int *perr);

TCB *sc_tinquiry(int *pinfo,
		 int tid,
		 int *perr);

void sc_tpriority(int tid,
		  int prio,
		  int *perr);

void sc_tresume(int tid,
		int opt,
		int *perr);

void sc_tslice(unsigned short ticks);

void sc_tsuspend(int tid,
		 int opt,
		 int *perr);

void sc_delay(long timeout);

void sc_lock(void);

void sc_unlock(void);

int sc_pcreate(int pid,
	       char *paddr,
	       long psize,
	       long bsize,
	       int *perr);

void sc_pdelete(int tid,
		int opt,
		int *perr);

void sc_pextend(int pid,
		char *eaddr,
		long esize,
		int *perr);

void sc_pinquiry(unsigned long info[3],
		 int pid,
		 int *errp);

char *sc_gblock(int pid,
		int *perr);

void sc_rblock(int pid,
	       char *blockp,
	       int *perr);

int sc_mcreate(unsigned int opt,
	       int *errp);

void sc_maccept (int mid,
		 int *errp);

void sc_mdelete (int mid,
		 int opt, int *errp);

int sc_minquiry (int mid,
		 int *errp);

void sc_mpend (int mid,
	       unsigned long timeout,
	       int *errp);

void sc_mpost (int mid,
	       int *errp);

void sc_post(char **mboxp,
	     char *msg,
	     int *perr);

char *sc_accept(char **mboxp,
		int *perr);

char *sc_pend(char **mboxp,
	      long timeout,
	      int *perr);

int sc_qcreate(int qid,
		int qsize,
		int *perr);

int sc_qecreate(int qid,
		int qsize,
		int opt,
		int *perr);
  
void sc_qdelete(int qid,
		int opt,
		int *perr);
  
void sc_qjam(int qid,
	     char *msg,
	     int *perr);

void sc_qpost(int qid,
	      char *msg,
	      int *perr);

void sc_qbrdcst(int qid,
	       char *msg,
	       int *perr);

char *sc_qaccept(int qid,
		 int *perr);

char *sc_qinquiry(int qid,
		  int *countp,
		  int *perr);

char *sc_qpend(int qid,
	       long timeout,
	       int *perr);

int sc_fcreate(int *perr);

void sc_fdelete(int fid,
		int opt,
		int *perr);

void sc_fpost(int fid,
	      int mask,
	      int *perr);

int sc_fclear(int fid,
	      int mask,
	      int *perr);

int sc_finquiry(int fid,
		int *perr);

int sc_fpend(int fid,
	     long timeout,
	     int mask,
	     int opt,
	     int *perr);

int sc_screate(unsigned initval,
	       int opt,
	       int *perr);

void sc_sdelete(int semid,
		int opt,
		int *perr);

void sc_spend(int semid,
	      long timeout,
	      int *perr);

void sc_saccept(int semid,
	      int *perr);

void sc_spost(int semid,
	      int *perr);

int sc_sinquiry(int semid,
		int *perr);

void sc_stime(long ticks);

unsigned long sc_gtime(void);

int sc_hcreate(char *heapaddr,
	       unsigned long heapsize,
	       unsigned log2psize,
	       int *perr);

void sc_hdelete(int hid, int opt, int *perr);

char *sc_halloc(int hid,
		unsigned long size,
		int *perr);

void sc_hfree(int hid,
	      char *block,
	      int *perr);

void sc_hinquiry(int info[3], int hid, int *errp);

void ui_timer(void);

void sc_gclock(struct timespec *timep,
		unsigned long *nsp,
		int *errp);

void sc_sclock(struct timespec time,
	       unsigned long ns,
	       int *errp);

unsigned long sc_gtime(void);

void sc_stime(long time);

int sc_gversion(void);

void sc_adelay (struct timespec time, int *errp);

#ifdef __cplusplus
};
#endif /* __cplusplus */

#define RET_OK   0x00
#define ER_TID   0x01
#define ER_TCB   0x02
#define ER_MEM   0x03
#define ER_NMB   0x04
#define ER_MIU   0x05
#define ER_ZMW   0x06
#define ER_BUF   0x07
#define ER_TMO   0x0A
#define ER_NMP   0x0B
#define ER_QID   0x0C
#define ER_QFL   0x0D
#define ER_PID   0x0E
#define ER_IIP   0x12
#define ER_NOCB  0x30
#define ER_ID    0x31
#define ER_PND   0x32
#define ER_DEL   0x33
#define ER_OVF   0x34

#endif /* !_XENO_SKIN_VRTX_H */
