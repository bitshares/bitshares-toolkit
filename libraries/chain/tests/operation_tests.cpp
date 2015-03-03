#include <bts/chain/database.hpp>
#include <bts/chain/operations.hpp>
#include <bts/chain/account_index.hpp>
#include <bts/chain/key_object.hpp>

#include <fc/crypto/digest.hpp>

#include "database_fixture.hpp"

using namespace bts::chain;

BOOST_FIXTURE_TEST_SUITE( operation_unit_tests, database_fixture )

BOOST_AUTO_TEST_CASE( create_account )
{
   try {
      trx.operations.push_back(make_account());
      trx.signatures.push_back(fc::ecc::private_key::regenerate(fc::sha256::hash(string("genesis"))).sign_compact(fc::digest(trx)));
      trx.validate();
      db.push_transaction(trx);

      const account_object* nathan_account = db.get_account_index().get("nathan");
      BOOST_REQUIRE(nathan_account);
      BOOST_CHECK(nathan_account->id.space() == protocol_ids);
      BOOST_CHECK(nathan_account->id.type() == account_object_type);
      BOOST_CHECK(nathan_account->name == "nathan");
      BOOST_CHECK(nathan_account->authorized_assets.empty());
      BOOST_CHECK(nathan_account->delegate_votes.empty());

      BOOST_REQUIRE(nathan_account->owner.auths.size() == 1);
      BOOST_CHECK(nathan_account->owner.auths.at(genesis_key) == 123);
      BOOST_REQUIRE(nathan_account->active.auths.size() == 1);
      BOOST_CHECK(nathan_account->active.auths.at(genesis_key) == 321);
      BOOST_CHECK(nathan_account->voting_key == genesis_key);
      BOOST_CHECK(nathan_account->memo_key == genesis_key);

      const account_balance_object* balances = nathan_account->balances(db);
      BOOST_REQUIRE(balances);
      BOOST_CHECK(balances->id.space() == implementation_ids);
      BOOST_CHECK(balances->id.type() == impl_account_balance_object_type);
      BOOST_CHECK(balances->balances.empty());

      const account_debt_object* debts = nathan_account->debts(db);
      BOOST_REQUIRE(debts);
      BOOST_CHECK(debts->id.space() == implementation_ids);
      BOOST_CHECK(debts->id.type() == impl_account_debt_object_type);
      BOOST_CHECK(debts->call_orders.empty());
   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( transfer )
{
   try {
      trx.operations.push_back(make_account());
      trx.validate();
      db.push_transaction(trx);

      trx = signed_transaction();
      const account_object* nathan_account = db.get_account_index().get("nathan");
      account_id_type genesis_account;
      trx.operations.push_back(transfer_operation({genesis_account,
                                                   nathan_account->id,
                                                   asset(10000),
                                                   asset(),
                                                   vector<char>()
                                                  }));
      for( auto& op : trx.operations ) op.visit( operation_set_fee( db.current_fee_schedule() ) );

      trx.validate();
      db.push_transaction(trx);

      BOOST_REQUIRE(nathan_account);
      const account_balance_object* nathan_balances = nathan_account->balances(db);
      BOOST_REQUIRE(nathan_balances);
      BOOST_CHECK(nathan_balances->get_balance(asset_id_type()) == asset(10000));

      trx = signed_transaction();
      trx.operations.push_back(transfer_operation({nathan_account->id,
                                                   genesis_account,
                                                   asset(2000),
                                                   asset(),
                                                   vector<char>()
                                                  }));
      for( auto& op : trx.operations ) op.visit( operation_set_fee( db.current_fee_schedule() ) );

      trx.validate();
      db.push_transaction(trx);

      BOOST_CHECK(nathan_balances->get_balance(asset_id_type()) == asset(10000 -
                                                                         2000 -
                                                                         trx.operations.front().get<transfer_operation>()
                                                                                               .fee.amount));

   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_SUITE_END()
