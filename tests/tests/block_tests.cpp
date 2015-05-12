#include <bts/chain/database.hpp>
#include <bts/chain/operations.hpp>
#include <bts/chain/time.hpp>
#include <bts/chain/key_object.hpp>
#include <bts/chain/account_object.hpp>
#include <bts/chain/proposal_object.hpp>

#include <fc/crypto/digest.hpp>

#include "../common/database_fixture.hpp"

using namespace bts::chain;

BOOST_AUTO_TEST_SUITE(block_tests)

BOOST_AUTO_TEST_CASE( generate_empty_blocks )
{
   try {
      fc::temp_directory data_dir;
      {
         database db;
         db.open(data_dir.path(), genesis_allocation() );

         start_simulated_time( bts::chain::now() );

         auto delegate_priv_key  = fc::ecc::private_key::regenerate(fc::sha256::hash(string("genesis")) );
         for( uint32_t i = 0; i < 100; ++i )
         {
            auto aw = db.get_global_properties().active_witnesses;
            advance_simulated_time_to( db.get_next_generation_time(  aw[i%aw.size()] ) );
            auto b =  db.generate_block( delegate_priv_key, aw[i%aw.size()] );
         }
         db.close();
      }
      {
         wlog( "------------------------------------------------" );
         database db;
         db.open(data_dir.path() );
         BOOST_CHECK_EQUAL( db.head_block_num(), 100 );
         auto delegate_priv_key  = fc::ecc::private_key::regenerate(fc::sha256::hash(string("genesis")) );
         for( uint32_t i = 0; i < 100; ++i )
         {
            auto aw = db.get_global_properties().active_witnesses;
            advance_simulated_time_to( db.get_next_generation_time(  aw[i%aw.size()] ) );
            auto b = db.generate_block( delegate_priv_key, aw[i%aw.size()] );
         }
         BOOST_CHECK_EQUAL( db.head_block_num(), 200 );
      }
   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( undo_block )
{
   try {
      fc::temp_directory data_dir;
      {
         database db;
         db.open(data_dir.path(), genesis_allocation() );

         start_simulated_time( bts::chain::now() );

         auto delegate_priv_key  = fc::ecc::private_key::regenerate(fc::sha256::hash(string("genesis")) );
         for( uint32_t i = 0; i < 5; ++i )
         {
            auto aw = db.get_global_properties().active_witnesses;
            advance_simulated_time_to( db.get_next_generation_time(  aw[i%aw.size()] ) );
            auto b =  db.generate_block( delegate_priv_key, aw[i%aw.size()] );
         }
         BOOST_CHECK( db.head_block_num() == 5 );
         db.pop_block();
         BOOST_CHECK( db.head_block_num() == 4 );
         db.pop_block();
         BOOST_CHECK( db.head_block_num() == 3 );
         db.pop_block();
         BOOST_CHECK( db.head_block_num() == 2 );
         for( uint32_t i = 0; i < 5; ++i )
         {
            auto aw = db.get_global_properties().active_witnesses;
            advance_simulated_time_to( db.get_next_generation_time(  aw[i%aw.size()] ) );
            auto b =  db.generate_block( delegate_priv_key, aw[i%aw.size()] );
         }
         BOOST_CHECK( db.head_block_num() == 7 );
      }
   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( fork_blocks )
{
   try {
      fc::temp_directory data_dir1;
      fc::temp_directory data_dir2;

      database db1;
      db1.open( data_dir1.path(), genesis_allocation() );
      database db2;
      db2.open( data_dir2.path(), genesis_allocation() );

      start_simulated_time( bts::chain::now() );

      auto delegate_priv_key  = fc::ecc::private_key::regenerate(fc::sha256::hash(string("genesis")) );
      for( uint32_t i = 0; i < 20; ++i )
      {
         auto aw = db1.get_global_properties().active_witnesses;
         advance_simulated_time_to( db1.get_next_generation_time(  aw[i%aw.size()] ) );
         auto b =  db1.generate_block( delegate_priv_key, aw[i%aw.size()] );
         try {
            db2.push_block(b);
         } FC_CAPTURE_AND_RETHROW( ("db2") );
      }
      for( uint32_t i = 20; i < 23; ++i )
      {
         auto ad1 = db1.get_global_properties().active_witnesses;
         advance_simulated_time_to( db1.get_next_generation_time(  ad1[i%ad1.size()] ) );
         auto b =  db1.generate_block( delegate_priv_key, ad1[i%ad1.size()] );
      }
      for( uint32_t i = 23; i < 26; ++i )
      {
         auto ad2 = db2.get_global_properties().active_witnesses;
         advance_simulated_time_to( db2.get_next_generation_time(  ad2[i%ad2.size()] ) );
         auto b =  db2.generate_block( delegate_priv_key, ad2[i%ad2.size()] );
         db1.push_block(b);
      }

      //The two databases are on distinct forks now, but at the same height. Make a block on db1, make it invalid, then
      //pass it to db2 and assert that db2 doesn't switch to the new fork.
      signed_block good_block;
      BOOST_CHECK_EQUAL(db1.head_block_num(), 23);
      {
         auto ad2 = db2.get_global_properties().active_witnesses;
         advance_simulated_time_to( db2.get_next_generation_time(  ad2[db2.head_block_num()%ad2.size()] ) );
         auto b =  db2.generate_block( delegate_priv_key, ad2[db2.head_block_num()%ad2.size()] );
         good_block = b;
         b.transactions.emplace_back(signed_transaction());
         b.transactions.back().operations.emplace_back(transfer_operation());
         b.sign(delegate_priv_key);
         BOOST_CHECK_EQUAL(b.block_num(), 24);
         BOOST_CHECK_THROW(db1.push_block(b), fc::exception);
      }
      BOOST_CHECK_EQUAL(db1.head_block_num(), 23);

      db1.push_block(good_block);
      BOOST_CHECK_EQUAL(db1.head_block_id().str(), db2.head_block_id().str());
   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( undo_pending )
{
   try {
      fc::temp_directory data_dir;
      {
         database db;
         db.open(data_dir.path());

         start_simulated_time( bts::chain::now() );
         auto delegate_priv_key  = fc::ecc::private_key::regenerate(fc::sha256::hash(string("genesis")) );
         const bts::db::index& account_idx = db.get_index(protocol_ids, account_object_type);

         {
            signed_transaction trx;
            trx.set_expiration(db.head_block_id());
            trx.operations.push_back(transfer_operation({asset(10000000), account_id_type(), account_id_type(1)}));
            db.push_transaction(trx, ~0);

            auto aw = db.get_global_properties().active_witnesses;
            advance_simulated_time_to( db.get_next_generation_time(  aw[db.head_block_num()%aw.size()] ) );
            auto b =  db.generate_block( delegate_priv_key, aw[db.head_block_num()%aw.size()], ~0 );
         }

         signed_transaction trx;
         trx.set_expiration(bts::chain::now() + db.get_global_properties().parameters.maximum_time_until_expiration);
         account_id_type nathan_id = account_idx.get_next_id();
         account_create_operation cop;
         cop.registrar = account_id_type(1);
         cop.name = "nathan";
         trx.operations.push_back(cop);
         //trx.signatures.push_back(delegate_priv_key.sign_compact(fc::digest((transaction&)trx)));
         trx.sign( key_id_type(), delegate_priv_key );
         db.push_transaction(trx);

         auto aw = db.get_global_properties().active_witnesses;
         advance_simulated_time_to( db.get_next_generation_time(  aw[db.head_block_num()%aw.size()] ) );
         auto b =  db.generate_block( delegate_priv_key, aw[db.head_block_num()%aw.size()] );

         BOOST_CHECK(nathan_id(db).name == "nathan");

         trx.clear();
         trx.set_expiration(bts::chain::now() + db.get_global_properties().parameters.maximum_time_until_expiration-1);
         trx.operations.push_back(transfer_operation({asset(1),account_id_type(1), nathan_id, asset(5000)}));
         trx.sign( key_id_type(), delegate_priv_key );
         db.push_transaction(trx);
         trx.clear();
         trx.set_expiration(bts::chain::now() + db.get_global_properties().parameters.maximum_time_until_expiration-2);
         trx.operations.push_back(transfer_operation({asset(1),account_id_type(1), nathan_id, asset(5000)}));
         trx.sign( key_id_type(), delegate_priv_key );
         db.push_transaction(trx);

         BOOST_CHECK(db.get_balance(nathan_id, asset_id_type()).amount == 10000);
         db.clear_pending();
         BOOST_CHECK(db.get_balance(nathan_id, asset_id_type()).amount == 0);
      }
   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( switch_forks_undo_create )
{
   try {
      fc::temp_directory dir1,
                         dir2;
      database db1,
               db2;
      db1.open(dir1.path());
      db2.open(dir2.path());

      start_simulated_time(bts::chain::now());
      auto delegate_priv_key  = fc::ecc::private_key::regenerate(fc::sha256::hash(string("genesis")) );
      const bts::db::index& account_idx = db1.get_index(protocol_ids, account_object_type);

      signed_transaction trx;
      trx.set_expiration(bts::chain::now() + db1.get_global_properties().parameters.maximum_time_until_expiration);
      account_id_type nathan_id = account_idx.get_next_id();
      account_create_operation cop;
      cop.registrar = account_id_type(1);
      cop.name = "nathan";
      trx.operations.push_back(cop);
      trx.sign( key_id_type(), delegate_priv_key );
      db1.push_transaction(trx);

      auto aw = db1.get_global_properties().active_witnesses;
      advance_simulated_time_to( db1.get_next_generation_time(  aw[db1.head_block_num()%aw.size()] ) );
      auto b =  db1.generate_block( delegate_priv_key, aw[db1.head_block_num()%aw.size()] );

      BOOST_CHECK(nathan_id(db1).name == "nathan");

      aw = db2.get_global_properties().active_witnesses;
      advance_simulated_time_to( db2.get_next_generation_time(  aw[db2.head_block_num()%aw.size()] ) );
      b =  db2.generate_block( delegate_priv_key, aw[db2.head_block_num()%aw.size()] );
      db1.push_block(b);
      aw = db2.get_global_properties().active_witnesses;
      advance_simulated_time_to( db2.get_next_generation_time(  aw[db2.head_block_num()%aw.size()] ) );
      b =  db2.generate_block( delegate_priv_key, aw[db2.head_block_num()%aw.size()] );
      db1.push_block(b);

      BOOST_CHECK_THROW(nathan_id(db1), fc::exception);

      db2.push_transaction(trx);

      aw = db2.get_global_properties().active_witnesses;
      advance_simulated_time_to( db2.get_next_generation_time(  aw[db2.head_block_num()%aw.size()] ) );
      b =  db2.generate_block( delegate_priv_key, aw[db2.head_block_num()%aw.size()] );
      db1.push_block(b);

      BOOST_CHECK(nathan_id(db1).name == "nathan");
      BOOST_CHECK(nathan_id(db2).name == "nathan");
   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( duplicate_transactions )
{
   try {
      fc::temp_directory dir1,
                         dir2;
      database db1,
               db2;
      db1.open(dir1.path());
      db2.open(dir2.path());

      auto skip_sigs = database::skip_transaction_signatures;

      start_simulated_time(bts::chain::now());
      auto delegate_priv_key  = fc::ecc::private_key::regenerate(fc::sha256::hash(string("genesis")) );
      const bts::db::index& account_idx = db1.get_index(protocol_ids, account_object_type);

      signed_transaction trx;
      trx.relative_expiration = 1000;
      account_id_type nathan_id = account_idx.get_next_id();
      account_create_operation cop;
      cop.name = "nathan";
      trx.operations.push_back(cop);
      trx.sign( key_id_type(), delegate_priv_key );
      db1.push_transaction(trx, skip_sigs);

      trx = decltype(trx)();
      trx.relative_expiration = 1000;
      trx.operations.push_back(transfer_operation({asset(), account_id_type(), nathan_id, asset(500)}));
      trx.sign( key_id_type(), delegate_priv_key );
      db1.push_transaction(trx, skip_sigs);

      BOOST_CHECK_THROW(db1.push_transaction(trx, skip_sigs), fc::exception);

      auto aw = db1.get_global_properties().active_witnesses;
      advance_simulated_time_to( db1.get_next_generation_time(  aw[db1.head_block_num()%aw.size()] ) );
      auto b =  db1.generate_block( delegate_priv_key, aw[db1.head_block_num()%aw.size()], skip_sigs );
      db2.push_block(b, skip_sigs);

      BOOST_CHECK_THROW(db1.push_transaction(trx, skip_sigs), fc::exception);
      BOOST_CHECK_THROW(db2.push_transaction(trx, skip_sigs), fc::exception);
      BOOST_CHECK_EQUAL(db1.get_balance(nathan_id, asset_id_type()).amount.value, 500);
      BOOST_CHECK_EQUAL(db2.get_balance(nathan_id, asset_id_type()).amount.value, 500);
   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( tapos )
{
   try {
      fc::temp_directory dir1,
                         dir2;
      database db1,
               db2;
      db1.open(dir1.path());
      db2.open(dir2.path());

      const account_object& init1 = *db1.get_index_type<account_index>().indices().get<by_name>().find("init1");

      start_simulated_time(bts::chain::now());
      auto delegate_priv_key  = fc::ecc::private_key::regenerate(fc::sha256::hash(string("genesis")) );
      const bts::db::index& account_idx = db1.get_index(protocol_ids, account_object_type);

      auto aw = db1.get_global_properties().active_witnesses;
      advance_simulated_time_to( db1.get_next_generation_time(  aw[db1.head_block_num()%aw.size()] ) );
      auto b =  db1.generate_block( delegate_priv_key, aw[db1.head_block_num()%aw.size()] );

      signed_transaction trx;
      trx.ref_block_num = db1.head_block_num();
      //This transaction must be in the next block after its reference, or it is invalid.
      trx.relative_expiration = 1;

      account_id_type nathan_id = account_idx.get_next_id();
      account_create_operation cop;
      cop.registrar = init1.id;
      cop.name = "nathan";
      trx.operations.push_back(cop);
      trx.sign( key_id_type(), delegate_priv_key );

      //ref_block_prefix isn't set, so we should see an exception here.
      BOOST_REQUIRE_THROW(db1.push_transaction(trx), fc::exception);
      trx.ref_block_prefix = db1.head_block_id()._hash[1];
      trx.signatures.clear();
      trx.sign( key_id_type(), delegate_priv_key );
      db1.push_transaction(trx);

      aw = db1.get_global_properties().active_witnesses;
      advance_simulated_time_to( db1.get_next_generation_time(  aw[db1.head_block_num()%aw.size()] ) );
      b =  db1.generate_block( delegate_priv_key, aw[db1.head_block_num()%aw.size()] );

      trx.operations.clear();
      trx.signatures.clear();
      trx.operations.push_back(transfer_operation({asset(), account_id_type(), nathan_id, asset(50)}));
      trx.sign( key_id_type(), delegate_priv_key );
      //relative_expiration is 1, but ref block is 2 blocks old, so this should fail.
      BOOST_REQUIRE_THROW(db1.push_transaction(trx, database::skip_transaction_signatures), fc::exception);
      trx.relative_expiration = 2;
      trx.signatures.clear();
      trx.sign( key_id_type(), delegate_priv_key );
      db1.push_transaction(trx, database::skip_transaction_signatures);
   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_FIXTURE_TEST_CASE( maintenance_interval, database_fixture )
{
   try {
      generate_block();
      BOOST_CHECK_EQUAL(db.head_block_num(), 1);

      fc::time_point_sec maintenence_time = db.get_dynamic_global_properties().next_maintenance_time;
      BOOST_CHECK_GT(maintenence_time.sec_since_epoch(), db.head_block_time().sec_since_epoch());
      BOOST_CHECK_GT(maintenence_time.sec_since_epoch(), bts::chain::now().sec_since_epoch());
      auto initial_properties = db.get_global_properties();
      const account_object& nathan = create_account("nathan");
      const delegate_object nathans_delegate = create_delegate(nathan);
      {
         account_update_operation op;
         op.account = nathan.id;
         op.vote = nathan.votes;
         op.vote->insert(nathans_delegate.vote_id);
         trx.operations.push_back(op);
         db.push_transaction(trx, ~0);
         trx.operations.clear();
      }
      transfer(account_id_type()(db), nathan, asset(5000));

      generate_blocks(maintenence_time - initial_properties.parameters.block_interval);
      BOOST_CHECK_EQUAL(db.get_global_properties().parameters.maximum_transaction_size,
                        initial_properties.parameters.maximum_transaction_size);
      BOOST_CHECK_EQUAL(db.get_dynamic_global_properties().next_maintenance_time.sec_since_epoch(),
                        db.head_block_time().sec_since_epoch() + db.get_global_properties().parameters.block_interval);
      //Technically the next check could fail because the shuffled witness list happens to match the original list.
      //This should be exceedingly rare, though.
      BOOST_CHECK(db.get_global_properties().active_witnesses != initial_properties.active_witnesses);
      BOOST_CHECK(db.get_global_properties().active_delegates == initial_properties.active_delegates);

      generate_block();

      auto new_properties = db.get_global_properties();
      BOOST_CHECK(new_properties.active_delegates != initial_properties.active_delegates);
      BOOST_CHECK(std::find(new_properties.active_delegates.begin(),
                            new_properties.active_delegates.end(), nathans_delegate.id) !=
                  new_properties.active_delegates.end());
      BOOST_CHECK_EQUAL(db.get_dynamic_global_properties().next_maintenance_time.sec_since_epoch(),
                        maintenence_time.sec_since_epoch() + new_properties.parameters.maintenance_interval);
      maintenence_time = db.get_dynamic_global_properties().next_maintenance_time;
      BOOST_CHECK_GT(maintenence_time.sec_since_epoch(), db.head_block_time().sec_since_epoch());
      BOOST_CHECK_GT(maintenence_time.sec_since_epoch(), bts::chain::now().sec_since_epoch());
      db.close();
   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

/**
 *  Orders should specify a valid expiration time and they will ba automatically canceled if not filled by that time.
 *  This feature allows people to safely submit orders that have a limited lifetime, which is essential to some
 *  traders.
 */
BOOST_FIXTURE_TEST_CASE( short_order_expiration, database_fixture )
{ try {
   //Get a sane head block time
   generate_block();

   auto* test = &create_bitasset("TEST");
   auto* core = &asset_id_type()(db);
   auto* nathan = &create_account("nathan");
   auto* genesis = &account_id_type()(db);

   transfer(*genesis, *nathan, core->amount(50000));

   BOOST_CHECK_EQUAL( get_balance(*nathan, *core), 50000 );

   short_order_create_operation op;
   op.seller = nathan->id;
   op.amount_to_sell = test->amount(500);
   op.collateral = core->amount(500);
   op.expiration = db.head_block_time() + fc::seconds(10);
   trx.operations.push_back(op);
   auto ptrx = db.push_transaction(trx, ~0);

   BOOST_CHECK_EQUAL( get_balance(*nathan, *core), 49500 );

   auto ptrx_id = ptrx.operation_results.back().get<object_id_type>();
   auto short_index = db.get_index_type<short_order_index>().indices();
   auto short_itr = short_index.begin();
   BOOST_REQUIRE( short_itr != short_index.end() );
   BOOST_REQUIRE( short_itr->id == ptrx_id );
   BOOST_REQUIRE( db.find_object(short_itr->id) );
   BOOST_CHECK_EQUAL( get_balance(*nathan, *core), 49500 );
   auto id = short_itr->id;

   generate_blocks(op.expiration);
   test = &get_asset("TEST");
   core = &asset_id_type()(db);
   nathan = &get_account("nathan");
   genesis = &account_id_type()(db);

   BOOST_CHECK(db.find_object(id) == nullptr);
   BOOST_CHECK_EQUAL( get_balance(*nathan, *core), 50000 );
} FC_LOG_AND_RETHROW() }

BOOST_FIXTURE_TEST_CASE( limit_order_expiration, database_fixture )
{ try {
   //Get a sane head block time
   generate_block();

   auto* test = &create_bitasset("TEST");
   auto* core = &asset_id_type()(db);
   auto* nathan = &create_account("nathan");
   auto* genesis = &account_id_type()(db);

   transfer(*genesis, *nathan, core->amount(50000));

   BOOST_CHECK_EQUAL( get_balance(*nathan, *core), 50000 );

   limit_order_create_operation op;
   op.seller = nathan->id;
   op.amount_to_sell = core->amount(500);
   op.min_to_receive = test->amount(500);
   op.expiration = db.head_block_time() + fc::seconds(10);
   trx.operations.push_back(op);
   auto ptrx = db.push_transaction(trx, ~0);

   BOOST_CHECK_EQUAL( get_balance(*nathan, *core), 49500 );

   auto ptrx_id = ptrx.operation_results.back().get<object_id_type>();
   auto limit_index = db.get_index_type<limit_order_index>().indices();
   auto limit_itr = limit_index.begin();
   BOOST_REQUIRE( limit_itr != limit_index.end() );
   BOOST_REQUIRE( limit_itr->id == ptrx_id );
   BOOST_REQUIRE( db.find_object(limit_itr->id) );
   BOOST_CHECK_EQUAL( get_balance(*nathan, *core), 49500 );
   auto id = limit_itr->id;

   generate_blocks(op.expiration);
   test = &get_asset("TEST");
   core = &asset_id_type()(db);
   nathan = &get_account("nathan");
   genesis = &account_id_type()(db);

   BOOST_CHECK(db.find_object(id) == nullptr);
   BOOST_CHECK_EQUAL( get_balance(*nathan, *core), 50000 );
} FC_LOG_AND_RETHROW() }

BOOST_FIXTURE_TEST_CASE( change_block_interval, database_fixture )
{ try {
   generate_block();

   db.modify(db.get_global_properties(), [](global_property_object& p) {
      p.parameters.genesis_proposal_review_period = fc::hours(1).to_seconds();
   });

   {
      proposal_create_operation cop = proposal_create_operation::genesis_proposal(db);
      cop.fee_paying_account = account_id_type(1);
      cop.expiration_time = db.head_block_time() + *cop.review_period_seconds + 10;
      global_parameters_update_operation uop;
      uop.new_parameters.block_interval = 1;
      cop.proposed_ops.emplace_back(uop);
      trx.operations.push_back(cop);
      trx.sign(key_id_type(),generate_private_key("genesis"));
      db.push_transaction(trx);
   }
   {
      proposal_update_operation uop;
      uop.fee_paying_account = account_id_type(1);
      uop.active_approvals_to_add = {account_id_type(1), account_id_type(2), account_id_type(3), account_id_type(4),
                                     account_id_type(5), account_id_type(6), account_id_type(7), account_id_type(8)};
      trx.operations.push_back(uop);
      trx.sign(key_id_type(),generate_private_key("genesis"));
      db.push_transaction(trx);
      BOOST_CHECK(proposal_id_type()(db).is_authorized_to_execute(&db));
   }

   BOOST_CHECK_EQUAL(db.get_global_properties().parameters.block_interval, 5);
   auto past_time = db.head_block_time().sec_since_epoch();
   generate_block();
   BOOST_CHECK_EQUAL(db.head_block_time().sec_since_epoch() - past_time, 5);
   generate_block();
   BOOST_CHECK_EQUAL(db.head_block_time().sec_since_epoch() - past_time, 10);

   generate_blocks(proposal_id_type()(db).expiration_time + 5);
   BOOST_CHECK_EQUAL(db.get_global_properties().parameters.block_interval, 5);
   generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);

   BOOST_CHECK_EQUAL(db.get_global_properties().parameters.block_interval, 1);
   past_time = db.head_block_time().sec_since_epoch();
   generate_block();
   BOOST_CHECK_EQUAL(db.head_block_time().sec_since_epoch() - past_time, 1);
   generate_block();
   BOOST_CHECK_EQUAL(db.head_block_time().sec_since_epoch() - past_time, 2);
} FC_LOG_AND_RETHROW() }

BOOST_FIXTURE_TEST_CASE( force_settlement, database_fixture )
{ try {
   auto private_key = generate_private_key("genesis");
   account_id_type nathan_id = create_account("nathan").get_id();
   account_id_type shorter1_id = create_account("shorter1").get_id();
   account_id_type shorter2_id = create_account("shorter2").get_id();
   account_id_type shorter3_id = create_account("shorter3").get_id();
   transfer(account_id_type()(db), nathan_id(db), asset(100000000));
   transfer(account_id_type()(db), shorter1_id(db), asset(100000000));
   transfer(account_id_type()(db), shorter2_id(db), asset(100000000));
   transfer(account_id_type()(db), shorter3_id(db), asset(100000000));
   asset_id_type bit_usd = create_bitasset("BITUSD", 0).get_id();
   {
      asset_update_bitasset_operation op;
      op.asset_to_update = bit_usd;
      op.issuer = bit_usd(db).issuer;
      op.new_options = bit_usd(db).bitasset_data(db).options;
      op.new_options.maximum_force_settlement_volume = 9000;
      trx.clear();
      trx.operations.push_back(op);
      db.push_transaction(trx, ~0);
      trx.clear();
   }
   generate_block();

   create_short(shorter1_id(db), asset(1000, bit_usd), asset(1000));
   create_sell_order(nathan_id(db), asset(1000), asset(1000, bit_usd));
   create_short(shorter2_id(db), asset(2000, bit_usd), asset(1999));
   create_sell_order(nathan_id(db), asset(1999), asset(2000, bit_usd));
   create_short(shorter3_id(db), asset(3000, bit_usd), asset(2990));
   create_sell_order(nathan_id(db), asset(2990), asset(3000, bit_usd));
   BOOST_CHECK_EQUAL(get_balance(nathan_id, bit_usd), 6000);

   transfer(nathan_id(db), account_id_type()(db), db.get_balance(nathan_id, asset_id_type()));

   {
      asset_update_bitasset_operation uop;
      uop.issuer = bit_usd(db).issuer;
      uop.asset_to_update = bit_usd;
      uop.new_options = bit_usd(db).bitasset_data(db).options;
      uop.new_options.force_settlement_delay_sec = 100;
      uop.new_options.force_settlement_offset_percent = 100;
      trx.operations.push_back(uop);
   } {
      asset_update_feed_producers_operation uop;
      uop.asset_to_update = bit_usd;
      uop.issuer = bit_usd(db).issuer;
      uop.new_feed_producers = {nathan_id};
      trx.operations.push_back(uop);
   } {
      asset_publish_feed_operation pop;
      pop.publisher = nathan_id;
      price_feed feed;
      feed.settlement_price = price(asset(1),asset(1, bit_usd));
      feed.call_limit = price::min(0, bit_usd);
      feed.short_limit = price::min(bit_usd, 0);
      pop.feed = feed;
      trx.operations.push_back(pop);
   }
   trx.sign(key_id_type(),private_key);
   db.push_transaction(trx);
   trx.clear();

   asset_settle_operation sop;
   sop.account = nathan_id;
   sop.amount = asset(50, bit_usd);
   trx.operations.push_back(sop);
   REQUIRE_THROW_WITH_VALUE(sop, amount, asset(999999, bit_usd));
   trx.operations.back() = sop;
   trx.sign(key_id_type(),private_key);

   //Partially settle a call
   force_settlement_id_type settle_id = db.push_transaction(trx).operation_results.front().get<object_id_type>();
   trx.clear();
   call_order_id_type call_id = db.get_index_type<call_order_index>().indices().get<by_collateral>().begin()->id;
   BOOST_CHECK_EQUAL(settle_id(db).balance.amount.value, 50);
   BOOST_CHECK_EQUAL(call_id(db).debt.value, 3000);
   BOOST_CHECK(settle_id(db).owner == nathan_id);

   generate_blocks(settle_id(db).settlement_date);
   BOOST_CHECK(db.find(settle_id) == nullptr);
   BOOST_CHECK_EQUAL(get_balance(nathan_id, asset_id_type()), 49);
   BOOST_CHECK_EQUAL(call_id(db).debt.value, 2950);

   //Exactly settle a call
   call_id = db.get_index_type<call_order_index>().indices().get<by_collateral>().begin()->id;
   sop.amount.amount = 2000;
   trx.operations.push_back(sop);
   trx.sign(key_id_type(),private_key);
   //Trx has expired by now. Make sure it throws.
   BOOST_CHECK_THROW(settle_id = db.push_transaction(trx).operation_results.front().get<object_id_type>(), fc::exception);
   trx.set_expiration(db.head_block_time() + fc::minutes(1));
   trx.sign(key_id_type(),private_key);
   settle_id = db.push_transaction(trx).operation_results.front().get<object_id_type>();
   trx.clear();

   generate_blocks(settle_id(db).settlement_date);
   BOOST_CHECK(db.find(settle_id) == nullptr);
   BOOST_CHECK_EQUAL(get_balance(nathan_id, asset_id_type()), 2029);
   BOOST_CHECK(db.find(call_id) == nullptr);
   trx.set_expiration(db.head_block_time() + fc::minutes(1));

   //Attempt to settle all existing asset
   sop.amount = db.get_balance(nathan_id, bit_usd);
   trx.operations.push_back(sop);
   trx.sign(key_id_type(),private_key);
   settle_id = db.push_transaction(trx).operation_results.front().get<object_id_type>();
   trx.clear();

   generate_blocks(settle_id(db).settlement_date);
   //We've hit the max force settlement. Can't settle more now.
   BOOST_CHECK(db.find(settle_id));
   BOOST_CHECK_EQUAL(get_balance(nathan_id, asset_id_type()), 3517);
   BOOST_CHECK(!db.get_index_type<call_order_index>().indices().empty());

   generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);
   //Now it's been another maintenance interval, so we should have some more settlement.
   //I can't force settle all existing asset, but with a 90% limit, I get pretty close.
   BOOST_CHECK(db.find(settle_id));
   BOOST_CHECK_EQUAL(get_balance(nathan_id, asset_id_type()), 5694);
   BOOST_CHECK(!db.get_index_type<call_order_index>().indices().empty());
} FC_LOG_AND_RETHROW() }

BOOST_FIXTURE_TEST_CASE( pop_block_twice, database_fixture )
{
   try
   {
/*
            skip_delegate_signature     = 0x01, ///< used while reindexing
            skip_transaction_signatures = 0x02, ///< used by non delegate nodes
            skip_undo_block             = 0x04, ///< used while reindexing
            skip_undo_transaction       = 0x08, ///< used while applying block
            skip_transaction_dupe_check = 0x10, ///< used while reindexing
            skip_fork_db                = 0x20, ///< used while reindexing
            skip_block_size_check       = 0x40, ///< used when applying locally generated transactions
            skip_tapos_check            = 0x80, ///< used while reindexing -- note this skips expiration check as well
*/

      uint32_t skip_flags = (
           database::skip_delegate_signature
         | database::skip_transaction_signatures
         );

      const asset_object& core = asset_id_type()(db);

      // Sam is the creator of accounts
      private_key_type genesis_key = generate_private_key("genesis");
      private_key_type sam_key = generate_private_key("sam");
      account_object sam_account_object = create_account( "sam", sam_key );

      //Get a sane head block time
      generate_block( skip_flags );

      db.modify(db.get_global_properties(), [](global_property_object& p) {
         p.parameters.genesis_proposal_review_period = fc::hours(1).to_seconds();
      });

      transaction tx;
      processed_transaction ptx;

      account_object genesis_account_object = genesis_account(db);
      // transfer from genesis account to Sam account
      transfer(genesis_account_object, sam_account_object, core.amount(100000));

      generate_block( skip_flags );

      create_account( "alice" );
      generate_block( skip_flags );
      create_account( "bob" );
      generate_block( skip_flags );

      db.pop_block();
      db.pop_block();
   }
   catch( const fc::exception& e )
   {
      edump( (e.to_detail_string()) );
      throw;
   }
}


BOOST_AUTO_TEST_SUITE_END()
