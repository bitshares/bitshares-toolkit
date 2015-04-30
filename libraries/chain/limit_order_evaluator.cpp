#include <bts/chain/limit_order_evaluator.hpp>
#include <bts/chain/account_object.hpp>
#include <bts/chain/limit_order_object.hpp>
#include <bts/chain/short_order_object.hpp>
#include <fc/uint128.hpp>

namespace bts { namespace chain {
object_id_type limit_order_create_evaluator::do_evaluate( const limit_order_create_operation& op )
{
   database& d = db();

   FC_ASSERT( op.expiration >= d.head_block_time() );

   _seller        = this->fee_paying_account;
   _sell_asset    = &op.amount_to_sell.asset_id(d);
   _receive_asset = &op.min_to_receive.asset_id(d);


   if( _sell_asset->options.flags & white_list ) FC_ASSERT( _seller->is_authorized_asset( *_sell_asset ) );
   if( _receive_asset->options.flags & white_list ) FC_ASSERT( _seller->is_authorized_asset( *_receive_asset ) );

   FC_ASSERT( d.get_balance( _seller, _sell_asset ) >= op.amount_to_sell, "insufficient balance",
              ("balance",d.get_balance(_seller,_sell_asset))("amount_to_sell",op.amount_to_sell) );

   return object_id_type();
}
template<typename I>
std::reverse_iterator<I> reverse( const I& itr ) { return std::reverse_iterator<I>(itr); }

object_id_type limit_order_create_evaluator::do_apply( const limit_order_create_operation& op )
{
   const auto& seller_stats = _seller->statistics(db());
   db().modify( seller_stats, [&]( account_statistics_object& bal ){
         if( op.amount_to_sell.asset_id == asset_id_type() )
         {
            bal.total_core_in_orders += op.amount_to_sell.amount;
         }
   });

   db().adjust_balance(op.seller, -op.amount_to_sell);

   const auto& new_order_object = db().create<limit_order_object>( [&]( limit_order_object& obj ){
       obj.seller   = _seller->id;
       obj.for_sale = op.amount_to_sell.amount;
       obj.sell_price = op.get_price();
       obj.expiration = op.expiration;
   });
   limit_order_id_type result = new_order_object.id; // save this because we may remove the object by filling it

   // Possible optimization: We only need to check calls if both are true:
   //  - The new order is at the front of the book
   //  - The new order is below the call limit price
   bool called_some = db().check_call_orders(*_sell_asset);
   called_some |= db().check_call_orders(*_receive_asset);
   if( called_some && !db().find(result) ) // then we were filled by call order
      return result;

   const auto& limit_order_idx = db().get_index_type<limit_order_index>();
   const auto& limit_price_idx = limit_order_idx.indices().get<by_price>();

   // TODO: it should be possible to simply check the NEXT/PREV iterator after new_order_object to
   // determine whether or not this order has "changed the book" in a way that requires us to
   // check orders.   For now I just lookup the lower bound and check for equality... this is log(n) vs
   // constant time check. Potential optimization.

   auto max_price  = ~op.get_price(); //op.min_to_receive / op.amount_to_sell;
   auto limit_itr = limit_price_idx.lower_bound( max_price.max() );
   auto limit_end = limit_price_idx.upper_bound( max_price );

   for( auto tmp = limit_itr; tmp != limit_end; ++tmp )
   {
      assert( tmp != limit_price_idx.end() );
   }

   bool filled = false;
   //if( new_order_object.amount_to_receive().asset_id(db()).is_market_issued() )
   if( _receive_asset->is_market_issued() )
   { // then we may also match against shorts
      if( _receive_asset->bitasset_data(db()).short_backing_asset == asset_id_type() )
      {
         bool converted_some = db().convert_fees( *_receive_asset );
         // just incase the new order was completely filled from fees
         if( converted_some && !db().find(result) ) // then we were filled by call order
            return result;
      }
      const auto& short_order_idx = db().get_index_type<short_order_index>();
      const auto& sell_price_idx = short_order_idx.indices().get<by_price>();

      FC_ASSERT( max_price.max() >= max_price );
      auto short_itr = sell_price_idx.lower_bound( max_price.max() );
      auto short_end = sell_price_idx.upper_bound( max_price );

      while( !filled )
      {
         if( limit_itr != limit_end )
         {
            if( short_itr != short_end && limit_itr->sell_price < short_itr->sell_price )
            {
               auto old_short_itr = short_itr;
               ++short_itr;
               filled = (db().match( new_order_object, *old_short_itr, old_short_itr->sell_price ) != 2 );
            }
            else
            {
               auto old_limit_itr = limit_itr;
               ++limit_itr;
               filled = (db().match( new_order_object, *old_limit_itr, old_limit_itr->sell_price ) != 2 );
            }
         }
         else if( short_itr != short_end  )
         {
            auto old_short_itr = short_itr;
            ++short_itr;
            filled = (db().match( new_order_object, *old_short_itr, old_short_itr->sell_price ) != 2 );
         }
         else break;
      }
   }
   else while( !filled && limit_itr != limit_end  )
   {
         auto old_itr = limit_itr;
         ++limit_itr;
         filled = (db().match( new_order_object, *old_itr, old_itr->sell_price ) != 2);
   }

   //Possible optimization: only check calls if the new order completely filled some old order
   //Do I need to check both assets?
   db().check_call_orders(*_sell_asset);
   db().check_call_orders(*_receive_asset);

   FC_ASSERT( !op.fill_or_kill || db().find_object(result) == nullptr );

   return result;
} // limit_order_evaluator::do_apply

asset limit_order_cancel_evaluator::do_evaluate( const limit_order_cancel_operation& o )
{
   database&    d = db();

   _order = &o.order(d);
   FC_ASSERT( _order->seller == o.fee_paying_account  );
   auto refunded = _order->amount_for_sale();
   //adjust_balance( fee_paying_account, &refunded.asset_id(d),  refunded.amount );

  return refunded;
}

asset limit_order_cancel_evaluator::do_apply( const limit_order_cancel_operation& o )
{
   database&   d = db();

   auto base_asset = _order->sell_price.base.asset_id;
   auto quote_asset = _order->sell_price.quote.asset_id;
   auto refunded = _order->amount_for_sale();

   db().cancel_order( *_order, false /* don't create a virtual op*/ );

   // Possible optimization: order can be called by canceling a limit order iff the canceled order was at the top of the book.
   // Do I need to check calls in both assets?
   db().check_call_orders(base_asset(d));
   db().check_call_orders(quote_asset(d));

   return refunded;
}
} } // bts::chain
