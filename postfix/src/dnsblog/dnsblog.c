/*++
/* NAME
/*	dnsblog 8
/* SUMMARY
/*	Postfix DNS blocklist logger
/* SYNOPSIS
/*	\fBdnsblog\fR [generic Postfix daemon options]
/* DESCRIPTION
/*	The \fBdnsblog\fR(8) server implements an ad-hoc DNS blocklist
/*	lookup service that will eventually be replaced by an UDP
/*	client that is built directly into the \fBpostscreen\fR(8)
/*	server.
/*
/*	With each connection, the \fBdnsblog\fR(8) server receives
/*	a DNS blocklist domain name and an IP address. If the address
/*	is listed under the DNS blocklist, the \fBdnsblog\fR(8)
/*	server logs the match and replies with the query arguments
/*	plus a non-zero status.  Otherwise it replies with the query
/*	arguments plus a zero status.  Finally, The \fBdnsblog\fR(8)
/*	server closes the connection.
/* DIAGNOSTICS
/*	Problems and transactions are logged to \fBsyslogd\fR(8).
/* CONFIGURATION PARAMETERS
/* .ad
/* .fi
/*	Changes to \fBmain.cf\fR are picked up automatically, as
/*	\fBdnsblog\fR(8) processes run for only a limited amount
/*	of time. Use the command "\fBpostfix reload\fR" to speed
/*	up a change.
/*
/*	The text below provides only a parameter summary. See
/*	\fBpostconf\fR(5) for more details including examples.
/* .IP "\fBconfig_directory (see 'postconf -d' output)\fR"
/*	The default location of the Postfix main.cf and master.cf
/*	configuration files.
/* .IP "\fBdaemon_timeout (18000s)\fR"
/*	How much time a Postfix daemon process may take to handle a
/*	request before it is terminated by a built-in watchdog timer.
/* .IP "\fBpostscreen_dnsbl_sites (empty)\fR"
/*	Optional list of DNS blocklist domains.
/* .IP "\fBipc_timeout (3600s)\fR"
/*	The time limit for sending or receiving information over an internal
/*	communication channel.
/* .IP "\fBprocess_id (read-only)\fR"
/*	The process ID of a Postfix command or daemon process.
/* .IP "\fBprocess_name (read-only)\fR"
/*	The process name of a Postfix command or daemon process.
/* .IP "\fBqueue_directory (see 'postconf -d' output)\fR"
/*	The location of the Postfix top-level queue directory.
/* .IP "\fBsyslog_facility (mail)\fR"
/*	The syslog facility of Postfix logging.
/* .IP "\fBsyslog_name (see 'postconf -d' output)\fR"
/*	The mail system name that is prepended to the process name in syslog
/*	records, so that "smtpd" becomes, for example, "postfix/smtpd".
/* SEE ALSO
/*	smtpd(8), Postfix SMTP server
/*	postconf(5), configuration parameters
/*	syslogd(5), system logging
/* LICENSE
/* .ad
/* .fi
/*	The Secure Mailer license must be distributed with this software.
/* HISTORY
/* .ad
/* .fi
/*	This service is temporary with Postfix version 2.7.
/* AUTHOR(S)
/*	Wietse Venema
/*	IBM T.J. Watson Research
/*	P.O. Box 704
/*	Yorktown Heights, NY 10598, USA
/*--*/

/* System library. */

#include <sys_defs.h>

/* Utility library. */

#include <msg.h>
#include <vstream.h>
#include <vstring.h>
#include <argv.h>
#include <myaddrinfo.h>
#include <valid_hostname.h>
#include <sock_addr.h>

/* Global library. */

#include <mail_conf.h>
#include <mail_version.h>
#include <mail_proto.h>

/* DNS library. */

#include <dns.h>

/* Server skeleton. */

#include <mail_server.h>

/* Application-specific. */

 /*
  * Static so we don't allocate and free on every request.
  */
static VSTRING *rbl_domain;
static VSTRING *addr;
static VSTRING *query;
static VSTRING *why;

 /*
  * Silly little macros.
  */
#define STR(x)			vstring_str(x)

/* static void dnsblog_query - query DNSBL for client address */

