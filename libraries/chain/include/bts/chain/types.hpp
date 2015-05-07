#pragma once
#include <fc/container/flat_fwd.hpp>
#include <fc/io/varint.hpp>
#include <fc/io/raw_fwd.hpp>
#include <fc/io/enum_type.hpp>
#include <fc/crypto/sha224.hpp>
#include <fc/crypto/elliptic.hpp>
#include <fc/reflect/reflect.hpp>
#include <fc/reflect/variant.hpp>
#include <fc/optional.hpp>
#include <fc/safe.hpp>
#include <fc/container/flat.hpp>
#include <fc/string.hpp>
#include <memory>
#include <vector>
#include <deque>
#include <bts/chain/address.hpp>
#include <bts/db/object_id.hpp>

namespace bts { namespace chain {
   using namespace bts::db;

   using                               std::map;
   using                               std::vector;
   using                               std::unordered_map;
   using                               std::string;
   using                               std::deque;
   using                               std::shared_ptr;
   using                               std::weak_ptr;
   using                               std::unique_ptr;
   using                               std::set;
   using                               std::pair;
   using                               std::enable_shared_from_this;
   using                               std::tie;
   using                               std::make_pair;

   using                               fc::variant_object;
   using                               fc::variant;
   using                               fc::enum_type;
   using                               fc::optional;
   using                               fc::unsigned_int;
   using                               fc::signed_int;
   using                               fc::time_point_sec;
   using                               fc::time_point;
   using                               fc::safe;
   using                               fc::flat_map;
   using                               fc::flat_set;
   using                               fc::static_variant;

   typedef fc::ecc::private_key        private_key_type;

   enum asset_issuer_permission_flags
   {
      charge_market_fee   = 0x01,
      white_list          = 0x02,
      halt_transfer       = 0x04,
      override_authority  = 0x08,
      market_issued       = 0x10
   };
   const static uint32_t ASSET_ISSUER_PERMISSION_MASK = 0x1f;

   enum reserved_spaces
   {
      relative_protocol_ids = 0,
      protocol_ids          = 1,
      implementation_ids    = 2
   };

   inline bool is_relative( object_id_type o ){ return o.space() == 0; }
   /**
    *  There are many types of fees charged by the network
    *  for different operations. These fees are published by
    *  the delegates and can change over time.
    */
   enum fee_type
   {
      key_create_fee_type, ///< the cost to register a public key with the blockchain
      account_create_fee_type, ///< the cost to register the cheapest non-free account
      account_whitelist_fee_type, ///< the fee to whitelist an account
      delegate_create_fee_type, ///< fixed fee for registering as a delegate, used to discourage frivioulous delegates
      witness_withdraw_pay_fee_type, ///< fee for withdrawing witness pay
      transfer_fee_type, ///< fee for transferring some asset
      limit_order_fee_type, ///< fee for placing a limit order in the markets
      short_order_fee_type, ///< fee for placing a short order in the markets
      publish_feed_fee_type, ///< fee for publishing a price feed
      asset_create_fee_type, ///< the cost to register the cheapest asset
      asset_update_fee_type, ///< the cost to modify a registered asset
      asset_issue_fee_type, ///< the cost to modify a registered asset
      asset_fund_fee_pool_fee_type, ///< the cost to add funds to an asset's fee pool
      asset_settle_fee_type, ///< the cost to trigger a forced settlement of a market-issued asset
      market_fee_type, ///< a percentage charged on market orders
      transaction_fee_type, ///< a base price for every transaction
      data_fee_type, ///< a price per 1024 bytes of user data
      signature_fee_type, ///< a surcharge on transactions with more than 2 signatures.
      global_parameters_update_fee_type, ///< the cost to update the global parameters
      prime_upgrade_fee_type, ///< the cost to upgrade an account to prime
      withdraw_permission_update_fee_type, ///< the cost to create/update a withdraw permission
      create_bond_offer_fee_type,
      cancel_bond_offer_fee_type,
      accept_bond_offer_fee_type,
      claim_bond_collateral_fee_type,
      file_storage_fee_per_day_type, ///< the cost of leasing a file with 2^16 bytes for 1 day
      vesting_balance_create_fee_type,
      vesting_balance_withdraw_fee_type,
      FEE_TYPE_COUNT ///< Sentry value which contains the number of different fee types
   };

