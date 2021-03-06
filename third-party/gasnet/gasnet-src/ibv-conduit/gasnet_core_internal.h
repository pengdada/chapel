/*   $Source: bitbucket.org:berkeleylab/gasnet.git/ibv-conduit/gasnet_core_internal.h $
 * Description: GASNet ibv conduit header for internal definitions in Core API
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _GASNET_CORE_INTERNAL_H
#define _GASNET_CORE_INTERNAL_H

#include <gasnet_internal.h>
#include <firehose.h>

#include <infiniband/verbs.h>

/* TODO: flatten these? */
  #if HAVE_IBV_SRQ
    #define GASNETC_IBV_SRQ 1
    #if !GASNET_PSHM
      #undef GASNETC_IBV_XRC
    #elif HAVE_IBV_CMD_OPEN_XRCD
      #define GASNETC_IBV_XRC 1
      #define GASNETC_IBV_XRC_OFED 1
      typedef struct ibv_xrcd gasnetc_xrcd_t;
    #elif HAVE_IBV_OPEN_XRC_DOMAIN
      #define GASNETC_IBV_XRC 1
      #define GASNETC_IBV_XRC_MLNX 1
      typedef struct ibv_xrc_domain gasnetc_xrcd_t;
    #endif
  #endif

/*  whether or not to use spin-locking for HSL's */
#define GASNETC_HSL_SPINLOCK 1

/* GASNETC_FH_OPTIONAL: whether or not firehose can be switched OFF at runtime */
/* Enabled by default for DEBUG builds.  For NDEBUG builds, can force at compile time. */
#if defined(GASNETC_FH_OPTIONAL)
  /* Leave as-is */
#elif GASNET_DEBUG
  #define GASNETC_FH_OPTIONAL 1
#endif

/* check (even in optimized build) for ibv errors */
#define GASNETC_IBV_CHECK(rc,msg) \
    if_pf ((rc) != 0) \
      { gasneti_fatalerror("Unexpected error %s (rc=%d errno=%d) %s",strerror(errno),(rc), errno,(msg)); }
#define GASNETC_IBV_CHECK_PTR(ptr,msg) GASNETC_IBV_CHECK((ptr)==NULL,(msg))

#define GASNETC_CEP_SQ_SEMA(_cep) ((_cep)->sq_sema_p)

#if GASNETC_IBV_SRQ 
  #define GASNETC_QPI_IS_REQ(_qpi) ((_qpi) >= gasnetc_num_qps)
#else
  #define GASNETC_QPI_IS_REQ(_qpi) (0)
#endif

/* check for exit in progress */
extern gasneti_atomic_t gasnetc_exit_running;
#define GASNETC_IS_EXITING() gasneti_atomic_read(&gasnetc_exit_running, GASNETI_ATOMIC_RMB_PRE)

/* May eventually be a hash? */
#define GASNETC_NODE2CEP(_node) (gasnetc_node2cep[_node])


/*
 * In theory all resources should be recovered automatically at process exit.
 * However, a least Solaris 11.2 has been seen to eventually begin returning
 * ENOSPC from ibv_create_cq() after a few thousand tests have run.
 * So, we will make a best-effort to at least destroy QPs and CQs.
 * This is also needed for BLCR-based checkpoint/restart suport.
 */
#if PLATFORM_OS_SOLARIS || GASNET_BLCR || GASNET_DEBUG
  #define GASNETC_IBV_SHUTDOWN 1
  extern void gasnetc_connect_shutdown(void);
#endif

/* ------------------------------------------------------------------------------------ */
/* Core handlers.
 * These are registered early and are available even before _attach()
 */
#define _hidx_gasnetc_ack                     0 /* Special case */
#define _hidx_gasnetc_exchg_reqh              (GASNETC_HANDLER_BASE+0)
#define _hidx_gasnetc_amrdma_grant_reqh       (GASNETC_HANDLER_BASE+1)
#define _hidx_gasnetc_exit_reduce_reqh        (GASNETC_HANDLER_BASE+2)
#define _hidx_gasnetc_exit_role_reqh          (GASNETC_HANDLER_BASE+3)
#define _hidx_gasnetc_exit_role_reph          (GASNETC_HANDLER_BASE+4)
#define _hidx_gasnetc_exit_reqh               (GASNETC_HANDLER_BASE+5)
#define _hidx_gasnetc_exit_reph               (GASNETC_HANDLER_BASE+6)
#define _hidx_gasnetc_sys_barrier_reqh        (GASNETC_HANDLER_BASE+7)
#define _hidx_gasnetc_sys_exchange_reqh       (GASNETC_HANDLER_BASE+8)
#define _hidx_gasnetc_sys_flush_reph          (GASNETC_HANDLER_BASE+9)
#define _hidx_gasnetc_sys_close_reqh          (GASNETC_HANDLER_BASE+10)
/* add new core API handlers here and to the bottom of gasnet_core.c */

/* ------------------------------------------------------------------------------------ */
/* Configure gasnet_event_internal.h and gasnet_event.c */
// TODO-EX: prefix needs to move from "extended" to "core"

#define gasnete_op_atomic_(_id) gasnetc_atomic_##_id

#define GASNETE_HAVE_LC