static int dnsblog_query(const char *dnsbl_domain, const char *addr)
{
    const char *myname = "dnsblog_query";
    ARGV   *octets;
    int     i;
    struct addrinfo *res;
    unsigned char *ipv6_addr;
    int     dns_status;
    DNS_RR *addr_list;
    DNS_RR *rr;
    MAI_HOSTADDR_STR hostaddr;
    int     found = 0;

    if (msg_verbose)
	msg_info("%s: addr %s dnsbl_domain %s",
		 myname, addr, dnsbl_domain);

    VSTRING_RESET(query);

    /*
     * Reverse the client IPV6 address, represented as 32 hexadecimal
     * nibbles. We use the binary address to avoid tricky code. Asking for an
     * AAAA record makes no sense here. Just like with IPv4 we use the lookup
     * result as a bit mask, not as an IP address.
     */
#ifdef HAS_IPV6
    if (valid_ipv6_hostaddr(addr, DONT_GRIPE)) {
	if (hostaddr_to_sockaddr(addr, (char *) 0, 0, &res) != 0
	    || res->ai_family != PF_INET6)
	    msg_fatal("%s: unable to convert address %s", myname, addr);
	ipv6_addr = (unsigned char *) &SOCK_ADDR_IN6_ADDR(res->ai_addr);
	for (i = sizeof(SOCK_ADDR_IN6_ADDR(res->ai_addr)) - 1; i >= 0; i--)
	    vstring_sprintf_append(query, "%x.%x.",
				   ipv6_addr[i] & 0xf, ipv6_addr[i] >> 4);
	freeaddrinfo(res);
    } else
#endif

	/*
	 * Reverse the client IPV4 address, represented as four decimal octet
	 * values. We use the textual address for convenience.
	 */
    {
	octets = argv_split(addr, ".");
	for (i = octets->argc - 1; i >= 0; i--) {
	    vstring_strcat(query, octets->argv[i]);
	    vstring_strcat(query, ".");
	}
	argv_free(octets);
    }

    /*
     * Tack on the RBL domain name and query the DNS for an A record. Don't
     * do this for AAAA records. Yet.
     */
    vstring_strcat(query, dnsbl_domain);
    dns_status = dns_lookup(STR(query), T_A, 0, &addr_list, (VSTRING *) 0, why);
    if (dns_status == DNS_OK) {
	for (rr = addr_list; rr != 0; rr = rr->next) {
	    if (dns_rr_to_pa(rr, &hostaddr) == 0) {
		msg_warn("%s: skipping reply record type %s for query %s: %m",
			 myname, dns_strtype(rr->type), STR(query));
	    } else {
		msg_info("addr %s blocked by domain %s as %s",
			 addr, dnsbl_domain, hostaddr.buf);
		found = 1;
	    }
	}
	dns_rr_free(addr_list);
    } else if (dns_status == DNS_NOTFOUND) {
	if (msg_verbose)
	    msg_info("%s: addr %s not listed under domain %s",
		     myname, addr, dnsbl_domain);
    } else {
	msg_warn("%s: lookup error for DNS query %s: %s",
		 myname, STR(query), STR(why));
    }
    return (found);
}

/* dnsblog_service - perform service for client */

static void dnsblog_service(VSTREAM *client_stream, char *unused_service,
			            char **argv)
{
    int     found;

    /*
     * Sanity check. This service takes no command-line arguments.
     */
    if (argv[0])
	msg_fatal("unexpected command-line argument: %s", argv[0]);

    /*
     * This routine runs whenever a client connects to the socket dedicated
     * to the dnsblog service. All connection-management stuff is handled by
     * the common code in single_server.c.
     */
    if (attr_scan(client_stream,
		  ATTR_FLAG_MORE | ATTR_FLAG_STRICT,
		  ATTR_TYPE_STR, MAIL_ATTR_RBL_DOMAIN, rbl_domain,
		  ATTR_TYPE_STR, MAIL_ATTR_ADDR, addr,
		  ATTR_TYPE_END) == 2) {
	found = dnsblog_query(STR(rbl_domain), STR(addr));
	attr_print(client_stream, ATTR_FLAG_NONE,
		   ATTR_TYPE_STR, MAIL_ATTR_RBL_DOMAIN, STR(rbl_domain),
		   ATTR_TYPE_STR, MAIL_ATTR_ADDR, STR(addr),
		   ATTR_TYPE_INT, MAIL_ATTR_STATUS, found,
		   ATTR_TYPE_END);
	vstream_fflush(client_stream);
    }
}

/* post_jail_init - post-jail initialization */

static void post_jail_init(char *unused_name, char **unused_argv)
{
    rbl_domain = vstring_alloc(100);
    addr = vstring_alloc(100);
    query = vstring_alloc(100);
    why = vstring_alloc(100);
}

MAIL_VERSION_STAMP_DECLARE;

/* main - pass control to the multi-threaded skeleton */

int     main(int argc, char **argv)
{

    /*
     * Fingerprint executables and core dumps.
     */
    MAIL_VERSION_STAMP_ALLOCATE;

    multi_server_main(argc, argv, dnsblog_service,
		      MAIL_SERVER_POST_INIT, post_jail_init,
		      MAIL_SERVER_UNLIMITED,
		      0);
}
