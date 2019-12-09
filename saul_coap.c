/*
 * Copyright (C) 2019 HAW Hamburg
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @ingroup     pkg
 * @{
 *
 * @file
 * @brief       CoAP endpoint for the SAUL registry
 *
 * @author      Micha Rosenbaum <micha.rosenbaum@haw-hamburg.de>
 *
 * @}
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "saul_reg.h"
#include "fmt.h"
#include "net/gcoap.h"
#include "cbor.h"

static ssize_t _saul_cnt_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, void *ctx);
static ssize_t _saul_dev_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, void *ctx);
static ssize_t _saul_sensortype_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, void *ctx);
static ssize_t _saul_type_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, void *ctx);

CborError export_phydat_to_cbor(CborEncoder *encoder, phydat_t data, int dim);

/* supported sense types, used for context pointer in coap_resource_t */
uint8_t class_servo = SAUL_ACT_SERVO;
uint8_t class_hum = SAUL_SENSE_HUM;
uint8_t class_press = SAUL_SENSE_PRESS;
uint8_t class_temp = SAUL_SENSE_TEMP;
uint8_t class_voltage = SAUL_SENSE_VOLTAGE;

/* CoAP resources. Must be sorted by path (ASCII order). */
static const coap_resource_t _resources[] = {
    { "/hum", COAP_GET, _saul_type_handler, &class_hum },
    { "/press", COAP_GET, _saul_type_handler, &class_press },
    { "/saul/cnt", COAP_GET, _saul_cnt_handler, NULL },
    { "/saul/dev", COAP_POST, _saul_dev_handler, NULL },
    { "/sensor", COAP_GET, _saul_sensortype_handler, NULL },
    { "/servo", COAP_GET, _saul_type_handler, &class_servo },
    { "/temp", COAP_GET, _saul_type_handler, &class_temp },
    { "/voltage", COAP_GET, _saul_type_handler, &class_voltage },
};

static gcoap_listener_t _listener = {
    &_resources[0],
    sizeof(_resources) / sizeof(_resources[0]),
    NULL,
    NULL
};

static uint8_t cbor_buf[64] = { 0 };

static ssize_t _saul_dev_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, void *ctx)
{
    int pos = 0;

    (void)ctx;

    if (pdu->payload_len <= 5) {
        char req_payl[6] = { 0 };
        memcpy(req_payl, (char *)pdu->payload, pdu->payload_len);
        pos = strtoul(req_payl, NULL, 10);
    }
    else {
        return gcoap_response(pdu, buf, len, COAP_CODE_BAD_REQUEST);
    }

    gcoap_resp_init(pdu, buf, len, COAP_CODE_CONTENT);
    coap_opt_add_format(pdu, COAP_FORMAT_TEXT);
    size_t resp_len = coap_opt_finish(pdu, COAP_OPT_FINISH_PAYLOAD);

    saul_reg_t *dev = saul_reg_find_nth(pos);

    if (!dev) {
        char *err = "device not found";

        if (pdu->payload_len >= strlen(err)) {
            memcpy(pdu->payload, err, strlen(err));
            gcoap_response(pdu, buf, len, COAP_CODE_404);
            return resp_len + strlen(err);
        }
        else {
            puts("saul_coap: msg buffer too small for payload");
            return gcoap_response(pdu, buf, len, COAP_CODE_INTERNAL_SERVER_ERROR);
        }
    }
    else {
        const char *class_str = saul_class_to_str(dev->driver->type);
        const char *dev_name = dev->name;
        size_t class_size = strlen(class_str);
        size_t dev_size = strlen(dev_name);
        /* Pos should be maximum 3 digits */
        size_t max_pos_size = 3;
        /* The `+4` is necessary because there will
           be additional 2 commas and one linebreak */
        size_t payl_size = class_size + dev_size + max_pos_size + 4;
        char payl[payl_size];

        snprintf(payl, payl_size, "%i,%s,%s\n", pos, class_str, dev_name);

        size_t payl_length = strlen(payl);

        if (pdu->payload_len >= payl_length) {
            memcpy(pdu->payload, payl, payl_length);
            gcoap_response(pdu, buf, len, COAP_CODE_204);
            return resp_len + payl_length;
        }
        else {
            printf("saul_coap: msg buffer (size: %d) too small"
                   " for payload (size: %d)\n",
                   pdu->payload_len, payl_length);
            return gcoap_response(pdu, buf, len, COAP_CODE_INTERNAL_SERVER_ERROR);
        }
    }
}

static ssize_t _saul_cnt_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, void *ctx)
{
    saul_reg_t *dev = saul_reg;
    int i = 0;

    (void)ctx;

    gcoap_resp_init(pdu, buf, len, COAP_CODE_CONTENT);
    coap_opt_add_format(pdu, COAP_FORMAT_TEXT);
    size_t resp_len = coap_opt_finish(pdu, COAP_OPT_FINISH_PAYLOAD);

    while (dev) {
        i++;
        dev = dev->next;
    }

    resp_len += fmt_u16_dec((char *)pdu->payload, i);
    return resp_len;
}