#define GASNETE_CONDUIT_EOP_FIELDS \
  gasnetc_atomic_val_t initiated_cnt; \
  gasnetc_atomic_t     completed_cnt; \
  gasnetc_atomic_val_t initiated_alc; \
  gasnetc_atomic_t     completed_alc;

#define GASNETE_EOP_ALLOC_EXTRA(_eop) do { \
    gasnetc_atomic_set(&(_eop)->completed_cnt, 0 , 0); \
    gasnetc_atomic_set(&(_eop)->completed_alc, 0 , 0); \
  } while (0)

#define GASNETE_EOP_PREP_FREE_EXTRA(_eop) do { \
    gasneti_assert(gasnetc_atomic_read(&(_eop)->completed_cnt, 0) \
                   == ((_eop)->initiated_cnt & GASNETI_ATOMIC_MAX)); \
    gasneti_assert(gasnetc_atomic_read(&(_eop)->completed_alc, 0) \
                   == ((_eop)->initiated_alc & GASNETI_ATOMIC_MAX)); \
  } while (0)

#define _GASNETE_EOP_NEW_EXTRA GASNETE_EOP_PREP_FREE_EXTRA

/* ------------------------------------------------------------------------------------ */
/* Internal threads */

/* GASNETC_USE_CONN_THREAD enables a progress thread for establishing dynamic connections. */
#if GASNETC_DYNAMIC_CONNECT && GASNETC_IBV_CONN_THREAD
  #define GASNETC_USE_CONN_THREAD 1
#else
  #define GASNETC_USE_CONN_THREAD 0
#endif

/* GASNETC_USE_RCV_THREAD enables a progress thread for running AMs. */
#if GASNETC_IBV_RCV_THREAD
  #define GASNETC_USE_RCV_THREAD 1
#else
  #define GASNETC_USE_RCV_THREAD 0
#endif

/* ------------------------------------------------------------------------------------ */
/* Measures of concurency
 *
 * GASNETC_ANY_PAR      Non-zero if multiple threads can be executing in GASNet.
 *                      This is inclusive of the AM receive thread.
 * GASNETC_CLI_PAR      Non-zero if multiple _client_ threads can be executing in GASNet.
 *                      This excludes the AM receive thread.
 * These differ from GASNETI_THREADS and GASNETI_CLIENT_THREADS in that they don't count
 * GASNET_PARSYNC, since it has threads which do not enter GASNet concurrently.
 */

#if GASNET_PAR
  #define GASNETC_CLI_PAR 1
#else
  #define GASNETC_CLI_PAR 0
#endif

#define GASNETC_ANY_PAR         (GASNETC_CLI_PAR || GASNETC_USE_RCV_THREAD)

/* ------------------------------------------------------------------------------------ */

#define GASNETC_ARGSEND_AUX(s,nargs) gasneti_offsetof(s,args[nargs])

typedef struct {
#if GASNETI_STATS_OR_TRACE
  gasneti_tick_t	stamp;
#endif
  gex_AM_Arg_t	args[GASNETC_MAX_ARGS];
} gasnetc_shortmsg_t;
#define GASNETC_MSG_SHORT_ARGSEND(nargs) GASNETC_ARGSEND_AUX(gasnetc_shortmsg_t,nargs)

typedef struct {
#if GASNETI_STATS_OR_TRACE
  gasneti_tick_t	stamp;
#endif
  uint32_t		nBytes;	/* 16 bits would be sufficient if we ever need the space */
  gex_AM_Arg_t	args[GASNETC_MAX_ARGS];
} gasnetc_medmsg_t;
#define GASNETC_MSG_MED_ARGSEND(nargs) /* Note 8-byte alignment for payload */ \
		GASNETI_ALIGNUP(GASNETC_ARGSEND_AUX(gasnetc_medmsg_t,nargs), 8)
#define GASNETC_MSG_MED_DATA(msg,nargs) \
		((void *)((uintptr_t)(msg) + GASNETC_MSG_MED_ARGSEND(nargs)))

typedef struct {
#if GASNETI_STATS_OR_TRACE
  gasneti_tick_t	stamp;
#endif
  uintptr_t		destLoc;
  int32_t		nBytes;
  gex_AM_Arg_t	args[GASNETC_MAX_ARGS];
} gasnetc_longmsg_t;
#define GASNETC_MSG_LONG_ARGSEND(nargs)  GASNETC_ARGSEND_AUX(gasnetc_longmsg_t,nargs)
#define GASNETC_MSG_LONG_DATA(msg,nargs) (void *)(&msg->longmsg.args[(unsigned int)nargs])

typedef union {
  uint8_t		raw[GASNETC_BUFSZ];
#if GASNETI_STATS_OR_TRACE
  gasneti_tick_t	stamp;
#endif
  gasnetc_shortmsg_t	shortmsg;
  gasnetc_medmsg_t	medmsg;
  gasnetc_longmsg_t	longmsg;
} gasnetc_buffer_t;

/* ------------------------------------------------------------------------------------ */

#if GASNETI_STATS_OR_TRACE
  #define GASNETC_TRACE_WAIT_BEGIN() \
    gasneti_tick_t _waitstart = GASNETI_TICKS_NOW_IFENABLED(C)
#else 
  #define GASNETC_TRACE_WAIT_BEGIN() \
    static char _dummy = (char)sizeof(_dummy)
#endif

#define GASNETC_TRACE_WAIT_END(name) \
  GASNETI_TRACE_EVENT_TIME(C,name,gasneti_ticks_now() - _waitstart)

