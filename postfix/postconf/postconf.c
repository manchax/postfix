/*++
/* NAME
/*	postconf 1
/* SUMMARY
/*	Postfix configuration utility
/* SYNOPSIS
/* .fi
/*	\fBpostconf\fR [\fB-d\fR] [\fB-h\fR] [\fB-n\fR] [\fB-v\fR]
/*		[\fIparameter ...\fR]
/* DESCRIPTION
/*	The \fBpostconf\fR command prints the actual value of
/*	\fIparameter\fR (all known parameters by default), one
/*	parameter per line.
/*
/*	Options:
/* .IP \fB-d\fR
/*	Print default parameter settings instead of actual settings.
/* .IP \fB-h\fR
/*	Show parameter values only, not the \fIname =\fR information
/*	that normally precedes the value.
/* .IP \fB-n\fR
/*	Print non-default parameter settings only.
/* .IP \fB-v\fR
/*	Enable verbose mode for debugging purposes. Multiple \fB-v\fR
/*	options make the software increasingly verbose.
/* DIAGNOSTICS
/*	Problems are reported to the standard error stream.
/* LICENSE
/* .ad
/* .fi
/*	The Secure Mailer license must be distributed with this software.
/* AUTHOR(S)
/*	Wietse Venema
/*	IBM T.J. Watson Research
/*	P.O. Box 704
/*	Yorktown Heights, NY 10598, USA
/*--*/

/* System library. */

#include <sys_defs.h>
#include <sys/stat.h>
#include <pwd.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#ifdef STRCASECMP_IN_STRINGS_H
#include <strings.h>
#endif

#ifdef USE_PATHS_H
#include <paths.h>
#endif

/* Utility library. */

#include <msg.h>
#include <vstream.h>
#include <msg_vstream.h>
#include <get_hostname.h>
#include <stringops.h>
#include <htable.h>
#include <dict.h>
#include <safe.h>
#include <mymalloc.h>
#include <line_wrap.h>

/* Global library. */

#include <mynetworks.h>
#include <mail_conf.h>
#include <mail_proto.h>
#include <mail_version.h>
#include <mail_params.h>
#include <mail_addr.h>

 /*
  * What output we should generate.
  */
#define SHOW_NONDEF	(1<<0)		/* show non-default settings */
#define SHOW_DEFS	(1<<1)		/* show default setting */
#define SHOW_NAME	(1<<2)		/* show parameter name */

 /*
  * Lookup table for in-core parameter info.
  */
HTABLE *param_table;

 /*
  * Lookup table for external parameter info.
  */
DICT   *text_table;

 /*
  * Declarations generated by scanning actual C source files.
  */
#include "bool_vars.h"
#include "int_vars.h"
#include "str_vars.h"

 /*
  * Manually extracted.
  */
#include "local_vars.h"
#include "smtp_vars.h"

 /*
  * Lookup tables generated by scanning actual C source files.
  */
static CONFIG_BOOL_TABLE bool_table[] = {
#include "bool_table.h"
    0,
};

static CONFIG_INT_TABLE int_table[] = {
#include "int_table.h"
    0,
};

static CONFIG_STR_TABLE str_table[] = {
#include "str_table.h"
#include "local_table.h"		/* XXX */
#include "smtp_table.h"			/* XXX */
    0,
};

 /*
  * Parameters with default values obtained via function calls.
  */
char   *var_myhostname;
char   *var_mydomain;
char   *var_mynetworks;

static const char *check_myhostname(void);
static const char *check_mydomainname(void);
static const char *check_mynetworks(void);

static CONFIG_STR_FN_TABLE str_fn_table[] = {
    VAR_MYHOSTNAME, check_myhostname, &var_myhostname, 1, 0,
    VAR_MYDOMAIN, check_mydomainname, &var_mydomain, 1, 0,
    0,
};
static CONFIG_STR_FN_TABLE str_fn_table_2[] = {
    VAR_MYNETWORKS, check_mynetworks, &var_mynetworks, 1, 0,
    0,
};

 /*
  * XXX Global so that call-backs can see it.
  */
static int mode = SHOW_NAME;

/* check_myhostname - lookup hostname and validate */

static const char *check_myhostname(void)
{
    const char *name;
    const char *dot;
    const char *domain;

    /*
     * If the local machine name is not in FQDN form, try to append the
     * contents of $mydomain.
     * 
     * XXX Do not complain when running as "postconf -d".
     */
    name = get_hostname();
    if ((mode & SHOW_DEFS) == 0 && (dot = strchr(name, '.')) == 0) {
	if ((domain = mail_conf_lookup_eval(VAR_MYDOMAIN)) == 0)
	    msg_fatal("My hostname %s is not a fully qualified name - set %s or %s in %s/main.cf",
		      name, VAR_MYHOSTNAME, VAR_MYDOMAIN, var_config_dir);
	name = concatenate(name, ".", domain, (char *) 0);
    }
    return (name);
}

/* get_myhostname - look up and store my hostname */