static ssize_t _saul_sensortype_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, void *ctx)
{
    unsigned char query[NANOCOAP_URI_MAX] = { 0 };
    char type_number[4] = { 0 };
    uint8_t type;

    (void)ctx;

    int size = coap_get_uri_query(pdu, query);

    /* FIXME: extract the type number from the query, which has to
       have the format `&class=123`; read number value from class key */
    if (size < 9 || size > 11) {
        return gcoap_response(pdu, buf, len, COAP_CODE_BAD_REQUEST);
    }
    strncpy(type_number, (char *)query+7, 3);

    type = atoi(type_number);

    return _saul_type_handler(pdu, buf, len, &type);
}

static ssize_t _saul_type_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, void *ctx)
{
    uint8_t type = *((uint8_t *)ctx);
    saul_reg_t *dev = saul_reg_find_type(type);
    phydat_t res;
    int dim;
    size_t resp_len, buf_size;
    CborEncoder encoder, aryEncoder;
    CborError cbor_err = CborNoError;

    gcoap_resp_init(pdu, buf, len, COAP_CODE_CONTENT);
    coap_opt_add_format(pdu, COAP_FORMAT_CBOR);
    resp_len = coap_opt_finish(pdu, COAP_OPT_FINISH_PAYLOAD);

    if (dev == NULL) {
        char *err = "device not found";
        if (pdu->payload_len >= strlen(err)) {
            memcpy(pdu->payload, err, strlen(err));
            resp_len += gcoap_response(pdu, buf, len, COAP_CODE_404);
            return resp_len;
        }
        else {
            return gcoap_response(pdu, buf, len, COAP_CODE_404);
        }
    }

    cbor_encoder_init(&encoder, cbor_buf, sizeof(cbor_buf), 0);

    cbor_err = cbor_encoder_create_array(&encoder, &aryEncoder, CborIndefiniteLength);
    if (cbor_err != CborNoError) {
        return gcoap_response(pdu, buf, len, COAP_CODE_INTERNAL_SERVER_ERROR);
    }

    while (dev != NULL && cbor_err == CborNoError) {
        dim = saul_reg_read(dev, &res);

        if (dim > 0) {
            cbor_err = export_phydat_to_cbor(&aryEncoder, res, dim);
            buf_size = cbor_encoder_get_buffer_size(&encoder, cbor_buf);
        }

        dev = dev->next;
    }

    cbor_err = cbor_encoder_close_container(&encoder, &aryEncoder);

    if (cbor_err == CborNoError && buf_size > 0 && pdu->payload_len >= buf_size) {
        memcpy(pdu->payload, cbor_buf, buf_size);
        resp_len += buf_size;
    } else {
        resp_len = gcoap_response(pdu, buf, len, COAP_CODE_INTERNAL_SERVER_ERROR);
    }

    memset(cbor_buf, 0, sizeof(cbor_buf));
    return resp_len;
}

CborError export_phydat_to_cbor(CborEncoder *encoder, phydat_t data, int dim)
{
    CborEncoder mapEncoder, aryEncoder;
    CborError err = CborNoError;

    err = cbor_encoder_create_map(encoder, &mapEncoder, 3);
    if (err != CborNoError) {
        return err;
    }

    err = cbor_encode_text_stringz(&mapEncoder, "values");
    if (err != CborNoError) {
        return err;
    }

    err = cbor_encoder_create_array(&mapEncoder, &aryEncoder, dim);
    if (err != CborNoError) {
        return err;
    }

    for (uint8_t i = 0; i < dim; i++) {
        err = cbor_encode_int(&aryEncoder, data.val[i]);
        if (err != CborNoError) {
            return err;
        }
    }

    err = cbor_encoder_close_container(&mapEncoder, &aryEncoder);
    if (err != CborNoError) {
        return err;
    }

    err = cbor_encode_text_stringz(&mapEncoder, "unit");
    if (err != CborNoError) {
        return err;
    }

    err = cbor_encode_int(&mapEncoder, data.unit);
    if (err != CborNoError) {
        return err;
    }

    err = cbor_encode_text_stringz(&mapEncoder, "scale");
    if (err != CborNoError) {
        return err;
    }

    err = cbor_encode_int(&mapEncoder, data.scale);
    if (err != CborNoError) {
        return err;
    }

    err = cbor_encoder_close_container(encoder, &mapEncoder);
    if (err != CborNoError) {
        return err;
    }

    return CborNoError;
}

void saul_coap_init(void)
{
    gcoap_register_listener(&_listener);
}
