/* -*- mode: c; indent-tabs-mode: nil -*- */
/*
 * Copyright 1993 by OpenVision Technologies, Inc.
 *
 * Permission to use, copy, modify, distribute, and sell this software
 * and its documentation for any purpose is hereby granted without fee,
 * provided that the above copyright notice appears in all copies and
 * that both that copyright notice and this permission notice appear in
 * supporting documentation, and that the name of OpenVision not be used
 * in advertising or publicity pertaining to distribution of the software
 * without specific, written prior permission. OpenVision makes no
 * representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 *
 * OPENVISION DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL OPENVISION BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF
 * USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#include "gssapiP_krb5.h"
#ifdef HAVE_MEMORY_H
#include <memory.h>
#endif

/* Checksumming the channel bindings always uses plain MD5.  */
krb5_error_code
kg_checksum_channel_bindings(context, cb, cksum, bigend)
    krb5_context context;
    gss_channel_bindings_t cb;
    krb5_checksum *cksum;
    int bigend;
{
    size_t len;
    char *buf = 0;
    char *ptr;
    size_t sumlen;
    krb5_data plaind;
    krb5_error_code code;
    void *temp;

    /* initialize the the cksum */
    code = krb5_c_checksum_length(context, CKSUMTYPE_RSA_MD5, &sumlen);
    if (code)
        return(code);

    cksum->checksum_type = CKSUMTYPE_RSA_MD5;
    cksum->length = sumlen;

    /* generate a buffer full of zeros if no cb specified */

    if (cb == GSS_C_NO_CHANNEL_BINDINGS) {
        if ((cksum->contents = (krb5_octet *) xmalloc(cksum->length)) == NULL) {
            return(ENOMEM);
        }
        memset(cksum->contents, '\0', cksum->length);
        return(0);
    }

    /* create the buffer to checksum into */

    len = (sizeof(krb5_int32)*5+
           cb->initiator_address.length+
           cb->acceptor_address.length+
           cb->application_data.length);

    if ((buf = (char *) xmalloc(len)) == NULL)
        return(ENOMEM);

    /* helper macros.  This code currently depends on a long being 32
       bits, and htonl dtrt. */

    ptr = buf;

    TWRITE_INT(ptr, cb->initiator_addrtype, bigend);
    TWRITE_BUF(ptr, cb->initiator_address, bigend);
    TWRITE_INT(ptr, cb->acceptor_addrtype, bigend);
    TWRITE_BUF(ptr, cb->acceptor_address, bigend);
    TWRITE_BUF(ptr, cb->application_data, bigend);

    /* checksum the data */

    plaind.length = len;
    plaind.data = buf;

    code = krb5_c_make_checksum(context, CKSUMTYPE_RSA_MD5, 0, 0,
                                &plaind, cksum);
    if (code)
        goto cleanup;

    if ((temp = xmalloc(cksum->length)) == NULL) {
        krb5_free_checksum_contents(context, cksum);
        code = ENOMEM;
        goto cleanup;
    }

    memcpy(temp, cksum->contents, cksum->length);
    krb5_free_checksum_contents(context, cksum);
    cksum->contents = (krb5_octet *)temp;

    /* success */
cleanup:
    if (buf)
        xfree(buf);
    return code;
}

krb5_error_code
kg_make_checksum_iov_v1(krb5_context context,
			krb5_cksumtype type,
			krb5_keyblock *seq,
			krb5_keyblock *enc,
			krb5_keyusage sign_usage,
			size_t iov_count,
			gss_iov_buffer_desc *iov,
			krb5_checksum *checksum)
{
    krb5_error_code code;
    gss_iov_buffer_desc *header;
    krb5_crypto_iov *kiov;
    size_t kiov_count;
    size_t i = 0, j;

    header = kg_locate_iov(iov_count, iov, GSS_IOV_BUFFER_TYPE_HEADER);
    assert(header != NULL);

    kiov_count = 3 + iov_count;
    kiov = (krb5_crypto_iov *)xmalloc(kiov_count * sizeof(krb5_crypto_iov));
    if (kiov == NULL)
	return ENOMEM;

    /* Checksum over ( Header | Confounder | Data | Pad ) */

    /* Checksum output */
    kiov[i].flags = KRB5_CRYPTO_TYPE_CHECKSUM;
    kiov[i].data.length = checksum->length;
    kiov[i].data.data = xmalloc(checksum->length);
    if (kiov[i].data.data == NULL) {
	xfree(kiov);
	return ENOMEM;
    }
    i++;

    /* Header */
    kiov[i].flags = KRB5_CRYPTO_TYPE_SIGN_ONLY;
    kiov[i].data.length = 8;
    kiov[i].data.data = (char *)header->buffer.value;
    i++;

    /* Confounder */
    kiov[i].flags = KRB5_CRYPTO_TYPE_DATA;
    kiov[i].data.length = kg_confounder_size(context, (krb5_keyblock *)enc);
    kiov[i].data.data = (char *)header->buffer.value +header->buffer.length - kiov[i].data.length;
    i++;

    for (j = 0; j < iov_count; j++) {
	kiov[i].flags = kg_translate_flag_iov(iov[j].type, iov[j].flags);
	kiov[i].data.length = iov[j].buffer.length;
	kiov[i].data.data = (char *)iov[j].buffer.value;

	i++;
    }

    code = krb5_c_make_checksum_iov(context, type, seq, sign_usage, kiov, kiov_count);
    if (code == 0) {
	checksum->length = kiov[0].data.length;
	checksum->contents = (unsigned char *)kiov[0].data.data;
    }

    xfree(kiov);

    return code;
}

