#include <bts/chain/database.hpp>
#include <bts/chain/operations.hpp>
#include <bts/chain/account_index.hpp>
#include <bts/chain/key_object.hpp>

#include <fc/crypto/digest.hpp>

#include <boost/test/auto_unit_test.hpp>

using namespace bts::chain;

BOOST_AUTO_TEST_CASE( operation_sanity_check )
{
   operation op = account_create_operation();
   op.get<account_create_operation>().active.add_authority(account_id_type(), 123);
   operation tmp = std::move(op);
   wdump((tmp.which()));
}

BOOST_AUTO_TEST_CASE( genesis_allocation_30k )
{ try {
   genesis_allocation allocation;
   public_key_type the_key = fc::ecc::private_key::generate().get_public_key();
   for( int i = 0; i < 2000000; ++i )
      allocation.emplace_back(the_key, BTS_INITIAL_SUPPLY / 2000000);

   database db;
   db.init_genesis(allocation);
   std::cin.get();
} catch(fc::exception& e) {
      edump((e.to_detail_string()));
} }