   /**
    *  List all object types from all namespaces here so they can
    *  be easily reflected and displayed in debug output.  If a 3rd party
    *  wants to extend the core code then they will have to change the
    *  packed_object::type field from enum_type to uint16 to avoid
    *  warnings when converting packed_objects to/from json.
    */
   enum object_type
   {
      null_object_type,
      base_object_type,
      key_object_type,
      account_object_type,
      asset_object_type,
      force_settlement_object_type,
      delegate_object_type,
      witness_object_type,
      limit_order_object_type,
      short_order_object_type,
      call_order_object_type,
      custom_object_type,
      proposal_object_type,
      operation_history_object_type,
      withdraw_permission_object_type,
      bond_offer_object_type,
      bond_object_type,
      file_object_type,
      vesting_balance_object_type,
      OBJECT_TYPE_COUNT ///< Sentry value which contains the number of different object types
   };

   enum impl_object_type
   {
      impl_global_property_object_type,
      impl_dynamic_global_property_object_type,
      impl_index_meta_object_type,
      impl_asset_dynamic_data_type,
      impl_asset_bitasset_data_type,
      impl_delegate_feeds_object_type,
      impl_account_balance_object_type,
      impl_account_statistics_object_type,
      impl_account_debt_object_type,
      impl_transaction_object_type,
      impl_block_summary_object_type,
      impl_account_transaction_history_object_type
   };

   enum meta_info_object_type
   {
      meta_asset_object_type,
      meta_account_object_type
   };


   //typedef fc::unsigned_int            object_id_type;
   //typedef uint64_t                    object_id_type;
   class account_object;
   class delegate_object;
   class witness_object;
   class asset_object;
   class force_settlement_object;
   class key_object;
   class limit_order_object;
   class short_order_object;
   class call_order_object;
   class custom_object;
   class proposal_object;
   class operation_history_object;
   class withdraw_permission_object;
   class bond_object;
   class bond_offer_object;
   class file_object;
   class vesting_balance_object;


   typedef object_id< protocol_ids, key_object_type,                key_object>                   key_id_type;
   typedef object_id< protocol_ids, account_object_type,            account_object>               account_id_type;
   typedef object_id< protocol_ids, asset_object_type,              asset_object>                 asset_id_type;
   typedef object_id< protocol_ids, force_settlement_object_type,   force_settlement_object>      force_settlement_id_type;
   typedef object_id< protocol_ids, delegate_object_type,           delegate_object>              delegate_id_type;
   typedef object_id< protocol_ids, witness_object_type,            witness_object>               witness_id_type;
   typedef object_id< protocol_ids, limit_order_object_type,        limit_order_object>           limit_order_id_type;
   typedef object_id< protocol_ids, short_order_object_type,        short_order_object>           short_order_id_type;
   typedef object_id< protocol_ids, call_order_object_type,         call_order_object>            call_order_id_type;
   typedef object_id< protocol_ids, custom_object_type,             custom_object>                custom_id_type;
   typedef object_id< protocol_ids, proposal_object_type,           proposal_object>              proposal_id_type;
   typedef object_id< protocol_ids, operation_history_object_type,  operation_history_object>     operation_history_id_type;
   typedef object_id< protocol_ids, withdraw_permission_object_type,withdraw_permission_object>   withdraw_permission_id_type;
   typedef object_id< protocol_ids, bond_offer_object_type,         bond_offer_object>            bond_offer_id_type;
   typedef object_id< protocol_ids, bond_object_type,               bond_object>                  bond_id_type;
   typedef object_id< protocol_ids, file_object_type,               file_object>                  file_id_type;
   typedef object_id< protocol_ids, vesting_balance_object_type,    vesting_balance_object>       vesting_balance_id_type;

   typedef object_id< relative_protocol_ids, key_object_type, key_object>           relative_key_id_type;
   typedef object_id< relative_protocol_ids, account_object_type, account_object>   relative_account_id_type;

