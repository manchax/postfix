/*++
/* NAME
/*	tls_proxy_client_print 3
/* SUMMARY
/*	write TLS_CLIENT_XXX structures to stream
/* SYNOPSIS
/*	#include <tls_proxy.h>
/*
/*	int     tls_proxy_client_init_print(print_fn, stream, flags, ptr)
/*	ATTR_PRINT_MASTER_FN print_fn;
/*	VSTREAM *stream;
/*	int     flags;
/*	void    *ptr;
/*
/*	int     tls_proxy_client_start_print(print_fn, stream, flags, ptr)
/*	ATTR_PRINT_MASTER_FN print_fn;
/*	VSTREAM *stream;
/*	int     flags;
/*	void    *ptr;
/* DESCRIPTION
/*	tls_proxy_client_init_print() writes a full TLS_CLIENT_INIT_PROPS
/*	structure to the named stream using the specified attribute
/*	print routine. tls_proxy_client_init_print() is meant to
/*	be passed as a call-back to attr_print(), thusly:
/*
/*	SEND_ATTR_FUNC(tls_proxy_client_init_print, (void *) init_props), ...
/*
/*	tls_proxy_client_start_print() writes a TLS_CLIENT_START_PROPS
/*	structure, without stream or file descriptor members, to
/*	the named stream using the specified attribute print routine.
/*	tls_proxy_client_start_print() is meant to be passed as a
/*	call-back to attr_print(), thusly:
/*
/*	SEND_ATTR_FUNC(tls_proxy_client_start_print, (void *) start_props), ...
/* DIAGNOSTICS
/*	Fatal: out of memory.
/* LICENSE
/* .ad
/* .fi
/*	The Secure Mailer license must be distributed with this software.
/* AUTHOR(S)
/*	Wietse Venema
/*	Google, Inc.
/*	111 8th Avenue
/*	New York, NY 10011, USA
/*--*/

#ifdef USE_TLS

/* System library. */

#include <sys_defs.h>

/* Utility library */

#include <argv_attr.h>
#include <attr.h>
#include <msg.h>

/* TLS library. */

#include <tls.h>
#include <tls_proxy.h>


#define STR(x) vstring_str(x)
#define LEN(x) VSTRING_LEN(x)

/* tls_proxy_client_init_print - send TLS_CLIENT_INIT_PROPS over stream */

int     tls_proxy_client_init_print(ATTR_PRINT_MASTER_FN print_fn, VSTREAM *fp,
				            int flags, void *ptr)
{
    TLS_CLIENT_INIT_PROPS *props = (TLS_CLIENT_INIT_PROPS *) ptr;
    int     ret;

    if (msg_verbose)
	msg_info("begin tls_proxy_client_init_print");

#define STRING_OR_EMPTY(s) ((s) ? (s) : "")

    ret = print_fn(fp, flags | ATTR_FLAG_MORE,
		   SEND_ATTR_STR(TLS_ATTR_LOG_PARAM,
				 STRING_OR_EMPTY(props->log_param)),
		   SEND_ATTR_STR(TLS_ATTR_LOG_LEVEL,
				 STRING_OR_EMPTY(props->log_level)),
		   SEND_ATTR_INT(TLS_ATTR_VERIFYDEPTH, props->verifydepth),
		   SEND_ATTR_STR(TLS_ATTR_CACHE_TYPE,
				 STRING_OR_EMPTY(props->cache_type)),
		   SEND_ATTR_STR(TLS_ATTR_CERT_FILE,
				 STRING_OR_EMPTY(props->cert_file)),
		   SEND_ATTR_STR(TLS_ATTR_KEY_FILE,
				 STRING_OR_EMPTY(props->key_file)),
		   SEND_ATTR_STR(TLS_ATTR_DCERT_FILE,
				 STRING_OR_EMPTY(props->dcert_file)),
		   SEND_ATTR_STR(TLS_ATTR_DKEY_FILE,
				 STRING_OR_EMPTY(props->dkey_file)),
		   SEND_ATTR_STR(TLS_ATTR_ECCERT_FILE,
				 STRING_OR_EMPTY(props->eccert_file)),
		   SEND_ATTR_STR(TLS_ATTR_ECKEY_FILE,
				 STRING_OR_EMPTY(props->eckey_file)),
		   SEND_ATTR_STR(TLS_ATTR_CAFILE,
				 STRING_OR_EMPTY(props->CAfile)),
		   SEND_ATTR_STR(TLS_ATTR_CAPATH,
				 STRING_OR_EMPTY(props->CApath)),
		   SEND_ATTR_STR(TLS_ATTR_MDALG,
				 STRING_OR_EMPTY(props->mdalg)),
		   ATTR_TYPE_END);
    /* Do not flush the stream. */
    if (msg_verbose)
	msg_info("tls_proxy_client_init_print ret=%d", ret);
    return (ret);
}