static void get_myhostname(void)
{
    const char *name;

    if ((name = dict_lookup(CONFIG_DICT, VAR_MYHOSTNAME)) == 0)
	name = check_myhostname();
    var_myhostname = mystrdup(name);
}

/* check_mydomainname - lookup domain name and validate */

static const char *check_mydomainname(void)
{
    char   *dot;

    /*
     * Use the hostname when it is not a FQDN ("foo"), or when the hostname
     * actually is a domain name ("foo.com").
     */
    if (var_myhostname == 0)
	get_myhostname();
    if ((dot = strchr(var_myhostname, '.')) == 0 || strchr(dot + 1, '.') == 0)
	return (var_myhostname);
    return (dot + 1);
}

/* check_mynetworks - lookup network address list */

static const char *check_mynetworks(void)
{
    if (var_inet_interfaces == 0)
	var_inet_interfaces = mystrdup(DEF_INET_INTERFACES);
    return (mynetworks());
}

/* read_parameters - read parameter info from file */

static void read_parameters(void)
{
    char   *config_dir;
    char   *path;

    /*
     * A direct rip-off of mail_conf_read(). XXX Avoid code duplication by
     * better code decomposition.
     */
    dict_unknown_allowed = 1;
    if (var_config_dir)
	myfree(var_config_dir);
    var_config_dir = mystrdup((config_dir = safe_getenv(CONF_ENV_PATH)) != 0 ?
			      config_dir : DEF_CONFIG_DIR);	/* XXX */
    set_mail_conf_str(VAR_CONFIG_DIR, var_config_dir);
    path = concatenate(var_config_dir, "/", "main.cf", (char *) 0);
    dict_load_file(CONFIG_DICT, path);
    myfree(path);
}

/* hash_parameters - hash all parameter names so we can find and sort them */

static void hash_parameters(void)
{
    CONFIG_BOOL_TABLE *cbt;
    CONFIG_INT_TABLE *cit;
    CONFIG_STR_TABLE *cst;
    CONFIG_STR_FN_TABLE *csft;

    param_table = htable_create(100);

    for (cbt = bool_table; cbt->name; cbt++)
	htable_enter(param_table, cbt->name, (char *) cbt);
    for (cit = int_table; cit->name; cit++)
	htable_enter(param_table, cit->name, (char *) cit);
    for (cst = str_table; cst->name; cst++)
	htable_enter(param_table, cst->name, (char *) cst);
    for (csft = str_fn_table; csft->name; csft++)
	htable_enter(param_table, csft->name, (char *) csft);
    for (csft = str_fn_table_2; csft->name; csft++)
	htable_enter(param_table, csft->name, (char *) csft);
}

/* show_strval - show string-valued parameter */

static void show_strval(int mode, const char *name, const char *value)
{
    if (mode & SHOW_NAME) {
	vstream_printf("%s = %s\n", name, value);
    } else {
	vstream_printf("%s\n", value);
    }
}

/* show_intval - show integer-valued parameter */

static void show_intval(int mode, const char *name, int value)
{
    if (mode & SHOW_NAME) {
	vstream_printf("%s = %d\n", name, value);
    } else {
	vstream_printf("%d\n", value);
    }
}

/* print_bool - print boolean parameter */

static void print_bool(int mode, CONFIG_BOOL_TABLE *cbt)
{
    const char *value;

    if (mode & SHOW_DEFS) {
	show_strval(mode, cbt->name, cbt->defval ? "yes" : "no");
    } else {
	value = dict_lookup(CONFIG_DICT, cbt->name);
	if ((mode & SHOW_NONDEF) == 0) {
	    if (value == 0) {
		show_strval(mode, cbt->name, cbt->defval ? "yes" : "no");
	    } else {
		show_strval(mode, cbt->name, value);
	    }
	} else {
	    if (value != 0)
		show_strval(mode, cbt->name, value);
	}
    }
}

/* print_int - print integer parameter */

static void print_int(int mode, CONFIG_INT_TABLE *cit)
{
    const char *value;

    if (mode & SHOW_DEFS) {
	show_intval(mode, cit->name, cit->defval);
    } else {
	value = dict_lookup(CONFIG_DICT, cit->name);
	if ((mode & SHOW_NONDEF) == 0) {
	    if (value == 0) {
		show_intval(mode, cit->name, cit->defval);
	    } else {
		show_strval(mode, cit->name, value);
	    }
	} else {
	    if (value != 0)
		show_strval(mode, cit->name, value);
	}
    }
}

/* print_str - print string parameter */

static void print_str(int mode, CONFIG_STR_TABLE *cst)
{
    const char *value;

    if (mode & SHOW_DEFS) {
	show_strval(mode, cst->name, cst->defval);
    } else {
	value = dict_lookup(CONFIG_DICT, cst->name);
	if ((mode & SHOW_NONDEF) == 0) {
	    if (value == 0) {
		show_strval(mode, cst->name, cst->defval);
	    } else {
		show_strval(mode, cst->name, value);
	    }
	} else {
	    if (value != 0)
		show_strval(mode, cst->name, value);
	}
    }
}

/* print_str_fn - print string-function parameter */