#define GASNETC_STAT_EVENT(name) \
  _GASNETI_STAT_EVENT(C,name)
#define GASNETC_STAT_EVENT_VAL(name,val) \
  _GASNETI_STAT_EVENT_VAL(C,name,val)

/* ------------------------------------------------------------------------------------ */
/* Configuration */

/* Maximum number of segments to gather on send */
#define GASNETC_SND_SG	4

/* maximum number of ops reaped from the send CQ per poll */
#ifndef GASNETC_SND_REAP_LIMIT
  #define GASNETC_SND_REAP_LIMIT	32
#endif

/* maximum number of ops reaped from the recv CQ per poll */
#ifndef GASNETC_RCV_REAP_LIMIT
  #define GASNETC_RCV_REAP_LIMIT	16
#endif

/* Define non-zero if we want to allow the mlock rlimit to bound the
 * amount of memory we will pin. */
#ifndef GASNETC_HONOR_RLIMIT_MEMLOCK
  #define GASNETC_HONOR_RLIMIT_MEMLOCK 1
#endif

/* Use alloca()?  (e.g. to work-around bug 2079) */
#ifdef GASNETI_USE_ALLOCA
  /* Keep defn */
#elif !PLATFORM_COMPILER_PGI
  #define GASNETI_USE_ALLOCA 1
#endif
#if GASNETI_USE_ALLOCA
  #include <alloca.h>
#endif

/* Can one send a 0-byte payload?
 * TODO: autoconf or runtime probe if/when we can determine which systems need this
 */
#define GASNETC_ALLOW_0BYTE_MSG 0

/* Should dynamic connections use TCP-style rtt estimation? */
#ifndef GASNETC_CONN_USE_SRTT
  #define GASNETC_CONN_USE_SRTT 1
#endif

/* ------------------------------------------------------------------------------------ */

/* Semaphore, lifo and atomics wrappers
 *
 * Only for GASNETC_ANY_PAR do we need true atomics.
 * In particular neither PARSYNC nor CONN_THREAD introduce concurrency,
 * but use of "weak" atomics would pay the unnecessary costs for those.
 */
#if GASNETC_ANY_PAR
  #define GASNETC_PARSEQ _PAR
  #define gasnetc_cons_atomic(_id) _CONCAT(gasneti_atomic_,_id)
#else
  #define GASNETC_PARSEQ _SEQ
  #define gasnetc_cons_atomic(_id) _CONCAT(gasneti_nonatomic_,_id)
#endif

#define GASNETC_SEMA_INITIALIZER  GASNETI_CONS_SEMA(GASNETC_PARSEQ,INITIALIZER)
#define gasnetc_sema_t            gasneti_cons_sema(GASNETC_PARSEQ,t)
#define gasnetc_sema_init         gasneti_cons_sema(GASNETC_PARSEQ,init)
#define gasnetc_sema_read         gasneti_cons_sema(GASNETC_PARSEQ,read)
#define gasnetc_sema_up           gasneti_cons_sema(GASNETC_PARSEQ,up)
#define gasnetc_sema_up_n         gasneti_cons_sema(GASNETC_PARSEQ,up_n)
#define gasnetc_sema_trydown      gasneti_cons_sema(GASNETC_PARSEQ,trydown)
#define gasnetc_sema_trydown_partial \
                                  gasneti_cons_sema(GASNETC_PARSEQ,trydown_partial)

#define GASNETC_LIFO_INITIALIZER  GASNETI_CONS_LIFO(GASNETC_PARSEQ,INITIALIZER)
#define gasnetc_lifo_head_t       gasneti_cons_lifo(GASNETC_PARSEQ,head_t)
#define gasnetc_lifo_init         gasneti_cons_lifo(GASNETC_PARSEQ,init)
#define gasnetc_lifo_pop          gasneti_cons_lifo(GASNETC_PARSEQ,pop)
#define gasnetc_lifo_push         gasneti_cons_lifo(GASNETC_PARSEQ,push)
#define gasnetc_lifo_push_many    gasneti_cons_lifo(GASNETC_PARSEQ,push_many)
#define gasnetc_lifo_link         gasneti_lifo_link
#define gasnetc_lifo_next         gasneti_lifo_next

typedef gasnetc_cons_atomic(t)            gasnetc_atomic_t;
typedef gasnetc_cons_atomic(val_t)        gasnetc_atomic_val_t;
#define gasnetc_atomic_init               gasnetc_cons_atomic(init)
#define gasnetc_atomic_read               gasnetc_cons_atomic(read)
#define gasnetc_atomic_set                gasnetc_cons_atomic(set)
#define gasnetc_atomic_increment          gasnetc_cons_atomic(increment)
#define gasnetc_atomic_decrement_and_test gasnetc_cons_atomic(decrement_and_test)
#define gasnetc_atomic_compare_and_swap   gasnetc_cons_atomic(compare_and_swap)
#define gasnetc_atomic_swap               gasnetc_cons_atomic(swap)
#define gasnetc_atomic_add                gasnetc_cons_atomic(add)
#define gasnetc_atomic_subtract           gasnetc_cons_atomic(subtract)

/* ------------------------------------------------------------------------------------ */
/* Wrap mmap and munmap so we can do without them if required */

