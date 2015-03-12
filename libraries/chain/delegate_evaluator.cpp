#include <bts/chain/delegate_evaluator.hpp>
#include <bts/chain/delegate_object.hpp>
#include <bts/chain/key_object.hpp>
#include <bts/chain/database.hpp>

namespace bts { namespace chain {
object_id_type delegate_create_evaluator::do_evaluate( const delegate_create_operation& op )
{
   database& d = db();

   auto bts_fee_paid = pay_fee( op.delegate_account, op.fee );
   auto bts_fee_required = op.calculate_fee( d.current_fee_schedule() );
   FC_ASSERT( bts_fee_paid >= bts_fee_required );
   op.signing_key(d); // deref just to check

   return object_id_type();
}

object_id_type delegate_create_evaluator::do_apply( const delegate_create_operation& op )
{
   apply_delta_balances();
   apply_delta_fee_pools();

   const auto& vote_obj = db().create<delegate_vote_object>( [&]( delegate_vote_object& obj ){
         // initial vote is 0
   });

   const auto& new_del_object = db().create<delegate_object>( [&]( delegate_object& obj ){
         obj.delegate_account         = op.delegate_account;
         obj.pay_rate                 = op.pay_rate;
         obj.signing_key              = op.signing_key;
         obj.next_secret              = op.first_secret_hash;
         obj.fee_schedule             = op.fee_schedule;
         obj.block_interval_sec       = op.block_interval_sec;
         obj.max_block_size           = op.max_block_size;
         obj.max_transaction_size     = op.max_transaction_size;
         obj.max_sec_until_expiration = op.max_sec_until_expiration;
         obj.vote                     = vote_obj.id;

   });
   return new_del_object.id;
}


object_id_type delegate_update_evaluator::do_evaluate( const delegate_update_operation& op )
{
   database& d = db();
   const delegate_object& del = op.delegate_id(d);

   auto bts_fee_paid = pay_fee( del.delegate_account, op.fee );
   auto bts_fee_required = op.calculate_fee( d.current_fee_schedule() );
   FC_ASSERT( bts_fee_paid >= bts_fee_required );

   if( op.fee_schedule ) FC_ASSERT( del.fee_schedule != *op.fee_schedule );
   if( op.pay_rate <= 255 ) FC_ASSERT( op.pay_rate != del.pay_rate );
   if( op.signing_key && !op.signing_key->is_relative() )
   {
      FC_ASSERT( *op.signing_key != del.signing_key );
      FC_ASSERT( d.find(key_id_type(op.signing_key->instance)) );
   }

   return object_id_type();
}

object_id_type delegate_update_evaluator::do_apply( const delegate_update_operation& op )
{
   apply_delta_balances();
   apply_delta_fee_pools();

   db().modify<delegate_object>( op.delegate_id(db()), [&]( delegate_object& obj ){
         if( op.pay_rate <= 100 ) obj.pay_rate     = op.pay_rate;
         if( op.signing_key     ) obj.signing_key  = get_relative_id( *op.signing_key );
         if( op.fee_schedule    ) obj.fee_schedule = *op.fee_schedule;

         obj.block_interval_sec       = op.block_interval_sec;
         obj.max_block_size           = op.max_block_size;
         obj.max_transaction_size     = op.max_transaction_size;
         obj.max_sec_until_expiration = op.max_sec_until_expiration;
   });
   return object_id_type();
}


} } // bts::chain
