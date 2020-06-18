#ifndef GSSAPI_MSPAC_H_
#define GSSAPI_MSPAC_H_

#include <gssapi/gssapi.h>

/*
 * This custom structure is used to obtain additional information from the domain controller
 * after an AD user has been authenticated, either by Kerberos or NTLM v2
 * domainSid is a buffer for the logon domain SID (host-endian)
 * userRid and groupRid are the RIDs for the user's account and their primary group
 */
struct gssapi_mspac_member;
struct gssapi_mspac {
    unsigned char *domainSid;
    unsigned long   userRid;
    unsigned long   groupRid;
    int   membershipCount;
    struct gssapi_mspac_member *memberships;
};

struct gssapi_mspac_member {
    unsigned long rid;
    unsigned long attributes;
};

#define GSSAPI_GET_MSPAC_OID_LEN 11
#define GSSAPI_GET_MSPAC_OID "\x2a\x86\x48\x86\xf7\x12\x01\x02\x02\x05\xee"

#endif /* GSSAPI_MSPAC_H_ */
