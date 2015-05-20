#include <bts/chain/types.hpp>
#include <bts/chain/database.hpp>

#include <bts/chain/key_object.hpp>
#include <bts/chain/account_object.hpp>
#include <bts/chain/asset_object.hpp>
#include <bts/chain/delegate_object.hpp>
#include <bts/chain/limit_order_object.hpp>
#include <bts/chain/short_order_object.hpp>
#include <bts/chain/block_summary_object.hpp>
#include <bts/chain/proposal_object.hpp>
#include <bts/chain/withdraw_permission_object.hpp>
#include <bts/chain/bond_object.hpp>
#include <bts/chain/file_object.hpp>
#include <bts/db/simple_index.hpp>
#include <bts/db/flat_index.hpp>

#include <bts/chain/transaction_evaluation_state.hpp>
#include <bts/chain/key_evaluator.hpp>
#include <bts/chain/custom_evaluator.hpp>
#include <bts/chain/account_evaluator.hpp>
#include <bts/chain/delegate_evaluator.hpp>
#include <bts/chain/asset_evaluator.hpp>
#include <bts/chain/limit_order_evaluator.hpp>
#include <bts/chain/short_order_evaluator.hpp>
#include <bts/chain/asset_evaluator.hpp>
#include <bts/chain/transaction_object.hpp>
#include <bts/chain/transfer_evaluator.hpp>
#include <bts/chain/proposal_evaluator.hpp>
#include <bts/chain/operation_history_object.hpp>
#include <bts/chain/global_parameters_evaluator.hpp>
#include <bts/chain/witness_object.hpp>
#include <bts/chain/witness_evaluator.hpp>
#include <bts/chain/bond_evaluator.hpp>
#include <bts/chain/vesting_balance_evaluator.hpp>
#include <bts/chain/vesting_balance_object.hpp>
#include <bts/chain/withdraw_permission_evaluator.hpp>
#include <bts/chain/worker_evaluator.hpp>

#include <fc/io/raw.hpp>
#include <fc/crypto/digest.hpp>
#include <fc/container/flat.hpp>
#include <fc/uint128.hpp>

