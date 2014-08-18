#include "php.h"

#include "aerospike/aerospike_key.h"
#include "aerospike/as_config.h"
#include "aerospike/as_policy.h"
#include "aerospike/as_status.h"

#include "aerospike_common.h"
#include "aerospike_policy.h"

#define MAX_CONSTANT_STR_SIZE 512
#define NESTING_DEPTH_PHP_INI INI_STR("aerospike.nesting_depth") ? atoi(INI_STR("aerospike.nesting_depth")) : 0
#define CONNECT_TIMEOUT_PHP_INI INI_STR("aerospike.connect_timeout") ? (uint32_t) atoi(INI_STR("aerospike.connect_timeout")) : 0
#define READ_TIMEOUT_PHP_INI INI_STR("aerospike.read_timeout") ? (uint32_t) atoi(INI_STR("aerospike.read_timeout")) : 0
#define WRITE_TIMEOUT_PHP_INI INI_STR("aerospike.write_timeout") ? (uint32_t) atoi(INI_STR("aerospike.write_timeout")) : 0
#define LOG_PATH_PHP_INI INI_STR("aerospike.log_path") ? INI_STR("aerospike.log_path") : NULL
#define LOG_LEVEL_PHP_INI INI_STR("aerospike.log_level") ? INI_STR("aerospike.log_level") : NULL
#define SERIALIZER_PHP_INI INI_STR("aerospike.serializer") ? (uint32_t) atoi(INI_STR("aerospike.serializer")) : 0

typedef struct Aerospike_Constants {
    int constantno;
    char constant_str[MAX_CONSTANT_STR_SIZE];
} AerospikeConstants;


static 
AerospikeConstants aerospike_constants[] = {
    { OPT_CONNECT_TIMEOUT               ,   "OPT_CONNECT_TIMEOUT"               },
    { OPT_READ_TIMEOUT                  ,   "OPT_READ_TIMEOUT"                  },
    { OPT_WRITE_TIMEOUT                 ,   "OPT_WRITE_TIMEOUT"                 },
    { OPT_POLICY_RETRY                  ,   "OPT_POLICY_RETRY"                  },
    { OPT_POLICY_EXISTS                 ,   "OPT_POLICY_EXISTS"                 },
    { OPT_SERIALIZER                    ,   "OPT_SERIALIZER"                    },
    { POLICY_RETRY_NONE                 ,   "POLICY_RETRY_NONE"                 },
    { POLICY_RETRY_ONCE                 ,   "POLICY_RETRY_ONCE"                 },
    { POLICY_EXISTS_IGNORE              ,   "POLICY_EXISTS_IGNORE"              },
    { POLICY_EXISTS_CREATE              ,   "POLICY_EXISTS_CREATE"              },
    { POLICY_EXISTS_UPDATE              ,   "POLICY_EXISTS_UPDATE"              },
    { POLICY_EXISTS_REPLACE             ,   "POLICY_EXISTS_REPLACE"             },
    { POLICY_EXISTS_CREATE_OR_REPLACE   ,   "POLICY_EXISTS_CREATE_OR_REPLACE"   },
    { SERIALIZER_NONE                   ,   "SERIALIZER_NONE"                   },
    { SERIALIZER_PHP                    ,   "SERIALIZER_PHP"                    },
    { SERIALIZER_JSON                   ,   "SERIALIZER_JSON"                   },
    { SERIALIZER_USER                   ,   "SERIALIZER_USER"                   }
};

#define AEROSPIKE_CONSTANTS_ARR_SIZE (sizeof(aerospike_constants)/sizeof(AerospikeConstants))

extern
as_status declare_policy_constants_php(zend_class_entry *Aerospike_ce)
{
    int32_t i;
    as_status   status = AEROSPIKE_OK;

    if (!Aerospike_ce) {
       status = AEROSPIKE_ERR;
       goto exit;
    }

    for (i = 0; i <= AEROSPIKE_CONSTANTS_ARR_SIZE; i++) {
        zend_declare_class_constant_long(
                Aerospike_ce, aerospike_constants[i].constant_str,
                    strlen(aerospike_constants[i].constant_str),
                        aerospike_constants[i].constantno TSRMLS_CC);
    }

exit:
    return status;
}

