#include <bts/chain/asset_evaluator.hpp>
#include <bts/chain/asset_object.hpp>
#include <bts/chain/account_object.hpp>
#include <bts/chain/short_order_object.hpp>
#include <bts/chain/database.hpp>

#include <functional>

namespace bts { namespace chain {
object_id_type asset_create_evaluator::do_evaluate( const asset_create_operation& op )
{ try {
   database& d = db();

   const auto& chain_parameters = d.get_global_properties().parameters;
   FC_ASSERT( op.common_options.whitelist_authorities.size() <= chain_parameters.maximum_asset_whitelist_authorities );
   FC_ASSERT( op.common_options.blacklist_authorities.size() <= chain_parameters.maximum_asset_whitelist_authorities );

   // Check that all authorities do exist
   for( auto id : op.common_options.whitelist_authorities )
      d.get_object(id);
   for( auto id : op.common_options.blacklist_authorities )
      d.get_object(id);

   auto& asset_indx = db().get_index_type<asset_index>().indices().get<by_symbol>();
   auto asset_symbol_itr = asset_indx.find( op.symbol );
   FC_ASSERT( asset_symbol_itr == asset_indx.end() );

   core_fee_paid -= op.calculate_fee(d.current_fee_schedule()).value/2;
   assert( core_fee_paid >= 0 );

   if( op.bitasset_options )
   {
      const asset_object&  backing = op.bitasset_options->short_backing_asset(d);
      if( backing.bitasset_data_id )
      {
         const asset_bitasset_data_object& backing_bitasset_data = (*(backing.bitasset_data_id))(d);
         const asset_object& backing_backing = backing_bitasset_data.short_backing_asset(d);
         FC_ASSERT( !backing_backing.bitasset_data_id );
      }
      FC_ASSERT( op.bitasset_options->feed_lifetime_sec > chain_parameters.block_interval &&
                 op.bitasset_options->force_settlement_delay_sec > chain_parameters.block_interval );
   }

   return object_id_type();
} FC_CAPTURE_AND_RETHROW( (op) ) }

object_id_type asset_create_evaluator::do_apply( const asset_create_operation& op )
{
   const asset_dynamic_data_object& dyn_asset =
      db().create<asset_dynamic_data_object>( [&]( asset_dynamic_data_object& a ) {
         a.current_supply = 0;
         a.fee_pool = op.calculate_fee(db().current_fee_schedule()).value / 2;
      });

   asset_bitasset_data_id_type bit_asset_id;
   if( op.common_options.flags & market_issued )
      bit_asset_id = db().create<asset_bitasset_data_object>( [&]( asset_bitasset_data_object& a ) {
            a.options = *op.bitasset_options;
         }).id;

   auto next_asset_id = db().get_index_type<asset_index>().get_next_id();

   const asset_object& new_asset =
     db().create<asset_object>( [&]( asset_object& a ) {
         a.issuer = op.issuer;
         a.symbol = op.symbol;
         a.precision = op.precision;
         a.options = op.common_options;
         if( a.options.core_exchange_rate.base.asset_id.instance.value == 0 )
            a.options.core_exchange_rate.quote.asset_id = next_asset_id;
         else
            a.options.core_exchange_rate.base.asset_id = next_asset_id;
         a.dynamic_asset_data_id = dyn_asset.id;
         if( a.is_market_issued() )
            a.bitasset_data_id = bit_asset_id;
      });
   assert( new_asset.id == next_asset_id );

   return next_asset_id;
}

object_id_type asset_issue_evaluator::do_evaluate( const asset_issue_operation& o )
{ try {
   database& d   = db();

   const asset_object& a = o.asset_to_issue.asset_id(d);
   FC_ASSERT( o.issuer == a.issuer );
   FC_ASSERT( !(a.options.issuer_permissions & market_issued) );

   to_account = &o.issue_to_account(d);

   if( a.options.flags & white_list )
   {
      FC_ASSERT( to_account->is_authorized_asset( a ) );
   }

   asset_dyn_data = &a.dynamic_asset_data_id(d);
   FC_ASSERT( (asset_dyn_data->current_supply + o.asset_to_issue.amount) <= a.options.max_supply );

   return object_id_type();
} FC_CAPTURE_AND_RETHROW( (o) ) }

object_id_type asset_issue_evaluator::do_apply( const asset_issue_operation& o )
{
   db().adjust_balance( o.issue_to_account, o.asset_to_issue );

   db().modify( *asset_dyn_data, [&]( asset_dynamic_data_object& data ){
        data.current_supply += o.asset_to_issue.amount;
   });

   return object_id_type();
}

object_id_type asset_burn_evaluator::do_evaluate( const asset_burn_operation& o )
{ try {
   database& d   = db();

   const asset_object& a = o.amount_to_burn.asset_id(d);
   FC_ASSERT( !a.is_market_issued() );

   from_account = &o.payer(d);

   if( a.options.flags & white_list )
   {
      FC_ASSERT( from_account->is_authorized_asset( a ) );
   }

   asset_dyn_data = &a.dynamic_asset_data_id(d);
   FC_ASSERT( (asset_dyn_data->current_supply - o.amount_to_burn.amount) >= 0 );

   return object_id_type();
} FC_CAPTURE_AND_RETHROW( (o) ) }

object_id_type asset_burn_evaluator::do_apply( const asset_burn_operation& o )
{
   db().adjust_balance( o.payer, -o.amount_to_burn );

   db().modify( *asset_dyn_data, [&]( asset_dynamic_data_object& data ){
        data.current_supply -= o.amount_to_burn.amount;
   });

   return object_id_type();
}

object_id_type asset_fund_fee_pool_evaluator::do_evaluate(const asset_fund_fee_pool_operation& o)
{ try {
   database& d = db();

   const asset_object& a = o.asset_id(d);

   asset_dyn_data = &a.dynamic_asset_data_id(d);

   return object_id_type();
} FC_CAPTURE_AND_RETHROW( (o) ) }

object_id_type asset_fund_fee_pool_evaluator::do_apply(const asset_fund_fee_pool_operation& o)
{
   db().adjust_balance(o.from_account, -o.amount);

   db().modify( *asset_dyn_data, [&]( asset_dynamic_data_object& data ) {
      data.fee_pool += o.amount;
   });

   return object_id_type();
}

object_id_type asset_update_evaluator::do_evaluate(const asset_update_operation& o)
{ try {
   database& d = db();

   if( o.new_issuer ) FC_ASSERT(d.find_object(*o.new_issuer));

   const asset_object& a = o.asset_to_update(d);

   FC_ASSERT((a.options.flags & market_issued) == (o.new_options.flags & market_issued),
             "Cannot convert a market-issued asset to/from a user-issued asset.");
   //There must be no bits set in o.permissions which are unset in a.issuer_permissions.
   FC_ASSERT(!(o.new_options.issuer_permissions & ~a.options.issuer_permissions),
             "Cannot reinstate previously revoked issuer permissions on an asset.");

   asset_to_update = &a;
   FC_ASSERT( o.issuer == a.issuer, "", ("o.issuer", o.issuer)("a.issuer", a.issuer) );

   const auto& chain_parameters = d.get_global_properties().parameters;

   FC_ASSERT( o.new_options.whitelist_authorities.size() <= chain_parameters.maximum_asset_whitelist_authorities );
   for( auto id : o.new_options.whitelist_authorities )
      d.get_object(id);
   FC_ASSERT( o.new_options.blacklist_authorities.size() <= chain_parameters.maximum_asset_whitelist_authorities );
   for( auto id : o.new_options.blacklist_authorities )
      d.get_object(id);

   return object_id_type();
} FC_CAPTURE_AND_RETHROW((o)) }

object_id_type asset_update_evaluator::do_apply(const asset_update_operation& o)
{
   db().modify(*asset_to_update, [&](asset_object& a) {
      if( o.new_issuer )
         a.issuer = *o.new_issuer;
      a.options = o.new_options;
   });

   return object_id_type();
}

object_id_type asset_update_bitasset_evaluator::do_evaluate(const asset_update_bitasset_operation& o)
{
   database& d = db();

   const asset_object& a = o.asset_to_update(d);

   FC_ASSERT(a.is_market_issued(), "Cannot update BitAsset-specific settings on a non-BitAsset.");

   const asset_bitasset_data_object& b = a.bitasset_data(d);
   if( o.new_options.short_backing_asset != b.short_backing_asset )
   {
      FC_ASSERT(a.dynamic_asset_data_id(d).current_supply == 0);
      FC_ASSERT(d.find_object(o.new_options.short_backing_asset));
   }

   bitasset_to_update = &b;
   FC_ASSERT( o.issuer == a.issuer, "", ("o.issuer", o.issuer)("a.issuer", a.issuer) );

   return object_id_type();
}

object_id_type asset_update_bitasset_evaluator::do_apply(const asset_update_bitasset_operation& o)
{
   db().modify(*bitasset_to_update, [&o](asset_bitasset_data_object& b) {
      b.options = o.new_options;
   });

   return object_id_type();
}

object_id_type asset_update_feed_producers_evaluator::do_evaluate(const asset_update_feed_producers_evaluator::operation_type& o)
{
   database& d = db();

   FC_ASSERT( o.new_feed_producers.size() <= d.get_global_properties().parameters.maximum_asset_feed_publishers );
   for( auto id : o.new_feed_producers )
      d.get_object(id);

   const asset_object& a = o.asset_to_update(d);

   FC_ASSERT(a.is_market_issued(), "Cannot update feed producers on a non-BitAsset.");
   FC_ASSERT(a.issuer != account_id_type(), "Cannot set feed producers on a genesis-issued asset.");

   const asset_bitasset_data_object& b = a.bitasset_data(d);
   bitasset_to_update = &b;
   FC_ASSERT( a.issuer == o.issuer );
   return object_id_type();
}

object_id_type asset_update_feed_producers_evaluator::do_apply(const asset_update_feed_producers_evaluator::operation_type& o)
{
   db().modify(*bitasset_to_update, [&](asset_bitasset_data_object& a) {
      //This is tricky because I have a set of publishers coming in, but a map of publisher to feed is stored.
      //I need to update the map such that the keys match the new publishers, but not munge the old price feeds from
      //publishers who are being kept.
      //First, remove any old publishers who are no longer publishers
      for( auto itr = a.feeds.begin(); itr != a.feeds.end(); )
      {
         if( !o.new_feed_producers.count(itr->first) )
            itr = a.feeds.erase(itr);
         else
            ++itr;
      }
      //Now, add any new publishers
      for( auto itr = o.new_feed_producers.begin(); itr != o.new_feed_producers.end(); ++itr )
         if( !a.feeds.count(*itr) )
            a.feeds[*itr];
      a.update_median_feeds(db().head_block_time());
   });

   return object_id_type();
}


object_id_type asset_global_settle_evaluator::do_evaluate(const asset_global_settle_evaluator::operation_type& op)
{
   const database& d = db();
   asset_to_settle = &op.asset_to_settle(d);
   FC_ASSERT(asset_to_settle->is_market_issued());
   FC_ASSERT(asset_to_settle->can_global_settle());
   FC_ASSERT(asset_to_settle->issuer == op.issuer );
   FC_ASSERT(asset_to_settle->dynamic_data(d).current_supply > 0);
   const auto& idx = d.get_index_type<call_order_index>().indices().get<by_collateral>();
   assert( !idx.empty() );
   auto itr = idx.lower_bound(boost::make_tuple(price::min(asset_to_settle->bitasset_data(d).short_backing_asset,
                                                           op.asset_to_settle)));
   assert( itr != idx.end() && itr->debt_type() == op.asset_to_settle );
   const call_order_object& least_collateralized_short = *itr;
   FC_ASSERT(least_collateralized_short.get_debt() * op.settle_price <= least_collateralized_short.get_collateral(),
             "Cannot force settle at supplied price: least collateralized short lacks sufficient collateral to settle.");

   return object_id_type();
}

object_id_type asset_global_settle_evaluator::do_apply(const asset_global_settle_evaluator::operation_type& op)
{
   database& d = db();
   d.globally_settle_asset( op.asset_to_settle(db()), op.settle_price );
   return object_id_type();
}

object_id_type asset_settle_evaluator::do_evaluate(const asset_settle_evaluator::operation_type& op)
{
   const database& d = db();
   asset_to_settle = &op.amount.asset_id(d);
   FC_ASSERT(asset_to_settle->is_market_issued());
   FC_ASSERT(asset_to_settle->can_force_settle());
   FC_ASSERT(d.get_balance(d.get(op.account), *asset_to_settle) >= op.amount);

   return d.get_index_type<force_settlement_index>().get_next_id();
}

object_id_type asset_settle_evaluator::do_apply(const asset_settle_evaluator::operation_type& op)
{
   database& d = db();
   d.adjust_balance(op.account, -op.amount);
   return d.create<force_settlement_object>([&](force_settlement_object& s) {
      s.owner = op.account;
      s.balance = op.amount;
      s.settlement_date = d.head_block_time() + asset_to_settle->bitasset_data(d).options.force_settlement_delay_sec;
   }).id;
}

object_id_type asset_publish_feeds_evaluator::do_evaluate(const asset_publish_feed_operation& o)
{ try {
   database& d = db();

   const asset_object& quote = o.asset_id(d);
   //Verify that this feed is for a market-issued asset and that asset is backed by the base
   FC_ASSERT(quote.is_market_issued());

   const asset_bitasset_data_object& bitasset = quote.bitasset_data(d);
   FC_ASSERT(bitasset.short_backing_asset == o.feed.call_limit.base.asset_id);
   //Verify that the publisher is authoritative to publish a feed
   if( quote.issuer == account_id_type() )
   {
      //It's a delegate-fed asset. Verify that publisher is an active delegate or witness.
      FC_ASSERT(d.get(account_id_type()).active.auths.count(o.publisher) ||
                d.get_global_properties().witness_accounts.count(o.publisher));
   } else {
      FC_ASSERT(bitasset.feeds.count(o.publisher));
   }

   return object_id_type();
} FC_CAPTURE_AND_RETHROW((o)) }

object_id_type asset_publish_feeds_evaluator::do_apply(const asset_publish_feed_operation& o)
{ try {
   database& d = db();

   const asset_object& quote = o.asset_id(d);
   // Store medians for this asset
   d.modify(quote.bitasset_data(d), [&o,&d](asset_bitasset_data_object& a) {
      a.feeds[o.publisher] = make_pair(d.head_block_time(), o.feed);
      a.update_median_feeds(d.head_block_time());
   });

   return object_id_type();
   } FC_CAPTURE_AND_RETHROW((o)) }

} } // bts::chain