   // implementation types
   class global_property_object;
   class dynamic_global_property_object;
   class index_meta_object;
   class asset_dynamic_data_object;
   class asset_bitasset_data_object;
   class account_balance_object;
   class account_statistics_object;
   class account_debt_object;
   class transaction_object;
   class block_summary_object;
   class account_transaction_history_object;

   typedef object_id< implementation_ids, impl_global_property_object_type,  global_property_object>                    global_property_id_type;
   typedef object_id< implementation_ids, impl_dynamic_global_property_object_type,  dynamic_global_property_object>    dynamic_global_property_id_type;
   typedef object_id< implementation_ids, impl_asset_dynamic_data_type,      asset_dynamic_data_object>                 dynamic_asset_data_id_type;
   typedef object_id< implementation_ids, impl_asset_bitasset_data_type,     asset_bitasset_data_object>                asset_bitasset_data_id_type;
   typedef object_id< implementation_ids, impl_account_balance_object_type,  account_balance_object>                    account_balance_id_type;
   typedef object_id< implementation_ids, impl_account_statistics_object_type,account_statistics_object>                account_statistics_id_type;
   typedef object_id< implementation_ids, impl_account_debt_object_type,     account_debt_object>                       account_debt_id_type;
   typedef object_id< implementation_ids, impl_transaction_object_type,      transaction_object>                        transaction_obj_id_type;
   typedef object_id< implementation_ids, impl_block_summary_object_type,    block_summary_object>                      block_summary_id_type;

   typedef object_id< implementation_ids,
                      impl_account_transaction_history_object_type,
                      account_transaction_history_object>       account_transaction_history_id_type;


   typedef fc::array<char,BTS_MAX_SYMBOL_NAME_LENGTH>   symbol_type;
   typedef fc::ripemd160                                block_id_type;
   typedef fc::ripemd160                                checksum_type;
   typedef fc::ripemd160                                transaction_id_type;
   typedef fc::sha256                                   digest_type;
   typedef fc::ecc::compact_signature                   signature_type;
   typedef safe<int64_t>                                share_type;
   typedef fc::sha224                                   secret_hash_type;
   typedef uint16_t                                     weight_type;

   /**
    * @brief An ID for some votable object
    *
    * This class stores an ID for a votable object. The ID is comprised of two fields: a type, and an instance. The
    * type field stores which kind of object is being voted on, and the instance stores which specific object of that
    * type is being referenced by this ID.
    *
    * A value of vote_id_type is implicitly convertible to an unsigned 32-bit integer containing only the instance. It
    * may also be implicitly assigned from a uint32_t, which will update the instance. It may not, however, be
    * implicitly constructed from a uint32_t, as in this case, the type would be unknown.
    *
    * On the wire, a vote_id_type is represented as a 32-bit integer with the type in the lower 8 bits and the instance
    * in the upper 24 bits. This means that types may never exceed 8 bits, and instances may never exceed 24 bits.
    *
    * In JSON, a vote_id_type is represented as a string "type:instance", i.e. "1:5" would be type 1 and instance 5.
    *
    * @note In the Graphene protocol, vote_id_type instances are unique across types; that is to say, if an object of
    * type 1 has instance 4, an object of type 0 may not also have instance 4. In other words, the type is not a
    * namespace for instances; it is only an informational field.
    */
   struct vote_id_type
   {
      /// Lower 8 bits are type; upper 24 bits are instance
      uint32_t content;

      enum vote_type
      {
         committee,
         witness,
         worker,
         VOTE_TYPE_COUNT
      };

      /// Default constructor. Sets type and instance to 0
      vote_id_type():content(0){}
      /// Construct this vote_id_type with provided type and instance
      vote_id_type(vote_type type, uint32_t instance = 0)
         : content(instance<<8 | type)
      {}
      /// Construct this vote_id_type from a serial string in the form "type:instance"
      explicit vote_id_type(const std::string& serial)
      {
         auto colon = serial.find(':');
         if( colon != string::npos )
            *this = vote_id_type(vote_type(std::stoul(serial.substr(0, colon))), std::stoul(serial.substr(colon+1)));
      }

