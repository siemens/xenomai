demodir = @XENO_DEMO_DIR@

CCLD = $(top_srcdir)/scripts/wrap-link.sh $(CC)

demo_PROGRAMS = 	\
	gpiopwm		\
	bufp-label	\
	bufp-readwrite	\
	can_rtt		\
	eth_p_all	\
	iddp-label	\
	iddp-sendrecv	\
	xddp-echo	\
	xddp-label	\
	xddp-stream

cppflags = 			\
	$(XENO_USER_CFLAGS)	\
	-I$(top_srcdir)/include

ldflags = @XENO_AUTOINIT_LDFLAGS@ $(XENO_POSIX_WRAPPERS)

ldadd = 					\
	 @XENO_CORE_LDADD@			\
	 @XENO_USER_LDADD@			\
	-lpthread -lrt

gpiopwm_SOURCES = gpiopwm.c
gpiopwm_CPPFLAGS = $(cppflags) -I$(top_srcdir)/include/rtdm/uapi
gpiopwm_LDFLAGS = $(ldflags)
gpiopwm_LDADD = $(ldadd)

bufp_label_SOURCES = bufp-label.c
bufp_label_CPPFLAGS = $(cppflags)
bufp_label_LDFLAGS = $(ldflags)
bufp_label_LDADD = $(ldadd)

bufp_readwrite_SOURCES = bufp-readwrite.c
bufp_readwrite_CPPFLAGS = $(cppflags)
bufp_readwrite_LDFLAGS = $(ldflags)
bufp_readwrite_LDADD = $(ldadd)

can_rtt_SOURCES = can-rtt.c
can_rtt_CPPFLAGS = $(cppflags)
can_rtt_LDFLAGS = $(ldflags)
can_rtt_LDADD = $(ldadd)

eth_p_all_SOURCES = eth_p_all.c
eth_p_all_CPPFLAGS = $(cppflags)
eth_p_all_LDFLAGS = $(ldflags)
eth_p_all_LDADD = $(ldadd)

iddp_label_SOURCES = iddp-label.c
iddp_label_CPPFLAGS = $(cppflags)
iddp_label_LDFLAGS = $(ldflags)
iddp_label_LDADD = $(ldadd)

iddp_sendrecv_SOURCES = iddp-sendrecv.c
iddp_sendrecv_CPPFLAGS = $(cppflags)
iddp_sendrecv_LDFLAGS = $(ldflags)
iddp_sendrecv_LDADD = $(ldadd)

xddp_echo_SOURCES = xddp-echo.c
xddp_echo_CPPFLAGS = $(cppflags)
xddp_echo_LDFLAGS = $(ldflags)
xddp_echo_LDADD = $(ldadd)

xddp_label_SOURCES = xddp-label.c
xddp_label_CPPFLAGS = $(cppflags)
xddp_label_LDFLAGS = $(ldflags)
xddp_label_LDADD = $(ldadd)

xddp_stream_SOURCES = xddp-stream.c
xddp_stream_CPPFLAGS = $(cppflags)
xddp_stream_LDFLAGS = $(ldflags)
xddp_stream_LDADD = $(ldadd)