#if HAVE_MMAP
  #include <sys/mman.h> /* For MAP_FAILED */
  #define GASNETC_MMAP_FAILED        MAP_FAILED
  #define gasnetc_mmap(_size)        gasneti_mmap(_size)
  #define gasnetc_munmap(_ptr,_size) gasneti_munmap(_ptr,_size)
#else
  #define GASNETI_MMAP_GRANULARITY   (((size_t)1)<<22)  /* 4 MB */
  #define GASNETI_MMAP_LIMIT         (((size_t)1)<<31)  /* 2 GB */
  #define GASNETC_MMAP_FAILED        NULL
  extern int gasnetc_munmap(void *addr, size_t size); 
  GASNETI_INLINE(gasnetc_mmap)
  void *gasnetc_mmap(size_t size) {
    void *result;
    return posix_memalign(&result, GASNET_PAGESIZE, size) ? GASNETC_MMAP_FAILED : result;
  }
#endif

/* ------------------------------------------------------------------------------------ */
/* Type and ops for rdma counters */

typedef struct {
    gasnetc_atomic_t     completed;
    gasnetc_atomic_val_t initiated;
} gasnetc_counter_t;

#define GASNETC_COUNTER_INITIALIZER   {gasnetc_atomic_init(0), 0}

#define gasnetc_counter_init(P)       do { gasnetc_atomic_set(&(P)->completed, 0, 0); \
                                           (P)->initiated = 0;                         \
                                      } while (0)
#define gasnetc_counter_done(P)       (((P)->initiated & GASNETI_ATOMIC_MAX) == \
                                           gasnetc_atomic_read(&(P)->completed, 0))

#define gasnetc_counter_inc(P)		do { (P)->initiated++; } while (0)
#define gasnetc_counter_inc_by(P,v)	do { (P)->initiated += (v); } while (0)
#define gasnetc_counter_inc_if(P)	do { if(P) gasnetc_counter_inc(P); } while (0)
#define gasnetc_counter_inc_if_pf(P)	do { if_pf(P) gasnetc_counter_inc(P); } while (0)
#define gasnetc_counter_inc_if_pt(P)	do { if_pt(P) gasnetc_counter_inc(P); } while (0)

#define gasnetc_counter_dec(P)		gasnetc_atomic_increment(&(P)->completed, 0)
#define gasnetc_counter_dec_by(P,v)	gasnetc_atomic_add(&(P)->completed,(v),0)
#define gasnetc_counter_dec_if(P)	do { if(P) gasnetc_counter_dec(P); } while (0)
#define gasnetc_counter_dec_if_pf(P)	do { if_pf(P) gasnetc_counter_dec(P); } while (0)
#define gasnetc_counter_dec_if_pt(P)	do { if_pt(P) gasnetc_counter_dec(P); } while (0)

/* Wait until given counter is marked as done.
 * Note that no AMPoll is done in the best case.
 */
extern void gasnetc_counter_wait_aux(gasnetc_counter_t *counter, int handler_context GASNETI_THREAD_FARG);
GASNETI_INLINE(gasnetc_counter_wait)
void gasnetc_counter_wait(gasnetc_counter_t *counter, int handler_context GASNETI_THREAD_FARG) {
  if_pf (!gasnetc_counter_done(counter)) {
    gasnetc_counter_wait_aux(counter, handler_context GASNETI_THREAD_PASS);
  }
  gasneti_sync_reads();
} 

/* ------------------------------------------------------------------------------------ */

#if (GASNETC_IB_MAX_HCAS > 1)
  #define GASNETC_FOR_ALL_HCA_INDEX(h)	for (h = 0; h < gasnetc_num_hcas; ++h)
  #define GASNETC_FOR_ALL_HCA(p)	for (p = &gasnetc_hca[0]; p < &gasnetc_hca[gasnetc_num_hcas]; ++p)
#else
  #define GASNETC_FOR_ALL_HCA_INDEX(h)	for (h = 0; h < 1; ++h)
  #define GASNETC_FOR_ALL_HCA(p)	for (p = &gasnetc_hca[0]; p < &gasnetc_hca[1]; ++p)
#endif

/* ------------------------------------------------------------------------------------ */

/* Description of a pre-pinned memory region */
typedef struct {
  struct ibv_mr *	handle;	/* used to release or modify the region */
  uintptr_t		addr;
  size_t		len;
} gasnetc_memreg_t;

typedef struct {
	/* Length excludes immediate data but zeros includes it */
	int16_t		length;	
	int16_t		length_again;
	int16_t		zeros;
	int16_t		zeros_again;
	/* Immediate data that IB would otherwise send in its own header */
	uint32_t	immediate_data;
} gasnetc_amrdma_hdr_t;

/* GASNETC_AMRDMA_SZ must a power-of-2, and GASNETC_AMRDMA_SZ_LG2 its base-2 logarithm.
 * GASNETC_AMRDMA_SZ can safely be smaller or larger than GASNETC_BUFSZ.
 * However space is wasted if larger than 2^ceil(log_2(GASNETC_BUFSZ)).
 */
#if defined(GASNETC_AMRDMA_SZ)
  /* Keep existing defn */
#elif (GASNETC_BUFSZ >  2048)
  #define GASNETC_AMRDMA_SZ     4096
  #define GASNETC_AMRDMA_SZ_LG2 12
