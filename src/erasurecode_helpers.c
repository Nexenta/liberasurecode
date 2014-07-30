/*
 * <Copyright>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice, this
 * list of conditions and the following disclaimer in the documentation and/or
 * other materials provided with the distribution.  THIS SOFTWARE IS PROVIDED BY
 * THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * liberasurecode API helpers implementation
 *
 * vi: set noai tw=79 ts=4 sw=4:
 */
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include "erasurecode_backend.h"
#include "erasurecode_helpers.h"
#include "erasurecode_stdinc.h"

/* ==~=*=~==~=*=~==~=*=~==~=*=~==~=*=~==~=*=~==~=*=~==~=*=~==~=*=~==~=*=~== */

/**
 * Memory Management Methods
 * 
 * The following methods provide wrappers for allocating and deallocating
 * memory.  
 */
void *get_aligned_buffer16(int size)
{
    void *buf;

    /**
     * Ensure all memory is aligned to 16-byte boundaries
     * to support 128-bit operations
     */
    if (posix_memalign(&buf, 16, size) < 0) {
        return NULL;
    }

    memset(buf, 0, size);

    return buf;
}

/**
 * Allocate a zero-ed buffer of a specific size.
 *
 * @param size integer size in bytes of buffer to allocate
 * @return pointer to start of allocated buffer or NULL on error
 */
void * alloc_zeroed_buffer(int size)
{
    return alloc_and_set_buffer(size, 0);
}

/**
 * Allocate a buffer of a specific size and set its' contents
 * to the specified value.
 *
 * @param size integer size in bytes of buffer to allocate
 * @param value
 * @return pointer to start of allocated buffer or NULL on error
 */
void * alloc_and_set_buffer(int size, int value) {
    void * buf = NULL;  /* buffer to allocate and return */
  
    /* Allocate and zero the buffer, or set the appropriate error */
    buf = malloc((size_t) size);
    if (buf) {
        buf = memset(buf, value, (size_t) size);
    }
    return buf;
}

/**
 * Deallocate memory buffer if it's not NULL.  This methods returns NULL so 
 * that you can free and reset a buffer using a single line as follows:
 *
 * my_ptr = check_and_free_buffer(my_ptr);
 *
 * @return NULL
 */
void * check_and_free_buffer(void * buf)
{
    if (buf)
        free(buf);
    return NULL;
}

char *alloc_fragment_buffer(int size)
{
    char *buf;
    fragment_header_t *header = NULL;

    size += sizeof(fragment_header_t);
    buf = get_aligned_buffer16(size);

    if (buf) {
        header = (fragment_header_t *) buf;
        header->magic = LIBERASURECODE_FRAG_HEADER_MAGIC;
    }

    return buf;
}

int free_fragment_buffer(char *buf)
{
    fragment_header_t *header;

    if (NULL == buf) {
        return -1;
    }

    buf -= sizeof(fragment_header_t);

    header = (fragment_header_t *) buf;
    if (header->magic != LIBERASURECODE_FRAG_HEADER_MAGIC) {
        log_error("Invalid fragment header (free fragment)!");
        return -1;
    }

    free(buf);
    return 0;
}

/* ==~=*=~==~=*=~==~=*=~==~=*=~==~=*=~==~=*=~==~=*=~==~=*=~==~=*=~==~=*=~== */

/**
 * Return total fragment length (on-disk, on-wire)
 *
 * @param buf - pointer to fragment buffer
 *
 * @return fragment size on disk
 */
uint64_t get_fragment_size(char *buf)
{
    fragment_header_t *header = NULL;

    if (NULL == buf)
        return -1;

    header = (fragment_header_t *) buf;
    return (header->size + sizeof(fragment_header_t));
 }

/**
 * Compute a size aligned to the number of data and the underlying wordsize 
 * of the EC algorithm.
 * 
 * @param instance, ec_backend_t instance (to extract args)
 * @param data_len, integer length of data in bytes
 * @return integer data length aligned with wordsize of EC algorithm
 */
int get_aligned_data_size(ec_backend_t instance, int data_len)
{
    int k = instance->args.uargs.k;
    int m = instance->args.uargs.m;
    int w = instance->args.uargs.w;
    int word_size = w / 8;
    int alignment_multiple;
    int aligned_size = 0;

    /*
     * For Cauchy reed-solomon align to k*word_size*packet_size
     * For Vandermonde reed-solomon and flat-XOR, align to k*word_size
     */
    if (EC_BACKEND_JERASURE_RS_CAUCHY == instance->common.id) {
        alignment_multiple = k * w * (sizeof(long) * 128);
    } else {
        alignment_multiple = k * word_size;
    }

    aligned_size = (int) 
        ceill((double) data_len / alignment_multiple) * alignment_multiple;

    return aligned_size;
}

/* ==~=*=~==~=*=~==~=*=~==~=*=~==~=*=~==~=*=~==~=*=~==~=*=~==~=*=~==~=*=~== */