/* tls_proxy_client_certs_print - send x509 certificates over stream */

static int tls_proxy_client_certs_print(ATTR_PRINT_MASTER_FN print_fn,
				          VSTREAM *fp, int flags, void *ptr)
{
    TLS_CERTS *tls_certs = (TLS_CERTS *) ptr;
    TLS_CERTS *tp;
    int     count;
    int     ret;

    for (tp = tls_certs, count = 0; tp != 0; tp = tp->next, count++)
	 /* void */ ;
    if (msg_verbose)
	msg_info("tls_proxy_client_certs_print count=%d", count);

    ret = print_fn(fp, flags | ATTR_FLAG_MORE,
		   SEND_ATTR_INT(TLS_ATTR_COUNT, count),
		   ATTR_TYPE_END);

    if (ret == 0 && count > 0) {
	VSTRING *buf = vstring_alloc(100);
	int     n;

	for (tp = tls_certs, n = 0; ret == 0 && n < count; tp = tp->next, n++) {
	    size_t  len = i2d_X509(tp->cert, (unsigned char **) 0);
	    unsigned char *bp;

	    VSTRING_RESET(buf);
	    VSTRING_SPACE(buf, len);
	    bp = (unsigned char *) STR(buf);
	    i2d_X509(tp->cert, &bp);
	    if ((char *) bp - STR(buf) != len)
		msg_panic("i2d_X509 failed to encode certificate");
	    ret = print_fn(fp, flags | ATTR_FLAG_MORE,
			   SEND_ATTR_DATA(TLS_ATTR_CERT, LEN(buf), STR(buf)),
			   ATTR_TYPE_END);
	}
	vstring_free(buf);
    }
    /* Do not flush the stream. */
    if (msg_verbose)
	msg_info("tls_proxy_client_certs_print ret=%d", count);
    return (ret);
}

/* tls_proxy_client_pkeys_print - send public keys over stream */

static int tls_proxy_client_pkeys_print(ATTR_PRINT_MASTER_FN print_fn,
				          VSTREAM *fp, int flags, void *ptr)
{
    TLS_PKEYS *tls_pkeys = (TLS_PKEYS *) ptr;
    TLS_PKEYS *tp;
    int     count;
    int     ret;

    for (tp = tls_pkeys, count = 0; tp != 0; tp = tp->next, count++)
	 /* void */ ;
    if (msg_verbose)
	msg_info("tls_proxy_client_pkeys_print count=%d", count);

    ret = print_fn(fp, flags | ATTR_FLAG_MORE,
		   SEND_ATTR_INT(TLS_ATTR_COUNT, count),
		   ATTR_TYPE_END);

    if (ret == 0 && count > 0) {
	VSTRING *buf = vstring_alloc(100);
	int     n;

	for (tp = tls_pkeys, n = 0; ret == 0 && n < count; tp = tp->next, n++) {
	    size_t  len = i2d_PUBKEY(tp->pkey, (unsigned char **) 0);
	    unsigned char *bp;

	    VSTRING_RESET(buf);
	    VSTRING_SPACE(buf, len);
	    bp = (unsigned char *) STR(buf);
	    i2d_PUBKEY(tp->pkey, &bp);
	    if ((char *) bp - STR(buf) != len)
		msg_panic("i2d_PUBKEY failed to encode public key");
	    ret = print_fn(fp, flags | ATTR_FLAG_MORE,
			   SEND_ATTR_DATA(TLS_ATTR_PKEY, LEN(buf), STR(buf)),
			   ATTR_TYPE_END);
	}
	vstring_free(buf);
    }
    /* Do not flush the stream. */
    if (msg_verbose)
	msg_info("tls_proxy_client_pkeys_print ret=%d", count);
    return (ret);
}

/* tls_proxy_client_tlsa_print - send TLS_TLSA over stream */

static int tls_proxy_client_tlsa_print(ATTR_PRINT_MASTER_FN print_fn,
				          VSTREAM *fp, int flags, void *ptr)
{
    TLS_TLSA *tls_tlsa = (TLS_TLSA *) ptr;
    TLS_TLSA *tp;
    int     count;
    int     ret;

    for (tp = tls_tlsa, count = 0; tp != 0; tp = tp->next, count++)
	 /* void */ ;
    if (msg_verbose)
	msg_info("tls_proxy_client_tlsa_print count=%d", count);

    ret = print_fn(fp, flags | ATTR_FLAG_MORE,
		   SEND_ATTR_INT(TLS_ATTR_COUNT, count),
		   ATTR_TYPE_END);

    if (ret == 0 && count > 0) {
	int     n;

	for (tp = tls_tlsa, n = 0; ret == 0 && n < count; tp = tp->next, n++) {
	    ret = print_fn(fp, flags | ATTR_FLAG_MORE,
			   SEND_ATTR_STR(TLS_ATTR_MDALG, tp->mdalg),
			   SEND_ATTR_FUNC(argv_attr_print,
					  (void *) tp->certs),
			   SEND_ATTR_FUNC(argv_attr_print,
					  (void *) tp->pkeys),
			   ATTR_TYPE_END);
	}
    }
    /* Do not flush the stream. */
    if (msg_verbose)
	msg_info("tls_proxy_client_tlsa_print ret=%d", count);
    return (ret);
}