/*
 * as_config_p, read_policy, write_policy pointers and not checked.
 * Calling functions should check them.
 */
static void
check_and_set_default_policies(as_config *as_config_p, 
                               as_policy_read *read_policy_p, 
                               as_policy_write *write_policy_p, 
                               as_policy_operate *operate_policy_p,
                               as_policy_remove *remove_policy_p,
                               uint32_t *serializer_policy_p)
{
    uint32_t ini_value = 0;

    if ((ini_value = READ_TIMEOUT_PHP_INI) && read_policy_p) {
        read_policy_p->timeout = ini_value;
    }

    if ((ini_value = WRITE_TIMEOUT_PHP_INI) && write_policy_p) {
        write_policy_p->timeout = ini_value;
    }

    if ((ini_value = CONNECT_TIMEOUT_PHP_INI) && as_config_p) {
        as_config_p->conn_timeout_ms = ini_value;
    }

    if ((ini_value = WRITE_TIMEOUT_PHP_INI) && operate_policy_p) {
        operate_policy_p->timeout = ini_value;
    }

    if ((ini_value = WRITE_TIMEOUT_PHP_INI) && remove_policy_p) {
        remove_policy_p->timeout = ini_value;
    }

    if ((ini_value = SERIALIZER_PHP_INI) && serializer_policy_p) {
        *serializer_policy_p = ini_value;
    }
}