      /// Set the type of this vote_id_type
      void set_type(vote_type type)
      {
         content &= 0xffffff00;
         content |= type & 0xff;
      }
      /// Get the type of this vote_id_type
      vote_type type()const
      {
         return vote_type(content & 0xff);
      }

      /// Set the instance of this vote_id_type
      void set_instance(uint32_t instance)
      {
         assert(instance < 0x01000000);
         content &= 0xff;
         content |= instance << 8;
      }
      /// Get the instance of this vote_id_type
      uint32_t instance()const
      {
         return content >> 8;
      }

      vote_id_type& operator =(vote_id_type other)
      {
         content = other.content;
         return *this;
      }
      /// Set the instance of this vote_id_type
      vote_id_type& operator =(uint32_t instance)
      {
         set_instance(instance);
         return *this;
      }
      /// Get the instance of this vote_id_type
      operator uint32_t()const
      {
         return instance();
      }

      /// Convert this vote_id_type to a serial string in the form "type:instance"
      explicit operator std::string()const
      {
         return std::to_string(type()) + ":" + std::to_string(instance());
      }
   };

   struct fee_schedule_type
   {
       fee_schedule_type()
       {
          memset( (char*)fees.data, 0, sizeof(fees) );
       }
       void             set( uint32_t f, share_type v ){ FC_ASSERT( f < FEE_TYPE_COUNT && v.value <= uint32_t(-1) ); fees.at(f) = v.value; }
       const share_type at( uint32_t f )const { FC_ASSERT( f < FEE_TYPE_COUNT ); return fees.at(f); }
       size_t           size()const{ return fees.size(); }


       friend bool operator != ( const fee_schedule_type& a, const fee_schedule_type& b )
       {
          return a.fees != b.fees;
       }

       fc::array<uint32_t,FEE_TYPE_COUNT>    fees;
   };


   struct public_key_type
   {
       struct binary_key
       {
          binary_key():check(0){}
          uint32_t                 check;
          fc::ecc::public_key_data data;
       };

       fc::ecc::public_key_data key_data;

       public_key_type();
       public_key_type( const fc::ecc::public_key_data& data );
       public_key_type( const fc::ecc::public_key& pubkey );
       explicit public_key_type( const std::string& base58str );
       operator fc::ecc::public_key_data() const;
       operator fc::ecc::public_key() const;
       explicit operator std::string() const;
       friend bool operator == ( const public_key_type& p1, const fc::ecc::public_key& p2);
       friend bool operator == ( const public_key_type& p1, const public_key_type& p2);
       friend bool operator != ( const public_key_type& p1, const public_key_type& p2);
   };