char *get_data_ptr_from_fragment(char *buf)
{
    buf += sizeof(fragment_header_t);

    return buf;
}

int get_data_ptr_array_from_fragments(char **data_array, char **fragments,
                                      int num_fragments)
{
    int i = 0, num = 0;
    for (i = 0; i < num_fragments; i++) {
        char *frag = fragments[i];
        if (frag == NULL) {
            data_array[i] = NULL;
            continue;
        }
        data_array[i] = get_data_ptr_from_fragment(frag);
        num++;
    }
    return num;
}

char *get_fragment_ptr_from_data_novalidate(char *buf)
{
    buf -= sizeof(fragment_header_t);

    return buf;
}

char *get_fragment_ptr_from_data(char *buf)
{
    fragment_header_t *header;

    buf -= sizeof(fragment_header_t);

    header = (fragment_header_t *) buf;

    if (header->magic != LIBERASURECODE_FRAG_HEADER_MAGIC) {
        log_error("Invalid fragment header (get header ptr)!\n");
        return NULL;
    }

    return buf;
}

/* ==~=*=~==~=*=~==~=*=~==~=*=~==~=*=~==~=*=~==~=*=~==~=*=~==~=*=~==~=*=~== */

int set_fragment_idx(char *buf, int idx)
{
    fragment_header_t *header = (fragment_header_t *) buf;

    assert(NULL != header);
    if (header->magic != LIBERASURECODE_FRAG_HEADER_MAGIC) {
        log_error("Invalid fragment header (idx check)!\n");
        return -1;
    }

    header->idx = idx;

    return 0;
}

int get_fragment_idx(char *buf)
{
    fragment_header_t *header = (fragment_header_t *) buf;

    assert(NULL != header);
    if (header->magic != LIBERASURECODE_FRAG_HEADER_MAGIC) {
        log_error("Invalid fragment header (get idx)!");
        return -1;
    }

    return header->idx;
}

int set_fragment_payload_size(char *buf, int size)
{
    fragment_header_t *header = (fragment_header_t *) buf;

    assert(NULL != header);
    if (header->magic != LIBERASURECODE_FRAG_HEADER_MAGIC) {
        log_error("Invalid fragment header (size check)!");
        return -1;
    }

    header->size = size;

    return 0;
}

int get_fragment_payload_size(char *buf)
{
    fragment_header_t *header = (fragment_header_t *) buf;

    assert(NULL != header);
    if (header->magic != LIBERASURECODE_FRAG_HEADER_MAGIC) {
        log_error("Invalid fragment header (get size)!");
        return -1;
    }

    return header->size;
}

int set_orig_data_size(char *buf, int orig_data_size)
{
    fragment_header_t *header = (fragment_header_t *) buf;

    assert(NULL != header);
    if (header->magic != LIBERASURECODE_FRAG_HEADER_MAGIC) {
        log_error("Invalid fragment header (set orig data check)!");
        return -1;
    }

    header->orig_data_size = orig_data_size;

    return 0;
}

int get_orig_data_size(char *buf)
{
    fragment_header_t *header = (fragment_header_t *) buf;

    assert(NULL != header);
    if (header->magic != LIBERASURECODE_FRAG_HEADER_MAGIC) {
        log_error("Invalid fragment header (get orig data check)!");
        return -1;
    }

    return header->orig_data_size;
}

/* ==~=*=~==~=*=~==~=*=~==~=*=~==~=*=~==~=*=~==~=*=~==~=*=~==~=*=~==~=*=~== */

int validate_fragment(char *buf)
{
    fragment_header_t *header = (fragment_header_t *) buf;

    assert(NULL != header);
    if (header->magic != LIBERASURECODE_FRAG_HEADER_MAGIC) {
        return -1;
    }

    return 0;
}

/* ==~=*=~==~=*=~==~=*=~==~=*=~==~=*=~==~=*=~==~=*=~==~=*=~==~=*=~==~=*=~== */

inline int set_chksum(char *buf, int chksum)
{
    fragment_header_t* header = (fragment_header_t*) buf;

    assert(NULL != header);
    if (header->magic != LIBERASURECODE_FRAG_HEADER_MAGIC) {
        log_error("Invalid fragment header (set chksum)!\n");
        return -1; 
    }

    header->chksum = chksum;
    
    return 0;
}

inline int get_chksum(char *buf)
{
    fragment_header_t* header = (fragment_header_t*) buf;

    assert(NULL != header);
    if (header->magic != LIBERASURECODE_FRAG_HEADER_MAGIC) {
        log_error("Invalid fragment header (get chksum)!");
        return -1;
    }

    return header->chksum;
}

/* ==~=*=~==~=*=~==~=*=~==~=*=~==~=*=~==~=*=~==~=*=~==~=*=~==~=*=~==~=*=~== */

