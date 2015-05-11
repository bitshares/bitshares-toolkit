#include <bts/chain/database.hpp>
#include <bts/chain/operations.hpp>
#include <bts/chain/account_object.hpp>
#include <bts/chain/asset_object.hpp>
#include <bts/chain/key_object.hpp>
#include <bts/chain/delegate_object.hpp>
#include <bts/chain/vesting_balance_object.hpp>
#include <bts/chain/withdraw_permission_object.hpp>

#include <fc/crypto/digest.hpp>

#include "../common/database_fixture.hpp"

using namespace bts::chain;

BOOST_FIXTURE_TEST_SUITE( operation_tests, database_fixture )

BOOST_AUTO_TEST_CASE( withdraw_permission_create )
{ try {
   auto nathan_private_key = generate_private_key("nathan");
   auto dan_private_key = generate_private_key("dan");
   key_id_type nathan_key_id = register_key(nathan_private_key.get_public_key()).id;
   key_id_type dan_key_id = register_key(dan_private_key.get_public_key()).id;
   account_id_type nathan_id = create_account("nathan", nathan_key_id).id;
   account_id_type dan_id = create_account("dan", dan_key_id).id;
   transfer(account_id_type(), nathan_id, asset(1000));
   generate_block();
   trx.set_expiration(db.head_block_time() + BTS_DEFAULT_MAX_TIME_UNTIL_EXPIRATION);

   {
      withdraw_permission_create_operation op;
      op.authorized_account = dan_id;
      op.withdraw_from_account = nathan_id;
      op.withdrawal_limit = asset(5);
      op.withdrawal_period_sec = fc::hours(1).to_seconds();
      op.periods_until_expiration = 5;
      op.period_start_time = db.head_block_time() + db.get_global_properties().parameters.block_interval*5;
      trx.operations.push_back(op);
      REQUIRE_OP_VALIDATION_FAILURE(op, withdrawal_limit, asset());
      REQUIRE_OP_VALIDATION_FAILURE(op, periods_until_expiration, 0);
      REQUIRE_OP_VALIDATION_FAILURE(op, withdraw_from_account, dan_id);
      REQUIRE_OP_VALIDATION_FAILURE(op, withdrawal_period_sec, 0);
      REQUIRE_THROW_WITH_VALUE(op, withdrawal_limit, asset(10, 10));
      REQUIRE_THROW_WITH_VALUE(op, authorized_account, account_id_type(1000));
      REQUIRE_THROW_WITH_VALUE(op, period_start_time, fc::time_point_sec(10000));
      REQUIRE_THROW_WITH_VALUE(op, withdrawal_period_sec, 1);
      trx.operations.back() = op;
   }

   trx.sign(nathan_key_id, nathan_private_key);
   db.push_transaction(trx);
   trx.clear();
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( withdraw_permission_test )
{ try {
   INVOKE(withdraw_permission_create);

   auto nathan_private_key = generate_private_key("nathan");
   auto dan_private_key = generate_private_key("dan");
   account_id_type nathan_id = get_account("nathan").id;
   account_id_type dan_id = get_account("dan").id;
   key_id_type dan_key_id = dan_id(db).active.auths.begin()->first;
   withdraw_permission_id_type permit;
   trx.set_expiration(db.head_block_time() + BTS_DEFAULT_MAX_TIME_UNTIL_EXPIRATION);

   fc::time_point_sec first_start_time;
   {
      const withdraw_permission_object& permit_object = permit(db);
      BOOST_CHECK(permit_object.authorized_account == dan_id);
      BOOST_CHECK(permit_object.withdraw_from_account == nathan_id);
      BOOST_CHECK(permit_object.next_period_start_time > db.head_block_time());
      first_start_time = permit_object.next_period_start_time;
      BOOST_CHECK(permit_object.withdrawal_limit == asset(5));
      BOOST_CHECK(permit_object.withdrawal_period_sec == fc::hours(1).to_seconds());
      BOOST_CHECK(permit_object.remaining_periods == 5);
   }

   generate_blocks(2);

   {
      withdraw_permission_claim_operation op;
      op.withdraw_permission = permit;
      op.withdraw_from_account = nathan_id;
      op.withdraw_to_account = dan_id;
      op.amount_to_withdraw = asset(2);
      trx.operations.push_back(op);
      //Throws because we haven't entered the first withdrawal period yet.
      BOOST_REQUIRE_THROW(db.push_transaction(trx), fc::exception);
      //Get to the actual withdrawal period
      generate_blocks(permit(db).next_period_start_time);

      REQUIRE_THROW_WITH_VALUE(op, withdraw_permission, withdraw_permission_id_type(5));
      REQUIRE_THROW_WITH_VALUE(op, withdraw_from_account, dan_id);
      REQUIRE_THROW_WITH_VALUE(op, withdraw_from_account, account_id_type());
      REQUIRE_THROW_WITH_VALUE(op, withdraw_to_account, nathan_id);
      REQUIRE_THROW_WITH_VALUE(op, withdraw_to_account, account_id_type());
      REQUIRE_THROW_WITH_VALUE(op, amount_to_withdraw, asset(10));
      REQUIRE_THROW_WITH_VALUE(op, amount_to_withdraw, asset(6));
      trx.clear();
      trx.operations.push_back(op);
      trx.sign(dan_key_id, dan_private_key);
      db.push_transaction(trx);
      //Make sure we can't withdraw again this period, even if we're not exceeding the periodic limit
      REQUIRE_THROW_WITH_VALUE(op, amount_to_withdraw, asset(1));
      trx.clear();
   }

   BOOST_CHECK_EQUAL(get_balance(nathan_id, asset_id_type()), 998);
   BOOST_CHECK_EQUAL(get_balance(dan_id, asset_id_type()), 2);

   {
      const withdraw_permission_object& permit_object = permit(db);
      BOOST_CHECK(permit_object.authorized_account == dan_id);
      BOOST_CHECK(permit_object.withdraw_from_account == nathan_id);
      BOOST_CHECK(permit_object.next_period_start_time == first_start_time + permit_object.withdrawal_period_sec);
      BOOST_CHECK(permit_object.withdrawal_limit == asset(5));
      BOOST_CHECK(permit_object.withdrawal_period_sec == fc::hours(1).to_seconds());
      BOOST_CHECK(permit_object.remaining_periods == 4);
      generate_blocks(permit_object.next_period_start_time + permit_object.withdrawal_period_sec);
   }

   {
      transfer(nathan_id, dan_id, asset(997));
      withdraw_permission_claim_operation op;
      op.withdraw_permission = permit;
      op.withdraw_from_account = nathan_id;
      op.withdraw_to_account = dan_id;
      op.amount_to_withdraw = asset(5);
      trx.operations.push_back(op);
      trx.sign(dan_key_id, dan_private_key);
      //Throws because nathan doesn't have the money
      BOOST_CHECK_THROW(db.push_transaction(trx), fc::exception);
      op.amount_to_withdraw = asset(1);
      trx.operations.back() = op;
      trx.sign(dan_key_id, dan_private_key);
      db.push_transaction(trx);
   }

   BOOST_CHECK_EQUAL(get_balance(nathan_id, asset_id_type()), 0);
   BOOST_CHECK_EQUAL(get_balance(dan_id, asset_id_type()), 1000);
   trx.clear();
   transfer(dan_id, nathan_id, asset(1000));

   {
      const withdraw_permission_object& permit_object = permit(db);
      BOOST_CHECK(permit_object.authorized_account == dan_id);
      BOOST_CHECK(permit_object.withdraw_from_account == nathan_id);
      BOOST_CHECK(permit_object.next_period_start_time == first_start_time + 3*permit_object.withdrawal_period_sec);
      BOOST_CHECK(permit_object.withdrawal_limit == asset(5));
      BOOST_CHECK(permit_object.withdrawal_period_sec == fc::hours(1).to_seconds());
      BOOST_CHECK(permit_object.remaining_periods == 2);
      generate_blocks(permit_object.next_period_start_time + 3*permit_object.withdrawal_period_sec);
   }

   {
      withdraw_permission_claim_operation op;
      op.withdraw_permission = permit;
      op.withdraw_from_account = nathan_id;
      op.withdraw_to_account = dan_id;
      op.amount_to_withdraw = asset(5);
      trx.operations.push_back(op);
      trx.sign(dan_key_id, dan_private_key);
      //Throws because the permission has expired
      BOOST_CHECK_THROW(db.push_transaction(trx), fc::exception);
   }
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( withdraw_permission_nominal_case )
{ try {
   INVOKE(withdraw_permission_create);

   auto nathan_private_key = generate_private_key("nathan");
   auto dan_private_key = generate_private_key("dan");
   account_id_type nathan_id = get_account("nathan").id;
   account_id_type dan_id = get_account("dan").id;
   key_id_type dan_key_id = dan_id(db).active.auths.begin()->first;
   withdraw_permission_id_type permit;
   trx.set_expiration(db.head_block_time() + BTS_DEFAULT_MAX_TIME_UNTIL_EXPIRATION);

   while(db.find_object(permit))
   {
      generate_blocks(db.get(permit).next_period_start_time + 50);
      BOOST_CHECK(db.get(permit).claimable);
      withdraw_permission_claim_operation op;
      op.withdraw_permission = permit;
      op.withdraw_from_account = nathan_id;
      op.withdraw_to_account = dan_id;
      op.amount_to_withdraw = asset(5);
      trx.operations.push_back(op);
      trx.sign(dan_key_id, dan_private_key);
      db.push_transaction(trx);
      trx.clear();
      BOOST_CHECK(!db.find_object(permit) || !db.get(permit).claimable);
   }

   BOOST_CHECK_EQUAL(get_balance(nathan_id, asset_id_type()), 975);
   BOOST_CHECK_EQUAL(get_balance(dan_id, asset_id_type()), 25);
   BOOST_CHECK(db.find_object(permit) == nullptr);
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( withdraw_permission_update )
{ try {
   INVOKE(withdraw_permission_create);

   auto nathan_private_key = generate_private_key("nathan");
   account_id_type nathan_id = get_account("nathan").id;
   account_id_type dan_id = get_account("dan").id;
   key_id_type nathan_key_id = nathan_id(db).active.auths.begin()->first;
   withdraw_permission_id_type permit;
   trx.set_expiration(db.head_block_time() + BTS_DEFAULT_MAX_TIME_UNTIL_EXPIRATION);

   {
      withdraw_permission_update_operation op;
      op.permission_to_update = permit;
      op.authorized_account = dan_id;
      op.withdraw_from_account = nathan_id;
      op.periods_until_expiration = 2;
      op.period_start_time = db.head_block_time() + 10;
      op.withdrawal_period_sec = 10;
      op.withdrawal_limit = asset(12);
      trx.operations.push_back(op);
      REQUIRE_THROW_WITH_VALUE(op, periods_until_expiration, 0);
      REQUIRE_THROW_WITH_VALUE(op, withdrawal_period_sec, 0);
      REQUIRE_THROW_WITH_VALUE(op, withdrawal_limit, asset(1, 12));
      REQUIRE_THROW_WITH_VALUE(op, withdrawal_limit, asset(0));
      REQUIRE_THROW_WITH_VALUE(op, withdraw_from_account, account_id_type(0));
      REQUIRE_THROW_WITH_VALUE(op, authorized_account, account_id_type(0));
      REQUIRE_THROW_WITH_VALUE(op, period_start_time, db.head_block_time() - 50);
      trx.operations.back() = op;
      trx.sign(nathan_key_id, nathan_private_key);
      db.push_transaction(trx);
   }

   {
      const withdraw_permission_object& permit_object = db.get(permit);
      BOOST_CHECK(permit_object.authorized_account == dan_id);
      BOOST_CHECK(permit_object.withdraw_from_account == nathan_id);
      BOOST_CHECK(permit_object.next_period_start_time == db.head_block_time() + 10);
      BOOST_CHECK(permit_object.withdrawal_limit == asset(12));
      BOOST_CHECK(permit_object.withdrawal_period_sec == 10);
      BOOST_CHECK(permit_object.remaining_periods == 2);
   }
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( withdraw_permission_delete )
{ try {
   INVOKE(withdraw_permission_update);

   withdraw_permission_delete_operation op;
   op.authorized_account = get_account("dan").id;
   op.withdraw_from_account = get_account("nathan").id;
   trx.set_expiration(db.head_block_id());
   trx.operations.push_back(op);
   trx.sign(get_account("nathan").active.auths.begin()->first, generate_private_key("nathan"));
   db.push_transaction(trx);
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( mia_feeds )
{ try {
   auto nathan_private_key = generate_private_key("nathan");
   auto nathan_key_id = register_key(nathan_private_key.get_public_key()).get_id();
   account_id_type nathan_id = create_account("nathan", nathan_key_id).id;
   auto dan_private_key = generate_private_key("dan");
   auto dan_key_id = register_key(dan_private_key.get_public_key()).get_id();
   account_id_type dan_id = create_account("dan", dan_key_id).id;
   auto ben_private_key = generate_private_key("ben");
   auto ben_key_id = register_key(ben_private_key.get_public_key()).get_id();
   account_id_type ben_id = create_account("ben", ben_key_id).id;
   auto vikram_private_key = generate_private_key("vikram");
   auto vikram_key_id = register_key(vikram_private_key.get_public_key()).get_id();
   account_id_type vikram_id = create_account("vikram", vikram_key_id).id;
   asset_id_type bit_usd_id = create_bitasset("BITUSD").id;

   {
      asset_update_operation op;
      const asset_object& obj = bit_usd_id(db);
      op.asset_to_update = bit_usd_id;
      op.issuer = obj.issuer;
      op.new_issuer = nathan_id;
      op.new_options = obj.options;
      trx.operations.push_back(op);
      db.push_transaction(trx, ~0);
      generate_block();
      trx.clear();
   }
   {
      asset_update_feed_producers_operation op;
      op.asset_to_update = bit_usd_id;
      op.issuer = nathan_id;
      op.new_feed_producers = {dan_id, ben_id, vikram_id};
      trx.operations.push_back(op);
      trx.sign(nathan_key_id, nathan_private_key);
      db.push_transaction(trx);
      generate_block(database::skip_nothing);
   }
   {
      const asset_bitasset_data_object& obj = bit_usd_id(db).bitasset_data(db);
      BOOST_CHECK_EQUAL(obj.feeds.size(), 3);
      BOOST_CHECK(obj.current_feed == price_feed());
   }
   {
      const asset_object& bit_usd = bit_usd_id(db);
      asset_publish_feed_operation op({vikram_id, asset()});
      op.feed.call_limit = price(asset(BTS_BLOCKCHAIN_PRECISION),bit_usd.amount(30));
      op.feed.short_limit = ~price(asset(BTS_BLOCKCHAIN_PRECISION),bit_usd.amount(10));
      // We'll expire margins after a month
      op.feed.max_margin_period_sec = fc::days(30).to_seconds();
      // Accept defaults for required collateral
      trx.operations.emplace_back(op);
      db.push_transaction(trx, ~0);

      const asset_bitasset_data_object& bitasset = bit_usd.bitasset_data(db);
      BOOST_CHECK(bitasset.current_feed.call_limit.to_real() == BTS_BLOCKCHAIN_PRECISION / 30.0);
      BOOST_CHECK_EQUAL(bitasset.current_feed.short_limit.to_real(), 10.0 / BTS_BLOCKCHAIN_PRECISION);
      BOOST_CHECK(bitasset.current_feed.max_margin_period_sec == fc::days(30).to_seconds());
      BOOST_CHECK(bitasset.current_feed.required_initial_collateral == BTS_DEFAULT_INITIAL_COLLATERAL_RATIO);
      BOOST_CHECK(bitasset.current_feed.required_maintenance_collateral == BTS_DEFAULT_MAINTENANCE_COLLATERAL_RATIO);

      op.publisher = ben_id;
      op.feed.call_limit = price(asset(BTS_BLOCKCHAIN_PRECISION),bit_usd.amount(25));
      op.feed.short_limit = ~price(asset(BTS_BLOCKCHAIN_PRECISION),bit_usd.amount(20));
      op.feed.max_margin_period_sec = fc::days(10).to_seconds();
      trx.operations.back() = op;
      db.push_transaction(trx, ~0);

      BOOST_CHECK_EQUAL(bitasset.current_feed.call_limit.to_real(), BTS_BLOCKCHAIN_PRECISION / 25.0);
      BOOST_CHECK_EQUAL(bitasset.current_feed.short_limit.to_real(), 20.0 / BTS_BLOCKCHAIN_PRECISION);
      BOOST_CHECK(bitasset.current_feed.max_margin_period_sec == fc::days(30).to_seconds());
      BOOST_CHECK(bitasset.current_feed.required_initial_collateral == BTS_DEFAULT_INITIAL_COLLATERAL_RATIO);
      BOOST_CHECK(bitasset.current_feed.required_maintenance_collateral == BTS_DEFAULT_MAINTENANCE_COLLATERAL_RATIO);

      op.publisher = dan_id;
      op.feed.call_limit = price(asset(BTS_BLOCKCHAIN_PRECISION),bit_usd.amount(40));
      op.feed.short_limit = ~price(asset(BTS_BLOCKCHAIN_PRECISION),bit_usd.amount(10));
      op.feed.max_margin_period_sec = fc::days(100).to_seconds();
      op.feed.required_initial_collateral = 1001;
      op.feed.required_maintenance_collateral = 1000;
      trx.operations.back() = op;
      db.push_transaction(trx, ~0);

      BOOST_CHECK_EQUAL(bitasset.current_feed.call_limit.to_real(), BTS_BLOCKCHAIN_PRECISION / 30.0);
      BOOST_CHECK_EQUAL(bitasset.current_feed.short_limit.to_real(), 10.0 / BTS_BLOCKCHAIN_PRECISION);
      BOOST_CHECK(bitasset.current_feed.max_margin_period_sec == fc::days(30).to_seconds());
      BOOST_CHECK(bitasset.current_feed.required_initial_collateral == BTS_DEFAULT_INITIAL_COLLATERAL_RATIO);
      BOOST_CHECK(bitasset.current_feed.required_maintenance_collateral == BTS_DEFAULT_MAINTENANCE_COLLATERAL_RATIO);

      op.publisher = nathan_id;
      trx.operations.back() = op;
      BOOST_CHECK_THROW(db.push_transaction(trx, ~0), fc::exception);
   }
} FC_LOG_AND_RETHROW() }

// TODO:  Write linear VBO tests

BOOST_AUTO_TEST_SUITE_END()
