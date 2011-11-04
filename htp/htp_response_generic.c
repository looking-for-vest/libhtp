/***************************************************************************
 * Copyright 2009-2010 Open Information Security Foundation
 * Copyright 2010-2011 Qualys, Inc.
 *
 * Licensed to You under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 ***************************************************************************/

/**
 * @file
 * @author Ivan Ristic <ivanr@webkreator.com>
 */

#include "htp.h"

/**
 * Generic response line parser.
 * 
 * @param connp
 * @return HTP status
 */
int htp_parse_response_line_generic(htp_connp_t *connp) {
    htp_tx_t *tx = connp->out_tx;
    unsigned char *data = (unsigned char *) bstr_ptr(tx->response_line);
    size_t len = bstr_len(tx->response_line);
    size_t pos = 0;
    
    // Ignore whitespace at the beginning of the line
    while ((pos < len) && (htp_is_space(data[pos]))) {
        pos++;
    }

    size_t start = pos;

    // Find the end of the protocol string
    while ((pos < len) && (!htp_is_space(data[pos]))) {
        pos++;
    }

    tx->response_protocol = bstr_dup_mem((char *) data + start, pos - start);
    if (tx->response_protocol == NULL) {
        return HTP_ERROR;
    }

    tx->response_protocol_number = htp_parse_protocol(tx->response_protocol);        

    #ifdef HTP_DEBUG
    fprint_raw_data(stderr, __FUNCTION__, (unsigned char *) bstr_ptr(tx->response_protocol), bstr_len(tx->response_protocol));
    #endif

    // Ignore whitespace after response protocol
    // TODO Why use both isspace (below) and htp_is_space (above)?
    while ((pos < len) && (isspace(data[pos]))) {
        pos++;
    }

    start = pos;

    // Find the next whitespace character
    while ((pos < len) && (!htp_is_space(data[pos]))) {
        pos++;
    }

    tx->response_status = bstr_dup_mem((char *) data + start, pos - start);
    if (tx->response_status == NULL) {
        return HTP_ERROR;
    }

    tx->response_status_number = htp_parse_status(tx->response_status);    

    #ifdef HTP_DEBUG
    fprint_raw_data(stderr, __FUNCTION__, (unsigned char *) bstr_ptr(tx->response_status), bstr_len(tx->response_status));
    #endif

    // Ignore whitespace that follows
    while ((pos < len) && (isspace(data[pos]))) {
        pos++;
    }

    tx->response_message = bstr_dup_mem((char *) data + pos, len - pos);
    if (tx->response_message == NULL) {
        return HTP_ERROR;
    }

    #ifdef HTP_DEBUG
    fprint_raw_data(stderr, __FUNCTION__, (unsigned char *) bstr_ptr(tx->response_message), bstr_len(tx->response_message));
    #endif

    return HTP_OK;
}

/**
 * Generic response header parser.
 * 
 * @param connp
 * @param h
 * @param data
 * @param len
 * @return HTP status
 */
int htp_parse_response_header_generic(htp_connp_t *connp, htp_header_t *h, char *data, size_t len) {
    size_t name_start, name_end;
    size_t value_start, value_end;

    htp_chomp((unsigned char *)data, &len);

    name_start = 0;

    // Look for the colon
    size_t colon_pos = 0;
    while ((colon_pos < len) && (data[colon_pos] != ':')) colon_pos++;

    if (colon_pos == len) {
        // Missing colon
        h->flags |= HTP_FIELD_UNPARSEABLE;

        if (!(connp->out_tx->flags & HTP_FIELD_UNPARSEABLE)) {
            connp->out_tx->flags |= HTP_FIELD_UNPARSEABLE;
            // Only log once per transaction
            htp_log(connp, HTP_LOG_MARK, HTP_LOG_WARNING, 0, "Request field invalid: colon missing");
        }

        return HTP_ERROR;
    }

    if (colon_pos == 0) {
        // Empty header name
        h->flags |= HTP_FIELD_INVALID;

        if (!(connp->out_tx->flags & HTP_FIELD_INVALID)) {
            connp->out_tx->flags |= HTP_FIELD_INVALID;
            // Only log once per transaction
            htp_log(connp, HTP_LOG_MARK, HTP_LOG_WARNING, 0, "Request field invalid: empty name");
        }
    }

    name_end = colon_pos;

    // Ignore LWS after field-name
    size_t prev = name_end;
    while ((prev > name_start) && (htp_is_lws(data[prev - 1]))) {
        prev--;
        name_end--;

        h->flags |= HTP_FIELD_INVALID;

        if (!(connp->out_tx->flags & HTP_FIELD_INVALID)) {
            connp->out_tx->flags |= HTP_FIELD_INVALID;
            htp_log(connp, HTP_LOG_MARK, HTP_LOG_WARNING, 0, "Request field invalid: LWS after name");
        }
    }

    // Value

    value_start = colon_pos;

    // Go over the colon
    if (value_start < len) {
        value_start++;
    }

    // Ignore LWS before field-content
    while ((value_start < len) && (htp_is_lws(data[value_start]))) {
        value_start++;
    }

    // Look for the end of field-content
    value_end = value_start;
    while (value_end < len) value_end++;

    // Ignore LWS after field-content
    prev = value_end - 1;
    while ((prev > value_start) && (htp_is_lws(data[prev]))) {
        prev--;
        value_end--;
    }

    // Check that the header name is a token
    size_t i = name_start;
    while (i < name_end) {
        if (!htp_is_token(data[i])) {
            h->flags |= HTP_FIELD_INVALID;

            if (!(connp->out_tx->flags & HTP_FIELD_INVALID)) {
                connp->out_tx->flags |= HTP_FIELD_INVALID;
                htp_log(connp, HTP_LOG_MARK, HTP_LOG_WARNING, 0, "Request header name is not a token");
            }

            break;
        }

        i++;
    }

    // Now extract the name and the value
    h->name = bstr_dup_mem(data + name_start, name_end - name_start);
    h->value = bstr_dup_mem(data + value_start, value_end - value_start);
    if ((h->name == NULL)||(h->value == NULL)) {
        bstr_free(&h->name);
        bstr_free(&h->value);
        return HTP_ERROR;
    }

    return HTP_OK;
}