static void print_str_fn(int mode, CONFIG_STR_FN_TABLE *csft)
{
    const char *value;

    if (mode & SHOW_DEFS) {
	show_strval(mode, csft->name, csft->defval());
    } else {
	value = dict_lookup(CONFIG_DICT, csft->name);
	if ((mode & SHOW_NONDEF) == 0) {
	    if (value == 0) {
		show_strval(mode, csft->name, csft->defval());
	    } else {
		show_strval(mode, csft->name, value);
	    }
	} else {
	    if (value != 0)
		show_strval(mode, csft->name, value);
	}
    }
}

/* print_str_fn_2 - print string-function parameter */

static void print_str_fn_2(int mode, CONFIG_STR_FN_TABLE *csft)
{
    const char *value;

    if (mode & SHOW_DEFS) {
	show_strval(mode, csft->name, csft->defval());
    } else {
	value = dict_lookup(CONFIG_DICT, csft->name);
	if ((mode & SHOW_NONDEF) == 0) {
	    if (value == 0) {
		show_strval(mode, csft->name, csft->defval());
	    } else {
		show_strval(mode, csft->name, value);
	    }
	} else {
	    if (value != 0)
		show_strval(mode, csft->name, value);
	}
    }
}

/* print_parameter - show specific parameter */

static void print_parameter(int mode, char *ptr)
{

#define INSIDE(p,t) (ptr >= (char *) t && ptr < ((char *) t) + sizeof(t))

    /*
     * This is gross, but the best we can do on short notice.
     */
    if (INSIDE(ptr, bool_table))
	print_bool(mode, (CONFIG_BOOL_TABLE *) ptr);
    if (INSIDE(ptr, int_table))
	print_int(mode, (CONFIG_INT_TABLE *) ptr);
    if (INSIDE(ptr, str_table))
	print_str(mode, (CONFIG_STR_TABLE *) ptr);
    if (INSIDE(ptr, str_fn_table))
	print_str_fn(mode, (CONFIG_STR_FN_TABLE *) ptr);
    if (INSIDE(ptr, str_fn_table_2))
	print_str_fn_2(mode, (CONFIG_STR_FN_TABLE *) ptr);
    if (msg_verbose)
	vstream_fflush(VSTREAM_OUT);
}

/* comp_names - qsort helper */

static int comp_names(const void *a, const void *b)
{
    HTABLE_INFO **ap = (HTABLE_INFO **) a;
    HTABLE_INFO **bp = (HTABLE_INFO **) b;

    return (strcmp(ap[0]->key, bp[0]->key));
}

/* show_parameters - show parameter info */

static void show_parameters(int mode, char **names)
{
    HTABLE_INFO **list;
    HTABLE_INFO **ht;
    char  **namep;
    char   *value;

    /*
     * Show all parameters.
     */
    if (*names == 0) {
	list = htable_list(param_table);
	qsort((char *) list, param_table->used, sizeof(*list), comp_names);
	for (ht = list; *ht; ht++)
	    print_parameter(mode, ht[0]->value);
	myfree((char *) list);
	return;
    }

    /*
     * Show named parameters.
     */
    for (namep = names; *namep; namep++) {
	if ((value = htable_find(param_table, *namep)) == 0) {
	    msg_warn("%s: unknown parameter", *namep);
	} else {
	    print_parameter(mode, value);
	}
    }
}

/* main */

int     main(int argc, char **argv)
{
    int     ch;
    int     fd;
    struct stat st;

    /*
     * To minimize confusion, make sure that the standard file descriptors
     * are open before opening anything else. XXX Work around for 44BSD where
     * fstat can return EBADF on an open file descriptor.
     */
    for (fd = 0; fd < 3; fd++)
	if (fstat(fd, &st) == -1
	    && (close(fd), open("/dev/null", O_RDWR, 0)) != fd)
	    msg_fatal("open /dev/null: %m");

    /*
     * Set up logging.
     */
    msg_vstream_init(argv[0], VSTREAM_ERR);

    /*
     * Parse JCL.
     */
    while ((ch = GETOPT(argc, argv, "dhnv")) > 0) {
	switch (ch) {
	case 'd':
	    if (mode & SHOW_NONDEF)
		msg_fatal("specify one of -d and -n");
	    mode |= SHOW_DEFS;
	    break;
	case 'h':
	    mode &= ~SHOW_NAME;
	    break;
	case 'n':
	    if (mode & SHOW_DEFS)
		msg_fatal("specify one of -d and -n");
	    mode |= SHOW_NONDEF;
	    break;
	case 'v':
	    msg_verbose++;
	    break;
	default:
	    msg_fatal("usage: %s [-d (defaults)] [-h (no names)] [-n (non-defaults)] [-v] name...", argv[0]);
	}
    }

    /*
     * If showing non-default values, read main.cf.
     */
    if ((mode & SHOW_DEFS) == 0)
	read_parameters();

    /*
     * Throw together all parameters and show the asked values.
     */
    hash_parameters();
    show_parameters(mode, argv + optind);
    vstream_fflush(VSTREAM_OUT);
    exit(0);
}
