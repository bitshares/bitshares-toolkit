#include <bts/chain/database.hpp>
#include <bts/chain/short_order_evaluator.hpp>
#include <bts/chain/account_object.hpp>
#include <bts/chain/short_order_object.hpp>
#include <bts/chain/limit_order_object.hpp>
#include <fc/uint128.hpp>

namespace bts { namespace chain {
object_id_type short_order_create_evaluator::do_evaluate( const short_order_create_operation& op )
{
   database& d = db();

   FC_ASSERT( op.expiration >= d.head_block_time() );

   const asset_object& base_asset  = op.amount_to_sell.asset_id(d);
   const asset_object& quote_asset = op.collateral.asset_id(d);

   FC_ASSERT( base_asset.is_market_issued() );
   FC_ASSERT( quote_asset.id == base_asset.short_backing_asset );
   _seller = fee_paying_account;
   _receive_asset = &quote_asset;
   _sell_asset    = &base_asset;

   // TODO: FC_ASSERT( op.initial_collateral_ratio >= CURRENT_INIT_COLLATERAL_RATIO_REQUIREMENTS )
   // TODO: FC_ASSERT( op.maintenance_collateral_ratio >= CURRENT_INIT_COLLATERAL_RATIO_REQUIREMENTS )
   // TODO: FC_ASSERT( op.sell_price() >= CURRENT_PRICE_LIMIT  )

   return object_id_type();
}

object_id_type short_order_create_evaluator::do_apply( const short_order_create_operation& op )
{
   db().adjust_balance(op.seller, -op.collateral);

   const auto& new_order_object = db().create<short_order_object>( [&]( short_order_object& obj ){
       obj.seller                       = _seller->id;
       obj.for_sale                     = op.amount_to_sell.amount;
       obj.available_collateral         = op.collateral.amount;
       obj.sell_price                   = op.sell_price();
       obj.call_price                   = op.call_price();
       obj.initial_collateral_ratio     = op.initial_collateral_ratio;
       obj.maintenance_collateral_ratio = op.maintenance_collateral_ratio;
       obj.expiration                   = op.expiration;
   });
   short_order_id_type new_id = new_order_object.id;

   if( op.collateral.asset_id == asset_id_type() )
   {
      auto& bal_obj = fee_paying_account->statistics(db());
      db().modify( bal_obj, [&]( account_statistics_object& obj ){
          obj.total_core_in_orders += op.collateral.amount;
      });
   }

   // Possible optimization: We only need to check calls if both are true:
   //  - The new order is at the front of the book
   //  - The new order is below the call limit price
   db().check_call_orders(*_sell_asset);

   if( !db().find(new_id) ) // then we were filled by call order
      return new_id;

   const auto& limit_order_idx = db().get_index_type<limit_order_index>();
   const auto& limit_price_idx = limit_order_idx.indices().get<by_price>();

   auto min_limit_price  = ~op.sell_price();

   auto itr = limit_price_idx.lower_bound( min_limit_price.max() );
   auto end = limit_price_idx.upper_bound( min_limit_price );

   while( itr != end )
   {
      auto old_itr = itr;
      ++itr;
      if( db().match( *old_itr, new_order_object, old_itr->sell_price ) != 1 )
         break; // 1 means ONLY old iter filled
   }

   //Possible optimization: only check calls if the new order completely filled some old order
   //Do I need to check both assets?
   db().check_call_orders(*_sell_asset);
   db().check_call_orders(*_receive_asset);

   return new_id;
} // short_order_evaluator::do_apply


asset short_order_cancel_evaluator::do_evaluate( const short_order_cancel_operation& o )
{
   database&    d = db();

   _order = &o.order(d);
   FC_ASSERT( _order->seller == o.fee_paying_account  );

  return _order->get_collateral();
}

asset short_order_cancel_evaluator::do_apply( const short_order_cancel_operation& o )
{
   database&   d = db();

   auto refunded = _order->get_collateral();
   d.adjust_balance(o.fee_paying_account, refunded);
   auto base_asset = _order->sell_price.base.asset_id;
   auto quote_asset = _order->sell_price.quote.asset_id;

   d.remove( *_order );

   if( refunded.asset_id == asset_id_type() )
   {
      auto& stats_obj = fee_paying_account->statistics(d);
      d.modify( stats_obj, [&]( account_statistics_object& obj ){
          obj.total_core_in_orders -= refunded.amount;
      });
   }

   // Possible optimization: order can be called by canceling a short order iff the canceled order was at the top of the book.
   // Do I need to check calls in both assets?
   db().check_call_orders(base_asset(d));
   db().check_call_orders(quote_asset(d));

   return refunded;
}

asset call_order_update_evaluator::do_evaluate(const call_order_update_operation& o)
{ try {
   database& d = db();

   _paying_account = &o.funding_account(d);

   _debt_asset = &o.amount_to_cover.asset_id(d);
   FC_ASSERT( _debt_asset->is_market_issued(), "Unable to cover ${sym} as it is not a market-issued asset.",
              ("sym", _debt_asset->symbol) );
   FC_ASSERT( o.collateral_to_add.asset_id == _debt_asset->short_backing_asset );
   FC_ASSERT( o.maintenance_collateral_ratio == 0 ||
              o.maintenance_collateral_ratio > _debt_asset->current_feed.required_maintenance_collateral );
   FC_ASSERT( d.get_balance(*_paying_account, *_debt_asset) >= o.amount_to_cover,
              "Cannot cover by ${c} when payer has ${b}",
              ("c", o.amount_to_cover.amount)("b", d.get_balance(*_paying_account, *_debt_asset).amount) );
   FC_ASSERT( d.get_balance(*_paying_account, _debt_asset->short_backing_asset(d)) >= o.collateral_to_add,
              "Cannot increase collateral by ${c} when payer has ${b}", ("c", o.amount_to_cover.amount)
              ("b", d.get_balance(*_paying_account, _debt_asset->short_backing_asset(d)).amount) );

   auto& call_idx = d.get_index_type<call_order_index>().indices().get<by_account>();
   auto itr = call_idx.find( boost::make_tuple(o.funding_account, o.amount_to_cover.asset_id) );
   FC_ASSERT( itr != call_idx.end(), "Could not find call order for ${sym} belonging to ${acct}.",
              ("sym", _debt_asset->symbol)("acct", _paying_account->name) );
   _order = &*itr;

   FC_ASSERT( o.amount_to_cover.asset_id == _order->debt_type() );

   FC_ASSERT( o.amount_to_cover.amount <= _order->get_debt().amount );

   if( o.amount_to_cover.amount < _order->get_debt().amount )
   {
      FC_ASSERT( (_order->get_debt() - o.amount_to_cover) *
                 price::call_price(_order->get_debt() - o.amount_to_cover,
                                   _order->get_collateral() + o.collateral_to_add,
                                   o.maintenance_collateral_ratio? o.maintenance_collateral_ratio
                                                                 : _order->maintenance_collateral_ratio)
                 < _order->get_collateral(),
                 "Order would be called immediately following this update. Refusing to apply update." );
      FC_ASSERT( o.amount_to_cover < _order->get_debt(), "Cover amount is greater than debt." );
   } else {
      _closing_order = true;
      FC_ASSERT( o.collateral_to_add.amount == -_order->get_collateral().amount, "",
                 ("collateral", _order->get_collateral()) );
      return _order->get_collateral();
   }
   return asset();
} FC_CAPTURE_AND_RETHROW( (o) ) }

asset call_order_update_evaluator::do_apply(const call_order_update_operation& o)
{
   database& d = db();

   d.adjust_balance(_paying_account->get_id(), -o.amount_to_cover);

   // Deduct the debt paid from the total supply of the debt asset.
   d.modify(_debt_asset->dynamic_asset_data_id(d), [&](asset_dynamic_data_object& dynamic_asset) {
      dynamic_asset.current_supply -= o.amount_to_cover.amount;
      assert(dynamic_asset.current_supply >= 0);
   });

   asset collateral_returned;
   if( _closing_order )
   {
      collateral_returned = _order->get_collateral();
      // Credit the account's balances for his returned collateral.
      d.adjust_balance(_paying_account->get_id(), collateral_returned);
      d.modify(_paying_account->statistics(d), [&](account_statistics_object& stats) {
         if( _order->get_collateral().asset_id == asset_id_type() )
            stats.total_core_in_orders -= collateral_returned.amount;
      });
      // Remove the call order.
      d.remove(*_order);
   } else {
      // Update the call order.
      d.modify(*_order, [&o](call_order_object& call) {
         call.debt -= o.amount_to_cover.amount;
         call.collateral += o.collateral_to_add.amount;
         if( o.maintenance_collateral_ratio )
            call.maintenance_collateral_ratio = o.maintenance_collateral_ratio;
         call.update_call_price();
      });
      if( o.collateral_to_add.amount > 0 )
         // Deduct the added collateral from the account.
         d.adjust_balance(_paying_account->get_id(), -o.collateral_to_add);
         d.modify(_paying_account->statistics(d), [&](account_statistics_object& stats) {
            if( o.collateral_to_add.asset_id == asset_id_type() )
               stats.total_core_in_orders += o.collateral_to_add.amount;
         });
   }

   return collateral_returned;
}

} } // bts::chain
