#pragma once

#define BTS_SYMBOL "BTS"
#define BTS_ADDRESS_PREFIX "BTS"
#define BTS_MAX_SYMBOL_NAME_LENGTH 16 
#define BTS_MAX_ASSET_NAME_LENGTH 127
#define BTS_MAX_SHARE_SUPPLY 1000000000000ll
#define BTS_MAX_PAY_RATE 10000 /* 100% */
#define BTS_MAX_SIG_CHECK_DEPTH 2
#define BTS_MIN_DELEGATE_COUNT 10
/**
 * Don't allow the delegates to publish a limit that would
 * make the network unable to operate.
 */
#define BTS_MIN_TRANSACTION_SIZE_LIMIT 1024
#define BTS_MAX_BLOCK_INTERVAL  30 /* seconds */
#define BTS_MIN_BLOCK_SIZE_LIMIT (BTS_MIN_TRANSACTION_SIZE_LIMIT*5) // 5 transactions per block
#define BTS_MIN_TRANSACTION_EXPIRATION_LIMIT (BTS_MAX_BLOCK_INTERVAL * 5) // 5 transactions per block
#define BTS_BLOCKCHAIN_MAX_SHARES                          (1000*1000*int64_t(1000)*1000*int64_t(1000))
#define BTS_BLOCKCHAIN_PRECISION                           100000
#define BTS_BLOCKCHAIN_PRECISION_DIGITS                    5
#define BTS_INITIAL_SUPPLY                                 BTS_BLOCKCHAIN_MAX_SHARES
#define BTS_DEFAULT_TRANSFER_FEE                           (1*BTS_BLOCKCHAIN_PRECISION)
#define BTS_MAX_INSTANCE_ID                                (uint64_t(-1)>>16)
#define BTS_MAX_MARKET_FEE_PERCENT                         10000