/**
 * Generic response header line(s) processor, which assembles folded lines
 * into a single buffer before invoking the parsing function.
 * 
 * @param connp
 * @return HTP status
 */
int htp_process_response_header_generic(htp_connp_t *connp) {
    bstr *tempstr = NULL;
    char *data = NULL;
    size_t len = 0;

    // Parse header
    htp_header_t *h = calloc(1, sizeof (htp_header_t));
    if (h == NULL) return HTP_ERROR;

    // Ensure we have the necessary header data in a single buffer
    if (connp->out_header_line_index + 1 == connp->out_header_line_counter) {
        // Single line
        htp_header_line_t *hl = list_get(connp->out_tx->response_header_lines,
            connp->out_header_line_index);
        if (hl == NULL) {
            // Internal error
            htp_log(connp, HTP_LOG_MARK, HTP_LOG_ERROR, 0,
                "Process response header (generic): Internal error");
            free(h);
            return HTP_ERROR;
        }

        data = bstr_ptr(hl->line);
        len = bstr_len(hl->line);
        hl->header = h;
    } else {
        // Multiple lines (folded)
        int i = 0;

        for (i = connp->out_header_line_index; i < connp->out_header_line_counter; i++) {
            htp_header_line_t *hl = list_get(connp->out_tx->response_header_lines, i);
            len += bstr_len(hl->line);
        }

        tempstr = bstr_alloc(len);
        if (tempstr == NULL) {
            htp_log(connp, HTP_LOG_MARK, HTP_LOG_ERROR, 0,
                "Process reqsponse header (generic): Failed to allocate bstring of %d bytes", len);
            free(h);
            return HTP_ERROR;
        }

        for (i = connp->out_header_line_index; i < connp->out_header_line_counter; i++) {
            htp_header_line_t *hl = list_get(connp->out_tx->response_header_lines, i);
            char *line = bstr_ptr(hl->line);
            size_t llen = bstr_len(hl->line);
            htp_chomp((unsigned char *)line, &llen);
            bstr_add_mem_noex(tempstr, line, llen);
            hl->header = h;
        }

        data = bstr_ptr(tempstr);
    }

    if (htp_parse_response_header_generic(connp, h, data, len) != HTP_OK) {
        // Note: downstream responsible for error logging
        bstr_free(&tempstr);
        free(h);        
        return HTP_ERROR;
    }

    // Do we already have a header with the same name?
    htp_header_t *h_existing = table_get(connp->out_tx->response_headers, h->name);
    if (h_existing != NULL) {
        // TODO Do we want to keep a list of the headers that are
        //      allowed to be combined in this way?

        // Add to existing header
        bstr *new_value = bstr_expand(h_existing->value, bstr_len(h_existing->value)
            + 2 + bstr_len(h->value));
        if (new_value == NULL) {
            bstr_free(&h->name);
            bstr_free(&h->value);
            free(h);            
            bstr_free(&tempstr);
            return HTP_ERROR;
        }

        h_existing->value = new_value;
        bstr_add_mem_noex(h_existing->value, ", ", 2);
        bstr_add_noex(h_existing->value, h->value);

        // The header is no longer needed
        bstr_free(&h->name);
        bstr_free(&h->value);
        free(h);

        // Keep track of same-name headers
        h_existing->flags |= HTP_FIELD_REPEATED;
    } else {
        // Add as a new header
        table_add(connp->out_tx->response_headers, h->name, h);
    }

    bstr_free(&tempstr);

    return HTP_OK;
}