namespace bts { namespace chain {

database::database()
{
   initialize_indexes();
   initialize_evaluators();
}

database::~database(){
   if( _pending_block_session )
      _pending_block_session->commit();
}

void database::close(uint32_t blocks_to_rewind)
{
   _pending_block_session.reset();

   for(uint32_t i = 0; i < blocks_to_rewind && head_block_num() > 0; ++i)
      pop_block();

   object_database::close();

   if( _block_id_to_block.is_open() )
      _block_id_to_block.close();

   _fork_db.reset();
}

const asset_object& database::get_core_asset() const
{
   return get(asset_id_type());
}

void database::wipe(const fc::path& data_dir, bool include_blocks)
{
   ilog("Wiping database", ("include_blocks", include_blocks));
   close();
   object_database::wipe(data_dir);
   if( include_blocks )
      fc::remove_all( data_dir / "database" );
}

void database::open( const fc::path& data_dir, const genesis_allocation& initial_allocation )
{ try {
   ilog("Open database in ${d}", ("d", data_dir));
   object_database::open( data_dir );

   _block_id_to_block.open( data_dir / "database" / "block_num_to_block" );

   if( !find(global_property_id_type()) )
      init_genesis(initial_allocation);

   _pending_block.previous  = head_block_id();
   _pending_block.timestamp = head_block_time();

   auto last_block_itr = _block_id_to_block.last();
   if( last_block_itr.valid() )
      _fork_db.start_block( last_block_itr.value() );

} FC_CAPTURE_AND_RETHROW( (data_dir) ) }

void database::reindex(fc::path data_dir, genesis_allocation initial_allocation)
{ try {
   wipe(data_dir, false);
   open(data_dir, initial_allocation);

   auto start = fc::time_point::now();
   auto itr = _block_id_to_block.begin();
   // TODO: disable undo tracking durring reindex, this currently causes crashes in the benchmark test
   //_undo_db.disable();
   while( itr.valid() )
   {
      apply_block( itr.value(), skip_delegate_signature |
                                skip_transaction_signatures |
                                skip_undo_block |
                                skip_undo_transaction |
                                skip_transaction_dupe_check |
                                skip_tapos_check |
                                skip_authority_check );
      ++itr;
   }
   //_undo_db.enable();
   auto end = fc::time_point::now();
   wdump( ((end-start).count()/1000000.0) );
} FC_CAPTURE_AND_RETHROW( (data_dir) ) }

void database::initialize_evaluators()
{
   _operation_evaluators.resize(255);
   register_evaluator<key_create_evaluator>();
   register_evaluator<account_create_evaluator>();
   register_evaluator<account_update_evaluator>();
   register_evaluator<account_whitelist_evaluator>();
   register_evaluator<delegate_create_evaluator>();
   register_evaluator<custom_evaluator>();
   register_evaluator<asset_create_evaluator>();
   register_evaluator<asset_issue_evaluator>();
   register_evaluator<asset_burn_evaluator>();
   register_evaluator<asset_update_evaluator>();
   register_evaluator<asset_update_bitasset_evaluator>();
   register_evaluator<asset_update_feed_producers_evaluator>();
   register_evaluator<asset_settle_evaluator>();
   register_evaluator<asset_global_settle_evaluator>();
   register_evaluator<limit_order_create_evaluator>();
   register_evaluator<limit_order_cancel_evaluator>();
   register_evaluator<short_order_create_evaluator>();
   register_evaluator<short_order_cancel_evaluator>();
   register_evaluator<call_order_update_evaluator>();
   register_evaluator<transfer_evaluator>();
   register_evaluator<asset_fund_fee_pool_evaluator>();
   register_evaluator<asset_publish_feeds_evaluator>();
   register_evaluator<proposal_create_evaluator>();
   register_evaluator<proposal_update_evaluator>();
   register_evaluator<proposal_delete_evaluator>();
   register_evaluator<global_parameters_update_evaluator>();
   register_evaluator<witness_create_evaluator>();
   register_evaluator<witness_withdraw_pay_evaluator>();
   register_evaluator<bond_create_offer_evaluator>();
   register_evaluator<bond_cancel_offer_evaluator>();
   register_evaluator<bond_accept_offer_evaluator>();
   register_evaluator<bond_claim_collateral_evaluator>();
   register_evaluator<vesting_balance_create_evaluator>();
   register_evaluator<vesting_balance_withdraw_evaluator>();
   register_evaluator<withdraw_permission_create_evaluator>();
   register_evaluator<withdraw_permission_claim_evaluator>();
   register_evaluator<withdraw_permission_update_evaluator>();
   register_evaluator<withdraw_permission_delete_evaluator>();
   register_evaluator<worker_create_evaluator>();
}

void database::initialize_indexes()
{
   reset_indexes();

   //Protocol object indexes
   add_index< primary_index<asset_index> >();
   add_index< primary_index<force_settlement_index> >();
   add_index< primary_index<account_index> >();
   add_index< primary_index<simple_index<key_object>> >();
   add_index< primary_index<simple_index<delegate_object>> >();
   add_index< primary_index<simple_index<witness_object>> >();
   add_index< primary_index<limit_order_index > >();
   add_index< primary_index<short_order_index > >();
   add_index< primary_index<call_order_index > >();
   add_index< primary_index<proposal_index > >();
   add_index< primary_index<withdraw_permission_index > >();
   add_index< primary_index<bond_index > >();
   add_index< primary_index<bond_offer_index > >();
   add_index< primary_index<file_object_index> >();
   add_index< primary_index<simple_index<vesting_balance_object> > >();
   add_index< primary_index<worker_index> >();

   //Implementation object indexes
   add_index< primary_index<transaction_index                             > >();
   add_index< primary_index<account_balance_index                         > >();
   add_index< primary_index<asset_bitasset_data_index                     > >();
   add_index< primary_index<simple_index< global_property_object         >> >();
   add_index< primary_index<simple_index< dynamic_global_property_object >> >();
   add_index< primary_index<simple_index< account_statistics_object      >> >();
   add_index< primary_index<simple_index< asset_dynamic_data_object      >> >();
   add_index< primary_index<flat_index<   block_summary_object           >> >();
}

void database::init_genesis(const genesis_allocation& initial_allocation)
{ try {
   _undo_db.disable();

   fc::ecc::private_key genesis_private_key = fc::ecc::private_key::regenerate(fc::sha256::hash(string("genesis")));
   const key_object& genesis_key =
      create<key_object>( [&genesis_private_key](key_object& k) {
         k.key_data = public_key_type(genesis_private_key.get_public_key());
      });
   const account_statistics_object& genesis_statistics =
      create<account_statistics_object>( [&](account_statistics_object& b){
      });
   create<account_balance_object>( [](account_balance_object& b) {
      b.balance = BTS_INITIAL_SUPPLY;
   });
   const account_object& genesis_account =
      create<account_object>( [&](account_object& n) {
         n.name = "genesis";
         n.owner.add_authority(genesis_key.get_id(), 1);
         n.owner.weight_threshold = 1;
         n.active = n.owner;
         n.memo_key = genesis_key.id;
         n.statistics = genesis_statistics.id;
      });

   vector<delegate_id_type> init_delegates;
   vector<witness_id_type> init_witnesses;

   auto delegates_and_witnesses = std::max(BTS_MIN_WITNESS_COUNT, BTS_MIN_DELEGATE_COUNT);
   for( int i = 0; i < delegates_and_witnesses; ++i )
   {
      const account_statistics_object& stats_obj =
         create<account_statistics_object>( [&](account_statistics_object&){
         });
      const account_object& delegate_account =
         create<account_object>( [&](account_object& a) {
            a.active = a.owner = genesis_account.owner;
            a.name = string("init") + fc::to_string(i);
            a.statistics = stats_obj.id;
         });
      const delegate_object& init_delegate = create<delegate_object>( [&](delegate_object& d) {
         d.delegate_account = delegate_account.id;
         d.vote_id = i * 2;
      });
      init_delegates.push_back(init_delegate.id);

      const witness_object& init_witness = create<witness_object>( [&](witness_object& d) {
            d.witness_account = delegate_account.id;
            d.vote_id = i * 2 + 1;
            secret_hash_type::encoder enc;
            fc::raw::pack( enc, genesis_private_key );
            fc::raw::pack( enc, d.last_secret );
            d.next_secret = secret_hash_type::hash(enc.result());
      });
      init_witnesses.push_back(init_witness.id);

   }
   create<block_summary_object>( [&](block_summary_object& p) {
   });

   const global_property_object& properties =
      create<global_property_object>( [&](global_property_object& p) {
         p.active_delegates = init_delegates;
         p.active_witnesses = init_witnesses;
         p.next_available_vote_id = delegates_and_witnesses * 2;
         p.chain_id = fc::digest(initial_allocation);
      });
   (void)properties;

   create<dynamic_global_property_object>( [&](dynamic_global_property_object& p) {
      p.time = fc::time_point_sec( BTS_GENESIS_TIMESTAMP );
      });

   const asset_dynamic_data_object& dyn_asset =
      create<asset_dynamic_data_object>( [&]( asset_dynamic_data_object& a ) {
         a.current_supply = BTS_INITIAL_SUPPLY;
      });

   const asset_object& core_asset =
     create<asset_object>( [&]( asset_object& a ) {
         a.symbol = BTS_SYMBOL;
         a.options.max_supply = BTS_INITIAL_SUPPLY;
         a.options.flags = 0;
         a.options.issuer_permissions = 0;
         a.issuer = genesis_account.id;
         a.options.core_exchange_rate.base.amount = 1;
         a.options.core_exchange_rate.base.asset_id = 0;
         a.options.core_exchange_rate.quote.amount = 1;
         a.options.core_exchange_rate.quote.asset_id = 0;
         a.dynamic_asset_data_id = dyn_asset.id;
      });
   assert( asset_id_type(core_asset.id) == asset().asset_id );
   assert( get_balance(account_id_type(), asset_id_type()) == asset(dyn_asset.current_supply) );
   (void)core_asset;

   if( !initial_allocation.empty() )
   {
      share_type total_allocation = 0;
      for( const auto& handout : initial_allocation )
         total_allocation += handout.second;

      auto mangle_to_name = [](const fc::static_variant<public_key_type, address>& key) {
         string addr = string(key.which() == std::decay<decltype(key)>::type::tag<address>::value? key.get<address>()
                                                                                                 : key.get<public_key_type>());
         string result = "bts";
         string key_string = string(addr).substr(sizeof(BTS_ADDRESS_PREFIX)-1);
         for( char c : key_string )
         {
            if( isupper(c) )
               result += string("-") + char(tolower(c));
            else
               result += c;
         }
         return result;
      };

      fc::time_point start_time = fc::time_point::now();

      for( const auto& handout : initial_allocation )
      {
         asset amount(handout.second);
         amount.amount = ((fc::uint128(amount.amount.value) * BTS_INITIAL_SUPPLY)/total_allocation.value).to_uint64();
         if( amount.amount == 0 )
         {
            wlog("Skipping zero allocation to ${k}", ("k", handout.first));
            continue;
         }

         signed_transaction trx;
         trx.operations.emplace_back(key_create_operation({asset(), genesis_account.id, handout.first}));
         relative_key_id_type key_id(0);
         authority account_authority(1, key_id, 1);
         account_create_operation cop;
         cop.name = mangle_to_name(handout.first);
         cop.registrar = account_id_type(1);
         cop.active = account_authority;
         cop.owner = account_authority;
         cop.memo_key = key_id;
         trx.operations.push_back(cop);
         trx.validate();
         auto ptrx = apply_transaction(trx, ~0);
         trx = signed_transaction();
         account_id_type account_id(ptrx.operation_results.back().get<object_id_type>());
         trx.operations.emplace_back(transfer_operation({  asset(),
                                                           genesis_account.id,
                                                           account_id,
                                                           amount,
                                                           memo_data()//vector<char>()
                                                        }));
         trx.validate();
         apply_transaction(trx, ~0);
      }

      asset leftovers = get_balance(account_id_type(), asset_id_type());
      if( leftovers.amount > 0 )
      {
         modify(*get_index_type<account_balance_index>().indices().get<by_balance>().find(boost::make_tuple(account_id_type(), asset_id_type())),
                [](account_balance_object& b) {
            b.adjust_balance(-b.get_balance());
         });
         modify(core_asset.dynamic_asset_data_id(*this), [&leftovers](asset_dynamic_data_object& d) {
            d.accumulated_fees += leftovers.amount;
         });
      }

      fc::microseconds duration = fc::time_point::now() - start_time;
      ilog("Finished allocating to ${n} accounts in ${t} milliseconds.",
           ("n", initial_allocation.size())("t", duration.count() / 1000));
   }
   _undo_db.enable();
} FC_LOG_AND_RETHROW() }

asset database::get_balance(account_id_type owner, asset_id_type asset_id) const
{
   auto& index = get_index_type<account_balance_index>().indices().get<by_balance>();
   auto itr = index.find(boost::make_tuple(owner, asset_id));
   if( itr == index.end() )
      return asset(0, asset_id);
   return itr->get_balance();
}

asset database::get_balance(const account_object& owner, const asset_object& asset_obj) const
{
   return get_balance(owner.get_id(), asset_obj.get_id());
}

void database::adjust_core_in_orders( const account_object& acnt, asset delta )
{
   if( delta.asset_id == asset_id_type(0) && delta.amount != 0 )
   {
      modify( acnt.statistics(*this), [&](account_statistics_object& stat){
          stat.total_core_in_orders += delta.amount;
      });
   }
}

void database::adjust_balance(account_id_type account, asset delta )
{ try {
   if( delta.amount == 0 )
      return;

   auto& index = get_index_type<account_balance_index>().indices().get<by_balance>();
   auto itr = index.find(boost::make_tuple(account, delta.asset_id));
   if(itr == index.end())
   {
      FC_ASSERT(delta.amount > 0);
      create<account_balance_object>([account,&delta](account_balance_object& b) {
         b.owner = account;
         b.asset_type = delta.asset_id;
         b.balance = delta.amount;
      });
   } else {
      FC_ASSERT(delta.amount > 0 || itr->get_balance() >= -delta);
      modify(*itr, [delta](account_balance_object& b) {
         b.adjust_balance(delta);
      });
   }

} FC_CAPTURE_AND_RETHROW( (account)(delta) ) }

/**
 *  Matches the two orders,
 *
 *  @return a bit field indicating which orders were filled (and thus removed)
 *
 *  0 - no orders were matched
 *  1 - bid was filled
 *  2 - ask was filled
 *  3 - both were filled
 */
template<typename OrderType>
int database::match( const limit_order_object& usd, const OrderType& core, const price& match_price )
{
   assert( usd.sell_price.quote.asset_id == core.sell_price.base.asset_id );
   assert( usd.sell_price.base.asset_id  == core.sell_price.quote.asset_id );
   assert( usd.for_sale > 0 && core.for_sale > 0 );

   auto usd_for_sale = usd.amount_for_sale();
   auto core_for_sale = core.amount_for_sale();

   asset usd_pays, usd_receives, core_pays, core_receives;

   if( usd_for_sale <= core_for_sale * match_price )
   {
      core_receives = usd_for_sale;
      usd_receives  = usd_for_sale * match_price;
   }
   else
   {
      //This line once read: assert( core_for_sale < usd_for_sale * match_price );
      //This assert is not always true -- see trade_amount_equals_zero in operation_tests.cpp
      //Although usd_for_sale is greater than core_for_sale * match_price, core_for_sale == usd_for_sale * match_price
      //Removing the assert seems to be safe -- apparently no asset is created or destroyed.
      usd_receives = core_for_sale;
      core_receives = core_for_sale * match_price;
   }

   core_pays = usd_receives;
   usd_pays  = core_receives;

   assert( usd_pays == usd.amount_for_sale() ||
           core_pays == core.amount_for_sale() );

   int result = 0;
   result |= fill_order( usd, usd_pays, usd_receives );
   result |= fill_order( core, core_pays, core_receives ) << 1;
   assert( result != 0 );
   return result;
}

asset database::match( const call_order_object& call, const force_settlement_object& settle, const price& match_price,
                     asset max_settlement )
{
   assert(call.get_debt().asset_id == settle.balance.asset_id );
   assert(call.debt > 0 && call.collateral > 0 && settle.balance.amount > 0);

   auto settle_for_sale = std::min(settle.balance, max_settlement);
   auto call_debt = call.get_debt();

   asset call_receives = std::min(settle_for_sale, call_debt),
         call_pays = call_receives * match_price,
         settle_pays = call_receives,
         settle_receives = call_pays;

   assert( settle_pays == settle_for_sale || call_receives == call.get_debt() );

   fill_order(call, call_pays, call_receives);
   fill_order(settle, settle_pays, settle_receives);

   return call_receives;
}

int database::match( const limit_order_object& bid, const limit_order_object& ask, const price& match_price )
{
   return match<limit_order_object>( bid, ask, match_price );
}
int database::match( const limit_order_object& bid, const short_order_object& ask, const price& match_price )
{
   return match<short_order_object>( bid, ask, match_price );
}

/**
 *
 */
bool database::check_call_orders( const asset_object& mia )
{ try {
    if( !mia.is_market_issued() ) return false;
    const asset_bitasset_data_object& bitasset = mia.bitasset_data(*this);
    if( bitasset.current_feed.call_limit.is_null() ) return false;

    const call_order_index& call_index = get_index_type<call_order_index>();
    const auto& call_price_index = call_index.indices().get<by_price>();

    const limit_order_index& limit_index = get_index_type<limit_order_index>();
    const auto& limit_price_index = limit_index.indices().get<by_price>();

    const short_order_index& short_index = get_index_type<short_order_index>();
    const auto& short_price_index = short_index.indices().get<by_price>();

    auto short_itr = short_price_index.lower_bound( price::max( mia.id, bitasset.short_backing_asset ) );
    auto short_end = short_price_index.upper_bound( ~bitasset.current_feed.call_limit );

    auto limit_itr = limit_price_index.lower_bound( price::max( mia.id, bitasset.short_backing_asset ) );
    auto limit_end = limit_price_index.upper_bound( ~bitasset.current_feed.call_limit );

    auto call_itr = call_price_index.lower_bound( price::min( bitasset.short_backing_asset, mia.id ) );
    auto call_end = call_price_index.upper_bound( price::max( bitasset.short_backing_asset, mia.id ) );

    bool filled_short_or_limit = false;

    while( call_itr != call_end )
    {
       bool  current_is_limit = true;
       bool  filled_call      = false;
       price match_price;
       asset usd_for_sale;
       if( limit_itr != limit_end )
       {
          assert( limit_itr != limit_price_index.end() );
          if( short_itr != short_end && limit_itr->sell_price < short_itr->sell_price )
          {
             assert( short_itr != short_price_index.end() );
             current_is_limit = false;
             match_price      = short_itr->sell_price;
             usd_for_sale     = short_itr->amount_for_sale();
          }
          else
          {
             current_is_limit = true;
             match_price      = limit_itr->sell_price;
             usd_for_sale     = limit_itr->amount_for_sale();
          }
       }
       else if( short_itr != short_end )
       {
          assert( short_itr != short_price_index.end() );
          current_is_limit = false;
          match_price      = short_itr->sell_price;
          usd_for_sale     = short_itr->amount_for_sale();
       }
       else return filled_short_or_limit;

       match_price.validate();

       if( match_price > ~call_itr->call_price )
       {
          return filled_short_or_limit;
       }

       auto usd_to_buy   = call_itr->get_debt();

       if( usd_to_buy * match_price > call_itr->get_collateral() )
       {
          elog( "black swan, we do not have enough collateral to cover at this price" );
          globally_settle_asset( mia, call_itr->get_debt() / call_itr->get_collateral() );
          return true;
       }

       asset call_pays, call_receives, order_pays, order_receives;
       if( usd_to_buy >= usd_for_sale )
       {  // fill order
          call_receives   = usd_for_sale;
          order_receives  = usd_for_sale * match_price;
          call_pays       = order_receives;
          order_pays      = usd_for_sale;

          filled_short_or_limit = true;
          filled_call           = (usd_to_buy == usd_for_sale);
       }
       else // fill call
       {
          call_receives  = usd_to_buy;
          order_receives = usd_to_buy * match_price;
          call_pays      = order_receives;
          order_pays     = usd_to_buy;

          filled_call    = true;
       }

       auto old_call_itr = call_itr;
       if( filled_call ) ++call_itr;
       fill_order( *old_call_itr, call_pays, call_receives );
       if( current_is_limit )
       {
          auto old_limit_itr = !filled_call ? limit_itr++ : limit_itr;
          fill_order( *old_limit_itr, order_pays, order_receives );
       }
       else
       {
          auto old_short_itr = !filled_call ? short_itr++ : short_itr;
          fill_order( *old_short_itr, order_pays, order_receives );
       }
    } // whlie call_itr != call_end

    return filled_short_or_limit;
} FC_CAPTURE_AND_RETHROW() }

void database::cancel_order( const limit_order_object& order, bool create_virtual_op  )
{
   auto refunded = order.amount_for_sale();

   modify( order.seller(*this).statistics(*this),[&]( account_statistics_object& obj ){
      if( refunded.asset_id == asset_id_type() )
         obj.total_core_in_orders -= refunded.amount;
   });
   adjust_balance(order.seller, refunded);

   if( create_virtual_op )
   {
      // TODO: create a virtual cancel operation
   }

   remove( order );
}

/**
    for each short order, fill it at settlement price and place funds received into a total
    calculate the USD->BTS price and convert all USD balances to BTS at that price and subtract BTS from total
       - any fees accumulated by the issuer in the bitasset are forfeit / not redeemed
       - cancel all open orders with bitasset in it
       - any bonds with the bitasset as collateral get converted to BTS as collateral
       - any bitassets that use this bitasset as collateral are immediately settled at their feed price
       - convert all balances in bitasset to BTS and subtract from total
       - any prediction markets with usd as the backing get converted to BTS as the backing
    any BTS left over due to rounding errors is paid to accumulated fees
*/
void database::globally_settle_asset( const asset_object& mia, const price& settlement_price )
{ try {
   elog( "BLACK SWAN!" );
   debug_dump();

   edump( (mia.symbol)(settlement_price) );

   const asset_bitasset_data_object& bitasset = mia.bitasset_data(*this);
   const asset_object& backing_asset = bitasset.short_backing_asset(*this);
   asset collateral_gathered = backing_asset.amount(0);

   const asset_dynamic_data_object& mia_dyn = mia.dynamic_asset_data_id(*this);
   auto original_mia_supply = mia_dyn.current_supply;

   const call_order_index& call_index = get_index_type<call_order_index>();
   const auto& call_price_index = call_index.indices().get<by_price>();

    auto call_itr = call_price_index.lower_bound( price::min( bitasset.short_backing_asset, mia.id ) );
    auto call_end = call_price_index.upper_bound( price::max( bitasset.short_backing_asset, mia.id ) );
    while( call_itr != call_end )
    {
       auto pays = call_itr->get_debt() * settlement_price;
       wdump( (call_itr->get_debt() ) );
       collateral_gathered += pays;
       const auto&  order = *call_itr;
       ++call_itr;
       FC_ASSERT( fill_order( order, pays, order.get_debt() ) );
    }

   const limit_order_index& limit_index = get_index_type<limit_order_index>();
   const auto& limit_price_index = limit_index.indices().get<by_price>();

    // cancel all orders selling the market issued asset
    auto limit_itr = limit_price_index.lower_bound( price::max( mia.id, bitasset.short_backing_asset ) );
    auto limit_end = limit_price_index.upper_bound( ~bitasset.current_feed.call_limit );
    while( limit_itr != limit_end )
    {
       const auto& order = *limit_itr;
       ilog( "CANCEL LIMIT ORDER" );
        idump((order));
       ++limit_itr;
       cancel_order( order );
    }

    limit_itr = limit_price_index.begin();
    while( limit_itr != limit_end )
    {
       if( limit_itr->amount_for_sale().asset_id == mia.id )
       {
          const auto& order = *limit_itr;
          ilog( "CANCEL_AGAIN" );
          edump((order));
          ++limit_itr;
          cancel_order( order );
       }
    }

   limit_itr = limit_price_index.begin();
   while( limit_itr != limit_end )
   {
      if( limit_itr->amount_for_sale().asset_id == mia.id )
      {
         const auto& order = *limit_itr;
         edump((order));
         ++limit_itr;
         cancel_order( order );
      }
   }

    // settle all balances  
    asset total_mia_settled = mia.amount(0);

   // convert collateral held in bonds
    const auto& bond_idx = get_index_type<bond_index>().indices().get<by_collateral>();
    auto bond_itr = bond_idx.find( bitasset.id );
    while( bond_itr != bond_idx.end() )
    {
       if( bond_itr->collateral.asset_id == bitasset.id )
       {
          auto settled_amount = bond_itr->collateral * settlement_price;
          total_mia_settled += bond_itr->collateral;
          collateral_gathered -= settled_amount;
          modify( *bond_itr, [&]( bond_object& obj ) {
                  obj.collateral = settled_amount;
                  });
       }
       else break;
    }

    // cancel all bond offers holding the bitasset and refund the offer
    const auto& bond_offer_idx = get_index_type<bond_offer_index>().indices().get<by_asset>();
    auto bond_offer_itr = bond_offer_idx.find( bitasset.id );
    while( bond_offer_itr != bond_offer_idx.end() )
    {
       if( bond_offer_itr->amount.asset_id == bitasset.id )
       {
          adjust_balance( bond_offer_itr->offered_by_account, bond_offer_itr->amount );
          auto old_itr = bond_offer_itr;
          bond_offer_itr++;
          remove( *old_itr );
       }
       else break;
    }

    const auto& index = get_index_type<account_balance_index>().indices().get<by_asset>();
    auto range = index.equal_range(mia.get_id());
    for( auto itr = range.first; itr != range.second; ++itr )
    {
       auto mia_balance = itr->get_balance();
       if( mia_balance.amount > 0 )
       {
          adjust_balance(itr->owner, -mia_balance);
          auto settled_amount = mia_balance * settlement_price;
          idump( (mia_balance)(settled_amount)(settlement_price) );
          adjust_balance(itr->owner, settled_amount);
          total_mia_settled += mia_balance;
          collateral_gathered -= settled_amount;
       }
    }

    // TODO: convert payments held in escrow

    modify( mia_dyn, [&]( asset_dynamic_data_object& obj ){
       total_mia_settled.amount += obj.accumulated_fees;
       obj.accumulated_fees = 0;
    });

    wlog( "====================== AFTER SETTLE BLACK SWAN UNCLAIMED SETTLEMENT FUNDS ==============\n" );
    wdump((collateral_gathered)(total_mia_settled)(original_mia_supply)(mia_dyn.current_supply));
    modify( bitasset.short_backing_asset(*this).dynamic_asset_data_id(*this), [&]( asset_dynamic_data_object& obj ){
       obj.accumulated_fees += collateral_gathered.amount;
    });

    FC_ASSERT( total_mia_settled.amount == original_mia_supply, "", ("total_settled",total_mia_settled)("original",original_mia_supply) );
} FC_CAPTURE_AND_RETHROW( (mia)(settlement_price) ) }

asset database::calculate_market_fee( const asset_object& trade_asset, const asset& trade_amount )
{
   assert( trade_asset.id == trade_amount.asset_id );

   if( !trade_asset.charges_market_fees() )
      return trade_asset.amount(0);
   if( trade_asset.options.market_fee_percent == 0 )
      return trade_asset.amount(trade_asset.options.min_market_fee);

   fc::uint128 a(trade_amount.amount.value);
   a *= trade_asset.options.market_fee_percent;
   a /= BTS_100_PERCENT;
   asset percent_fee = trade_asset.amount(a.to_uint64());

   if( percent_fee.amount > trade_asset.options.max_market_fee )
      percent_fee.amount = trade_asset.options.max_market_fee;
   else if( percent_fee.amount < trade_asset.options.min_market_fee )
      percent_fee.amount = trade_asset.options.min_market_fee;

   return percent_fee;
}

asset database::pay_market_fees( const asset_object& recv_asset, const asset& receives )
{
   auto issuer_fees = calculate_market_fee( recv_asset, receives );
   assert(issuer_fees <= receives );

   //Don't dirty undo state if not actually collecting any fees
   if( issuer_fees.amount > 0 )
   {
      const auto& recv_dyn_data = recv_asset.dynamic_asset_data_id(*this);
      modify( recv_dyn_data, [&]( asset_dynamic_data_object& obj ){
                   idump((issuer_fees));
         obj.accumulated_fees += issuer_fees.amount;
      });
   }

   return issuer_fees;
}

void database::pay_order( const account_object& receiver, const asset& receives, const asset& pays )
{
   const auto& balances = receiver.statistics(*this);
   modify( balances, [&]( account_statistics_object& b ){
         if( pays.asset_id == asset_id_type() )
            b.total_core_in_orders -= pays.amount;
   });
   adjust_balance(receiver.get_id(), receives);
}

/**
 *  For Market Issued assets Managed by Delegates, any fees collected in the MIA need
 *  to be sold and converted into CORE by accepting the best offer on the table.
 */
bool database::convert_fees( const asset_object& mia )
{
   if( mia.issuer != account_id_type() ) return false;
   return false;
}

void database::deposit_cashback( const account_object& acct, share_type amount )
{
   // If we don't have a VBO, or if it has the wrong maturity
   // due to a policy change, cut it loose.

   if( amount == 0 )
      return;

   uint32_t global_vesting_seconds = get_global_properties().parameters.cashback_vesting_period_seconds;
   fc::time_point_sec now = head_block_time();

   while( true )
   {
      if( !acct.cashback_vb.valid() )
         break;
      const vesting_balance_object& cashback_vb = (*acct.cashback_vb)(*this);
      if( cashback_vb.policy.which() != vesting_policy::tag< cdd_vesting_policy >::value )
         break;
      if( cashback_vb.policy.get< cdd_vesting_policy >().vesting_seconds != global_vesting_seconds )
         break;

      modify( cashback_vb, [&]( vesting_balance_object& obj )
      {
         obj.deposit( now, amount );
      } );
      return;
   }

   const vesting_balance_object& cashback_vb = create< vesting_balance_object >( [&]( vesting_balance_object& obj )
   {
      obj.owner = acct.id;
      obj.balance = amount;

      cdd_vesting_policy policy;
      policy.vesting_seconds = global_vesting_seconds;
      policy.coin_seconds_earned = 0;
      policy.coin_seconds_earned_last_update = now;

      obj.policy = policy;
   } );

   modify( acct, [&]( account_object& _acct )
   {
      _acct.cashback_vb = cashback_vb.id;
   } );

   return;
}

void database::pay_workers( share_type& budget )
{
   ilog("Processing payroll! Available budget is ${b}", ("b", budget));
   vector<std::reference_wrapper<const worker_object>> active_workers;
   get_index_type<worker_index>().inspect_all_objects([this, &active_workers](const object& o) {
      const worker_object& w = static_cast<const worker_object&>(o);
      auto now = _pending_block.timestamp;
      if( w.is_active(now) && w.approving_stake(_vote_tally_buffer) > 0 )
         active_workers.emplace_back(w);
   });

   std::sort(active_workers.begin(), active_workers.end(), [this](const worker_object& wa, const worker_object& wb) {
      return wa.approving_stake(_vote_tally_buffer) > wb.approving_stake(_vote_tally_buffer);
   });

   for( int i = 0; i < active_workers.size() && budget > 0; ++i )
   {
      const worker_object& active_worker = active_workers[i];
      share_type requested_pay = active_worker.daily_pay;
      if( _pending_block.timestamp - get_dynamic_global_properties().last_budget_time != fc::days(1) )
      {
         fc::uint128 pay(requested_pay.value);
         pay *= (_pending_block.timestamp - get_dynamic_global_properties().last_budget_time).count();
         pay /= fc::days(1).count();
         requested_pay = pay.to_uint64();
      }

      share_type actual_pay = std::min(budget, requested_pay);
      ilog(" ==> Paying ${a} to worker ${w}", ("w", active_worker.id)("a", actual_pay));
      modify(active_worker, [&](worker_object& w) {
         w.worker.visit(worker_pay_visitor(actual_pay, *this));
      });

      budget -= actual_pay;
   }
}

bool database::fill_order( const limit_order_object& order, const asset& pays, const asset& receives )
{
   assert( order.amount_for_sale().asset_id == pays.asset_id );
   assert( pays.asset_id != receives.asset_id );

   const account_object& seller = order.seller(*this);
   const asset_object& recv_asset = receives.asset_id(*this);

   auto issuer_fees = pay_market_fees( recv_asset, receives );
   pay_order( seller, receives - issuer_fees, pays );

   push_applied_operation( fill_order_operation{ order.id, order.seller, pays, receives, issuer_fees } );

   if( pays == order.amount_for_sale() )
   {
      remove( order );
      return true;
   }
   else
   {
      modify( order, [&]( limit_order_object& b ) {
                             b.for_sale -= pays.amount;
                          });
      /**
       *  There are times when the AMOUNT_FOR_SALE * SALE_PRICE == 0 which means that we
       *  have hit the limit where the seller is asking for nothing in return.  When this
       *  happens we must refund any balance back to the seller, it is too small to be
       *  sold at the sale price.
       */
      if( order.amount_to_receive().amount == 0 )
      {
         cancel_order(order);
         return true;
      }
      return false;
   }
}
bool database::fill_order( const call_order_object& order, const asset& pays, const asset& receives )
{ try {
   idump((pays)(receives)(order));
   assert( order.get_debt().asset_id == receives.asset_id );
   assert( order.get_collateral().asset_id == pays.asset_id );
   assert( order.get_collateral() >= pays );

   optional<asset> collateral_freed;
   modify( order, [&]( call_order_object& o ){
            o.debt       -= receives.amount;
            o.collateral -= pays.amount;
            if( o.debt == 0 )
            {
              collateral_freed = o.get_collateral();
              o.collateral = 0;
            }
       });
   const asset_object& mia = receives.asset_id(*this);
   assert( mia.is_market_issued() );

   const asset_dynamic_data_object& mia_ddo = mia.dynamic_asset_data_id(*this);

   modify( mia_ddo, [&]( asset_dynamic_data_object& ao ){
       idump((receives));
        ao.current_supply -= receives.amount;
      });

   const account_object& borrower = order.borrower(*this);
   if( collateral_freed || pays.asset_id == asset_id_type() )
   {
      const account_statistics_object& borrower_statistics = borrower.statistics(*this);
      if( collateral_freed )
         adjust_balance(borrower.get_id(), *collateral_freed);
      modify( borrower_statistics, [&]( account_statistics_object& b ){
              if( collateral_freed && collateral_freed->amount > 0 )
                b.total_core_in_orders -= collateral_freed->amount;
              if( pays.asset_id == asset_id_type() )
                b.total_core_in_orders -= pays.amount;
              assert( b.total_core_in_orders >= 0 );
           });
   }

   if( collateral_freed )
   {
      remove( order );
   }

   push_applied_operation( fill_order_operation{ order.id, order.borrower, pays, receives, asset(0, pays.asset_id) } );

   return collateral_freed.valid();
} FC_CAPTURE_AND_RETHROW( (order)(pays)(receives) ) }


bool database::fill_order( const short_order_object& order, const asset& pays, const asset& receives )
{ try {
   assert( order.amount_for_sale().asset_id == pays.asset_id );
   assert( pays.asset_id != receives.asset_id );

   const call_order_index& call_index = get_index_type<call_order_index>();

   const account_object& seller = order.seller(*this);
   const asset_object& recv_asset = receives.asset_id(*this);
   const asset_object& pays_asset = pays.asset_id(*this);
   assert( pays_asset.is_market_issued() );

   auto issuer_fees = pay_market_fees( recv_asset, receives );

   bool filled               = pays == order.amount_for_sale();
   auto seller_to_collateral = filled ? order.get_collateral() : pays * order.sell_price;
   auto buyer_to_collateral  = receives - issuer_fees;

   if( receives.asset_id == asset_id_type() )
   {
      const auto& statistics = seller.statistics(*this);
      modify( statistics, [&]( account_statistics_object& b ){
             b.total_core_in_orders += buyer_to_collateral.amount;
      });
   }

   modify( pays_asset.dynamic_asset_data_id(*this), [&]( asset_dynamic_data_object& obj ){
      obj.current_supply += pays.amount;
   });

   const auto& call_account_index = call_index.indices().get<by_account>();
   auto call_itr = call_account_index.find(  boost::make_tuple(order.seller, pays.asset_id) );
   if( call_itr == call_account_index.end() )
   {
      create<call_order_object>( [&]( call_order_object& c ){
         c.borrower    = seller.id;
         c.collateral  = seller_to_collateral.amount + buyer_to_collateral.amount;
         c.debt        = pays.amount;
         c.maintenance_collateral_ratio = order.maintenance_collateral_ratio;
         c.call_price  = price::max(seller_to_collateral.asset_id, pays.asset_id);
         c.update_call_price();
      });
   }
   else
   {
      modify( *call_itr, [&]( call_order_object& c ){
         c.debt       += pays.amount;
         c.collateral += seller_to_collateral.amount + buyer_to_collateral.amount;
         c.maintenance_collateral_ratio = order.maintenance_collateral_ratio;
         c.update_call_price();
      });
   }

   if( filled )
   {
      remove( order );
   }
   else
   {
      modify( order, [&]( short_order_object& b ) {
         b.for_sale -= pays.amount;
         b.available_collateral -= seller_to_collateral.amount;
         assert( b.available_collateral > 0 );
         assert( b.for_sale > 0 );
      });

      /**
       *  There are times when the AMOUNT_FOR_SALE * SALE_PRICE == 0 which means that we
       *  have hit the limit where the seller is asking for nothing in return.  When this
       *  happens we must refund any balance back to the seller, it is too small to be
       *  sold at the sale price.
       */
      if( order.amount_to_receive().amount == 0 )
      {
         adjust_balance(seller.get_id(), order.get_collateral());
         if( order.get_collateral().asset_id == asset_id_type() )
         {
            const auto& statistics = seller.statistics(*this);
            modify( statistics, [&]( account_statistics_object& b ){
                 b.total_core_in_orders -= order.available_collateral;
            });
         }

         remove( order );
         filled = true;
      }
   }

   push_applied_operation( fill_order_operation{ order.id, order.seller, pays, receives, issuer_fees } );

   return filled;
} FC_CAPTURE_AND_RETHROW( (order)(pays)(receives) ) }

bool database::fill_order(const force_settlement_object& settle, const asset& pays, const asset& receives)
{ try {
   bool filled = false;

   auto issuer_fees = pay_market_fees(get(receives.asset_id), receives);

   if( pays < settle.balance )
   {
      modify(settle, [&pays](force_settlement_object& s) {
         s.balance -= pays;
      });
      filled = false;
   } else {
      remove(settle);
      filled = true;
   }
   adjust_balance(settle.owner, receives - issuer_fees);

   push_applied_operation( fill_order_operation{ settle.id, settle.owner, pays, receives, issuer_fees } );

   return filled;
} FC_CAPTURE_AND_RETHROW( (settle)(pays)(receives) ) }

asset database::current_delegate_registration_fee()const
{
   return asset();
}

void database::apply_block( const signed_block& next_block, uint32_t skip )
{ try {
   _applied_ops.clear();

   const witness_object& signing_witness = validate_block_header(skip, next_block);
   const auto& global_props = get_global_properties();
   const auto& dynamic_global_props = get<dynamic_global_property_object>(dynamic_global_property_id_type());

   _current_block_num    = next_block.block_num();
   _current_trx_in_block = 0;

   for( const auto& trx : next_block.transactions )
   {
      /* We do not need to push the undo state for each transaction
       * because they either all apply and are valid or the
       * entire block fails to apply.  We only need an "undo" state
       * for transactions when validating broadcast transactions or
       * when building a block.
       */
      apply_transaction( trx, skip | skip_transaction_signatures );
      ++_current_trx_in_block;
   }

   update_global_dynamic_data( next_block );
   update_signing_witness(signing_witness, next_block);

   auto current_block_interval = global_props.parameters.block_interval;

   // Are we at the maintenance interval?
   if( dynamic_global_props.next_maintenance_time <= next_block.timestamp )
      // This will update _pending_block.timestamp if the block interval has changed
      perform_chain_maintenance(next_block, global_props);
   // If we're at the end of a round, shuffle the active witnesses
   // We can skip this if they were just updated during chain maintenance
   else if( (next_block.block_num() % global_props.active_delegates.size()) == 0 )
      modify(global_props, [this](global_property_object& p) {
         shuffle_vector(p.active_witnesses);
      });

   create_block_summary(next_block);
   clear_expired_transactions();
   clear_expired_proposals();
   clear_expired_orders();
   update_expired_feeds();
   update_withdraw_permissions();

   // notify observers that the block has been applied
   applied_block( next_block ); //emit
   _applied_ops.clear();

   const auto& head_undo = _undo_db.head();
   vector<object_id_type> changed_ids;  changed_ids.reserve(head_undo.old_values.size());
   for( const auto& item : head_undo.old_values ) changed_ids.push_back(item.first);
   changed_objects(changed_ids);


   update_pending_block(next_block, current_block_interval);
} FC_CAPTURE_AND_RETHROW( (next_block.block_num())(skip) )  }

optional< pair< fc::time_point_sec, witness_id_type > > database::get_scheduled_witness( fc::time_point_sec when )const
{
   const global_property_object& gpo = get_global_properties();
   uint8_t interval = gpo.parameters.block_interval;
   uint64_t w_abs_slot = when.sec_since_epoch() / interval;
   uint64_t h_abs_slot = head_block_time().sec_since_epoch() / interval;
   optional< pair< fc::time_point_sec, witness_id_type > > result;

   if( w_abs_slot <= h_abs_slot )
      return result;

   time_point_sec canonical_time = fc::time_point_sec( w_abs_slot * interval );
   return pair< fc::time_point_sec, witness_id_type >(
      canonical_time,
      gpo.active_witnesses[ w_abs_slot % gpo.active_witnesses.size() ]
      );
}

signed_block database::generate_block(
   fc::time_point_sec when,
   witness_id_type witness_id,
   const fc::ecc::private_key& block_signing_private_key,
   uint32_t skip /* = 0 */
   )
{
   try {
   optional< pair< fc::time_point_sec, witness_id_type > > scheduled_witness = get_scheduled_witness( when );
   FC_ASSERT( scheduled_witness.valid() );
   FC_ASSERT( scheduled_witness->second == witness_id );

   const auto& witness_obj = witness_id(*this);

   if( !(skip & skip_delegate_signature) )
      FC_ASSERT( witness_obj.signing_key(*this).key() == block_signing_private_key.get_public_key() );

   // we behave reasonably when given a non-normalized timestamp
   // after the given block time:  block simply gets normalized timestamp
   _pending_block.timestamp = scheduled_witness->first;

   secret_hash_type::encoder last_enc;
   fc::raw::pack( last_enc, block_signing_private_key );
   fc::raw::pack( last_enc, witness_obj.last_secret );
   _pending_block.previous_secret = last_enc.result();

   secret_hash_type::encoder next_enc;
   fc::raw::pack( next_enc, block_signing_private_key );
   fc::raw::pack( next_enc, _pending_block.previous_secret );
   _pending_block.next_secret_hash = secret_hash_type::hash(next_enc.result());

   _pending_block.witness = witness_id;
   if( !(skip & skip_delegate_signature) ) _pending_block.sign( block_signing_private_key );

   FC_ASSERT( fc::raw::pack_size(_pending_block) <= get_global_properties().parameters.maximum_block_size );
   //This line used to std::move(_pending_block) but this is unsafe as _pending_block is later referenced without being
   //reinitialized. Future optimization could be to move it, then reinitialize it with the values we need to preserve.
   signed_block tmp = _pending_block;
   _pending_block.transactions.clear();
   push_block( tmp, skip );
   return tmp;
} FC_CAPTURE_AND_RETHROW( (witness_id) ) }

void database::update_active_witnesses()
{ try {
   share_type stake_target = _total_voting_stake / 2;
   share_type stake_tally = _witness_count_histogram_buffer[0];
   int witness_count = 0;
   while( stake_tally <= stake_target )
      stake_tally += _witness_count_histogram_buffer[++witness_count];

   auto wits = sort_votable_objects<witness_object>(std::max(witness_count*2+1, BTS_MIN_WITNESS_COUNT));
   shuffle_vector(wits);

   modify( get_global_properties(), [&]( global_property_object& gp ){
      gp.active_witnesses.clear();
      std::transform(wits.begin(), wits.end(),
                     std::inserter(gp.active_witnesses, gp.active_witnesses.end()),
                     [](const witness_object& w) {
         return w.id;
      });
      gp.witness_accounts.clear();
      std::transform(wits.begin(), wits.end(),
                     std::inserter(gp.witness_accounts, gp.witness_accounts.end()),
                     [](const witness_object& w) {
         return w.witness_account;
      });
   });
} FC_CAPTURE_AND_RETHROW() }

void database::update_active_delegates()
{ try {
   uint64_t stake_target = _total_voting_stake / 2;
   uint64_t stake_tally = _committee_count_histogram_buffer[0];
   int delegate_count = 0;
   while( stake_tally <= stake_target )
      stake_tally += _committee_count_histogram_buffer[++delegate_count];

   auto delegates = sort_votable_objects<delegate_object>(std::max(delegate_count*2+1, BTS_MIN_DELEGATE_COUNT));

   // Update genesis authorities
   if( !delegates.empty() )
      modify( get(account_id_type()), [&]( account_object& a ) {
         uint64_t total_votes = 0;
         map<account_id_type, uint64_t> weights;
         a.owner.weight_threshold = 0;
         a.owner.auths.clear();

         for( const delegate_object& del : delegates )
         {
            weights.emplace(del.delegate_account, _vote_tally_buffer[del.vote_id]);
            total_votes += _vote_tally_buffer[del.vote_id];
         }

         // total_votes is 64 bits. Subtract the number of leading low bits from 64 to get the number of useful bits,
         // then I want to keep the most significant 16 bits of what's left.
         int8_t bits_to_drop = std::max(int(64 - __builtin_clzll(total_votes)) - 16, 0);
         for( const auto& weight : weights )
         {
            // Ensure that everyone has at least one vote. Zero weights aren't allowed.
            uint16_t votes = std::max((weight.second >> bits_to_drop), uint64_t(1) );
            a.owner.auths[weight.first] += votes;
            a.owner.weight_threshold += votes;
         }

         a.owner.weight_threshold /= 2;
         a.owner.weight_threshold += 1;
         a.active = a.owner;
      });
   modify( get_global_properties(), [&]( global_property_object& gp ) {
      gp.active_delegates.clear();
      std::transform(delegates.begin(), delegates.end(),
                     std::back_inserter(gp.active_delegates),
                     [](const delegate_object& d) { return d.id; });
   });
} FC_CAPTURE_AND_RETHROW() }

void database::update_global_dynamic_data( const signed_block& b )
{
   const dynamic_global_property_object& _dgp =
      dynamic_global_property_id_type(0)(*this);

   //
   // dynamic global properties updating
   //
   modify( _dgp, [&]( dynamic_global_property_object& dgp ){
      secret_hash_type::encoder enc;
      fc::raw::pack( enc, dgp.random );
      fc::raw::pack( enc, b.previous_secret );
      dgp.random = enc.result();
      dgp.head_block_number = b.block_num();
      dgp.head_block_id = b.id();
      dgp.time = b.timestamp;
      dgp.current_witness = b.witness;
   });
}

/**
 *  Removes the most recent block from the database and
 *  undoes any changes it made.
 */
void database::pop_block()
{ try {
   _pending_block_session.reset();
   _block_id_to_block.remove( _pending_block.previous );
   pop_undo();
   _pending_block.previous  = head_block_id();
   _pending_block.timestamp = head_block_time();
   _fork_db.pop_block();
} FC_CAPTURE_AND_RETHROW() }

void database::clear_pending()
{ try {
   _pending_block.transactions.clear();
   _pending_block_session.reset();
} FC_CAPTURE_AND_RETHROW() }

bool database::is_known_block( const block_id_type& id )const
{
   return _fork_db.is_known_block(id) || _block_id_to_block.find(id).valid();
}
/**
 *  Only return true *if* the transaction has not expired or been invalidated. If this
 *  method is called with a VERY old transaction we will return false, they should
 *  query things by blocks if they are that old.
 */
bool database::is_known_transaction( const transaction_id_type& id )const
{
  const auto& trx_idx = get_index_type<transaction_index>().indices().get<by_trx_id>();
  return trx_idx.find( id ) != trx_idx.end();
}

/**
 *  For each prime account, adjust the vote total object
 */
void database::update_vote_totals(const global_property_object& props)
{ try {
    _vote_tally_buffer.resize(props.next_available_vote_id);
    _witness_count_histogram_buffer.resize(props.parameters.maximum_witness_count / 2 + 1);
    _committee_count_histogram_buffer.resize(props.parameters.maximum_committee_count / 2 + 1);

    const account_index& account_idx = get_index_type<account_index>();
    _total_voting_stake = 0;

    bool count_non_prime_votes = props.parameters.count_non_prime_votes;
    auto timestamp = fc::time_point::now();
    for( const account_object& stake_account : account_idx.indices() )
    {
       if( count_non_prime_votes || stake_account.is_prime() )
       {
          // There may be a difference between the account whose stake is voting and the one specifying opinions.
          // Usually they're the same, but if the stake account has specified a voting_account, that account is the one
          // specifying the opinions.
          const account_object& opinion_account =
                (stake_account.voting_account == account_id_type())? stake_account
                                                                   : get(stake_account.voting_account);

          const auto& stats = stake_account.statistics(*this);
          uint64_t voting_stake = stats.total_core_in_orders.value
                   + (stake_account.cashback_vb.valid() ? (*stake_account.cashback_vb)(*this).balance.amount.value: 0)
                   + get_balance(stake_account.get_id(), asset_id_type()).amount.value;

          for( vote_id_type id : opinion_account.votes )
          {
             uint32_t offset = id.instance();
             // if they somehow managed to specify an illegal offset, ignore it.
             if( offset < _vote_tally_buffer.size() )
                _vote_tally_buffer[ offset ] += voting_stake;
          }

          if( opinion_account.num_witness <= props.parameters.maximum_witness_count )
          {
             uint16_t offset = std::min(
                size_t(opinion_account.num_witness/2),
                _witness_count_histogram_buffer.size() - 1
                );
             //
             // votes for a number greater than maximum_witness_count
             // are turned into votes for maximum_witness_count.
             //
             // in particular, this takes care of the case where a
             // member was voting for a high number, then the
             // parameter was lowered.
             //
             _witness_count_histogram_buffer[ offset ] += voting_stake;
          }
          if( opinion_account.num_committee <= props.parameters.maximum_committee_count )
          {
             uint16_t offset = std::min(
                size_t(opinion_account.num_committee/2),
                _committee_count_histogram_buffer.size() - 1
                );
             //
             // votes for a number greater than maximum_committee_count
             // are turned into votes for maximum_committee_count.
             //
             // same rationale as for witnesses
             //
             _committee_count_histogram_buffer[ offset ] += voting_stake;
          }

          _total_voting_stake += voting_stake;
       }
    }
    ilog("Tallied votes in ${time} milliseconds.", ("time", (fc::time_point::now() - timestamp).count() / 1000.0));
} FC_CAPTURE_AND_RETHROW() }

share_type database::get_max_budget( fc::time_point_sec now )const
{
   const dynamic_global_property_object& dpo = get_dynamic_global_properties();
   const asset_object& core = asset_id_type(0)(*this);
   const asset_dynamic_data_object& core_dd = core.dynamic_asset_data_id(*this);

   if(    (dpo.last_budget_time == fc::time_point_sec())
       || (now <= dpo.last_budget_time) )
      return share_type(0);

   int64_t dt = (now - dpo.last_budget_time).to_seconds();

   // We'll consider accumulated_fees to be burned at the BEGINNING
   // of the maintenance interval.  However, for speed we only
   // call modify() on the asset_dynamic_data_object once at the
   // end of the maintenance interval.  Thus the accumulated_fees
   // are available for the budget at this point, but not included
   // in core.burned().
   share_type reserve = core.burned(*this) + core_dd.accumulated_fees;

   fc::uint128_t budget_u128 = reserve.value;
   budget_u128 *= uint64_t(dt);
   budget_u128 *= BTS_CORE_ASSET_CYCLE_RATE;
   //round up to the nearest satoshi -- this is necessary to ensure
   //   there isn't an "untouchable" reserve, and we will eventually
   //   be able to use the entire reserve
   budget_u128 += ((uint64_t(1) << BTS_CORE_ASSET_CYCLE_RATE_BITS) - 1);
   budget_u128 >>= BTS_CORE_ASSET_CYCLE_RATE_BITS;
   share_type budget;
   if( budget_u128 < reserve.value )
      budget = share_type(budget_u128.to_uint64());
   else
      budget = reserve;

   return budget;
}

/**
 * Update the budget for witnesses and workers.
 */
void database::process_budget()
{
   try
   {
      const global_property_object& gpo = get_global_properties();
      const dynamic_global_property_object& dpo = get_dynamic_global_properties();
      const asset_dynamic_data_object& core =
         asset_id_type(0)(*this).dynamic_asset_data_id(*this);
      fc::time_point_sec now = _pending_block.timestamp;

      int64_t time_to_maint = (dpo.next_maintenance_time - now).to_seconds();
      //
      // The code that generates the next maintenance time should
      //    only produce a result in the future.  If this assert
      //    fails, then the next maintenance time algorithm is buggy.
      //
      assert( time_to_maint > 0 );
      //
      // Code for setting chain parameters should validate
      //    block_interval > 0 (as well as the humans proposing /
      //    voting on changes to block interval).
      //
      assert( gpo.parameters.block_interval > 0 );
      uint64_t blocks_to_maint = (uint64_t(time_to_maint) + gpo.parameters.block_interval - 1) / gpo.parameters.block_interval;

      // blocks_to_maint > 0 because time_to_maint > 0,
      // which means numerator is at least equal to block_interval

      share_type available_funds = get_max_budget( now );

      share_type witness_budget = gpo.parameters.witness_pay_per_block.value * blocks_to_maint;
      witness_budget = std::min( witness_budget, available_funds );
      available_funds -= witness_budget;

      fc::uint128_t worker_budget_u128 = gpo.parameters.worker_budget_per_day.value;
      worker_budget_u128 *= uint64_t(time_to_maint);
      worker_budget_u128 /= 60*60*24;

      share_type worker_budget;
      if( worker_budget_u128 >= available_funds.value )
         worker_budget = available_funds;
      else
         worker_budget = worker_budget_u128.to_uint64();
      available_funds -= worker_budget;

      share_type leftover_worker_funds = worker_budget;
      pay_workers( leftover_worker_funds );
      available_funds += leftover_worker_funds;

      modify( core, [&]( asset_dynamic_data_object& _core )
      {
         _core.current_supply = (_core.current_supply + witness_budget +
                                 worker_budget - leftover_worker_funds -
                                 _core.accumulated_fees);
         _core.accumulated_fees = 0;
      } );
      modify( dpo, [&]( dynamic_global_property_object& _dpo )
      {
         _dpo.witness_budget = witness_budget;
         _dpo.last_budget_time = now;
      } );

      // available_funds is money we could spend, but don't want to.
      // we simply let it evaporate back into the reserve.
   }
   FC_CAPTURE_AND_RETHROW()
}

/**
 *  Push block "may fail" in which case every partial change is unwound.  After
 *  push block is successful the block is appended to the chain database on disk.
 *
 *  @return true if we switched forks as a result of this push.
 */
bool database::push_block( const signed_block& new_block, uint32_t skip )
{ try {
   if( !(skip&skip_fork_db) )
   {
      auto new_head = _fork_db.push_block( new_block );
      //If the head block from the longest chain does not build off of the current head, we need to switch forks.
      if( new_head->data.previous != head_block_id() )
      {
         edump((new_head->data.previous));
         //If the newly pushed block is the same height as head, we get head back in new_head
         //Only switch forks if new_head is actually higher than head
         if( new_head->data.block_num() > head_block_num() )
         {
            auto branches = _fork_db.fetch_branch_from( new_head->data.id(), _pending_block.previous );
            for( auto item : branches.first )
               wdump( ("new")(item->id)(item->data.previous) );
            for( auto item : branches.second )
               wdump( ("old")(item->id)(item->data.previous) );

            // pop blocks until we hit the forked block
            while( head_block_id() != branches.second.back()->data.previous )
               pop_block();

            // push all blocks on the new fork
            for( auto ritr = branches.first.rbegin(); ritr != branches.first.rend(); ++ritr )
            {
                optional<fc::exception> except;
                try {
                   auto session = _undo_db.start_undo_session();
                   apply_block( (*ritr)->data, skip );
                   _block_id_to_block.store( new_block.id(), (*ritr)->data );
                   session.commit();
                }
                catch ( const fc::exception& e ) { except = e; }
                if( except )
                {
                   elog( "Encountered error when switching to a longer fork at id ${id}. Going back.",
                          ("id", (*ritr)->id) );
                   // remove the rest of branches.first from the fork_db, those blocks are invalid
                   while( ritr != branches.first.rend() )
                   {
                      _fork_db.remove( (*ritr)->data.id() );
                      ++ritr;
                   }
                   _fork_db.set_head( branches.second.front() );

                   // pop all blocks from the bad fork
                   while( head_block_id() != branches.second.back()->data.previous )
                      pop_block();

                   // restore all blocks from the good fork
                   for( auto ritr = branches.second.rbegin(); ritr != branches.second.rend(); ++ritr )
                   {
                      auto session = _undo_db.start_undo_session();
                      apply_block( (*ritr)->data, skip );
                      _block_id_to_block.store( new_block.id(), (*ritr)->data );
                      session.commit();
                   }
                   throw *except;
                }
            }
            return true;
         }
         else return false;
      }
   }

   // If there is a pending block session, then the database state is dirty with pending transactions.
   // Drop the pending session to reset the database to a clean head block state.
   // TODO: Preserve pending transactions, and re-apply any which weren't included in the new block.
   clear_pending();

   try {
      auto session = _undo_db.start_undo_session();
      apply_block( new_block, skip );
      _block_id_to_block.store( new_block.id(), new_block );
      session.commit();
   } catch ( const fc::exception& e ) {
      elog("Failed to push new block:\n${e}", ("e", e.to_detail_string()));
      _fork_db.remove(new_block.id());
      throw;
   }

   return false;
} FC_CAPTURE_AND_RETHROW( (new_block) ) }

/**
 *  Attempts to push the transaction into the pending queue
 *
 * When called to push a locally generated transaction, set the skip_block_size_check bit on the skip argument. This
 * will allow the transaction to be pushed even if it causes the pending block size to exceed the maximum block size.
 * Although the transaction will probably not propagate further now, as the peers are likely to have their pending
 * queues full as well, it will be kept in the queue to be propagated later when a new block flushes out the pending
 * queues.
 */
processed_transaction database::push_transaction( const signed_transaction& trx, uint32_t skip )
{
   //wdump((trx.digest())(trx.id()));
   // If this is the first transaction pushed after applying a block, start a new undo session.
   // This allows us to quickly rewind to the clean state of the head block, in case a new block arrives.
   if( !_pending_block_session ) _pending_block_session = _undo_db.start_undo_session();
   auto session = _undo_db.start_undo_session();
   auto processed_trx = apply_transaction( trx, skip );
   _pending_block.transactions.push_back(processed_trx);

   FC_ASSERT( (skip & skip_block_size_check) ||
              fc::raw::pack_size(_pending_block) <= get_global_properties().parameters.maximum_block_size );

   // The transaction applied successfully. Merge its changes into the pending block session.
   session.merge();
   return processed_trx;
}

processed_transaction database::push_proposal(const proposal_object& proposal)
{
   transaction_evaluation_state eval_state(this);
   eval_state._is_proposed_trx = true;

   //Inject the approving authorities into the transaction eval state
   std::transform(proposal.required_active_approvals.begin(),
                  proposal.required_active_approvals.end(),
                  std::inserter(eval_state.approved_by, eval_state.approved_by.begin()),
                  []( account_id_type id ) {
                     return std::make_pair(id, authority::active);
                  });
   std::transform(proposal.required_owner_approvals.begin(),
                  proposal.required_owner_approvals.end(),
                  std::inserter(eval_state.approved_by, eval_state.approved_by.begin()),
                  []( account_id_type id ) {
                     return std::make_pair(id, authority::owner);
                  });

   ilog("Attempting to push proposal ${prop}", ("prop", proposal));
   idump((eval_state.approved_by));

   eval_state.operation_results.reserve(proposal.proposed_transaction.operations.size());
   processed_transaction ptrx(proposal.proposed_transaction);
   eval_state._trx = &ptrx;

   auto session = _undo_db.start_undo_session();
   for( auto& op : proposal.proposed_transaction.operations )
      eval_state.operation_results.emplace_back(apply_operation(eval_state, op));
   remove(proposal);
   session.merge();

   ptrx.operation_results = std::move(eval_state.operation_results);
   return ptrx;
}

processed_transaction database::apply_transaction( const signed_transaction& trx, uint32_t skip )
{ try {
   trx.validate();
   auto& trx_idx = get_mutable_index_type<transaction_index>();
   auto trx_id = trx.id();
   FC_ASSERT( (skip & skip_transaction_dupe_check) ||
              trx_idx.indices().get<by_trx_id>().find(trx_id) == trx_idx.indices().get<by_trx_id>().end() );
   transaction_evaluation_state eval_state(this, skip&skip_authority_check );
   const chain_parameters& chain_parameters = get_global_properties().parameters;
   eval_state._trx = &trx;

   //This check is used only if this transaction has an absolute expiration time.
   if( !(skip & skip_transaction_signatures) && trx.relative_expiration == 0 )
   {
      for( const auto& sig : trx.signatures )
      {
         FC_ASSERT( sig.first(*this).key_address() == fc::ecc::public_key( sig.second, trx.digest() ), "",
                    ("trx",trx)
                    ("digest",trx.digest())
                    ("sig.first",sig.first)
                    ("key_address",sig.first(*this).key_address())
                    ("addr", address(fc::ecc::public_key( sig.second, trx.digest() ))) );
      }
   }

   //If we're skipping tapos check, but not dupe check, assume all transactions have maximum expiration time.
   fc::time_point_sec trx_expiration = _pending_block.timestamp + chain_parameters.maximum_time_until_expiration;

   //Skip all manner of expiration and TaPoS checking if we're on block 1; It's impossible that the transaction is
   //expired, and TaPoS makes no sense as no blocks exist.
   if( BOOST_LIKELY(head_block_num() > 0) )
   {
      if( !(skip & skip_tapos_check) && trx.relative_expiration != 0 )
      {
         //Check the TaPoS reference and expiration time
         //Remember that the TaPoS block number is abbreviated; it contains only the lower 16 bits.
         //Lookup TaPoS block summary by block number (remember block summary instances are the block numbers)
         const block_summary_object& tapos_block_summary
               = static_cast<const block_summary_object&>(get_index<block_summary_object>()
                                                          .get(block_summary_id_type((head_block_num() & ~0xffff)
                                                                                     + trx.ref_block_num)));

         //This is the signature check for transactions with relative expiration.
         if( !(skip & skip_transaction_signatures) )
         {
            for( const auto& sig : trx.signatures )
            {
               FC_ASSERT(sig.first(*this).key_address() == fc::ecc::public_key(sig.second,
                                                                               trx.digest(tapos_block_summary.block_id)
                                                                               ),
                          "",
                          ("sig.first",sig.first)
                          ("key_address",sig.first(*this).key_address())
                          ("addr", address(fc::ecc::public_key(sig.second, trx.digest()))));
            }
         }

         //Verify TaPoS block summary has correct ID prefix, and that this block's time is not past the expiration
         FC_ASSERT( trx.ref_block_prefix == tapos_block_summary.block_id._hash[1] );
         trx_expiration = tapos_block_summary.timestamp + chain_parameters.block_interval*trx.relative_expiration;
      } else if( trx.relative_expiration == 0 ) {
         trx_expiration = fc::time_point_sec(trx.ref_block_prefix);
         FC_ASSERT( trx_expiration <= _pending_block.timestamp + chain_parameters.maximum_time_until_expiration );
      }
      FC_ASSERT( _pending_block.timestamp <= trx_expiration );
   } else if( !(skip & skip_transaction_signatures) ) {
      FC_ASSERT(trx.relative_expiration == 0, "May not use transactions with a reference block in block 1!");
   }

   //Insert transaction into unique transactions database.
   if( !(skip & skip_transaction_dupe_check) )
   {
      create<transaction_object>([&](transaction_object& transaction) {
         transaction.expiration = trx_expiration;
         transaction.trx_id = trx.id();
         transaction.trx = trx;
      });
   }

   eval_state.operation_results.reserve( trx.operations.size() );

   processed_transaction ptrx(trx);
   _current_op_in_trx = 0;
   for( auto op : ptrx.operations )
   {
      eval_state.operation_results.emplace_back(apply_operation(eval_state, op));
      ++_current_op_in_trx;
   }
   ptrx.operation_results = std::move( eval_state.operation_results );

   return ptrx;
} FC_CAPTURE_AND_RETHROW( (trx) ) }

operation_result database::apply_operation(transaction_evaluation_state& eval_state, const operation& op)
{
   int i_which = op.which();
   uint64_t u_which = uint64_t( i_which );
   if( i_which < 0 )
      assert( "Negative operation tag" && false );
   if( u_which >= _operation_evaluators.size() )
      assert( "No registered evaluator for this operation" && false );
   unique_ptr<op_evaluator>& eval = _operation_evaluators[ u_which ];
   if( !eval )
      assert( "No registered evaluator for this operation" && false );
   auto op_id = push_applied_operation( op );
   auto result = eval->evaluate( eval_state, op, true );
   set_applied_operation_result( op_id, result );
   return result;
}
uint32_t database::push_applied_operation( const operation& op )
{
   _applied_ops.emplace_back(op);
   auto& oh = _applied_ops.back();
   oh.block_num    = _current_block_num;
   oh.trx_in_block = _current_trx_in_block;
   oh.op_in_trx    = _current_op_in_trx;
   oh.virtual_op   = _current_virtual_op++;
   return _applied_ops.size() - 1;
}
void database::set_applied_operation_result( uint32_t op_id, const operation_result& result )
{
   assert( op_id < _applied_ops.size() );
   _applied_ops[op_id].result = result;
}

const vector<operation_history_object>& database::get_applied_operations() const
{
   return _applied_ops;
}

const global_property_object& database::get_global_properties()const
{
   return get( global_property_id_type() );
}

const dynamic_global_property_object&database::get_dynamic_global_properties() const
{
   return get( dynamic_global_property_id_type() );
}
const fee_schedule_type&  database::current_fee_schedule()const
{
   return get_global_properties().parameters.current_fees;
}
time_point_sec database::head_block_time()const
{
   return get( dynamic_global_property_id_type() ).time;
}
uint32_t       database::head_block_num()const
{
   return get( dynamic_global_property_id_type() ).head_block_number;
}
block_id_type       database::head_block_id()const
{
   return get( dynamic_global_property_id_type() ).head_block_id;
}

block_id_type  database::get_block_id_for_num( uint32_t block_num )const
{ try {
   block_id_type lb; lb._hash[0] = htonl(block_num);
   auto itr = _block_id_to_block.lower_bound( lb );
   FC_ASSERT( itr.valid() && itr.key()._hash[0] == lb._hash[0] );
   return itr.key();
} FC_CAPTURE_AND_RETHROW( (block_num) ) }

optional<signed_block> database::fetch_block_by_id( const block_id_type& id )const
{
   auto b = _fork_db.fetch_block( id );
   if( !b )
      return _block_id_to_block.fetch_optional(id);
   return b->data;
}

optional<signed_block> database::fetch_block_by_number( uint32_t num )const
{
   auto results = _fork_db.fetch_block_by_number(num);
   if( results.size() == 1 )
      return results[0]->data;
   else
   {
      block_id_type lb; lb._hash[0] = htonl(num);
      auto itr = _block_id_to_block.lower_bound( lb );
      if( itr.valid() && itr.key()._hash[0] == lb._hash[0] )
         return itr.value();
   }
   return optional<signed_block>();
}

const signed_transaction& database::get_recent_transaction(const transaction_id_type& trx_id) const
{
   auto& index = get_index_type<transaction_index>().indices().get<by_trx_id>();
   auto itr = index.find(trx_id);
   FC_ASSERT(itr != index.end());
   return itr->trx;
}

const witness_object& database::validate_block_header( uint32_t skip, const signed_block& next_block )const
{
   const auto& global_props = get_global_properties();
   FC_ASSERT( _pending_block.previous == next_block.previous, "", ("pending.prev",_pending_block.previous)("next.prev",next_block.previous) );
   FC_ASSERT( _pending_block.timestamp <= next_block.timestamp, "", ("_pending_block.timestamp",_pending_block.timestamp)("next",next_block.timestamp)("blocknum",next_block.block_num()) );
   FC_ASSERT( _pending_block.timestamp.sec_since_epoch() % global_props.parameters.block_interval == 0 );
   const witness_object& witness = next_block.witness(*this);
   FC_ASSERT( secret_hash_type::hash(next_block.previous_secret) == witness.next_secret, "",
              ("previous_secret", next_block.previous_secret)("next_secret", witness.next_secret));
   if( !(skip&skip_delegate_signature) ) FC_ASSERT( next_block.validate_signee( witness.signing_key(*this).key() ) );

   optional< pair< fc::time_point_sec, witness_id_type > > scheduled_witness = get_scheduled_witness( next_block.timestamp );

   // following assert should never trip.  invalid value should
   // be prevented by _pending_block.timestamp <= next_block.timestamp check above
   FC_ASSERT( scheduled_witness.valid() );

   // following assert should never trip.  non-normalized timestamp
   // should be prevented by sec_since_epoch % block_interval == 0 check above
   FC_ASSERT( scheduled_witness->first == next_block.timestamp );

   FC_ASSERT( next_block.witness == scheduled_witness->second );

   return witness;
}

void database::update_signing_witness(const witness_object& signing_witness, const signed_block& new_block)
{
   const global_property_object& gpo = get_global_properties();
   const dynamic_global_property_object& dpo = get_dynamic_global_properties();

   share_type witness_pay = std::min( gpo.parameters.witness_pay_per_block, dpo.witness_budget );

   modify( dpo, [&]( dynamic_global_property_object& _dpo )
   {
      _dpo.witness_budget -= witness_pay;
   } );

   modify( signing_witness, [&]( witness_object& _wit )
   {
      _wit.last_secret = new_block.previous_secret;
      _wit.next_secret = new_block.next_secret_hash;
      _wit.accumulated_income += witness_pay;
   } );
}

void database::update_pending_block(const signed_block& next_block, uint8_t current_block_interval)
{
   _pending_block.timestamp = next_block.timestamp + current_block_interval;
   _pending_block.previous = next_block.id();
   auto old_pending_trx = std::move(_pending_block.transactions);
   _pending_block.transactions.clear();
   for( auto old_trx : old_pending_trx )
      push_transaction( old_trx );
}

void database::perform_chain_maintenance(const signed_block& next_block, const global_property_object& global_props)
{
   update_vote_totals(global_props);

   struct clear_canary {
      clear_canary(vector<uint64_t>& target): target(target){}
      ~clear_canary() { target.clear(); }
   private:
      vector<uint64_t>& target;
   };
   clear_canary a(_witness_count_histogram_buffer),
                b(_committee_count_histogram_buffer),
                c(_vote_tally_buffer);

   update_active_witnesses();
   update_active_delegates();

   const global_property_object& global_properties = get_global_properties();
   if( global_properties.pending_parameters )
      modify(get_global_properties(), [](global_property_object& p) {
         p.parameters = std::move(*p.pending_parameters);
         p.pending_parameters.reset();
      });

   auto new_block_interval = global_props.parameters.block_interval;

   // if block interval CHANGED during this block *THEN* we cannot simply
   // add the interval if we want to maintain the invariant that all timestamps are a multiple
   // of the interval.
   _pending_block.timestamp = next_block.timestamp + fc::seconds(new_block_interval);
   uint32_t r = _pending_block.timestamp.sec_since_epoch()%new_block_interval;
   if( !r )
   {
      _pending_block.timestamp -=  r;
      assert( (_pending_block.timestamp.sec_since_epoch() % new_block_interval)  == 0 );
   }

   auto next_maintenance_time = get<dynamic_global_property_object>(dynamic_global_property_id_type()).next_maintenance_time;
   auto maintenance_interval = get_global_properties().parameters.maintenance_interval;

   if( next_maintenance_time <= next_block.timestamp )
   {
      if( next_block.block_num() == 1 )
         next_maintenance_time = time_point_sec() +
               (((next_block.timestamp.sec_since_epoch() / maintenance_interval) + 1) * maintenance_interval);
      else
         next_maintenance_time += maintenance_interval;
      assert( next_maintenance_time > next_block.timestamp );
   }

   modify(get_dynamic_global_properties(), [next_maintenance_time](dynamic_global_property_object& d) {
      d.next_maintenance_time = next_maintenance_time;
   });

   // Reset all BitAsset force settlement volumes to zero
   for( const asset_bitasset_data_object* d : get_index_type<asset_bitasset_data_index>() )
      modify(*d, [](asset_bitasset_data_object& d) { d.force_settled_volume = 0; });

   // process_budget needs to run at the bottom because
   //   it needs to know the next_maintenance_time
   process_budget();
}

void database::create_block_summary(const signed_block& next_block)
{
   const auto& sum = create<block_summary_object>( [&](block_summary_object& p) {
         p.block_id = next_block.id();
         p.timestamp = next_block.timestamp;
   });
   FC_ASSERT( sum.id.instance() == next_block.block_num(), "", ("summary.id",sum.id)("next.block_num",next_block.block_num()) );
}

void database::clear_expired_transactions()
{
   //Look for expired transactions in the deduplication list, and remove them.
   //Transactions must have expired by at least two forking windows in order to be removed.
   auto& transaction_idx = static_cast<transaction_index&>(get_mutable_index(implementation_ids, impl_transaction_object_type));
   const auto& dedupe_index = transaction_idx.indices().get<by_expiration>();
   const auto& global_parameters = get_global_properties().parameters;
   auto forking_window_time = global_parameters.maximum_undo_history * global_parameters.block_interval;
   while( !dedupe_index.empty()
          && head_block_time() - dedupe_index.rbegin()->expiration >= fc::seconds(forking_window_time) )
      transaction_idx.remove(*dedupe_index.rbegin());
}

void database::clear_expired_proposals()
{
   const auto& proposal_expiration_index = get_index_type<proposal_index>().indices().get<by_expiration>();
   while( !proposal_expiration_index.empty() && proposal_expiration_index.begin()->expiration_time <= head_block_time() )
   {
      const proposal_object& proposal = *proposal_expiration_index.begin();
      processed_transaction result;
      try {
         if( proposal.is_authorized_to_execute(this) )
         {
            result = push_proposal(proposal);
            //TODO: Do something with result so plugins can process it.
            continue;
         }
      } catch( const fc::exception& e ) {
         elog("Failed to apply proposed transaction on its expiration. Deleting it.\n${proposal}\n${error}",
              ("proposal", proposal)("error", e.to_detail_string()));
      }
      remove(proposal);
   }
}

void database::clear_expired_orders()
{
   transaction_evaluation_state cancel_context(this, true);

   //Cancel expired limit orders
   auto& limit_index = get_index_type<limit_order_index>().indices().get<by_expiration>();
   while( !limit_index.empty() && limit_index.begin()->expiration <= head_block_time() )
   {
      const limit_order_object& order = *limit_index.begin();
      limit_order_cancel_operation canceler;
      canceler.fee_paying_account = order.seller;
      canceler.order = order.id;
      apply_operation(cancel_context, canceler);
   }

   //Cancel expired short orders
   auto& short_index = get_index_type<short_order_index>().indices().get<by_expiration>();
   while( !short_index.empty() && short_index.begin()->expiration <= head_block_time() )
   {
      const short_order_object& order = *short_index.begin();
      short_order_cancel_operation canceler;
      canceler.fee_paying_account = order.seller;
      canceler.order = order.id;
      apply_operation(cancel_context, canceler);
   }

   //Process expired force settlement orders
   //TODO: Do this on an asset-by-asset basis, and skip the current asset if it's maximally settled or has settlements disabled
   auto& settlement_index = get_index_type<force_settlement_index>().indices().get<by_expiration>();
   if( !settlement_index.empty() )
   {
      asset_id_type current_asset = settlement_index.begin()->settlement_asset_id();
      asset max_settlement_volume;

      // At each iteration, we either consume the current order and remove it, or we move to the next asset
      for( auto itr = settlement_index.lower_bound(current_asset);
           itr != settlement_index.end();
           itr = settlement_index.lower_bound(current_asset) )
      {
         const force_settlement_object& order = *itr;
         auto order_id = order.id;
         current_asset = order.settlement_asset_id();
         const asset_object& mia_object = get(current_asset);
         const asset_bitasset_data_object mia = mia_object.bitasset_data(*this);

         // Can we still settle in this asset?
         if( max_settlement_volume.asset_id != current_asset )
            max_settlement_volume = mia_object.amount(mia.max_force_settlement_volume(mia_object.dynamic_data(*this).current_supply));
         if( mia.current_feed.settlement_price.is_null() || mia.force_settled_volume >= max_settlement_volume.amount )
         {
            auto bound = settlement_index.upper_bound(boost::make_tuple(current_asset));
            if( bound == settlement_index.end() )
               break;
            current_asset = bound->settlement_asset_id();
            continue;
         }

         auto& pays = order.balance;
         auto receives = (order.balance * mia.current_feed.settlement_price);
         receives.amount = (fc::uint128_t(receives.amount.value) *
                            (BTS_100_PERCENT - mia.options.force_settlement_offset_percent) / BTS_100_PERCENT).to_uint64();
         assert(receives <= order.balance * mia.current_feed.settlement_price);

         price settlement_price = pays / receives;

         auto& call_index = get_index_type<call_order_index>().indices().get<by_collateral>();
         asset settled = mia_object.amount(mia.force_settled_volume);
         // Match against the least collateralized short until the settlement is finished or we reach max settlements
         while( settled < max_settlement_volume && find_object(order_id) )
         {
            auto itr = call_index.lower_bound(boost::make_tuple(price::min(mia_object.bitasset_data(*this).short_backing_asset,
                                                                           mia_object.get_id())));
            // There should always be a call order, since asset exists!
            assert(itr != call_index.end() && itr->debt_type() == mia_object.get_id());
            asset max_settlement = max_settlement_volume - settled;
            settled += match(*itr, order, settlement_price, max_settlement);
         }
         modify(mia, [settled](asset_bitasset_data_object& b) {
            b.force_settled_volume = settled.amount;
         });
      }
   }
}

void database::update_expired_feeds()
{
   auto& asset_idx = get_index_type<asset_bitasset_data_index>();
   for( const asset_bitasset_data_object* b : asset_idx )
      if( b->feed_is_expired(head_block_time()) )
         modify(*b, [this](asset_bitasset_data_object& a) {
            a.update_median_feeds(head_block_time());
         });
}

void database::update_withdraw_permissions()
{
   auto& permit_index = get_index_type<withdraw_permission_index>().indices().get<by_next_period>();
   while( !permit_index.empty() && permit_index.begin()->next_period_start_time <= head_block_time() )
   {
      const withdraw_permission_object& permit = *permit_index.begin();
      bool expired = false;
      modify(permit, [this, &expired](withdraw_permission_object& p) {
         expired = p.update_period(head_block_time());
      });
      if( expired )
         remove(permit);
   }
}

/**
 *  This method dumps the state of the blockchain in a semi-human readable form for the
 *  purpose of tracking down funds and mismatches in currency allocation
 */
void database::debug_dump()
{
   const auto& db = *this;
   const asset_dynamic_data_object& core_asset_data = db.get_core_asset().dynamic_asset_data_id(db);

   const auto& balance_index = db.get_index_type<account_balance_index>().indices();
   const simple_index<account_statistics_object>& statistics_index = db.get_index_type<simple_index<account_statistics_object>>();
   map<asset_id_type,share_type> total_balances;
   map<asset_id_type,share_type> total_debts;
   share_type core_in_orders;
   share_type reported_core_in_orders;

   for( const account_balance_object& a : balance_index )
   {
      idump(("balance")(a));
      total_balances[a.asset_type] += a.balance;
   }
   for( const account_statistics_object& s : statistics_index )
   {
      idump(("statistics")(s));
      reported_core_in_orders += s.total_core_in_orders;
   }
   for( const limit_order_object& o : db.get_index_type<limit_order_index>().indices() )
   {
      idump(("limit_order")(o));
      auto for_sale = o.amount_for_sale();
      if( for_sale.asset_id == asset_id_type() ) core_in_orders += for_sale.amount;
      total_balances[for_sale.asset_id] += for_sale.amount;
   }
   for( const short_order_object& o : db.get_index_type<short_order_index>().indices() )
   {
      idump(("short_order")(o));
      auto col = o.get_collateral();
      if( col.asset_id == asset_id_type() ) core_in_orders += col.amount;
      total_balances[col.asset_id] += col.amount;
   }
   for( const call_order_object& o : db.get_index_type<call_order_index>().indices() )
   {
      idump(("call_order")(o));
      auto col = o.get_collateral();
      if( col.asset_id == asset_id_type() ) core_in_orders += col.amount;
      total_balances[col.asset_id] += col.amount;
      total_debts[o.get_debt().asset_id] += o.get_debt().amount;
   }
   for( const asset_object& asset_obj : db.get_index_type<asset_index>().indices() )
   {
      total_balances[asset_obj.id] += asset_obj.dynamic_asset_data_id(db).accumulated_fees;
      total_balances[asset_id_type()] += asset_obj.dynamic_asset_data_id(db).fee_pool;
   }
   for( const witness_object& witness_obj : db.get_index_type<simple_index<witness_object>>() )
   {
      //idump((witness_obj));
      total_balances[asset_id_type()] += witness_obj.accumulated_income;
   }
   if( total_balances[asset_id_type()].value != core_asset_data.current_supply.value )
   {
      edump( (total_balances[asset_id_type()].value)(core_asset_data.current_supply.value ));
   }
   // TODO:  Add vesting_balance_object to this method
}
void database::adjust_balance(const account_object& account, asset delta ){ adjust_balance( account.id, delta); }

} } // namespace bts::chain
