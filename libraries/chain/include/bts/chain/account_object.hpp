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
         static const object_type type = impl_account_balance_object_type;
         static const uint16_t id_space = implementation_ids;

         account_balance_object():object( impl_account_balance_object_type ){};

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

   class account_object : public object
   {
      public:
         static const object_type type = account_object_type;

         account_object():object( account_object_type ){};

         bool                  is_for_sale()const { return for_sale.first != 0; }

         void                  authorize_asset( asset_id_type asset_id, bool state );
         bool                  is_authorized_asset( asset_id_type )const;

         string                name;
         authority             owner;
         authority             active;
         authority             voting;

         vector<delegate_id_type>      delegate_votes;

         /**
          *  If the account is for sale, list the price and account that
          *  should be paid.  If the account that should be paid is 0 then
          *  this account is not for sale.
          */
         pair<account_id_type, asset>  for_sale;

         object_id_type                          balances;
         vector<asset_id_type>                   authorized_assets;
         delegate_id_type                        delegate_id; // optional
   };

}} 
FC_REFLECT_DERIVED( bts::chain::account_object, 
                    (bts::chain::object), 
                    (name)(owner)(active)(voting)(delegate_votes)(for_sale)(balances)(authorized_assets)(delegate_id) )

FC_REFLECT_DERIVED( bts::chain::account_balance_object, (bts::chain::object), (balances) )
