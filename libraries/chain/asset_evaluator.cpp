#include <bts/chain/asset_evaluator.hpp>
#include <bts/chain/asset_object.hpp>
#include <bts/chain/account_object.hpp>
#include <bts/chain/database.hpp>

namespace bts { namespace chain {
object_id_type asset_create_evaluator::do_evaluate( const asset_create_operation& op )
{ try {
   database& d = db();

   auto& asset_indx = db().get_index_type<asset_index>();
   auto asset_symbol_itr = asset_indx.indices().get<by_symbol>().find( op.symbol );
   FC_ASSERT( asset_symbol_itr == asset_indx.indices().get<by_symbol>().end() );

   auto bts_fee_paid = pay_fee( op.issuer, op.fee );
   bts_fee_required = op.calculate_fee( d.current_fee_schedule() );
   FC_ASSERT( bts_fee_paid >= bts_fee_required );

   const asset_object& core_asset = d.get_core_asset();
   fees_paid[&core_asset].to_issuer -= bts_fee_required.value/2;
   assert( fees_paid[&core_asset].to_issuer >= 0 );

   FC_ASSERT( &op.short_backing_asset(d) != nullptr );

   return object_id_type();
} FC_CAPTURE_AND_RETHROW( (op) ) }

object_id_type asset_create_evaluator::do_apply( const asset_create_operation& op )
{
   apply_delta_balances();
   apply_delta_fee_pools();

   const asset_dynamic_data_object& dyn_asset =
      db().create<asset_dynamic_data_object>( [&]( asset_dynamic_data_object& a ) {
         a.current_supply = 0;
         a.fee_pool = bts_fee_required.value / 2;
      });

   auto next_asset_id = db().get_index_type<asset_index>().get_next_id();

   const asset_object& new_asset =
     db().create<asset_object>( [&]( asset_object& a ) {
         a.symbol = op.symbol;
         a.max_supply = op.max_supply;
         a.market_fee_percent = op.market_fee_percent;
         a.flags = op.flags;
         a.issuer_permissions = op.permissions;
         a.short_backing_asset = op.short_backing_asset;
         a.issuer = op.issuer;
         a.core_exchange_rate = op.core_exchange_rate;
         a.core_exchange_rate.base.asset_id = 0;
         a.core_exchange_rate.quote.asset_id = next_asset_id;
         a.dynamic_asset_data_id = dyn_asset.id;
      });
   assert( new_asset.id == next_asset_id );

   return next_asset_id;
}

object_id_type asset_issue_evaluator::do_evaluate( const asset_issue_operation& o )
{ try {
   database& d   = db();

   const asset_object& a = o.asset_to_issue.asset_id(d);
   FC_ASSERT( !(a.issuer_permissions & market_issued) );

   auto bts_fee_paid = pay_fee( a.issuer, o.fee );
   bts_fee_required = o.calculate_fee( d.current_fee_schedule() );
   FC_ASSERT( bts_fee_paid >= bts_fee_required );

   to_account = &o.issue_to_account(d);

   if( a.flags & white_list )
   {
      FC_ASSERT( to_account->is_authorized_asset( a.id ) );
   }

   asset_dyn_data = &a.dynamic_asset_data_id(d);
   FC_ASSERT( (asset_dyn_data->current_supply + o.asset_to_issue.amount) <= a.max_supply );

   adjust_balance( to_account, &a, o.asset_to_issue.amount );

   return object_id_type();

} FC_CAPTURE_AND_RETHROW( (o) ) }

object_id_type asset_issue_evaluator::do_apply( const asset_issue_operation& o )
{
   apply_delta_balances();
   apply_delta_fee_pools();

   db().modify( *asset_dyn_data, [&]( asset_dynamic_data_object& data ){
        data.current_supply += o.asset_to_issue.amount;
   });

   return object_id_type();
}

object_id_type asset_fund_fee_pool_evaluator::do_evaluate(const asset_fund_fee_pool_operation& o)
{ try {
   database& d = db();

   const asset_object& a = o.asset_id(d);

   auto bts_fee_paid = pay_fee( a.issuer, o.fee );
   bts_fee_required = o.calculate_fee( d.current_fee_schedule() );
   FC_ASSERT( bts_fee_paid >= bts_fee_required );

   asset_dyn_data = &a.dynamic_asset_data_id(d);
   adjust_balance(&o.from_account(d), &d.get_core_asset(), -o.amount);

   return object_id_type();
} FC_CAPTURE_AND_RETHROW( (o) ) }

object_id_type asset_fund_fee_pool_evaluator::do_apply(const asset_fund_fee_pool_operation& o)
{
   apply_delta_balances();
   apply_delta_fee_pools();

   db().modify( *asset_dyn_data, [&]( asset_dynamic_data_object& data ) {
      data.fee_pool += o.amount;
   });

   return object_id_type();
}


} } // bts::chain