   struct chain_parameters
   {
      fee_schedule_type       current_fees; // indexed by fee_type
      uint32_t                witness_pay_percent_of_accumulated  = BTS_DEFAULT_WITNESS_PAY_PERCENT_OF_ACCUMULATED;
      uint8_t                 block_interval                      = BTS_DEFAULT_BLOCK_INTERVAL; // seconds
      uint32_t                maintenance_interval                = BTS_DEFAULT_MAINTENANCE_INTERVAL;
      uint32_t                maximum_transaction_size            = BTS_DEFAULT_MAX_TRANSACTION_SIZE;
      uint32_t                maximum_block_size                  = BTS_DEFAULT_MAX_BLOCK_SIZE;
      uint32_t                maximum_undo_history                = BTS_DEFAULT_MAX_UNDO_HISTORY;
      uint32_t                maximum_time_until_expiration       = BTS_DEFAULT_MAX_TIME_UNTIL_EXPIRATION;
      uint32_t                maximum_proposal_lifetime           = BTS_DEFAULT_MAX_PROPOSAL_LIFETIME_SEC;
      uint32_t                genesis_proposal_review_period      = BTS_DEFAULT_GENESIS_PROPOSAL_REVIEW_PERIOD_SEC;
      uint8_t                 maximum_asset_whitelist_authorities = BTS_DEFAULT_MAX_ASSET_WHITELIST_AUTHORITIES;
      uint16_t                maximum_witness_count               = BTS_DEFAULT_NUM_WITNESSES; ///< maximum number of active witnesses
      uint16_t                maximum_committee_count             = BTS_DEFAULT_NUM_COMMITTEE; ///< maximum number of active delegates
      uint16_t                maximum_authority_membership        = BTS_DEFAULT_MAX_AUTHORITY_MEMBERSHIP; ///< largest number of keys/accounts an authority can have
      uint16_t                burn_percent_of_fee                 = BTS_DEFAULT_BURN_PERCENT_OF_FEE; ///< the percentage of every fee that is taken out of circulation
      uint16_t                witness_percent_of_fee              = BTS_DEFAULT_WITNESS_PERCENT; ///< percent of revenue paid to witnesses
      uint32_t                cashback_vesting_period_seconds     = BTS_DEFAULT_CASHBACK_VESTING_PERIOD_SEC; ///< time after cashback rewards are accrued before they become liquid
      uint16_t                max_bulk_discount_percent_of_fee    = BTS_DEFAULT_MAX_BULK_DISCOUNT_PERCENT; ///< the maximum percentage discount for bulk discounts
      share_type              bulk_discount_threshold_min         = BTS_DEFAULT_BULK_DISCOUNT_THRESHOLD_MIN; ///< the minimum amount of fees paid to qualify for bulk discounts
      share_type              bulk_discount_threshold_max         = BTS_DEFAULT_BULK_DISCOUNT_THRESHOLD_MAX; ///< the amount of fees paid to qualify for the max bulk discount percent
      uint8_t                 maximum_asset_feed_publishers       = BTS_DEFAULT_MAX_ASSET_FEED_PUBLISHERS; ///< the maximum number of feed publishers for a given asset

      void validate()const
      {
         FC_ASSERT( witness_percent_of_fee <= BTS_100_PERCENT );
         FC_ASSERT( burn_percent_of_fee <= BTS_100_PERCENT );
         FC_ASSERT( max_bulk_discount_percent_of_fee <= BTS_100_PERCENT );
         FC_ASSERT( burn_percent_of_fee + witness_percent_of_fee <= BTS_100_PERCENT );
         FC_ASSERT( bulk_discount_threshold_min <= bulk_discount_threshold_max );
         FC_ASSERT( bulk_discount_threshold_min > 0 );

         FC_ASSERT( witness_pay_percent_of_accumulated < BTS_WITNESS_PAY_PERCENT_PRECISION );
         FC_ASSERT( block_interval <= BTS_MAX_BLOCK_INTERVAL );
         FC_ASSERT( block_interval > 0 );
         FC_ASSERT( maintenance_interval > block_interval,
                    "Maintenance interval must be longer than block interval" );
         FC_ASSERT( maintenance_interval % block_interval == 0,
                    "Maintenance interval must be a multiple of block interval" );
         FC_ASSERT( maximum_transaction_size >= BTS_MIN_TRANSACTION_SIZE_LIMIT,
                    "Transaction size limit is too low" );
         FC_ASSERT( maximum_block_size >= BTS_MIN_BLOCK_SIZE_LIMIT,
                    "Block size limit is too low" );
         FC_ASSERT( maximum_time_until_expiration > block_interval,
                    "Maximum transaction expiration time must be greater than a block interval" );
         FC_ASSERT( maximum_proposal_lifetime - genesis_proposal_review_period > block_interval,
                    "Genesis proposal review period must be less than the maximum proposal lifetime" );
         for( auto fe : current_fees.fees ) FC_ASSERT( fe >= 0 );
      }
   };

} }  // bts::chain

namespace fc
{
    void to_variant( const bts::chain::public_key_type& var,  fc::variant& vo );
    void from_variant( const fc::variant& var,  bts::chain::public_key_type& vo );
    void to_variant( const bts::chain::fee_schedule_type& var,  fc::variant& vo );
    void from_variant( const fc::variant& var,  bts::chain::fee_schedule_type& vo );
    void to_variant( const bts::chain::vote_id_type& var, fc::variant& vo );
    void from_variant( const fc::variant& var, bts::chain::vote_id_type& vo );
}