#elif (GASNETC_BUFSZ >  1024)
  #define GASNETC_AMRDMA_SZ     2048
  #define GASNETC_AMRDMA_SZ_LG2 11
#else /* GASNETC_BUFSZ is never less than 512 */
  #define GASNETC_AMRDMA_SZ     1024
  #define GASNETC_AMRDMA_SZ_LG2 10
#endif
#define GASNETC_AMRDMA_HDRSZ    sizeof(gasnetc_amrdma_hdr_t)
#define GASNETC_AMRDMA_PAD      (GASNETI_ALIGNUP(GASNETC_AMRDMA_HDRSZ,SIZEOF_VOID_P) - GASNETC_AMRDMA_HDRSZ)
#define GASNETC_AMRDMA_LIMIT_MAX (MIN(GASNETC_BUFSZ,GASNETC_AMRDMA_SZ) - GASNETC_AMRDMA_HDRSZ - GASNETC_AMRDMA_PAD)
typedef char gasnetc_amrdma_buf_t[GASNETC_AMRDMA_SZ];

#define GASNETC_DEFAULT_AMRDMA_MAX_PEERS 32
#define GASNETC_AMRDMA_DEPTH_MAX	32	/* Power-of-2 <= 32 */
#define GASNETC_DEFAULT_AMRDMA_DEPTH	16
#define GASNETC_DEFAULT_AMRDMA_LIMIT	GASNETC_AMRDMA_LIMIT_MAX
#define GASNETC_DEFAULT_AMRDMA_CYCLE	1024	/* 2^i, Number of AM rcvs before hot-peer heuristic */

#if GASNETI_CONDUIT_THREADS
  typedef struct {
    /* Initialized by create_cq or spawn_progress_thread: */
    pthread_t               thread_id;
    uint64_t                prev_time;
    uint64_t                min_ns;
    struct ibv_cq *         cq;
    struct ibv_comp_channel *compl;
    volatile int            done;
    /* Initialized by client: */
    void                    (*fn)(struct ibv_wc *, void *);
    void                    *fn_arg;
  } gasnetc_progress_thread_t;
#else
  typedef void gasnetc_progress_thread_t;
#endif

/*
 * gasnetc_epid_d is node and qp encoded together
 * passing just a node (the default) means any qp to that node
 */
typedef uint32_t gasnetc_epid_t;

/* Forward decl */
struct gasnetc_cep_t_;
typedef struct gasnetc_cep_t_ gasnetc_cep_t;

/* Struct for assignment of AMRDMA peers */
typedef struct gasnetc_amrdma_balance_tbl_t_ {
  gasnetc_atomic_val_t	count;
  gasnetc_cep_t		*cep;
} gasnetc_amrdma_balance_tbl_t;

/* Structure for an HCA */
typedef struct {
  struct ibv_context *	handle;
  gasnetc_memreg_t	rcv_reg;
  gasnetc_memreg_t	snd_reg;
  gasnetc_memreg_t      aux_reg;
#if GASNETC_PIN_SEGMENT
  uint32_t        *seg_lkeys;
  uint32_t	*rkeys;	/* RKey(s) registered at attach time */
  #if GASNETC_IBV_SHUTDOWN
    gasnetc_memreg_t    *seg_regs;
  #endif
#endif
  uint32_t              *aux_rkeys;
#if GASNETC_IBV_SRQ
  struct ibv_srq	*rqst_srq;
  struct ibv_srq	*repl_srq;
  gasnetc_sema_t	am_sema;
#endif
  gasnetc_sema_t	*snd_cq_sema_p;
#if GASNETC_IBV_XRC
  gasnetc_xrcd_t *xrc_domain;
#endif
  struct ibv_cq *	rcv_cq;
  struct ibv_cq *	snd_cq; /* Includes Reply AMs when SRQ in use */
  struct ibv_pd *	pd;
  int			hca_index;
  const char		*hca_id;
  struct ibv_device_attr	hca_cap;
  int			qps; /* qps per peer */
  int			max_qps; /* maximum total over all peers */
  int			num_qps; /* current total over all peers */

  gasnetc_cep_t		**cep; /* array of ptrs to all ceps */

  void			*rbufs;
  gasnetc_lifo_head_t	rbuf_freelist;

#if GASNETC_USE_RCV_THREAD
  /* Rcv thread */
  gasnetc_progress_thread_t rcv_thread;
  struct gasnetc_rbuf_s     *rcv_thread_priv;
 #if GASNETI_THREADINFO_OPT
  gasnet_threadinfo_t       rcv_threadinfo;
 #endif
#endif

  /* AM-over-RMDA */
  gasnetc_memreg_t	amrdma_reg;
  gasnetc_lifo_head_t	amrdma_freelist;
  struct {
    gasnetc_atomic_val_t max_peers;
    gasnetc_atomic_t	count;
    gasnetc_cep_t	**cep;
    volatile int        prev;
  }	  amrdma_rcv;
  struct {
    gasnetc_atomic_t		count;
    gasnetc_atomic_val_t	mask;
    gasnetc_atomic_t		state;
    gasnetc_atomic_val_t	floor;
    gasnetc_amrdma_balance_tbl_t *table;
  }	  amrdma_balance;
} gasnetc_hca_t;