/* tls_proxy_client_dane_print - send TLS_DANE over stream */

static int tls_proxy_client_dane_print(ATTR_PRINT_MASTER_FN print_fn,
				          VSTREAM *fp, int flags, void *ptr)
{
    TLS_DANE *dane = (TLS_DANE *) ptr;
    int     ret;

    ret = print_fn(fp, flags | ATTR_FLAG_MORE,
		   SEND_ATTR_INT(TLS_ATTR_DANE, dane != 0),
		   ATTR_TYPE_END);
    if (msg_verbose)
	msg_info("tls_proxy_client_dane_print dane=%d", dane != 0);

    if (ret == 0 && dane != 0) {
	ret = print_fn(fp, flags | ATTR_FLAG_MORE,
		       SEND_ATTR_FUNC(tls_proxy_client_tlsa_print,
				      (void *) dane->ta),
		       SEND_ATTR_FUNC(tls_proxy_client_tlsa_print,
				      (void *) dane->ee),
		       SEND_ATTR_FUNC(tls_proxy_client_certs_print,
				      (void *) dane->certs),
		       SEND_ATTR_FUNC(tls_proxy_client_pkeys_print,
				      (void *) dane->pkeys),
		       SEND_ATTR_STR(TLS_ATTR_DOMAIN,
				     STRING_OR_EMPTY(dane->base_domain)),
		       SEND_ATTR_INT(TLS_ATTR_FLAGS, dane->flags),
		       SEND_ATTR_LONG(TLS_ATTR_EXP, dane->expires),
		       ATTR_TYPE_END);
    }
    /* Do not flush the stream. */
    if (msg_verbose)
	msg_info("tls_proxy_client_dane_print ret=%d", ret);
    return (ret);
}

/* tls_proxy_client_start_print - send TLS_CLIENT_START_PROPS over stream */

int     tls_proxy_client_start_print(ATTR_PRINT_MASTER_FN print_fn,
				          VSTREAM *fp, int flags, void *ptr)
{
    TLS_CLIENT_START_PROPS *props = (TLS_CLIENT_START_PROPS *) ptr;
    int     ret;

    if (msg_verbose)
	msg_info("begin tls_proxy_client_start_print");

#define STRING_OR_EMPTY(s) ((s) ? (s) : "")

    ret = print_fn(fp, flags | ATTR_FLAG_MORE,
		   SEND_ATTR_INT(TLS_ATTR_TIMEOUT, props->timeout),
		   SEND_ATTR_INT(TLS_ATTR_TLS_LEVEL, props->tls_level),
		   SEND_ATTR_STR(TLS_ATTR_NEXTHOP,
				 STRING_OR_EMPTY(props->nexthop)),
		   SEND_ATTR_STR(TLS_ATTR_HOST,
				 STRING_OR_EMPTY(props->host)),
		   SEND_ATTR_STR(TLS_ATTR_NAMADDR,
				 STRING_OR_EMPTY(props->namaddr)),
		   SEND_ATTR_STR(TLS_ATTR_SERVERID,
				 STRING_OR_EMPTY(props->serverid)),
		   SEND_ATTR_STR(TLS_ATTR_HELO,
				 STRING_OR_EMPTY(props->helo)),
		   SEND_ATTR_STR(TLS_ATTR_PROTOCOLS,
				 STRING_OR_EMPTY(props->protocols)),
		   SEND_ATTR_STR(TLS_ATTR_CIPHER_GRADE,
				 STRING_OR_EMPTY(props->cipher_grade)),
		   SEND_ATTR_STR(TLS_ATTR_CIPHER_EXCLUSIONS,
				 STRING_OR_EMPTY(props->cipher_exclusions)),
		   SEND_ATTR_FUNC(argv_attr_print,
				  (void *) props->matchargv),
		   SEND_ATTR_STR(TLS_ATTR_MDALG,
				 STRING_OR_EMPTY(props->mdalg)),
		   SEND_ATTR_FUNC(tls_proxy_client_dane_print,
				  (void *) props->dane),
		   ATTR_TYPE_END);
    /* Do not flush the stream. */
    if (msg_verbose)
	msg_info("tls_proxy_client_start_print ret=%d", ret);
    return (ret);
}

#endif
