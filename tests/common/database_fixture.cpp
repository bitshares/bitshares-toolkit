#include <boost/test/unit_test.hpp>
#include <boost/program_options.hpp>

#include <bts/account_history/account_history_plugin.hpp>

#include <bts/db/simple_index.hpp>

#include <bts/chain/account_object.hpp>
#include <bts/chain/asset_object.hpp>
#include <bts/chain/bond_object.hpp>
#include <bts/chain/delegate_object.hpp>
#include <bts/chain/limit_order_object.hpp>
#include <bts/chain/short_order_object.hpp>
#include <bts/chain/vesting_balance_object.hpp>
#include <bts/chain/witness_object.hpp>

#include <fc/crypto/digest.hpp>

#include <iostream>
#include <iomanip>
#include <sstream>

#include "database_fixture.hpp"

namespace bts { namespace chain {

using std::cout;

database_fixture::database_fixture()
   : app(), db( *app.chain_database() )
{
   auto ahplugin = app.register_plugin<bts::account_history::account_history_plugin>();

   boost::program_options::variables_map options;

   // app.initialize();
   ahplugin->initialize_plugin( options );

   db.init_genesis();
   ahplugin->startup_plugin();

   now = db.head_block_time();
   generate_block();

   genesis_key(db); // attempt to deref
   trx.set_expiration(now + fc::minutes(1));

   return;
}

database_fixture::~database_fixture()
{
   if( !std::uncaught_exception() )
   {
      verify_asset_supplies();
      verify_account_history_plugin_index();
   }

   if( data_dir )
      db.close();
   return;
}

fc::ecc::private_key database_fixture::generate_private_key(string seed)
{
   static const fc::ecc::private_key genesis = fc::ecc::private_key::regenerate(fc::sha256::hash(string("genesis")));
   if( seed == "genesis" )
      return genesis;
   return fc::ecc::private_key::regenerate(fc::sha256::hash(seed));
}

string database_fixture::generate_anon_acct_name()
{
   // names of the form "anon-acct-x123" ; the "x" is necessary
   //    to workaround issue #46
   return "anon-acct-x" + std::to_string( anon_acct_count++ );
}

void database_fixture::_push_transaction( const signed_transaction& tx, uint32_t skip_flags, const char* file, int line )
{
   db.push_transaction( tx, skip_flags );
}

void database_fixture::verify_asset_supplies( )const
{
   wlog("*** Begin asset supply verification ***");
   const asset_dynamic_data_object& core_asset_data = db.get_core_asset().dynamic_asset_data_id(db);
   BOOST_CHECK(core_asset_data.fee_pool == 0);

   const simple_index<account_statistics_object>& statistics_index = db.get_index_type<simple_index<account_statistics_object>>();
   const auto& balance_index = db.get_index_type<account_balance_index>().indices();
   const auto& settle_index = db.get_index_type<force_settlement_index>().indices();
   map<asset_id_type,share_type> total_balances;
   map<asset_id_type,share_type> total_debts;
   share_type core_in_orders;
   share_type reported_core_in_orders;

   for( const account_balance_object& b : balance_index )
      total_balances[b.asset_type] += b.balance;
   for( const force_settlement_object& s : settle_index )
      total_balances[s.balance.asset_id] += s.balance.amount;
   for( const account_statistics_object& a : statistics_index )
   {
      reported_core_in_orders += a.total_core_in_orders;
   }
   for( const limit_order_object& o : db.get_index_type<limit_order_index>().indices() )
   {
      asset for_sale = o.amount_for_sale();
      if( for_sale.asset_id == asset_id_type() ) core_in_orders += for_sale.amount;
      total_balances[for_sale.asset_id] += for_sale.amount;
   }
   for( const short_order_object& o : db.get_index_type<short_order_index>().indices() )
   {
      asset col = o.get_collateral();
      if( col.asset_id == asset_id_type() ) core_in_orders += col.amount;
      total_balances[col.asset_id] += col.amount;
   }
   for( const call_order_object& o : db.get_index_type<call_order_index>().indices() )
   {
      asset col = o.get_collateral();
      if( col.asset_id == asset_id_type() ) core_in_orders += col.amount;
      total_balances[col.asset_id] += col.amount;
      total_debts[o.get_debt().asset_id] += o.get_debt().amount;
   }
   for( const asset_object& asset_obj : db.get_index_type<asset_index>().indices() )
   {
      total_balances[asset_obj.id] += asset_obj.dynamic_asset_data_id(db).accumulated_fees;
      if( asset_obj.id != asset_id_type() )
         BOOST_CHECK_EQUAL(total_balances[asset_obj.id].value, asset_obj.dynamic_asset_data_id(db).current_supply.value);
      total_balances[asset_id_type()] += asset_obj.dynamic_asset_data_id(db).fee_pool;
   }
   for( const witness_object& witness_obj : db.get_index_type<simple_index<witness_object>>() )
   {
      total_balances[asset_id_type()] += witness_obj.accumulated_income;
   }
   for( const bond_offer_object& bond_offer : db.get_index_type<bond_offer_index>().indices() )
   {
      total_balances[ bond_offer.amount.asset_id ] += bond_offer.amount.amount;
      if( bond_offer.amount.asset_id == asset_id_type() )
         core_in_orders += bond_offer.amount.amount;
   }
   for( const vesting_balance_object& vbo : db.get_index_type< simple_index<vesting_balance_object> >() )
      total_balances[ vbo.balance.asset_id ] += vbo.balance.amount;

   total_balances[asset_id_type()] += db.get_dynamic_global_properties().witness_budget;

   for( const auto& item : total_debts )
      BOOST_CHECK_EQUAL(item.first(db).dynamic_asset_data_id(db).current_supply.value, item.second.value);

   BOOST_CHECK_EQUAL( core_in_orders.value , reported_core_in_orders.value );
   BOOST_CHECK_EQUAL( total_balances[asset_id_type()].value , core_asset_data.current_supply.value );
   wlog("***  End  asset supply verification ***");
}

void database_fixture::verify_account_history_plugin_index( )const
{
   if( skip_key_index_test )
      return;

   const std::shared_ptr<bts::account_history::account_history_plugin> pin =
      app.get_plugin<bts::account_history::account_history_plugin>( "account_history" );
   if( pin->tracked_accounts().size() == 0 )
   {
      vector< pair< account_id_type, address > > tuples_from_db;
      const auto& primary_account_idx = db.get_index_type<account_index>().indices().get<by_id>();
      flat_set< address > acct_addresses;
      acct_addresses.reserve( 2 * BTS_DEFAULT_MAX_AUTHORITY_MEMBERSHIP + 2 );

      for( const account_object& acct : primary_account_idx )
      {
         account_id_type account_id = acct.id;
         acct_addresses.clear();
         for( const pair< object_id_type, weight_type >& auth : acct.owner.auths )
         {
            if( auth.first.type() == key_object_type )
               acct_addresses.insert( key_id_type( auth.first )(db).key_address() );
         }
         for( const pair< object_id_type, weight_type >& auth : acct.active.auths )
         {
            if( auth.first.type() == key_object_type )
               acct_addresses.insert( key_id_type( auth.first )(db).key_address() );
         }
         acct_addresses.insert( acct.memo_key(db).key_address() );
         for( const address& addr : acct_addresses )
            tuples_from_db.emplace_back( account_id, addr );
      }

      vector< pair< account_id_type, address > > tuples_from_index;
      tuples_from_index.reserve( tuples_from_db.size() );
      const auto& key_account_idx =
         db.get_index_type<bts::account_history::key_account_index>()
         .indices().get<bts::account_history::by_key>();

      for( const bts::account_history::key_account_object& key_account : key_account_idx )
      {
         address addr = key_account.key;
         for( const account_id_type& account_id : key_account.account_ids )
            tuples_from_index.emplace_back( account_id, addr );
      }

      // TODO:  use function for common functionality
      {
         // due to hashed index, account_id's may not be in sorted order...
         std::sort( tuples_from_db.begin(), tuples_from_db.end() );
         size_t size_before_uniq = tuples_from_db.size();
         auto last = std::unique( tuples_from_db.begin(), tuples_from_db.end() );
         tuples_from_db.erase( last, tuples_from_db.end() );
         // but they should be unique (multiple instances of the same
         //  address within an account should have been de-duplicated
         //  by the flat_set above)
         BOOST_CHECK( tuples_from_db.size() == size_before_uniq );
      }

      {
         // (address, account) should be de-duplicated by flat_set<>
         // in key_account_object
         std::sort( tuples_from_index.begin(), tuples_from_index.end() );
         auto last = std::unique( tuples_from_index.begin(), tuples_from_index.end() );
         size_t size_before_uniq = tuples_from_db.size();
         tuples_from_index.erase( last, tuples_from_index.end() );
         BOOST_CHECK( tuples_from_index.size() == size_before_uniq );
      }

      //BOOST_CHECK_EQUAL( tuples_from_db, tuples_from_index );
      bool is_equal = true;
      is_equal &= (tuples_from_db.size() == tuples_from_index.size());
      for( size_t i=0,n=tuples_from_db.size(); i<n; i++ )
         is_equal &= (tuples_from_db[i] == tuples_from_index[i] );

      bool account_history_plugin_index_ok = is_equal;
      BOOST_CHECK( account_history_plugin_index_ok );
   }
   return;
}

void database_fixture::open_database()
{
   if( !data_dir ) {
      data_dir = fc::temp_directory();
      db.open(data_dir->path());
   }
}

signed_block database_fixture::generate_block(uint32_t skip, const fc::ecc::private_key& key)
{
   open_database();

   now += db.block_interval();
   // skip == ~0 will skip checks specified in database::validation_steps
   return db.generate_block( now, db.get_scheduled_witness( now )->second, key, skip );
}

void database_fixture::generate_blocks( uint32_t block_count )
{
   for( uint32_t i = 0; i < block_count; ++i )
      generate_block();
}

void database_fixture::generate_blocks( fc::time_point_sec timestamp )
{
   while( db.head_block_time() < timestamp )
      generate_block();
   return;
}

account_create_operation database_fixture::make_account(
   const std::string& name /* = "nathan" */,
   key_id_type key /* = key_id_type() */
   )
{
   account_create_operation create_account;
   create_account.registrar = account_id_type();

   create_account.name = name;
   create_account.owner = authority(123, key, 123);
   create_account.active = authority(321, key, 321);
   create_account.memo_key = key;

   auto& active_delegates = db.get_global_properties().active_delegates;
   if( active_delegates.size() > 0 )
   {
      set<vote_id_type> votes;
      votes.insert(active_delegates[rand() % active_delegates.size()](db).vote_id);
      votes.insert(active_delegates[rand() % active_delegates.size()](db).vote_id);
      votes.insert(active_delegates[rand() % active_delegates.size()](db).vote_id);
      votes.insert(active_delegates[rand() % active_delegates.size()](db).vote_id);
      votes.insert(active_delegates[rand() % active_delegates.size()](db).vote_id);
      create_account.vote = flat_set<vote_id_type>(votes.begin(), votes.end());
   }
   create_account.num_committee = create_account.vote.size();

   create_account.fee = create_account.calculate_fee(db.current_fee_schedule());
   return create_account;
}

account_create_operation database_fixture::make_account(
   const std::string& name,
   const account_object& registrar,
   const account_object& referrer,
   uint8_t referrer_percent /* = 100 */,
   key_id_type key /* = key_id_type() */
   )
{
   try
   {
      account_create_operation          create_account;

      create_account.registrar          = registrar.id;
      create_account.referrer           = referrer.id;
      create_account.referrer_percent   = referrer_percent;

      create_account.name = name;
      create_account.owner = authority(123, key, 123);
      create_account.active = authority(321, key, 321);
      create_account.memo_key = key;

      const vector<delegate_id_type>& active_delegates = db.get_global_properties().active_delegates;
      if( active_delegates.size() > 0 )
      {
         set<vote_id_type> votes;
         votes.insert(active_delegates[rand() % active_delegates.size()](db).vote_id);
         votes.insert(active_delegates[rand() % active_delegates.size()](db).vote_id);
         votes.insert(active_delegates[rand() % active_delegates.size()](db).vote_id);
         votes.insert(active_delegates[rand() % active_delegates.size()](db).vote_id);
         votes.insert(active_delegates[rand() % active_delegates.size()](db).vote_id);
         create_account.vote = flat_set<vote_id_type>(votes.begin(), votes.end());
      }
      create_account.num_committee = create_account.vote.size();

      create_account.fee = create_account.calculate_fee(db.current_fee_schedule());
      return create_account;
   }
   FC_CAPTURE_AND_RETHROW((name)(referrer_percent))
}

const asset_object& database_fixture::get_asset( const string& symbol )const
{
   return *db.get_index_type<asset_index>().indices().get<by_symbol>().find(symbol);
}

const account_object& database_fixture::get_account( const string& name )const
{
   return *db.get_index_type<account_index>().indices().get<by_name>().find(name);
}

const asset_object& database_fixture::create_bitasset(
   const string& name,
   account_id_type issuer /* = 1 */,
   uint16_t market_fee_percent /* = 100 */ /* 1% */,
   uint16_t flags /* = market_issued | charge_market_fee */
   )
{
   asset_create_operation creator;
   creator.issuer = issuer;
   creator.fee = asset();
   creator.symbol = name;
   creator.common_options.max_supply = BTS_MAX_SHARE_SUPPLY;
   creator.precision = 2;
   creator.common_options.market_fee_percent = market_fee_percent;
   creator.common_options.issuer_permissions = flags;
   creator.common_options.flags = flags;
   creator.common_options.core_exchange_rate = price({asset(1,1),asset(1)});
   creator.bitasset_options = asset_object::bitasset_options();
   trx.operations.push_back(std::move(creator));
   trx.validate();
   processed_transaction ptx = db.push_transaction(trx, ~0);
   trx.operations.clear();
   return db.get<asset_object>(ptx.operation_results[0].get<object_id_type>());
}

const asset_object& database_fixture::create_user_issued_asset( const string& name )
{
   asset_create_operation creator;
   creator.issuer = account_id_type();
   creator.fee = asset();
   creator.symbol = name;
   creator.common_options.max_supply = 0;
   creator.precision = 2;
   creator.common_options.core_exchange_rate = price({asset(1,1),asset(1)});
   creator.common_options.max_supply = BTS_MAX_SHARE_SUPPLY;
   creator.common_options.flags = charge_market_fee;
   creator.common_options.issuer_permissions = charge_market_fee;
   trx.operations.push_back(std::move(creator));
   trx.validate();
   processed_transaction ptx = db.push_transaction(trx, ~0);
   trx.operations.clear();
   return db.get<asset_object>(ptx.operation_results[0].get<object_id_type>());
}

void database_fixture::issue_uia( const account_object& recipient, asset amount )
{
   asset_issue_operation op({asset(),amount.asset_id(db).issuer, amount, recipient.id});
   trx.validate();
   trx.operations.push_back(op);
   return;
}

const short_order_object*database_fixture::create_short(account_id_type seller, const asset& amount_to_sell, const asset& collateral_provided, uint16_t initial_collateral_ratio, uint16_t maintenance_collateral_ratio)
{
   return create_short(seller(db), amount_to_sell, collateral_provided, initial_collateral_ratio, maintenance_collateral_ratio);
}

const short_order_object* database_fixture::create_short(
      const account_object& seller,
      const asset& amount_to_sell,
      const asset& collateral_provided,
      uint16_t initial_collateral_ratio /* = 2000 */,
      uint16_t maintenance_collateral_ratio /* = 1750 */
      )
{
   short_order_create_operation op;
   op.seller = seller.id;
   op.amount_to_sell = amount_to_sell;
   op.collateral = collateral_provided;
   op.initial_collateral_ratio = initial_collateral_ratio;
   op.maintenance_collateral_ratio = maintenance_collateral_ratio;
   trx.operations.push_back(std::move(op));
   trx.validate();
   processed_transaction ptx = db.push_transaction(trx, ~0);
   trx.operations.clear();
   return db.find<short_order_object>(ptx.operation_results[0].get<object_id_type>());
}

const account_object& database_fixture::create_account(
   const string& name,
   const key_id_type& key /* = key_id_type() */
   )
{
   trx.operations.push_back(make_account(name, key));
   trx.validate();
   processed_transaction ptx = db.push_transaction(trx, ~0);
   auto& result = db.get<account_object>(ptx.operation_results[0].get<object_id_type>());
   trx.operations.clear();
   return result;
}

const account_object& database_fixture::create_account(
   const string& name,
   const account_object& registrar,
   const account_object& referrer,
   uint8_t referrer_percent /* = 100 */,
   const key_id_type& key /*= key_id_type()*/
   )
{
   try
   {
      trx.operations.resize(1);
      trx.operations.back() = (make_account(name, registrar, referrer, referrer_percent, key));
      trx.validate();
      auto r = db.push_transaction(trx, ~0);
      const auto& result = db.get<account_object>(r.operation_results[0].get<object_id_type>());
      trx.operations.clear();
      return result;
   }
   FC_CAPTURE_AND_RETHROW( (name)(registrar)(referrer) )
}

const account_object& database_fixture::create_account(
   const string& name,
   const private_key_type& key,
   const account_id_type& registrar_id /* = account_id_type() */,
   const account_id_type& referrer_id /* = account_id_type() */,
   uint8_t referrer_percent /* = 100 */
   )
{
   try
   {
      trx.operations.clear();

      key_create_operation key_op;
      key_op.fee_paying_account = registrar_id;
      key_op.key_data = public_key_type( key.get_public_key() );
      trx.operations.push_back( key_op );

      account_create_operation account_create_op;
      relative_key_id_type key_rkid(0);

      account_create_op.registrar = registrar_id;
      account_create_op.name = name;
      account_create_op.owner = authority(1234, key_rkid, 1234);
      account_create_op.active = authority(5678, key_rkid, 5678);
      account_create_op.memo_key = key_rkid;
      trx.operations.push_back( account_create_op );

      trx.validate();

      processed_transaction ptx = db.push_transaction(trx, ~0);
      wdump( (ptx) );
      const account_object& result = db.get<account_object>(ptx.operation_results[1].get<object_id_type>());
      trx.operations.clear();
      return result;
   }
   FC_CAPTURE_AND_RETHROW( (name)(registrar_id)(referrer_id) )
}

const delegate_object& database_fixture::create_delegate( const account_object& owner )
{
   delegate_create_operation op;
   op.delegate_account = owner.id;
   trx.operations.push_back(op);
   trx.validate();
   processed_transaction ptx = db.push_transaction(trx, ~0);
   trx.operations.clear();
   return db.get<delegate_object>(ptx.operation_results[0].get<object_id_type>());
}

const witness_object&database_fixture::create_witness(account_id_type owner, key_id_type signing_key, const fc::ecc::private_key& signing_private_key)
{
   return create_witness(owner(db), signing_key, signing_private_key);
}

const witness_object& database_fixture::create_witness( const account_object& owner, key_id_type signing_key,
                                                        const fc::ecc::private_key& signing_private_key )
{ try {
      FC_ASSERT(db.get(signing_key).key_address() == public_key_type(signing_private_key.get_public_key()));
   witness_create_operation op;
   op.witness_account = owner.id;
   op.block_signing_key = signing_key;
   secret_hash_type::encoder enc;
   fc::raw::pack(enc, signing_private_key);
   fc::raw::pack(enc, secret_hash_type());
   op.initial_secret = secret_hash_type::hash(enc.result());
   trx.operations.push_back(op);
   trx.validate();
   processed_transaction ptx = db.push_transaction(trx, ~0);
   trx.clear();
   return db.get<witness_object>(ptx.operation_results[0].get<object_id_type>());
} FC_CAPTURE_AND_RETHROW() }

const key_object& database_fixture::register_key( const public_key_type& key )
{
   trx.operations.push_back(key_create_operation({asset(),account_id_type(), key}));
   key_id_type new_key = db.push_transaction(trx, ~0).operation_results[0].get<object_id_type>();
   trx.operations.clear();
   return new_key(db);
}

const key_object& database_fixture::register_address( const address& addr )
{
   trx.operations.push_back(key_create_operation({asset(), account_id_type(), addr}));
   key_id_type new_key = db.push_transaction(trx, ~0).operation_results[0].get<object_id_type>();
   trx.operations.clear();
   return new_key(db);
}

uint64_t database_fixture::fund(
   const account_object& account,
   const asset& amount /* = asset(500000) */
   )
{
   transfer(account_id_type()(db), account, amount);
   return get_balance(account, amount.asset_id(db));
}

void database_fixture::sign(signed_transaction& trx, key_id_type key_id, const fc::ecc::private_key& key)
{
  trx.sign( key_id, key );
}

const limit_order_object*database_fixture::create_sell_order(account_id_type user, const asset& amount, const asset& recv)
{
   return create_sell_order(user(db), amount, recv);
}

const limit_order_object* database_fixture::create_sell_order( const account_object& user, const asset& amount, const asset& recv )
{
   limit_order_create_operation buy_order;
   buy_order.seller = user.id;
   buy_order.amount_to_sell = amount;
   buy_order.min_to_receive = recv;
   trx.operations.push_back(buy_order);
   for( auto& op : trx.operations ) op.visit( operation_set_fee( db.current_fee_schedule() ) );
   trx.validate();
   auto processed = db.push_transaction(trx, ~0);
   trx.operations.clear();
   return db.find<limit_order_object>( processed.operation_results[0].get<object_id_type>() );
}

asset database_fixture::cancel_limit_order( const limit_order_object& order )
{
  limit_order_cancel_operation cancel_order;
  cancel_order.fee_paying_account = order.seller;
  cancel_order.order = order.id;
  trx.operations.push_back(cancel_order);
  for( auto& op : trx.operations ) op.visit( operation_set_fee( db.current_fee_schedule() ) );
  trx.validate();
  auto processed = db.push_transaction(trx, ~0);
  trx.operations.clear();
  return processed.operation_results[0].get<asset>();
}

asset database_fixture::cancel_short_order( const short_order_object& order )
{
  short_order_cancel_operation cancel_order;
  cancel_order.fee_paying_account = order.seller;
  cancel_order.order = order.id;
  trx.operations.push_back(cancel_order);
  for( auto& op : trx.operations ) op.visit( operation_set_fee( db.current_fee_schedule() ) );
  trx.validate();
  auto processed = db.push_transaction(trx, ~0);
  trx.operations.clear();
  return processed.operation_results[0].get<asset>();
}

void database_fixture::transfer(
   account_id_type from,
   account_id_type to,
   const asset& amount,
   const asset& fee /* = asset() */
   )
{
   transfer(from(db), to(db), amount, fee);
}

void database_fixture::transfer(
   const account_object& from,
   const account_object& to,
   const asset& amount,
   const asset& fee /* = asset() */ )
{
   try
   {
      trx.set_expiration(db.head_block_time() + fc::minutes(1));
      trx.operations.push_back(transfer_operation({ fee, from.id, to.id, amount, memo_data() }));

      if( fee == asset() )
      {
         for( auto& op : trx.operations ) op.visit( operation_set_fee( db.current_fee_schedule() ) );
      }
      trx.validate();
      db.push_transaction(trx, ~0);
      trx.operations.clear();
   } FC_CAPTURE_AND_RETHROW( (from.id)(to.id)(amount)(fee) )
}

void database_fixture::fund_fee_pool( const account_object& from, const asset_object& asset_to_fund, const share_type amount )
{
   trx.operations.push_back( asset_fund_fee_pool_operation({asset(), from.id, asset_to_fund.id, amount}) );

   for( auto& op : trx.operations )
      op.visit( operation_set_fee( db.current_fee_schedule() ) );
   trx.validate();
   db.push_transaction(trx, ~0);
   trx.operations.clear();
}

void database_fixture::enable_fees(
   share_type fee /* = BTS_BLOCKCHAIN_PRECISION */
   )
{
   db.modify(global_property_id_type()(db), [fee](global_property_object& gpo)
   {
      for( int i=0; i < FEE_TYPE_COUNT; ++i)
         gpo.parameters.current_fees.set(i, fee);
      gpo.parameters.current_fees.set( prime_upgrade_fee_type, 10*fee.value );
   } );
}

void database_fixture::upgrade_to_prime(account_id_type account)
{
   upgrade_to_prime(account(db));
}

void database_fixture::upgrade_to_prime( const account_object& account )
{
   try
   {
      account_update_operation op;
      op.account = account.id;
      op.upgrade_to_prime = true;
      trx.operations.emplace_back(operation(op));
      db.push_transaction( trx, ~0 );
      FC_ASSERT( account.is_prime() );
      trx.clear();
   }
   FC_CAPTURE_AND_RETHROW((account))
}

void database_fixture::print_market( const string& syma, const string& symb )const
{
   const auto& limit_idx = db.get_index_type<limit_order_index>();
   const auto& price_idx = limit_idx.indices().get<by_price>();

   cout << std::fixed;
   cout.precision(5);
   cout << std::setw(10) << std::left  << "NAME"      << " ";
   cout << std::setw(16) << std::right << "FOR SALE"  << " ";
   cout << std::setw(16) << std::right << "FOR WHAT"  << " ";
   cout << std::setw(10) << std::right << "PRICE"   << " ";
   cout << std::setw(10) << std::right << "1/PRICE" << "\n";
   cout << string(70, '=') << std::endl;
   auto cur = price_idx.begin();
   while( cur != price_idx.end() )
   {
      cout << std::setw( 10 ) << std::left   << cur->seller(db).name << " ";
      cout << std::setw( 10 ) << std::right  << cur->for_sale.value << " ";
      cout << std::setw( 5 )  << std::left   << cur->amount_for_sale().asset_id(db).symbol << " ";
      cout << std::setw( 10 ) << std::right  << cur->amount_to_receive().amount.value << " ";
      cout << std::setw( 5 )  << std::left   << cur->amount_to_receive().asset_id(db).symbol << " ";
      cout << std::setw( 10 ) << std::right  << cur->sell_price.to_real() << " ";
      cout << std::setw( 10 ) << std::right  << (~cur->sell_price).to_real() << " ";
      cout << "\n";
      ++cur;
   }
}

string database_fixture::pretty( const asset& a )const
{
  std::stringstream ss;
  ss << a.amount.value << " ";
  ss << a.asset_id(db).symbol;
  return ss.str();
}

void database_fixture::print_short_order( const short_order_object& cur )const
{
  std::cout << std::setw(10) << cur.seller(db).name << " ";
  std::cout << std::setw(10) << "SHORT" << " ";
  std::cout << std::setw(16) << pretty( cur.amount_for_sale() ) << " ";
  std::cout << std::setw(16) << pretty( cur.amount_to_receive() ) << " ";
  std::cout << std::setw(16) << (~cur.sell_price).to_real() << " ";
}

void database_fixture::print_limit_order( const limit_order_object& cur )const
{
  std::cout << std::setw(10) << cur.seller(db).name << " ";
  std::cout << std::setw(10) << "LIMIT" << " ";
  std::cout << std::setw(16) << pretty( cur.amount_for_sale() ) << " ";
  std::cout << std::setw(16) << pretty( cur.amount_to_receive() ) << " ";
  std::cout << std::setw(16) << cur.sell_price.to_real() << " ";
}

void database_fixture::print_call_orders()const
{
  cout << std::fixed;
  cout.precision(5);
  cout << std::setw(10) << std::left  << "NAME"      << " ";
  cout << std::setw(10) << std::right << "TYPE"      << " ";
  cout << std::setw(16) << std::right << "DEBT"  << " ";
  cout << std::setw(16) << std::right << "COLLAT"  << " ";
  cout << std::setw(16) << std::right << "CALL PRICE"     << " ";
  cout << std::setw(16) << std::right << "~CALL PRICE"     << "\n";
  cout << string(70, '=');

  for( const call_order_object& o : db.get_index_type<call_order_index>().indices() )
  {
     std::cout << "\n";
     cout << std::setw( 10 ) << std::left   << o.borrower(db).name << " ";
     cout << std::setw( 16 ) << std::right  << pretty( o.get_debt() ) << " ";
     cout << std::setw( 16 ) << std::right  << pretty( o.get_collateral() ) << " ";
     cout << std::setw( 16 ) << std::right  << o.call_price.to_real() << " ";
     cout << std::setw( 16 ) << std::right  << (~o.call_price).to_real() << " ";
  }
     std::cout << "\n";
}

void database_fixture::print_joint_market( const string& syma, const string& symb )const
{
  cout << std::fixed;
  cout.precision(5);

  cout << std::setw(10) << std::left  << "NAME"      << " ";
  cout << std::setw(10) << std::right << "TYPE"      << " ";
  cout << std::setw(16) << std::right << "FOR SALE"  << " ";
  cout << std::setw(16) << std::right << "FOR WHAT"  << " ";
  cout << std::setw(16) << std::right << "PRICE"     << "\n";
  cout << string(70, '=');

  const auto& limit_idx = db.get_index_type<limit_order_index>();
  const auto& limit_price_idx = limit_idx.indices().get<by_price>();
  const auto& short_idx = db.get_index_type<short_order_index>();
  const auto& sell_price_idx = short_idx.indices().get<by_price>();

  auto limit_itr = limit_price_idx.begin();
  auto short_itr = sell_price_idx.rbegin();
  while( true )
  {
     std::cout << std::endl;
     if( limit_itr != limit_price_idx.end() )
     {
        if( short_itr != sell_price_idx.rend() && limit_itr->sell_price > ~short_itr->sell_price )
        {
           print_short_order( *short_itr );
           ++short_itr;
        }
        else // print the limit
        {
           print_limit_order( *limit_itr );
           ++limit_itr;
        }
     }
     else if( short_itr != sell_price_idx.rend() )
     {
        print_short_order( *short_itr );
        ++short_itr;
     }
     else
        break;
  }
}

void database_fixture::print_short_market( const string& syma, const string& symb )const
{
  const auto& limit_idx = db.get_index_type<short_order_index>();
  const auto& price_idx = limit_idx.indices().get<by_price>();

  cout << std::fixed;
  cout.precision(5);
  cout << std::setw(10) << std::left  << "NAME"        << " ";
  cout << std::setw(16) << std::right << "FOR SHORT"   << " ";
  cout << std::setw(16) << std::right << "COLLATERAL"  << " ";
  cout << std::setw(10) << std::right << "PRICE"       << " ";
  cout << std::setw(10) << std::right << "1/PRICE"     << " ";
  cout << std::setw(10) << std::right << "CALL PRICE"  << " ";
  cout << std::setw(10) << std::right << "I-Ratio"     << " ";
  cout << std::setw(10) << std::right << "M-Ratio"     << "\n";
  cout << string(100, '=') << std::endl;
  auto cur = price_idx.begin();
  while( cur != price_idx.end() )
  {
     cout << std::setw( 10 ) << std::left   << cur->seller(db).name << " ";
     cout << std::setw( 16 ) << std::right  << pretty( cur->amount_for_sale() ) << " ";
     cout << std::setw( 16 ) << std::right  << pretty( cur->get_collateral() ) << " ";
     cout << std::setw( 10 ) << std::right  << cur->sell_price.to_real() << " ";
     cout << std::setw( 10 ) << std::right  << (~cur->sell_price).to_real() << " ";
     cout << std::setw( 10 ) << std::right  << (cur->call_price).to_real() << " ";
     cout << std::setw( 10 ) << std::right  << (cur->initial_collateral_ratio)/double(1000) << " ";
     cout << std::setw( 10 ) << std::right  << (cur->maintenance_collateral_ratio)/double(1000) << " ";
     cout << "\n";
     ++cur;
  }
}

int64_t database_fixture::get_balance( account_id_type account, asset_id_type a )const
{
  return db.get_balance(account, a).amount.value;
}

int64_t database_fixture::get_balance( const account_object& account, const asset_object& a )const
{
  return db.get_balance(account.get_id(), a.get_id()).amount.value;
}

} } // bts::chain