/* Structure for AM-over-RDMA sender state */
typedef struct {
  gasnetc_atomic_t	head, tail;
  uint32_t	rkey;
  uintptr_t		addr;	/* write ONCE */
} gasnetc_amrdma_send_t;

/* Structure for AM-over-RDMA receiver state */
typedef struct {
  gasnetc_amrdma_buf_t	*addr;	/* write ONCE */
  gasnetc_atomic_t	head;
#if GASNETC_ANY_PAR
  gasnetc_atomic_val_t tail;
  gasneti_mutex_t	ack_lock;
  uint32_t		ack_bits;
  char			_pad[GASNETI_CACHE_LINE_BYTES];
  union {
    gasnetc_atomic_t        spinlock;
    char		    _pad[GASNETI_CACHE_LINE_BYTES];
  }			busy[GASNETC_AMRDMA_DEPTH_MAX]; /* A weak spinlock array */
#endif
} gasnetc_amrdma_recv_t;

/* Structure for a cep (connection end-point) */
struct gasnetc_cep_t_ {
  /* Read/write fields */
  int                   used;           /* boolean - true if cep has sent traffic */
  gasnetc_sema_t	am_rem;		/* control in-flight AM Requests (remote rcv queue slots)*/
  gasnetc_sema_t	am_loc;		/* control unmatched rcv buffers (local rcv queue slots) */
  gasnetc_sema_t	*snd_cq_sema_p;	/* control in-flight ops (send completion queue slots) */
  gasnetc_sema_t	*sq_sema_p;	/* Pointer to a sq_sema */
  /* XXX: The atomics in the next 2 structs really should get padded to full cache lines */
  struct {	/* AM flow control coallescing */
  	gasnetc_atomic_t    credit;
	gasnetc_atomic_t    ack;
  } am_flow;
  /* AM-over-RDMA local state */
  gasnetc_atomic_t	amrdma_eligable;	/* Number of AMs small enough for AMRDMA */
  gasnetc_amrdma_send_t *amrdma_send;
  gasnetc_amrdma_recv_t *amrdma_recv;

#if GASNETI_THREADS
  char			_pad1[GASNETI_CACHE_LINE_BYTES];
#endif

  /* Read-only fields - many duplicated from fields in cep->hca */
#if GASNETC_PIN_SEGMENT
  uint32_t      *rkeys;	/* RKey(s) registered at attach time */
#endif
#if GASNETC_PIN_SEGMENT && (GASNETC_IB_MAX_HCAS > 1)
  uint32_t      *seg_lkeys;
#endif
#if (GASNETC_IB_MAX_HCAS > 1)
  uint32_t      rcv_lkey;
  uint32_t      snd_lkey;
#endif
  gasnetc_lifo_head_t	*rbuf_freelist;	/* Source of rcv buffers for AMs */
  gasnetc_hca_t		*hca;
  struct ibv_qp *       qp_handle;
#if (GASNETC_IB_MAX_HCAS > 1)
  int			hca_index;
#endif
  gasnetc_epid_t	epid;		/* == uint32_t */
#if GASNETC_IBV_SRQ
  struct ibv_srq        *srq;
  uint32_t              rcv_qpn;
#endif
#if GASNETC_IBV_XRC_OFED
  struct ibv_qp         *rcv_qp;
#endif
#if GASNETC_IBV_XRC
  uint32_t		xrc_remote_srq_num;
#endif

#if GASNETI_THREADS
  char			_pad2[GASNETI_CACHE_LINE_BYTES];
#endif
};

/* Info used while probing for HCAs/ports */
typedef struct {
  int                   hca_index;      /* Slot in gasnetc_hca[] */
  uint8_t               port_num;       /* Port number */
  struct                ibv_port_attr    port;           /* Port info */
  int                   rd_atom;
  uint16_t             *remote_lids;
} gasnetc_port_info_t;

// Conduit-specific EP type
typedef struct gasnetc_EP_t_ {
  GASNETI_EP_COMMON // conduit-indep part as prefix

  // Per-EP resources will move here from gasnetc_hca_t
} *gasnetc_EP_t;
extern gasnetc_EP_t gasnetc_ep0;

/* Routines in gasnet_core_connect.c */
#if GASNETC_IBV_XRC
extern int gasnetc_xrc_init(void **shared_mem_p);
#endif
extern int gasnetc_connect_init(void);
extern int gasnetc_connect_fini(void);
#if GASNETC_DYNAMIC_CONNECT
extern gasnetc_cep_t *gasnetc_connect_to(gex_Rank_t node);
extern void gasnetc_conn_implied_ack(gex_Rank_t node);
extern void gasnetc_conn_rcv_wc(struct ibv_wc *comp);
extern void gasnetc_conn_snd_wc(struct ibv_wc *comp);
#endif

/* Callback functions in gasnet_core_sndrcv.c */
typedef void (*gasnetc_cb_t)(gasnetc_atomic_val_t *);
 /* eop: */
extern void gasnetc_cb_eop_alc(gasnetc_atomic_val_t *);
extern void gasnetc_cb_eop_put(gasnetc_atomic_val_t *);
extern void gasnetc_cb_eop_get(gasnetc_atomic_val_t *);
 /* iop within nbi-accessregion: */
extern void gasnetc_cb_nar_alc(gasnetc_atomic_val_t *);
extern void gasnetc_cb_nar_put(gasnetc_atomic_val_t *);
extern void gasnetc_cb_nar_get(gasnetc_atomic_val_t *);
 /* iop not in nbi-accessregion: */
