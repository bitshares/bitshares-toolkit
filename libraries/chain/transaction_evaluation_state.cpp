#include <bts/chain/transaction_evaluation_state.hpp>

namespace bts { namespace chain { 
   bool transaction_evaluation_state::check_authority( const address_authority& auth )const
   {
      if( _skip_signature_check ) return true;
      uint32_t count = 0;
      for( auto a : auth.addresses )
      {
         count += (signed_by.find( a ) != signed_by.end());
         if( count >= auth.required ) return true;
      }
      return false;
   }

   void transaction_evaluation_state::withdraw_from_account( account_id_type account_id, const asset& what )
   { try {
       FC_ASSERT( what.amount > 0 );
       auto asset_obj = _db->get<asset_object>(what.asset_id);
       FC_ASSERT( asset_obj );
       auto from_account = _db->get<account_object>( account_id );
       FC_ASSERT( from_account );

       if( !eval_state.check_authority( from_account->active )  )
          FC_CAPTURE_AND_THROW( missing_signature, (from_account->active) );

       auto const_acc_balances = _db->get<account_balance_object>( from_account->balances );
       FC_ASSERT( const_acc_balances );
       FC_ASSERT( const_acc_balances->get_balance() >= what );
       auto mutable_balance = _db->get_mutable<account_balance_object>( acc->balances );
       mutable_balance->sub_balance( what );

       if( asset_obj->issuer == 0 )
          adjust_votes( acc->delegate_votes, -what.amount );

   } FC_CAPTURE_AND_RETHROW( (account_id)(what) ) }

   void transaction_evaluation_state::deposit_to_account( account_id_type account_id, const asset& what )
   { try {
       auto asset_obj = _db->get<asset_object>(what.asset_id);
       FC_ASSERT( asset_obj );
       auto acc = _db->get<account_object>( account_id );
       FC_ASSERT( what.amount > 0 );
       FC_ASSERT( acc );
       auto mutable_balance = _db->get_mutable<account_balance_object>( acc->balances );
       mutable_balance->add_balance( what );

       if( asset_obj->issuer == 0 )
          adjust_votes( acc->delegate_votes, what.amount );
   } FC_CAPTURE_AND_RETHROW( (account_id)(what) ) }

} } // namespace bts::chain
