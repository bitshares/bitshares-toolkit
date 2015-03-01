#pragma once
#include <bts/chain/database.hpp>
#include <bts/chain/authority.hpp>
#include <bts/chain/asset.hpp>

namespace bts { namespace chain {
   /**
    *  @class account_balance_object
    *  @ingroup implementation
    *
    *  This object is provided for the purpose of separating the account data that
    *  changes frequently from the account data that is mostly static.  This will
    *  minimize the amount of data that must be backed up as part of the undo
    *  history everytime a transfer is made.  
    *
    *  Note: a single account with 1000 different asset types will require
    *  16KB in the undo buffer... this could significantly degrade performance
    *  at a large scale.  A future optimization would be to have a balance
    *  object for each asset type or at the very least group assets into
    *  smaller numbers.  
    */
   class account_balance_object : public object
   {
      public:
         static const uint8_t space_id = implementation_ids;
         static const uint8_t type_id  = impl_account_balance_object_type;

         void                  add_balance( const asset& a );
         void                  sub_balance( const asset& a );
         asset                 get_balance( asset_id_type asset_id )const;

         /**
          * Keep balances sorted for best performance of lookups in log(n) time,
          * balances need to be moved to their own OBJECT ID because they
          * will change all the time and are much smaller than an account.
          */
         vector<pair<asset_id_type,share_type> > balances;
   };

   class account_debt_object : public object
   {
      public:
         static const uint8_t space_id = implementation_ids;
         static const uint8_t type_id  = impl_account_debt_object_type;

         flat_map<asset_id_type, object_id_type> call_orders;
   };

   class account_object : public annotated_object
   {
      public:
         static const uint8_t space_id = protocol_ids;
         static const uint8_t type_id  = account_object_type;

         const string& get_name()const { return name; }

         void                  authorize_asset( asset_id_type asset_id, bool state );
         bool                  is_authorized_asset( asset_id_type )const;

         string                name;
         authority             owner;
         authority             active;
         key_id_type           memo_key;
         key_id_type           voting_key;

         vector<delegate_id_type> delegate_votes;

         account_balance_id_type  balances;
         account_debt_id_type     debts;
         flat_set<asset_id_type>  authorized_assets;
   };

   /**
    *  This object is attacked as the meta annotation on the account object, this
    *  information is not relevant to validation.
    */
   class meta_account_object : public object
   {
      public:
         static const uint8_t space_id = implementation_ids;
         static const uint8_t type_id  = meta_account_object_type;

         key_id_type         memo_key;
         delegate_id_type    delegate_id; // optional
   };

}} 
FC_REFLECT_DERIVED( bts::chain::account_object, 
                    (bts::chain::annotated_object), 
                    (name)(owner)(active)(memo_key)(voting_key)(delegate_votes)(balances)(debts)(authorized_assets) )

FC_REFLECT_DERIVED( bts::chain::meta_account_object, 
                    (bts::chain::object), 
                    (memo_key)(delegate_id) )

FC_REFLECT_DERIVED( bts::chain::account_balance_object, (bts::chain::object), (balances) )
FC_REFLECT_DERIVED( bts::chain::account_debt_object, (bts::chain::object), (call_orders) );