extern void gasnetc_cb_iop_alc(gasnetc_atomic_val_t *);
extern void gasnetc_cb_iop_put(gasnetc_atomic_val_t *);
extern void gasnetc_cb_iop_get(gasnetc_atomic_val_t *);
 /* gasnetc_counter_t: */
extern void gasnetc_cb_counter(gasnetc_atomic_val_t *);
extern void gasnetc_cb_counter_rel(gasnetc_atomic_val_t *);

/* Routines in gasnet_core_sndrcv.c */
extern gex_Rank_t gasnetc_msgsource(gex_Token_t token);
extern int gasnetc_create_cq(struct ibv_context *, int,
                             struct ibv_cq * *, int *,
                             gasnetc_progress_thread_t *);
extern int gasnetc_sndrcv_limits(void);
extern int gasnetc_sndrcv_init(gasnetc_EP_t);
extern void gasnetc_sys_flush_reph(gex_Token_t, gex_AM_Arg_t);
extern void gasnetc_sys_close_reqh(gex_Token_t);
extern void gasnetc_sndrcv_quiesce(void);
extern int gasnetc_sndrcv_shutdown(void);
extern void gasnetc_sndrcv_init_peer(gex_Rank_t node, gasnetc_cep_t *cep);
extern void gasnetc_sndrcv_init_inline(void);
extern void gasnetc_sndrcv_attach_peer(gex_Rank_t node, gasnetc_cep_t *cep);
extern void gasnetc_sndrcv_start_thread(void);
extern void gasnetc_sndrcv_stop_thread(int block);
extern gasnetc_amrdma_send_t *gasnetc_amrdma_send_alloc(uint32_t rkey, void *addr);
extern gasnetc_amrdma_recv_t *gasnetc_amrdma_recv_alloc(gasnetc_hca_t *hca);
extern void gasnetc_sndrcv_poll(int handler_context);
#if GASNETC_PIN_SEGMENT
  extern int gasnetc_rdma_put(
                  gasnetc_epid_t epid,
                  void *src_ptr, void *dst_ptr, size_t nbytes, gex_Flags_t flags,
                  gasnetc_atomic_val_t *local_cnt, gasnetc_cb_t local_cb,
                  gasnetc_atomic_val_t *remote_cnt, gasnetc_cb_t remote_cb
                  GASNETI_THREAD_FARG);
#else
  extern int gasnetc_rdma_put_fh(
                  gasnetc_epid_t epid,
                  void *src_ptr, void *dst_ptr, size_t nbytes, gex_Flags_t flags,
                  gasnetc_atomic_val_t *local_cnt, gasnetc_cb_t local_cb,
                  gasnetc_atomic_val_t *remote_cnt, gasnetc_cb_t remote_cb,
                  gasnetc_counter_t *am_oust
                  GASNETI_THREAD_FARG);
  GASNETI_INLINE(gasnetc_rdma_put)
  int gasnetc_rdma_put(
                  gasnetc_epid_t epid,
                  void *src_ptr, void *dst_ptr, size_t nbytes, gex_Flags_t flags,
                  gasnetc_atomic_val_t *local_cnt, gasnetc_cb_t local_cb,
                  gasnetc_atomic_val_t *remote_cnt, gasnetc_cb_t remote_cb
                  GASNETI_THREAD_FARG)
  {
    return gasnetc_rdma_put_fh(epid,src_ptr,dst_ptr,nbytes,flags,
                               local_cnt,local_cb,remote_cnt,remote_cb,
                               NULL GASNETI_THREAD_PASS);
  }
#endif
extern int gasnetc_rdma_get(
                  gasnetc_epid_t epid,
                  void *src_ptr, void *dst_ptr, size_t nbytes, gex_Flags_t flags,
                  gasnetc_atomic_val_t *remote_cnt, gasnetc_cb_t remote_cb
                  GASNETI_THREAD_FARG);

/* Routines in gasnet_core_thread.c */
#if GASNETI_CONDUIT_THREADS
extern void gasnetc_spawn_progress_thread(gasnetc_progress_thread_t *pthr);
extern void gasnetc_stop_progress_thread(gasnetc_progress_thread_t *pthr, int block);
#endif

/* General routines in gasnet_core.c */
extern int gasnetc_pin(gasnetc_hca_t *hca, void *addr, size_t size, enum ibv_access_flags acl, gasnetc_memreg_t *reg);
extern void gasnetc_unpin(gasnetc_hca_t *hca, gasnetc_memreg_t *reg);
#define gasnetc_unmap(reg)	gasnetc_munmap((void *)((reg)->addr), (reg)->len)

/* Bootstrap support */
extern gasneti_spawnerfn_t const *gasneti_spawner;

/* This indirection allows us to drop-in a native implementation after init */
extern void gasneti_bootstrapBarrier(void);
extern void gasneti_bootstrapExchange(void *src, size_t len, void *dest);
#define gasneti_bootstrapBroadcast      (*(gasneti_spawner->Broadcast))
#define gasneti_bootstrapSNodeBroadcast (*(gasneti_spawner->SNodeBroadcast))
#define gasneti_bootstrapAlltoall       (*(gasneti_spawner->Alltoall))
#define gasneti_bootstrapAbort          (*(gasneti_spawner->Abort))
#define gasneti_bootstrapCleanup        (*(gasneti_spawner->Cleanup))
#define gasneti_bootstrapFini           (*(gasneti_spawner->Fini))