krb5_error_code
checksum_iov_v3(krb5_context context,
		krb5_cksumtype type,
		int rrc,
		krb5_keyblock *key,
		krb5_keyusage sign_usage,
		size_t iov_count,
		gss_iov_buffer_desc *iov,
		krb5_boolean verify,
		krb5_boolean *valid)
{
    krb5_error_code code;
    gss_iov_buffer_desc *header;
    gss_iov_buffer_desc *trailer;
    krb5_crypto_iov *kiov;
    size_t kiov_count;
    size_t i = 0, j;
    size_t k5_checksumlen;

    if (verify)
	*valid = 0;

    code = krb5_c_crypto_length(context, key->enctype, KRB5_CRYPTO_TYPE_CHECKSUM, &k5_checksumlen);
    if (code != 0)
	return code;

    header = kg_locate_iov(iov_count, iov, GSS_IOV_BUFFER_TYPE_HEADER);
    assert(header != NULL);

    trailer = kg_locate_iov(iov_count, iov, GSS_IOV_BUFFER_TYPE_TRAILER);
    assert(rrc || trailer != NULL);

    kiov_count = 2 + iov_count;
    kiov = (krb5_crypto_iov *)xmalloc(kiov_count * sizeof(krb5_crypto_iov));
    if (kiov == NULL)
	return ENOMEM;

    /* Checksum over ( Data | Header ) */

    /* Data */
    for (j = 0; j < iov_count; j++) {
	kiov[i].flags = kg_translate_flag_iov(iov[j].type, iov[j].flags);
	kiov[i].data.length = iov[j].buffer.length;
	kiov[i].data.data = (char *)iov[j].buffer.value;
	i++;
    }

    /* Header */
    kiov[i].flags = KRB5_CRYPTO_TYPE_SIGN_ONLY;
    kiov[i].data.length = 16;
    kiov[i].data.data = (char *)header->buffer.value;
    i++;

    /* Checksum input/output; caller must specify correct buffer size upon
     * creation */
    kiov[i].flags = KRB5_CRYPTO_TYPE_CHECKSUM;
    if (rrc) {
	assert(rrc == k5_checksumlen);
	assert(verify || header->buffer.length == 16 + k5_checksumlen);

	kiov[i].data.length = header->buffer.length - 16;
	kiov[i].data.data = (char *)header->buffer.value + 16;
    } else {
	assert(verify || trailer->buffer.length == k5_checksumlen);

	kiov[i].data.length = trailer->buffer.length;
	kiov[i].data.data = (char *)trailer->buffer.value;
    }
    i++;

    if (verify)
	code = krb5_c_verify_checksum_iov(context, type, key, sign_usage, kiov, kiov_count, valid);
    else
	code = krb5_c_make_checksum_iov(context, type, key, sign_usage, kiov, kiov_count);

    xfree(kiov);

    return code;
}

krb5_error_code
kg_make_checksum_iov_v3(krb5_context context,
			krb5_cksumtype type,
			int rrc,
			krb5_keyblock *key,
			krb5_keyusage sign_usage,
			size_t iov_count,
			gss_iov_buffer_desc *iov)
{
    return checksum_iov_v3(context, type, rrc, key,
			   sign_usage, iov_count, iov, 0, NULL);
}

krb5_error_code
kg_verify_checksum_iov_v3(krb5_context context,
			  krb5_cksumtype type,
			  int rrc,
			  krb5_keyblock *key,
			  krb5_keyusage sign_usage,
			  size_t iov_count,
			  gss_iov_buffer_desc *iov,
			  krb5_boolean *valid)
{
    return checksum_iov_v3(context, type, rrc, key,
			   sign_usage, iov_count, iov, 1, valid);
}