static void
set_policy_ex(as_config *as_config_p, 
              as_policy_read *read_policy_p, 
              as_policy_write *write_policy_p, 
              as_policy_operate *operate_policy_p, 
              as_policy_remove *remove_policy_p,
              uint32_t *serializer_policy_p,
              zval *options_p,
              as_error *error_p)
{
    if ((!read_policy_p) && (!write_policy_p) && 
        (!operate_policy_p) && (!remove_policy_p) && (!serializer_policy_p)) {
        DEBUG_PHP_EXT_DEBUG("Unable to set policy");
        PHP_EXT_SET_AS_ERR(error_p, AEROSPIKE_ERR, "Unable to set policy");
        goto exit;
    }

    // case: connect => (read_policy_p != NULL && write_policy_p != NULL)

    if (read_policy_p && (!write_policy_p)) {
        // case: get
        as_policy_read_init(read_policy_p);
    } else if (write_policy_p && (!read_policy_p)) {
        // case: put
        as_policy_write_init(write_policy_p);
    } else if (operate_policy_p) {
        // case: operate
        as_policy_operate_init(operate_policy_p);
    } else if (remove_policy_p) {
        // case: remove
        as_policy_remove_init(remove_policy_p);
    }

    if (options_p == NULL) {
        check_and_set_default_policies(as_config_p, read_policy_p, 
                       write_policy_p, operate_policy_p, remove_policy_p, 
                       serializer_policy_p);
    } else {
        HashTable*          options_array = Z_ARRVAL_P(options_p);
        HashPosition        options_pointer;
        zval**              options_value;
        int8_t*             options_key;
        int16_t             read_flag = 0;
        int16_t             write_flag = 0;
        int16_t             connect_flag = 0;
        int16_t             serializer_flag = 0;

        foreach_hashtable(options_array, options_pointer, options_value) {
            uint options_key_len;
            ulong options_index;

            if (zend_hash_get_current_key_ex(options_array, (char **) &options_key, 
                        &options_key_len, &options_index, 0, &options_pointer) != HASH_KEY_IS_LONG) {
                DEBUG_PHP_EXT_DEBUG("Unable to set policy: Invalid Policy Constant Key");
                PHP_EXT_SET_AS_ERR(error_p, AEROSPIKE_ERR, "Unable to set policy: Invalid Policy Constant Key");
                goto exit;
            }
            switch((int) options_index) {
                case OPT_CONNECT_TIMEOUT:
                    if ((!as_config_p) || (Z_TYPE_PP(options_value) != IS_LONG)) {
                        DEBUG_PHP_EXT_DEBUG("Unable to set policy: Invalid Value for OPT_CONNECT_TIMEOUT");
                        PHP_EXT_SET_AS_ERR(error_p, AEROSPIKE_ERR, "Unable to set policy: Invalid Value for OPT_CONNECT_TIMEOUT");
                        goto exit;
                    }
                    as_config_p->conn_timeout_ms = (uint32_t) Z_LVAL_PP(options_value);
                    connect_flag = 1;
                    break;
                case OPT_READ_TIMEOUT:
                    if ((!read_policy_p) || (Z_TYPE_PP(options_value) != IS_LONG)) {
                        DEBUG_PHP_EXT_DEBUG("Unable to set policy: Invalid Value for OPT_READ_TIMEOUT");
                        PHP_EXT_SET_AS_ERR(error_p, AEROSPIKE_ERR, "Unable to set policy: Invalid Value for OPT_READ_TIMEOUT");
                        goto exit;
                    }
                    read_policy_p->timeout = (uint32_t) Z_LVAL_PP(options_value);
                    read_flag = 1;
                    break;
                case OPT_WRITE_TIMEOUT:
                    if ((Z_TYPE_PP(options_value) != IS_LONG)) {
                        DEBUG_PHP_EXT_DEBUG("Unable to set policy: Invalid Value for OPT_WRITE_TIMEOUT");
                        PHP_EXT_SET_AS_ERR(error_p, AEROSPIKE_ERR, "Unable to set policy: Invalid Value for OPT_WRITE_TIMEOUT");
                        goto exit;
                    }
                    if (write_policy_p) {
                        write_policy_p->timeout = (uint32_t) Z_LVAL_PP(options_value);
                    } else if(operate_policy_p) {
                        operate_policy_p->timeout = (uint32_t) Z_LVAL_PP(options_value);
                    } else if(remove_policy_p) {
                        remove_policy_p->timeout = (uint32_t) Z_LVAL_PP(options_value);
                    } else {
                        DEBUG_PHP_EXT_DEBUG("Unable to set policy: Invalid Value for OPT_WRITE_TIMEOUT");
                        PHP_EXT_SET_AS_ERR(error_p, AEROSPIKE_ERR, "Unable to set policy: Invalid Value for OPT_WRITE_TIMEOUT");
                        goto exit;
                    }
                    write_flag = 1;
                    break;
                case OPT_POLICY_EXISTS:
                    if ((!write_policy_p) || (Z_TYPE_PP(options_value) != IS_LONG)) {
                        DEBUG_PHP_EXT_DEBUG("Unable to set policy: Invalid Value for OPT_POLICY_EXISTS");
                        PHP_EXT_SET_AS_ERR(error_p, AEROSPIKE_ERR, "Unable to set policy: Invalid Value for OPT_POLICY_EXISTS");
                        goto exit;
                    }
                    if ((Z_LVAL_PP(options_value) & AS_POLICY_EXISTS) == AS_POLICY_EXISTS) {
                        write_policy_p->exists = Z_LVAL_PP(options_value) - AS_POLICY_EXISTS + 1;
                    } else {
                        DEBUG_PHP_EXT_DEBUG("Unable to set policy: Invalid Value for OPT_POLICY_EXISTS");
                        PHP_EXT_SET_AS_ERR(error_p, AEROSPIKE_ERR, "Unable to set policy: Invalid Value for OPT_POLICY_EXISTS");
                        goto exit;
                    }
                    break;
                case OPT_POLICY_RETRY:
                    if((Z_TYPE_PP(options_value) != IS_LONG) && ((Z_LVAL_PP(options_value) & AS_POLICY_RETRY) != AS_POLICY_RETRY)) {
                        DEBUG_PHP_EXT_DEBUG("Unable to set policy: Invalid Value for OPT_POLICY_RETRY");
                        PHP_EXT_SET_AS_ERR(error_p, AEROSPIKE_ERR, "Unable to set policy: Invalid Value for OPT_POLICY_RETRY");
                        goto exit;
                    }
                    if (write_policy_p) {
                        write_policy_p->retry = Z_LVAL_PP(options_value) - AS_POLICY_RETRY + 1;
                    } else if(operate_policy_p) {
                        operate_policy_p->retry = Z_LVAL_PP(options_value) - AS_POLICY_RETRY + 1;
                    } else if(remove_policy_p) {
                        remove_policy_p->retry = Z_LVAL_PP(options_value) - AS_POLICY_RETRY + 1;
                    } else {
                        DEBUG_PHP_EXT_DEBUG("Unable to set policy: Invalid Value for OPT_POLICY_RETRY");
                        PHP_EXT_SET_AS_ERR(error_p, AEROSPIKE_ERR, "Unable to set policy: Invalid Value for OPT_POLICY_RETRY");
                        goto exit;
                    }
                    break;
                case OPT_SERIALIZER:
                    if ((!serializer_policy_p) || (Z_TYPE_PP(options_value) != IS_LONG)) {
                        DEBUG_PHP_EXT_DEBUG("Unable to set policy: Invalid Value for OPT_POLICY_RETRY");
                        PHP_EXT_SET_AS_ERR(error_p, AEROSPIKE_ERR, "Unable to set policy: Invalid Value for OPT_POLICY_RETRY");
                        goto exit;
                    }
                    if ((Z_LVAL_PP(options_value) & AS_SERIALIZER_TYPE) == AS_SERIALIZER_TYPE) {
                        *serializer_policy_p = Z_LVAL_PP(options_value);
                    } else {
                        DEBUG_PHP_EXT_DEBUG("Unable to set policy: Invalid Value for OPT_POLICY_RETRY");
                        PHP_EXT_SET_AS_ERR(error_p, AEROSPIKE_ERR, "Unable to set policy: Invalid Value for OPT_POLICY_RETRY");
                        goto exit;
                    }
                    serializer_flag = 1;
                    break;
                default:
                    DEBUG_PHP_EXT_DEBUG("Unable to set policy: Invalid Policy Constant Key");
                    PHP_EXT_SET_AS_ERR(error_p, AEROSPIKE_ERR, "Unable to set policy: Invalid Policy Constant Key");
                    goto exit;
            }
        }
        if (!write_flag && write_policy_p) {
            check_and_set_default_policies((connect_flag ? NULL : as_config_p),
                    NULL, write_policy_p, NULL, NULL, NULL);
            connect_flag = 1;
        } 
        if (!read_flag && read_policy_p) {
            check_and_set_default_policies((connect_flag ? NULL : as_config_p), 
                    read_policy_p, NULL, NULL, NULL, NULL);
            connect_flag = 1;
        } 
        if (!connect_flag && as_config_p) {
            check_and_set_default_policies(as_config_p, NULL, NULL, NULL, NULL, NULL);
        }
        if (!serializer_flag && serializer_policy_p) {
            check_and_set_default_policies(NULL, NULL, NULL, NULL, NULL, serializer_policy_p);
        }
    }
    PHP_EXT_SET_AS_ERR(error_p, AEROSPIKE_OK, DEFAULT_ERROR);
exit:
    return;
}

extern void
set_policy(as_policy_read *read_policy_p, 
           as_policy_write *write_policy_p, 
           as_policy_operate *operate_policy_p, 
           as_policy_remove *remove_policy_p,
           uint32_t *serializer_policy_p,
           zval *options_p,
           as_error *error_p)
{
    set_policy_ex(NULL, read_policy_p, write_policy_p, operate_policy_p, remove_policy_p, 
            serializer_policy_p, options_p, error_p);
}

extern void
set_general_policies(as_config *as_config_p, 
                     zval *options_p,
                     as_error *error_p)
{
    if (!as_config_p) {
        DEBUG_PHP_EXT_DEBUG("Unable to set policy: Invalid as_config");
        PHP_EXT_SET_AS_ERR(error_p, AEROSPIKE_ERR, "Unable to set policy: Invalid as_config");
        goto exit;
    }

    set_policy_ex(as_config_p, &as_config_p->policies.read, &as_config_p->policies.write, 
                           NULL, NULL, NULL, options_p, error_p);
exit:
    return;
}