/* Global configuration variables */
extern int		gasnetc_op_oust_limit;
extern int		gasnetc_op_oust_pp;
extern int		gasnetc_am_oust_limit;
extern int		gasnetc_am_oust_pp;
extern int		gasnetc_bbuf_limit;
#if GASNETC_DYNAMIC_CONNECT
  extern int		gasnetc_ud_rcvs;
  extern int		gasnetc_ud_snds;
#else
  #define		gasnetc_ud_rcvs 0
  #define		gasnetc_ud_snds 0
#endif
extern int		gasnetc_use_rcv_thread;
extern int		gasnetc_am_credits_slack;
extern int		gasnetc_alloc_qps;    /* Number of QPs per node in gasnetc_ceps[] */
extern int		gasnetc_num_qps;      /* How many QPs to use per peer */
extern size_t		gasnetc_packedlong_limit;
extern size_t		gasnetc_inline_limit;
extern size_t		gasnetc_bounce_limit;
#if !GASNETC_PIN_SEGMENT
  extern size_t		gasnetc_putinmove_limit;
#endif
#if GASNETC_FH_OPTIONAL
  #define GASNETC_USE_FIREHOSE	GASNETT_PREDICT_TRUE(gasnetc_use_firehose)
  extern int		gasnetc_use_firehose;
#else
  #define GASNETC_USE_FIREHOSE	1
#endif
extern enum ibv_mtu    gasnetc_max_mtu;
extern int              gasnetc_qp_timeout;
extern int              gasnetc_qp_retry_count;
extern int		gasnetc_amrdma_max_peers;
extern size_t		gasnetc_amrdma_limit;
extern int		gasnetc_amrdma_depth;
extern int		gasnetc_amrdma_slot_mask;
extern gasnetc_atomic_val_t gasnetc_amrdma_cycle;

#if GASNETC_IBV_SRQ
  extern int			gasnetc_rbuf_limit;
  extern int			gasnetc_rbuf_set;
  extern int			gasnetc_use_srq;
#else
  #define gasnetc_use_srq	0
#endif

#if GASNETC_IBV_XRC
  extern int			gasnetc_use_xrc;
#else
  #define gasnetc_use_xrc	0
#endif

/* Global variables */
extern int		gasnetc_num_hcas;
extern gasnetc_hca_t	gasnetc_hca[GASNETC_IB_MAX_HCAS];
extern uintptr_t	gasnetc_max_msg_sz;
#if GASNETC_PIN_SEGMENT
  extern int			gasnetc_max_regs; /* max of length of seg_lkeys array over all nodes */
  extern uintptr_t		gasnetc_seg_start;
  extern uintptr_t		gasnetc_seg_len;
  extern uint64_t		gasnetc_pin_maxsz;
  extern uint64_t		gasnetc_pin_maxsz_mask;
  extern unsigned int		gasnetc_pin_maxsz_shift;
#endif
extern size_t			gasnetc_fh_align;
extern size_t			gasnetc_fh_align_mask;
extern firehose_info_t		gasnetc_firehose_info;
extern gasnetc_port_info_t      *gasnetc_port_tbl;
extern int                      gasnetc_num_ports;
extern gasnetc_cep_t            **gasnetc_node2cep;
extern gex_Rank_t            gasnetc_remote_nodes;
#if GASNETC_DYNAMIC_CONNECT
  extern gasnetc_sema_t         gasnetc_zero_sema;
#endif


/* ------------------------------------------------------------------------------------ */
/* To return failures internally - suited for both integer and pointer returns
 * We distinguish only 2 cases - temporary lack of resources or permanent failure
 */

#define GASNETC_OK       0
#define GASNETC_FAIL_IMM 1
#define GASNETC_FAIL_ERR 2

// To test a pointer value that could be valid, NULL or one of these two "FAIL" codes
#define GASNETC_FAILED_PTR(p) ((uintptr_t)(p)&0x3)


/* ------------------------------------------------------------------------------------ */
/* System AM Request/Reply Functions
 * These can be called between init and attach.
 * They have an optional counter allowing one to test/block for local completion,
 * and take an epid a dest to optionally allow selection of a specific QP.
 */

extern int gasnetc_RequestSysShort(gasnetc_epid_t dest,
                                   gasnetc_counter_t *counter, /* counter for local completion */
                                   gex_AM_Index_t handler,
                                   int numargs, ...);
extern int gasnetc_RequestSysMedium(gasnetc_epid_t dest,
                                    gasnetc_counter_t *counter, /* counter for local completion */
                                    gex_AM_Index_t handler,
                                    void *source_addr, size_t nbytes,
                                    int numargs, ...);

extern int gasnetc_ReplySysShort(gex_Token_t token,
                                 gasnetc_counter_t *counter, /* counter for local completion */
                                 gex_AM_Index_t handler,
                                 int numargs, ...);
extern int gasnetc_ReplySysMedium(gex_Token_t token,
                                  gasnetc_counter_t *counter, /* counter for local completion */
                                  gex_AM_Index_t handler,
                                  void *source_addr, size_t nbytes,
                                  int numargs, ...);

#endif