FC_REFLECT_TYPENAME( bts::chain::vote_id_type::vote_type )
FC_REFLECT_ENUM( bts::chain::vote_id_type::vote_type, (witness)(committee)(worker)(VOTE_TYPE_COUNT) )
FC_REFLECT( bts::chain::vote_id_type, (content) )

FC_REFLECT( bts::chain::public_key_type, (key_data) )
FC_REFLECT( bts::chain::public_key_type::binary_key, (data)(check) )
FC_REFLECT( bts::chain::fee_schedule_type, (fees) )

FC_REFLECT_ENUM( bts::chain::object_type,
                 (null_object_type)
                 (base_object_type)
                 (key_object_type)
                 (account_object_type)
                 (force_settlement_object_type)
                 (asset_object_type)
                 (delegate_object_type)
                 (witness_object_type)
                 (limit_order_object_type)
                 (short_order_object_type)
                 (call_order_object_type)
                 (custom_object_type)
                 (proposal_object_type)
                 (operation_history_object_type)
                 (withdraw_permission_object_type)
                 (bond_offer_object_type)
                 (bond_object_type)
                 (file_object_type)
                 (vesting_balance_object_type)
                 (OBJECT_TYPE_COUNT)
               )
FC_REFLECT_ENUM( bts::chain::impl_object_type,
                 (impl_global_property_object_type)
                 (impl_dynamic_global_property_object_type)
                 (impl_index_meta_object_type)
                 (impl_asset_dynamic_data_type)
                 (impl_asset_bitasset_data_type)
                 (impl_delegate_feeds_object_type)
                 (impl_account_balance_object_type)
                 (impl_account_statistics_object_type)
                 (impl_account_debt_object_type)
                 (impl_transaction_object_type)
                 (impl_block_summary_object_type)
                 (impl_account_transaction_history_object_type)
               )

FC_REFLECT_ENUM( bts::chain::meta_info_object_type, (meta_account_object_type)(meta_asset_object_type) )

FC_REFLECT_ENUM( bts::chain::fee_type,
                 (key_create_fee_type)
                 (account_create_fee_type)
                 (account_whitelist_fee_type)
                 (delegate_create_fee_type)
                 (witness_withdraw_pay_fee_type)
                 (transfer_fee_type)
                 (limit_order_fee_type)
                 (short_order_fee_type)
                 (publish_feed_fee_type)
                 (asset_create_fee_type)
                 (asset_update_fee_type)
                 (asset_issue_fee_type)
                 (asset_fund_fee_pool_fee_type)
                 (asset_settle_fee_type)
                 (market_fee_type)
                 (transaction_fee_type)
                 (data_fee_type)
                 (signature_fee_type)
                 (global_parameters_update_fee_type)
                 (prime_upgrade_fee_type)
                 (withdraw_permission_update_fee_type)
                 (create_bond_offer_fee_type)
                 (cancel_bond_offer_fee_type)
                 (accept_bond_offer_fee_type)
                 (claim_bond_collateral_fee_type)
                 (file_storage_fee_per_day_type)
                 (vesting_balance_create_fee_type)
                 (vesting_balance_withdraw_fee_type)
                 (FEE_TYPE_COUNT)
               )

FC_REFLECT( bts::chain::chain_parameters,
            (current_fees)
            (witness_pay_percent_of_accumulated)
            (block_interval)
            (maintenance_interval)
            (maximum_transaction_size)
            (maximum_block_size)
            (maximum_undo_history)
            (maximum_time_until_expiration)
            (maximum_proposal_lifetime)
            (maximum_asset_whitelist_authorities)
            (maximum_authority_membership)
            (burn_percent_of_fee)
            (witness_percent_of_fee)
            (max_bulk_discount_percent_of_fee)
            (cashback_vesting_period_seconds)
            (bulk_discount_threshold_min)
            (bulk_discount_threshold_max)
          )

FC_REFLECT_TYPENAME( bts::chain::account_id_type )
FC_REFLECT_TYPENAME( bts::chain::asset_id_type )
FC_REFLECT_TYPENAME( bts::chain::operation_history_id_type )
